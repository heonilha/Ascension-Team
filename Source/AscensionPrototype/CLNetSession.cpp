#include "CLNetSession.h"

#include "AscensionProtocol.h"
#include "AscensionMovement.h"
#include "CLRemoteHero.h"
#include "CombatLab.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Queue.h"
#include "DrawDebugHelpers.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

#include <atomic>

// 유니티 빌드에서 다른 .cpp로 이름이 새지 않도록 using namespace 대신 별칭을 쓴다.
namespace Proto = asc::proto;
namespace AscMove = asc::move; // "Move"는 언리얼 전역 함수 이름과 겹친다

namespace
{
    FVector ToFVector(const Proto::Vec3& V) { return FVector(V.x, V.y, V.z); }
    Proto::Vec3 ToVec3(const FVector& V) { return Proto::Vec3{float(V.X), float(V.Y), float(V.Z)}; }
    uint32 NowMs() { return uint32(FPlatformTime::Seconds() * 1000.0); }

    // 고정 길이 패킷 읽기. bExact가 false면 가변 길이 패킷의 고정 부분만 읽는다.
    template <class T>
    bool ReadFixed(const TArray<uint8>& Bytes, T& Out, bool bExact = true)
    {
        if (bExact ? Bytes.Num() != int32(sizeof(T)) : Bytes.Num() < int32(sizeof(T))) return false;
        FMemory::Memcpy(&Out, Bytes.GetData(), sizeof(T));
        return true;
    }

    template <class T>
    T ReadEntry(const TArray<uint8>& Bytes, SIZE_T Offset)
    {
        T Out;
        FMemory::Memcpy(&Out, Bytes.GetData() + Offset, sizeof(T));
        return Out;
    }
}

// 수신 전용 스레드. 접속도 여기서 해서 게임 스레드가 멈추지 않게 한다.
class FCLNetReceiver : public FRunnable
{
public:
    FCLNetReceiver(const FString& InHost, int32 InPort) : Host(InHost), Port(InPort) {}

    virtual uint32 Run() override
    {
        ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        TSharedRef<FInternetAddr> Address = Sockets->CreateInternetAddr();
        bool bValid = false;
        Address->SetIp(Host == TEXT("localhost") ? TEXT("127.0.0.1") : *Host, bValid); // P0: IPv4 주소만 지원
        Address->SetPort(Port);
        if (!bValid) return Finish(FString::Printf(TEXT("서버 주소가 IPv4 형식이 아닙니다: %s"), *Host));

        Socket = Sockets->CreateSocket(NAME_Stream, TEXT("AscensionServer"), Address->GetProtocolType());
        if (!Socket) return Finish(TEXT("소켓 생성 실패"));
        Socket->SetNoDelay(true);
        if (!Socket->Connect(*Address)) return Finish(FString::Printf(TEXT("%s:%d 접속 실패 (서버가 켜져 있는지 확인)"), *Host, Port));
        bConnected = true;

        // TCP는 경계가 없는 바이트 흐름이라, 받은 바이트를 모아 두었다가 헤더의 size만큼씩 잘라낸다.
        TArray<uint8> Pending;
        uint8 Chunk[4096];
        while (!bStop)
        {
            if (!Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(100)))
            {
                if (Socket->GetConnectionState() == SCS_ConnectionError) return Finish(TEXT("서버 연결 오류"));
                continue;
            }
            int32 Read = 0;
            if (!Socket->Recv(Chunk, sizeof(Chunk), Read) || Read <= 0) return Finish(TEXT("서버 연결이 끊어졌습니다."));
            Pending.Append(Chunk, Read);

            int32 Offset = 0;
            while (Pending.Num() - Offset >= int32(sizeof(Proto::PacketHeader)))
            {
                Proto::PacketHeader Header;
                FMemory::Memcpy(&Header, Pending.GetData() + Offset, sizeof(Header));
                if (Header.size < sizeof(Proto::PacketHeader) || Header.size > Proto::kMaxPacketSize) return Finish(TEXT("잘못된 패킷 크기"));
                if (Pending.Num() - Offset < Header.size) break; // 나머지는 다음 수신에서
                Inbox.Enqueue({TArray<uint8>(Pending.GetData() + Offset, Header.size), FPlatformTime::Seconds()});
                Offset += Header.size;
            }
            if (Offset > 0) Pending.RemoveAt(0, Offset, EAllowShrinking::No);
        }
        return Finish(FString());
    }

    virtual void Stop() override { bStop = true; }

    // 게임 스레드에서 호출. 송신과 수신을 서로 다른 스레드에서 해도 소켓은 안전하다.
    bool Send(const void* Data, int32 Size)
    {
        if (!bConnected || bClosed || !Socket) return false;
        const uint8* Bytes = static_cast<const uint8*>(Data);
        while (Size > 0)
        {
            int32 Sent = 0;
            if (!Socket->Send(Bytes, Size, Sent) || Sent <= 0) return false;
            Bytes += Sent;
            Size -= Sent;
        }
        return true;
    }

    // 스레드가 끝난 뒤에만 부른다.
    void DestroySocket()
    {
        if (!Socket) return;
        Socket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
        Socket = nullptr;
    }

    struct FReceived
    {
        TArray<uint8> Bytes;
        double ReceivedAt = 0; // 수신 스레드가 패킷을 잘라낸 순간(FPlatformTime::Seconds)
    };
    TQueue<FReceived, EQueueMode::Spsc> Inbox;
    std::atomic<bool> bConnected{false};
    std::atomic<bool> bClosed{false};
    FString Error; // bClosed가 true가 된 뒤에만 게임 스레드에서 읽는다

