#include "CLNetSession.h"

#include "AscensionProtocol.h"
#include "CLRemoteHero.h"
#include "CombatLab.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Queue.h"
#include "DrawDebugHelpers.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

#include <atomic>

// 유니티 빌드에서 다른 .cpp로 이름이 새지 않도록 using namespace 대신 별칭을 쓴다.
namespace Proto = asc::proto;

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
    if (Now >= NextMoveSend)
    {
        NextMoveSend = Now + MoveSendInterval;
        SendMove();
    }
    if (Now >= NextPing)
    {
        NextPing = Now + PingInterval;
        auto Ping = Proto::Make<Proto::C_Ping>();
        Ping.clientTimeMs = NowMs();
        Ping.lastRttMs = RttMs; // 서버가 판정 유예(되감기 허용량)를 정하는 데 쓴다
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
        if (ACLHero* Hero = LocalHero())
        {
            Hero->SetActorLocation(ToFVector(Packet.spawnPosition), false, nullptr, ETeleportType::TeleportPhysics);
            Hero->GetCharacterMovement()->StopMovementImmediately();
            Hero->ImmuneDuration = IframeMs / 1000.0;
        }
        for (int32 I = 0; I < Packet.count; ++I)
        {
            const Proto::PlayerInfo Info = ReadEntry<Proto::PlayerInfo>(Bytes, sizeof(Packet) + I * sizeof(Proto::PlayerInfo));
            SpawnRemote(Info.playerId, Info.name, ToFVector(Info.position), Info.yaw);
        }
        NextMoveSend = FPlatformTime::Seconds();
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
    case Proto::PacketId::S_Correction:
    {
        Proto::S_Correction Packet;
        if (!ReadFixed(Bytes, Packet)) break;
        ++Corrections;
        if (ACLHero* Hero = LocalHero())
        {
            Hero->SetActorLocation(ToFVector(Packet.position), false, nullptr, ETeleportType::TeleportPhysics);
            Hero->GetCharacterMovement()->StopMovementImmediately();
        }
        UE_LOG(LogTemp, Warning, TEXT("ASCENSION_NET_CORRECTION seq=%u"), Packet.sequence);
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
            const float Delay = FMath::Max(0.f, (HitInMs - IframeMs / 2) / 1000.f);
            FTimerHandle Handle;
            GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this] {
                if (ACLHero* Hero = LocalHero()) Hero->Dodge();
            }), FMath::Max(Delay, .001f), false);
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

void ACLNetSession::SendMove()
{
    ACLHero* Hero = LocalHero();
    if (!Hero) return;
    auto Move = Proto::Make<Proto::C_Move>();
    Move.sequence = ++MoveSequence;
    Move.position = ToVec3(Hero->GetActorLocation());
    Move.velocity = ToVec3(Hero->GetVelocity());
    Move.yaw = float(Hero->GetActorRotation().Yaw);
    Move.state = Hero->HP <= 0 ? Proto::MoveState::Dead : Proto::MoveState(FMath::Clamp(Hero->GetVisualState(), 0, 4));
    SendPacket(Move);
}

uint32 ACLNetSession::ServerNowMs() const
{
    return uint32(FMath::Max(0.0, FPlatformTime::Seconds() - ServerTimeOffset) * 1000.0);
}

void ACLNetSession::SendDodge(float Yaw)
{
    if (State != EState::InRoom) return;
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
                               Corrections > 0 ? *FString::Printf(TEXT(" · 위치 보정 %u회"), Corrections) : TEXT(""));
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