private:
    uint32 Finish(const FString& Reason)
    {
        Error = Reason;
        bClosed = true;
        return 0;
    }

    FString Host;
    int32 Port = 0;
    FSocket* Socket = nullptr;
    std::atomic<bool> bStop{false};
};

// 내 이동 예측 상태. 게임 스레드에서만 쓴다.
struct FCLPrediction
{
    AscMove::World World;
    AscMove::State Sim, Previous; // 마지막 틱 결과와 그 직전(화면 보간용)
    struct FPending
    {
        uint32 Sequence;
        AscMove::Input Input;
        AscMove::State After;
    };
    TArray<FPending> Pending;               // 서버 확정을 아직 못 받은 입력
    TArray<Proto::InputCommand> Outgoing;   // 아직 보내지 않은 입력
    uint32 Sequence = 0;
    double Accumulator = 0;
    double LastRealTime = 0; // 이동 틱은 게임 시간이 아니라 실제 시간으로 센다(서버의 초당 60틱과 맞춘다)
    FVector2D VisualOffset = FVector2D::ZeroVector; // 재조정 차이를 화면에서 녹이는 값
    double GroundZ = AscMove::kGroundZ;
    bool bDodgeQueued = false;
    FVector2D DodgeDirection = FVector2D::ZeroVector;
    uint32 Acks = 0, Corrections = 0;
    float MaxError = 0;
};

ACLNetSession::ACLNetSession()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true; // 일시정지 중에도 수신 큐를 비운다
}

void ACLNetSession::Connect(const FString& HostPort, const FString& InPlayerName, uint32 InTeamCode, bool bInPvp)
{
    FString Host = HostPort, PortText;
    int32 Port = Proto::kDefaultPort;
    if (HostPort.Split(TEXT(":"), &Host, &PortText)) Port = FCString::Atoi(*PortText);

    ServerText = FString::Printf(TEXT("%s:%d"), *Host, Port);
    PlayerName = InPlayerName;
    TeamCode = InTeamCode;
    bPvp = bInPvp;
    Capacity = bPvp ? Proto::kMaxPvpPlayers : Proto::kMaxRoomPlayers;

    // 이동 예측에 서버와 같은 충돌 지형 파일을 쓴다.
    FString MapText;
    const FString MapPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/ServerCollision/CombatLab.txt"));
    Prediction = new FCLPrediction();
    std::string ParseError;
    if (!FFileHelper::LoadFileToString(MapText, *MapPath) || !AscMove::ParseWorld(std::string(TCHAR_TO_UTF8(*MapText)), Prediction->World, ParseError))
    {
        Fail(FString::Printf(TEXT("충돌 지형을 읽지 못함: %s %s"), *MapPath, UTF8_TO_TCHAR(ParseError.c_str())));
        return;
    }
    State = EState::Connecting;

    Receiver = new FCLNetReceiver(Host, Port);
    Thread = FRunnableThread::Create(Receiver, TEXT("AscensionNetReceiver"));
    UE_LOG(LogTemp, Display, TEXT("ASCENSION_NET_CONNECT %s name=%s team=%u mode=%s"), *ServerText, *PlayerName, TeamCode, bPvp ? TEXT("pvp") : TEXT("pve"));
}

void ACLNetSession::Tick(float Delta)
{
    Super::Tick(Delta);
    if (!Receiver) return;

    if (State == EState::Connecting && Receiver->bConnected)
    {
        auto Login = Proto::Make<Proto::C_Login>();
        Login.version = Proto::kVersion;
        Proto::CopyName(Login.name, TCHAR_TO_UTF8(*PlayerName));
        SendPacket(Login);
        State = EState::LoggingIn;
    }

    FCLNetReceiver::FReceived Received;
    while (Receiver->Inbox.Dequeue(Received)) HandlePacket(Received.Bytes, Received.ReceivedAt);

    if (Receiver->bClosed && State != EState::Failed) Fail(Receiver->Error.IsEmpty() ? FString(TEXT("연결 종료")) : Receiver->Error);
    if (State != EState::InRoom) return;

    const double Now = FPlatformTime::Seconds();
    TickPrediction();
    if (AutoDodgeAt > 0 && Now >= AutoDodgeAt)
    {
        AutoDodgeAt = 0;
        if (ACLHero* Hero = LocalHero()) Hero->Dodge();
    }
    if (Now >= NextInputSend)
    {
        NextInputSend = Now + InputSendInterval;
        SendInputs();
    }
    if (Now >= NextPredictionLog && Prediction)
    {
        NextPredictionLog = Now + 5.0;
        UE_LOG(LogTemp, Display, TEXT("ASCENSION_NET_PREDICTION acks=%u corrections=%u max_error=%.2fcm pending=%d"), Prediction->Acks,
               Prediction->Corrections, Prediction->MaxError, Prediction->Pending.Num());
    }
    if (Now >= NextPing)
    {
        NextPing = Now + PingInterval;
        auto Ping = Proto::Make<Proto::C_Ping>();
        Ping.clientTimeMs = NowMs();
        // 서버가 판정 유예(되감기 허용량)를 정하는 데 쓴다. 0은 "아직 잰 적 없음"이라, 같은 PC처럼 1ms 미만이어도 잰 뒤에는 1 이상으로 알린다.
        Ping.lastRttMs = ClockSamples.Num() > 0 ? FMath::Max<uint32>(1, RttMs) : 0;
        SendPacket(Ping);
    }

    const double RenderTime = Now - ServerClockOffset - InterpolationDelay;
    for (const auto& Pair : Remotes)
    {
        if (IsValid(Pair.Value)) Pair.Value->UpdateVisual(RenderTime, Delta);
    }
}

void ACLNetSession::HandlePacket(const TArray<uint8>& Bytes, double ReceivedAt)
{
    Proto::PacketHeader Header;
    if (!ReadFixed(Bytes, Header, false)) return;

    switch (Header.id)
    {
    case Proto::PacketId::S_Login:
    {
        Proto::S_Login Packet;
        if (!ReadFixed(Bytes, Packet)) break;
        if (Packet.result != Proto::LoginResult::Ok)
        {
            Fail(Packet.result == Proto::LoginResult::VersionMismatch
                     ? FString::Printf(TEXT("프로토콜 버전 불일치 (서버 v%d / 클라이언트 v%d)"), Packet.serverVersion, Proto::kVersion)
                     : FString(TEXT("로그인 실패")));
            break;
        }
        MyId = Packet.playerId;
        auto Enter = Proto::Make<Proto::C_EnterRoom>();
        Enter.teamCode = TeamCode;
        Enter.mode = bPvp ? Proto::RoomMode::PvP : Proto::RoomMode::PvE;
        SendPacket(Enter);
        State = EState::Entering;
        break;
    }
    case Proto::PacketId::S_EnterRoom:
    {
        Proto::S_EnterRoom Packet;
        if (!ReadFixed(Bytes, Packet, false) || Bytes.Num() != int32(sizeof(Packet) + Packet.count * sizeof(Proto::PlayerInfo))) break;
        if (Packet.result != Proto::EnterResult::Ok)
        {
            Fail(Packet.result == Proto::EnterResult::RoomFull
                     ? FString::Printf(TEXT("방이 가득 찼습니다 (최대 %d명). 다른 팀 코드를 쓰세요."), Capacity)
                     : FString(TEXT("방 입장 실패")));
            break;
        }
        State = EState::InRoom;
        Capacity = Packet.capacity;
        IframeMs = Packet.rules.iframeMs;
        RewindCapMs = Packet.rules.rewindCapMs;
        // 첫 시계 추정(왕복 시간을 모르므로 거칠다). 1초 안에 S_Pong으로 바뀐다.
        ServerTimeOffset = ReceivedAt - Packet.serverTimeMs / 1000.0;
        if (ACLHero* Hero = LocalHero()) Hero->ImmuneDuration = IframeMs / 1000.0;
        StartPrediction(ToFVector(Packet.spawnPosition));
        for (int32 I = 0; I < Packet.count; ++I)
        {
            const Proto::PlayerInfo Info = ReadEntry<Proto::PlayerInfo>(Bytes, sizeof(Packet) + I * sizeof(Proto::PlayerInfo));
            SpawnRemote(Info.playerId, Info.name, ToFVector(Info.position), Info.yaw);
        }
        NextInputSend = FPlatformTime::Seconds();
        UE_LOG(LogTemp, Display, TEXT("ASCENSION_NET_ENTER team=%u id=%u others=%d mode=%s iframe=%ums rewind_cap=%ums"), TeamCode, MyId, Packet.count,
               bPvp ? TEXT("pvp") : TEXT("pve"), IframeMs, RewindCapMs);
        break;
    }
    case Proto::PacketId::S_PlayerJoined:
    {
        Proto::S_PlayerJoined Packet;
        if (ReadFixed(Bytes, Packet)) SpawnRemote(Packet.player.playerId, Packet.player.name, ToFVector(Packet.player.position), Packet.player.yaw);
        break;
    }
    case Proto::PacketId::S_PlayerLeft:
    {
        Proto::S_PlayerLeft Packet;
        if (ReadFixed(Bytes, Packet)) RemoveRemote(Packet.playerId);
        break;
    }
    case Proto::PacketId::S_MoveBatch:
    {
        Proto::S_MoveBatch Packet;
        if (!ReadFixed(Bytes, Packet, false) || Bytes.Num() != int32(sizeof(Packet) + Packet.count * sizeof(Proto::MoveEntry))) break;
        const double ServerTime = Packet.serverTick * (Proto::kServerTickMs / 1000.0);
        const double Offset = ReceivedAt - ServerTime;
        // 늦게 도착한 패킷일수록 Offset이 크다. 가장 작은 값을 따라가되 시계 차이를 조금씩 허용한다.
        ServerClockOffset = bHasClock ? FMath::Min(Offset, ServerClockOffset + .001) : Offset;
        bHasClock = true;
        for (int32 I = 0; I < Packet.count; ++I)
        {
            const Proto::MoveEntry Entry = ReadEntry<Proto::MoveEntry>(Bytes, sizeof(Packet) + I * sizeof(Proto::MoveEntry));
            if (Entry.playerId == MyId) continue; // 내 이동은 이미 화면에 있다
            if (TObjectPtr<ACLRemoteHero>* Remote = Remotes.Find(Entry.playerId); Remote && IsValid(*Remote))
                (*Remote)->PushSample(ServerTime, ToFVector(Entry.position), Entry.yaw, uint8(Entry.state));
        }
        break;
    }
    case Proto::PacketId::S_MoveAck:
    {
        Proto::S_MoveAck Packet;
        if (!ReadFixed(Bytes, Packet) || !Prediction) break;
        FCLPrediction& P = *Prediction;
        ++P.Acks;
        const AscMove::State Confirmed = AscMove::FromProto(Packet.state);
        int32 Done = 0;
        while (Done < P.Pending.Num() && P.Pending[Done].Sequence < Packet.sequence) ++Done;
        if (Done < P.Pending.Num() && P.Pending[Done].Sequence == Packet.sequence)
        {
            // 같은 입력까지의 내 예측과 서버 확정 위치의 차이(같은 코드라 보통 0)
            const float PredictionError = FMath::Sqrt(FMath::Square(P.Pending[Done].After.x - Confirmed.x) + FMath::Square(P.Pending[Done].After.y - Confirmed.y));
            P.MaxError = FMath::Max(P.MaxError, PredictionError);
            if (PredictionError > .5f) ++P.Corrections;
            ++Done;
        }
        P.Pending.RemoveAt(0, Done, EAllowShrinking::No);

        // 확정 상태에서 남은 입력을 다시 돌린다. 화면은 이어지게 두고 차이만 서서히 녹인다.
        const AscMove::State Before = P.Sim;
        P.Sim = Confirmed;
        for (FCLPrediction::FPending& Pending : P.Pending)
        {
            AscMove::Step(P.Sim, Pending.Input, P.World);
            Pending.After = P.Sim;
        }
        const FVector2D Shift(P.Sim.x - Before.x, P.Sim.y - Before.y);
        P.Previous.x += Shift.X;
        P.Previous.y += Shift.Y;
        P.VisualOffset -= Shift;
        break;
    }
    case Proto::PacketId::S_Pong:
    {
        Proto::S_Pong Packet;
        if (!ReadFixed(Bytes, Packet)) break;
        RttMs = uint32(ReceivedAt * 1000.0) - Packet.clientTimeMs;
        // 왕복 시간이 짧은 측정일수록 정확하다: 서버 시각 + 왕복/2 = 도착 시각. 최근 몇 개 중 가장 짧은 것을 쓴다.
        ClockSamples.Add({RttMs, ReceivedAt - (Packet.serverTimeMs + RttMs / 2.0) / 1000.0});
        if (ClockSamples.Num() > ClockWindow) ClockSamples.RemoveAt(0);
        uint32 BestRtt = MAX_uint32;
        for (const FClockSample& Sample : ClockSamples)
        {
            if (Sample.RttMs > BestRtt) continue; // 같은 왕복 시간이면 최신 값을 쓴다
            BestRtt = Sample.RttMs;
            ServerTimeOffset = Sample.Offset;
        }
        break;
    }
    case Proto::PacketId::S_AttackStart:
    {
        Proto::S_AttackStart Packet;
        if (!ReadFixed(Bytes, Packet)) break;
        // 서버가 정한 명중 시각까지 남은 시간만큼 예고를 보여 준다(패킷이 늦게 왔으면 그만큼 짧다).
        const int32 HitInMs = int32(Packet.hitServerMs) - int32(ServerNowMs());
        const FVector Center = ToFVector(Packet.center) - FVector(0, 0, 85);
        DrawDebugCircle(GetWorld(), Center, Packet.radius, 48, Packet.kind == Proto::AttackKind::Slam ? FColor::Orange : FColor::Red, false,
                        FMath::Max(.05f, HitInMs / 1000.f), 0, 4.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
        const bool bTargetsMe = Packet.attackerId != MyId && LocalHero() &&
                                FVector::Dist2D(LocalHero()->GetActorLocation(), ToFVector(Packet.center)) <= Packet.radius;
        if (bTargetsMe) LocalHero()->Notice = FString::Printf(TEXT("공격 예고! %.2f초 뒤 명중 · 회피 무적 %.2f초"), HitInMs / 1000.f, IframeMs / 1000.f);
        UE_LOG(LogTemp, Display, TEXT("ASCENSION_NET_ATTACK id=%u attacker=%u kind=%d hit_in=%dms"), Packet.attackId, Packet.attackerId,
               int32(Packet.kind), HitInMs);

        // 시험 옵션 -AscensionNetAutoDodge: 나를 노린 공격을 하나 걸러 하나씩 무적 구간 가운데에 맞춰 실제 회피 입력을 낸다.
        if (bTargetsMe && FParse::Param(FCommandLine::Get(), TEXT("AscensionNetAutoDodge")) && (AutoDodgeTurn++ % 2 == 0))
        {
            // 실제 시간으로 잰다. 게임 시간은 히트 스톱이나 2000fps 초과(언리얼의 최소 프레임 시간 0.5ms)에서 실제와 달라진다.
            AutoDodgeAt = FPlatformTime::Seconds() + FMath::Max(0, HitInMs - IframeMs / 2) / 1000.0 + 1e-6;
        }
        break;
    }
    case Proto::PacketId::S_HitResult:
    {
        Proto::S_HitResult Packet;
        if (!ReadFixed(Bytes, Packet)) break;
        const bool bDodged = Packet.result == Proto::HitResult::Dodged;
        if (Packet.targetId == MyId)
        {
            (bDodged ? ServerDodges : ServerHits)++;
            if (ACLHero* Hero = LocalHero()) Hero->ApplyServerHit(bDodged, Packet.damage, Packet.rewindMs);
        }
        UE_LOG(LogTemp, Display, TEXT("ASCENSION_NET_HIT id=%u target=%u%s result=%s damage=%.0f server_hp=%.0f local_hp=%.0f dodge_before_hit=%dms rewind=%ums"),
               Packet.attackId, Packet.targetId, Packet.targetId == MyId ? TEXT("(me)") : TEXT(""), bDodged ? TEXT("dodged") : TEXT("hit"),
               Packet.damage, Packet.hpAfter, LocalHero() ? LocalHero()->HP : -1.f,
               bDodged ? int32(Packet.hitServerMs) - int32(Packet.dodgeServerMs) : -1, uint32(Packet.rewindMs));
        break;
    }
    default:
        UE_LOG(LogTemp, Warning, TEXT("ASCENSION_NET_UNKNOWN_PACKET id=%d"), int32(Header.id));
        break;
    }
}

void ACLNetSession::SendBytes(const void* Data, int32 Size)
{
    if (Receiver && !Receiver->Send(Data, Size) && State != EState::Failed) Fail(TEXT("송신 실패"));
}

void ACLNetSession::StartPrediction(const FVector& Spawn)
{
    ACLHero* Hero = LocalHero();
    if (!Prediction || !Hero) return;
    FCLPrediction& P = *Prediction;
    P.Sim = AscMove::State{};
    P.Sim.x = float(Spawn.X);
    P.Sim.y = float(Spawn.Y);
    P.Previous = P.Sim;
    P.Pending.Reset();
    P.Outgoing.Reset();
    P.Accumulator = 0;
    P.LastRealTime = 0;
    P.VisualOffset = FVector2D::ZeroVector;

    // 바닥 높이: 서버 이동은 2D라 높이는 바닥에 선 캡슐 중심으로 고정한다.
    FHitResult Hit;
    FCollisionQueryParams Query(TEXT("AscensionGround"), false, Hero);
    if (GetWorld()->LineTraceSingleByChannel(Hit, FVector(Spawn.X, Spawn.Y, 500), FVector(Spawn.X, Spawn.Y, -500), ECC_WorldStatic, Query))
        P.GroundZ = Hit.ImpactPoint.Z + Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

    // 온라인에서는 언리얼 이동 대신 공유 이동 코드가 위치를 정한다.
    Hero->bNetMovement = true;
    Hero->GetCharacterMovement()->StopMovementImmediately();
    Hero->GetCharacterMovement()->SetMovementMode(MOVE_None);
    Hero->SetActorLocation(FVector(Spawn.X, Spawn.Y, P.GroundZ), false, nullptr, ETeleportType::TeleportPhysics);
}

void ACLNetSession::TickPrediction()
{
    ACLHero* Hero = LocalHero();
    if (!Prediction || !Hero || !Hero->bNetMovement) return;
    FCLPrediction& P = *Prediction;

    // 시험 옵션 -AscensionNetAutoMove: 원을 그리며 달리고 2.5초마다 회피한다(화면 없는 예측 검증용).
    static const bool bAutoMove = FParse::Param(FCommandLine::Get(), TEXT("AscensionNetAutoMove"));

    // 실제 경과 시간. 액터 Tick의 Delta는 게임 시간이라 히트 스톱·시간 배율·프레임 시간 하한(2000fps 초과)에서
    // 실제보다 빠르거나 느리게 흐르고, 그러면 서버(실제 시간 기준 초당 60틱)와 입력 수가 어긋난다.
    const double RealNow = FPlatformTime::Seconds();
    const double Delta = P.LastRealTime > 0 ? RealNow - P.LastRealTime : 0;
    P.LastRealTime = RealNow;
    P.Accumulator += FMath::Min(Delta, .25); // 긴 멈춤 뒤 몰아서 돌지 않는다
    while (P.Accumulator >= AscMove::kTick)
    {
        P.Accumulator -= AscMove::kTick;
        FVector Direction = Hero->NetMoveInput();
        if (bAutoMove)
        {
            const double T = P.Sequence * AscMove::kTick;
            Direction = FVector(FMath::Cos(T * .8), FMath::Sin(T * .8), 0);
            if (P.Sequence % 150 == 75) Hero->Dodge(); // SendDodge가 회피 입력을 예약한다
        }
        AscMove::Input Input;
        Input.moveX = float(Direction.X);
        Input.moveY = float(Direction.Y);
        if (P.bDodgeQueued)
        {
            Input.flags |= AscMove::kInputDodge;
            Input.dodgeX = float(P.DodgeDirection.X);
            Input.dodgeY = float(P.DodgeDirection.Y);
            P.bDodgeQueued = false;
        }
        P.Previous = P.Sim;
        AscMove::Step(P.Sim, Input, P.World);
        P.Pending.Add({++P.Sequence, Input, P.Sim});
        P.Outgoing.Add({Input.moveX, Input.moveY, Input.flags, Input.dodgeX, Input.dodgeY});
    }
    if (P.Pending.Num() > AscMove::kTickRate * 10) P.Pending.RemoveAt(0, P.Pending.Num() - AscMove::kTickRate * 10); // 확정이 오래 끊긴 경우

    // 화면: 두 틱 사이를 보간하고, 재조정 차이는 약 0.1초 안에 녹인다.
    P.VisualOffset *= FMath::Exp(-Delta * 25.0);
    if (P.VisualOffset.SizeSquared() < .01) P.VisualOffset = FVector2D::ZeroVector;
    const double Alpha = P.Accumulator / AscMove::kTick;
    const FVector Position(FMath::Lerp(P.Previous.x, P.Sim.x, Alpha) + P.VisualOffset.X, FMath::Lerp(P.Previous.y, P.Sim.y, Alpha) + P.VisualOffset.Y, P.GroundZ);
    Hero->SetActorLocation(Position);
    Hero->NetVelocity = FVector(P.Sim.vx, P.Sim.vy, 0);
}

void ACLNetSession::SendInputs()
{
    ACLHero* Hero = LocalHero();
    if (!Prediction || !Hero || Prediction->Outgoing.IsEmpty()) return;
    FCLPrediction& P = *Prediction;
    const uint32 First = P.Sequence - uint32(P.Outgoing.Num()) + 1;
    for (int32 Offset = 0; Offset < P.Outgoing.Num(); Offset += Proto::kMaxInputsPerPacket)
    {
        const int32 Count = FMath::Min<int32>(Proto::kMaxInputsPerPacket, P.Outgoing.Num() - Offset);
        auto Packet = Proto::Make<Proto::C_Input>();
        Packet.header.size = uint16(sizeof(Packet) + Count * sizeof(Proto::InputCommand));
        Packet.firstSequence = First + uint32(Offset);
        Packet.yaw = float(Hero->GetActorRotation().Yaw);
        Packet.state = Hero->HP <= 0 ? Proto::MoveState::Dead : Proto::MoveState(FMath::Clamp(Hero->GetVisualState(), 0, 4));
        Packet.count = uint8(Count);
        TArray<uint8> Buffer;
        Buffer.Append(reinterpret_cast<const uint8*>(&Packet), sizeof(Packet));
        Buffer.Append(reinterpret_cast<const uint8*>(P.Outgoing.GetData() + Offset), Count * sizeof(Proto::InputCommand));
        SendBytes(Buffer.GetData(), Buffer.Num());
    }
    P.Outgoing.Reset();
}

uint32 ACLNetSession::ServerNowMs() const
{
    return uint32(FMath::Max(0.0, FPlatformTime::Seconds() - ServerTimeOffset) * 1000.0);
}

void ACLNetSession::SendDodge(const FVector& Direction)
{
    if (State != EState::InRoom) return;
    if (Prediction)
    {
        // 다음 이동 틱에 회피 입력으로 넣는다(서버도 같은 틱에 회피를 시작한다)
        Prediction->bDodgeQueued = true;
        Prediction->DodgeDirection = FVector2D(Direction.X, Direction.Y);
    }
    const float Yaw = float(Direction.Rotation().Yaw);
    auto Dodge = Proto::Make<Proto::C_Dodge>();
    Dodge.sequence = ++DodgeSequence;
    Dodge.pressServerMs = ServerNowMs();
    Dodge.yaw = Yaw;
    SendPacket(Dodge);
}

void ACLNetSession::SendAttack(float Yaw)
{
    if (State != EState::InRoom || !bPvp) return; // PvE 몬스터는 아직 서버에 없다
    auto Attack = Proto::Make<Proto::C_Attack>();
    Attack.sequence = ++AttackSequence;
    Attack.pressServerMs = ServerNowMs();
    Attack.yaw = Yaw;
    Attack.kind = Proto::AttackKind::Slash;
    SendPacket(Attack);
}

void ACLNetSession::SpawnRemote(uint32 Id, const char* Name, const FVector& Position, float Yaw)
{
    if (Id == MyId || Remotes.Contains(Id)) return;
    char Terminated[Proto::kNameLength + 1] = {};
    FMemory::Memcpy(Terminated, Name, Proto::kNameLength);

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if (ACLRemoteHero* Remote = GetWorld()->SpawnActor<ACLRemoteHero>(Position, FRotator(0, Yaw, 0), Params))
    {
        Remote->Init(Id, UTF8_TO_TCHAR(Terminated));
        Remotes.Add(Id, Remote);
        if (ACLHero* Hero = LocalHero()) Hero->Notice = FString::Printf(TEXT("%s 님이 같은 층에 들어왔습니다."), *Remote->DisplayName);
    }
}

void ACLNetSession::RemoveRemote(uint32 Id)
{
    TObjectPtr<ACLRemoteHero> Remote;
    if (!Remotes.RemoveAndCopyValue(Id, Remote) || !IsValid(Remote)) return;
    if (ACLHero* Hero = LocalHero()) Hero->Notice = FString::Printf(TEXT("%s 님이 나갔습니다."), *Remote->DisplayName);
    Remote->Destroy();
}

void ACLNetSession::Fail(const FString& Reason)
{
    State = EState::Failed;
    Error = Reason;
    for (const auto& Pair : Remotes)
    {
        if (IsValid(Pair.Value)) Pair.Value->Destroy();
    }
    Remotes.Empty();
    if (ACLHero* Hero = LocalHero(); Hero && Hero->bNetMovement)
    {
        // 서버 연결이 끊기면 언리얼 이동으로 돌아간다
        Hero->bNetMovement = false;
        Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    }
    UE_LOG(LogTemp, Warning, TEXT("ASCENSION_NET_FAILED %s"), *Reason);
}

FString ACLNetSession::StatusText() const
{
    switch (State)
    {
    case EState::Connecting: return FString::Printf(TEXT("온라인 · %s 접속 중..."), *ServerText);
    case EState::LoggingIn: return TEXT("온라인 · 로그인 중...");
    case EState::Entering: return FString::Printf(TEXT("온라인 · 팀 %u 방 입장 중..."), TeamCode);
    case EState::InRoom:
        return FString::Printf(TEXT("온라인 %s · 팀 %u · 같은 방 %d/%d명 · 지연 %ums · 무적 %.2f초%s%s"), bPvp ? TEXT("PvP") : TEXT("PvE"), TeamCode,
                               Remotes.Num() + 1, Capacity, RttMs, IframeMs / 1000.f,
                               ServerDodges + ServerHits > 0 ? *FString::Printf(TEXT(" · 서버 판정 회피 %u/피격 %u"), ServerDodges, ServerHits) : TEXT(""),
                               Prediction && Prediction->Corrections > 0
                                   ? *FString::Printf(TEXT(" · 예측 보정 %u회(최대 %.0fcm)"), Prediction->Corrections, Prediction->MaxError)
                                   : TEXT(""));
    case EState::Failed: return FString::Printf(TEXT("온라인 오류 · %s"), *Error);
    default: return FString();
    }
}

ACLHero* ACLNetSession::LocalHero() const
{
    return Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this, 0));
}

void ACLNetSession::Shutdown()
{
    if (!Receiver) return;
    Receiver->Stop();
    if (Thread)
    {
        Thread->WaitForCompletion(); // 수신 대기는 0.1초마다 깨므로 곧 끝난다
        delete Thread;
        Thread = nullptr;
    }
    Receiver->DestroySocket();
    delete Receiver;
    Receiver = nullptr;
    delete Prediction;
    Prediction = nullptr;
}

void ACLNetSession::EndPlay(const EEndPlayReason::Type Reason)
{
    Shutdown();
    Super::EndPlay(Reason);
}

void ACLNetSession::BeginDestroy()
{
    Shutdown();
    Super::BeginDestroy();
}
