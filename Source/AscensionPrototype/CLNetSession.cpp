#include "CLNetSession.h"

#include "AscensionProtocol.h"
#include "AscensionMovement.h"
#include "CLRemoteHero.h"
#include "CLZone.h"
#include "CLSkillComponent.h"
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

void ACLNetSession::Connect(const FString& HostPort, const FString& InPlayerName, uint32 InMatchCode, uint8 InTeam, bool bInMatch)
{
    FString Host = HostPort, PortText;
    int32 Port = Proto::kDefaultPort;
    if (HostPort.Split(TEXT(":"), &Host, &PortText)) Port = FCString::Atoi(*PortText);

    ServerText = FString::Printf(TEXT("%s:%d"), *Host, Port);
    PlayerName = InPlayerName;
    TeamCode = InMatchCode;
    bMatch = bInMatch;
    MyTeam = bMatch ? uint8(FMath::Clamp<int32>(InTeam, 1, Proto::kMaxTeams)) : 0;
    Capacity = bMatch ? Proto::kMaxMatchPlayers : Proto::kMaxPracticePlayers;

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
    UE_LOG(LogTemp, Display, TEXT("ASCENSION_NET_CONNECT %s name=%s match=%u team=%u mode=%s"), *ServerText, *PlayerName, TeamCode, uint32(MyTeam), bMatch ? TEXT("match") : TEXT("practice"));
}

void ACLNetSession::Tick(float Delta)
{
    Super::Tick(Delta);
    if (!Receiver) return;
    DrawZone();
    DrawRift();

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
    TickMonsters(RenderTime, Delta);
    TickAutoHunt(Now);
    if (Now >= NextMonsterLog && MonsterBatches > 0)
    {
        NextMonsterLog = Now + 5.0;
        UE_LOG(LogTemp, Display, TEXT("ASCENSION_NET_MONSTERS alive=%d batches=%u waves=%u/%u my_hits=%u my_kills=%u deaths=%u respawns=%u"), Monsters.Num(),
               MonsterBatches, WavesConfirmed, WavesSent, MyMonsterHits, MyKills, Deaths, Respawns);
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
        Enter.mode = bMatch ? Proto::RoomMode::Match : Proto::RoomMode::Practice;
        Enter.team = MyTeam;
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
            Fail(Packet.result == Proto::EnterResult::RoomFull      ? FString::Printf(TEXT("방이 가득 찼습니다 (최대 %d명). 다른 판 번호를 쓰세요."), Capacity)
                 : Packet.result == Proto::EnterResult::TeamFull    ? FString::Printf(TEXT("팀 %d이 가득 찼습니다 (팀당 %d명). 다른 팀을 고르세요."), MyTeam, Proto::kMaxTeamPlayers)
                 : Packet.result == Proto::EnterResult::InvalidTeam ? FString(TEXT("팀 번호는 1~4입니다."))
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
        BindSkillCommands(); // 스킬 창의 배우기·스탯 분배를 서버로 보낸다
        for (int32 I = 0; I < Packet.count; ++I)
        {
            const Proto::PlayerInfo Info = ReadEntry<Proto::PlayerInfo>(Bytes, sizeof(Packet) + I * sizeof(Proto::PlayerInfo));
            SpawnRemote(Info.playerId, Info.name, ToFVector(Info.position), Info.yaw, Info.team);
        }
        NextInputSend = FPlatformTime::Seconds();
        UE_LOG(LogTemp, Display, TEXT("ASCENSION_NET_ENTER match=%u team=%u id=%u others=%d mode=%s iframe=%ums rewind_cap=%ums"), TeamCode, uint32(MyTeam), MyId, Packet.count,
               bMatch ? TEXT("match") : TEXT("practice"), IframeMs, RewindCapMs);
        break;
    }
    case Proto::PacketId::S_PlayerJoined:
    {
        Proto::S_PlayerJoined Packet;
        if (ReadFixed(Bytes, Packet)) SpawnRemote(Packet.player.playerId, Packet.player.name, ToFVector(Packet.player.position), Packet.player.yaw, Packet.player.team);
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
        UpdateServerClock(Packet.serverTick, ReceivedAt);
        for (int32 I = 0; I < Packet.count; ++I)
        {
            const Proto::MoveEntry Entry = ReadEntry<Proto::MoveEntry>(Bytes, sizeof(Packet) + I * sizeof(Proto::MoveEntry));
            if (Entry.playerId == MyId) continue; // 내 이동은 이미 화면에 있다
            TObjectPtr<ACLRemoteHero>* Remote = Remotes.Find(Entry.playerId);
            if (!Remote || !IsValid(*Remote)) continue;
            if (Entry.state == Proto::MoveState::Hidden) { (*Remote)->SetHiddenInBush(true); continue; } // 풀숲에 숨었다(위치가 오지 않는다)
            if ((*Remote)->bHiddenInBush)
            {
                (*Remote)->SetHiddenInBush(false);
                (*Remote)->ResetTo(ToFVector(Entry.position), Entry.yaw); // 숨었던 동안의 이동을 보간하지 않는다
            }
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
            if (!bSkipNextAckCompare)
            {
                P.MaxError = FMath::Max(P.MaxError, PredictionError);
                if (PredictionError > .5f) ++P.Corrections;
            }
            ++Done;
        }
        P.Pending.RemoveAt(0, Done, EAllowShrinking::No);

        bSkipNextAckCompare = false;
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
    case Proto::PacketId::S_MonsterBatch:
    {
        Proto::S_MonsterBatch Packet;
        if (!ReadFixed(Bytes, Packet, false) || Bytes.Num() != int32(sizeof(Packet) + Packet.count * sizeof(Proto::MonsterEntry))) break;
        UpdateServerClock(Packet.serverTick, ReceivedAt);
        const double ServerTime = Packet.serverTick * (Proto::kServerTickMs / 1000.0);
        ++MonsterBatches;
        // 서버가 살아 있는 몬스터 전부를 보낸다: 처음 보는 것은 만들고, 목록에서 빠진 것은 지운다.
        // 500m 필드에는 몬스터가 100마리 넘게 있으므로 내 주변(MonsterViewRadius)의 것만 화면에 만들고,
        // 한 번에 새로 만드는 수를 나눈다(한 프레임에 수십 마리의 모델을 만들지 않게).
        if (const ACLHero* Hero = LocalHero()) MonsterViewCenter = FVector2D(Hero->GetActorLocation());
        TSet<uint16> Alive;
        int32 Spawned = 0;
        for (int32 I = 0; I < Packet.count; ++I)
        {
            const Proto::MonsterEntry Entry = ReadEntry<Proto::MonsterEntry>(Bytes, sizeof(Packet) + I * sizeof(Proto::MonsterEntry));
            const FVector Position(Entry.x, Entry.y, 82.0); // 몬스터 캡슐 반높이 80
            const float Yaw = Entry.yaw * 360.f / 256.f;
            const float Distance = FVector2D::Distance(FVector2D(Position), MonsterViewCenter);
            ACLEnemy* Enemy = FindMonster(Entry.id);
            if (Distance > (Enemy ? MonsterViewRadius + 1500.f : MonsterViewRadius)) continue; // 멀어지면 아래에서 지운다
            Alive.Add(Entry.id);
            if (!Enemy)
            {
                if (Spawned >= MonsterSpawnsPerBatch) continue; // 다음 묶음(50ms 뒤)에 만든다
                ++Spawned;
                FActorSpawnParameters Params;
                Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                Enemy = GetWorld()->SpawnActor<ACLEnemy>(Position, FRotator(0, Yaw, 0), Params);
                if (!Enemy) continue;
                Enemy->InitPuppet(Entry.kind);
                Monsters.Add(Entry.id, Enemy);
            }
            if (Rift.BossId && Entry.id == Rift.BossId && Enemy->MaxHP < 1000)
            {
                // 균열 보스: 지휘관 모습을 키우고 체력바를 서버 체력(1,200)에 맞춘다
                Enemy->MaxHP = FMath::Max(1200.f, float(Entry.hp));
                Enemy->SetActorScale3D(FVector(1.8f));
            }
            Enemy->PushNetSample(ServerTime, Position, Yaw, uint8(Entry.state), float(Entry.hp));
        }
        for (auto It = Monsters.CreateIterator(); It; ++It)
        {
            if (Alive.Contains(It.Key())) continue;
            if (IsValid(It.Value())) It.Value()->Destroy();
            It.RemoveCurrent();
        }
        break;
    }
    case Proto::PacketId::S_MonsterHit:
    {
        Proto::S_MonsterHit Packet;
        if (!ReadFixed(Bytes, Packet)) break;
        if (Packet.attackerId == MyId)
        {
            ++MyMonsterHits;
            if (Packet.hpAfter == 0) ++MyKills;
        }
        if (ACLEnemy* Enemy = FindMonster(Packet.monsterId))
        {
            Enemy->ShowNetHit(Packet.damage, float(Packet.hpAfter));
            if (Packet.hpAfter == 0)
            {
                Enemy->Destroy();
                Monsters.Remove(Packet.monsterId);
            }
        }
        break;
    }
    case Proto::PacketId::S_WaveSpawn:
    {
        Proto::S_WaveSpawn Packet;
        if (!ReadFixed(Bytes, Packet)) break;
        if (Packet.ownerId == MyId)
        {
            ++WavesConfirmed; // 내 검기는 쏜 순간 이미 화면에 있다
            break;
        }
        RemoteWaves.Add({Packet.waveId, FVector(Packet.x, Packet.y, 100), FRotator(0, Packet.yaw, 0).Vector(), Packet.radius, Packet.speed, Packet.charge,
                         FPlatformTime::Seconds() + Packet.lifeMs / 1000.0});
        break;
    }
    case Proto::PacketId::S_WaveEnd:
    {
        Proto::S_WaveEnd Packet;
        if (ReadFixed(Bytes, Packet)) RemoteWaves.RemoveAll([&](const FRemoteWave& Wave) { return Wave.Id == Packet.waveId; });
        break;
    }
    case Proto::PacketId::S_Respawn:
    {
        Proto::S_Respawn Packet;
        if (!ReadFixed(Bytes, Packet) || Packet.playerId != MyId) break;
        // 서버가 시작 위치로 옮겼다. 이미 보낸 입력의 예측은 옛 위치 기준이라 다음 확정과 비교하지 않는다.
        ++Respawns;
        bSkipNextAckCompare = true;
        StartPrediction(ToFVector(Packet.position));
        MyRespawnAtMs = 0;
        bEliminated = false;
        if (ACLHero* Hero = LocalHero(); Hero && bMatch) Hero->ReviveOnline(Packet.hp); // 매치: 서버가 스스로 부활시켰다
        UE_LOG(LogTemp, Display, TEXT("ASCENSION_NET_RESPAWN hp=%.0f"), Packet.hp);
        break;
    }
    case Proto::PacketId::S_AttackCancel:
    {
        Proto::S_AttackCancel Packet;
        if (!ReadFixed(Bytes, Packet)) break;
        uint16 MonsterId = 0;
        if (TellAttacks.RemoveAndCopyValue(Packet.attackId, MonsterId))
        {
            if (ACLEnemy* Enemy = FindMonster(MonsterId)) Enemy->CancelNetTell();
        }
        if (AutoDodgeAttack == Packet.attackId) AutoDodgeAt = 0; // 끊긴 공격은 피하지 않는다
        break;
    }
    case Proto::PacketId::S_AttackStart:
    {
        Proto::S_AttackStart Packet;
        if (!ReadFixed(Bytes, Packet)) break;
        // 서버가 정한 명중 시각까지 남은 시간만큼 예고를 보여 준다(패킷이 늦게 왔으면 그만큼 짧다).
        const int32 HitInMs = int32(Packet.hitServerMs) - int32(ServerNowMs());
        const ACLHero* Hero = LocalHero();
        const FVector To = Hero ? (Hero->GetActorLocation() - ToFVector(Packet.center)) * FVector(1, 1, 0) : FVector::ZeroVector;
        const FVector Facing = FRotator(0, Packet.yaw, 0).Vector();
        bool bTargetsMe = false;
        int32 MyHitInMs = HitInMs; // 내가 맞는 시각까지(돌진은 거리에 따라 늦어진다)
        if (Packet.monsterId != 0)
        {
            // 몬스터 공격: 그 몬스터가 예고 자세를 잡고 부채꼴·돌진 길을 그린다(보간 지연 없이, 명중 시각까지).
            if (TellAttacks.Num() > 256) TellAttacks.Reset();
            TellAttacks.Add(Packet.attackId, Packet.monsterId);
            const bool bSecondStrike = Packet.kind == Proto::AttackKind::MonsterArc && Packet.hitServerMs - Packet.startServerMs < 500;
            if (ACLEnemy* Enemy = FindMonster(Packet.monsterId)) Enemy->ShowNetTell(Packet.yaw, FPlatformTime::Seconds() + HitInMs / 1000.0, bSecondStrike);
            if (Packet.kind == Proto::AttackKind::MonsterCharge)
            {
                // 돌진 길 위에 있으면: 출발 시각 + 내 위치까지 달려오는 시간(1200cm/s). 닿는 거리 105cm.
                const double Along = FVector::DotProduct(To, Facing), Side = FVector::CrossProduct(To, Facing).Size();
                bTargetsMe = Hero && Along > -105 && Along < Packet.radius + 105 && Side < 105;
                MyHitInMs = HitInMs + int32(FMath::Max(0.0, Along - 105) / 1.2);
            }
            else bTargetsMe = Hero && To.Size() <= Packet.radius && FVector::DotProduct(To.GetSafeNormal(), Facing) >= FMath::Cos(FMath::DegreesToRadians(Packet.halfAngle));
        }
        else
        {
            DrawDebugCircle(GetWorld(), ToFVector(Packet.center) - FVector(0, 0, 85), Packet.radius, 48,
                            Packet.kind == Proto::AttackKind::Slam ? FColor::Orange : FColor::Red, false, FMath::Max(.05f, HitInMs / 1000.f), 0, 4.f,
                            FVector(1, 0, 0), FVector(0, 1, 0), false);
            bTargetsMe = Packet.attackerId != MyId && Hero && To.Size() <= Packet.radius;
        }
        if (bTargetsMe && Packet.monsterId == 0)
            LocalHero()->Notice = FString::Printf(TEXT("공격 예고! %.2f초 뒤 명중 · 회피 무적 %.2f초"), HitInMs / 1000.f, IframeMs / 1000.f);
        if (bTargetsMe || Packet.monsterId == 0)
            UE_LOG(LogTemp, Display, TEXT("ASCENSION_NET_ATTACK id=%u attacker=%u monster=%u kind=%d hit_in=%dms"), Packet.attackId, Packet.attackerId,
                   uint32(Packet.monsterId), int32(Packet.kind), MyHitInMs);

        // 시험 옵션 -AscensionNetAutoDodge: 나를 노린 공격을 하나 걸러 하나씩 무적 구간 가운데에 맞춰 실제 회피 입력을 낸다.
        if (bTargetsMe && FParse::Param(FCommandLine::Get(), TEXT("AscensionNetAutoDodge")) && (AutoDodgeTurn++ % 2 == 0))
        {
            // 실제 시간으로 잰다. 게임 시간은 히트 스톱이나 2000fps 초과(언리얼의 최소 프레임 시간 0.5ms)에서 실제와 달라진다.
            AutoDodgeAt = FPlatformTime::Seconds() + FMath::Max(0, MyHitInMs - IframeMs / 2) / 1000.0 + 1e-6;
            AutoDodgeAttack = Packet.attackId;
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
            if (Packet.hpAfter <= 0.f) ++Deaths;
            if (ACLHero* Hero = LocalHero()) Hero->ApplyServerHit(bDodged, Packet.damage, Packet.rewindMs);
        }
        if (Packet.targetId == MyId) // 다른 플레이어의 판정은 기록하지 않는다(몬스터가 많으면 로그가 넘친다)
        UE_LOG(LogTemp, Display, TEXT("ASCENSION_NET_HIT id=%u target=%u%s result=%s damage=%.0f server_hp=%.0f local_hp=%.0f dodge_before_hit=%dms rewind=%ums"),
               Packet.attackId, Packet.targetId, Packet.targetId == MyId ? TEXT("(me)") : TEXT(""), bDodged ? TEXT("dodged") : TEXT("hit"),
               Packet.damage, Packet.hpAfter, LocalHero() ? LocalHero()->HP : -1.f,
               bDodged ? int32(Packet.hitServerMs) - int32(Packet.dodgeServerMs) : -1, uint32(Packet.rewindMs));
        break;
    }
    case Proto::PacketId::S_MatchState:
    {
        Proto::S_MatchState Packet;
        if (!ReadFixed(Bytes, Packet)) break;
        const uint8 PreviousPhase = Match.Phase, PreviousRound = Match.Round;
        Match.bValid = true;
        Match.Phase = uint8(Packet.phase);
        Match.Round = Packet.round;
        Match.TeamsAlive = Packet.teamsAlive;
        Match.Winner = Packet.winnerTeam;
        Match.bRespawnAllowed = Packet.respawnAllowed != 0;
        Match.PhaseStartMs = Packet.phaseStartServerMs;
        Match.PhaseEndMs = Packet.phaseEndServerMs;
        Match.Center = FVector2D(Packet.zoneX, Packet.zoneY);
        Match.RadiusFrom = Packet.zoneRadiusFrom;
        Match.RadiusTo = Packet.zoneRadiusTo;
        Match.NextRadius = Packet.zoneNextRadius;
        Match.DamagePercent = Packet.zoneDamagePercent;
        if (PreviousPhase != Match.Phase || PreviousRound != Match.Round)
        {
            UE_LOG(LogTemp, Display, TEXT("ASCENSION_NET_MATCH phase=%d round=%d teams=%d winner=%d radius=%.0f->%.0f damage=%.0f%%"), int32(Match.Phase),
                   int32(Match.Round), int32(Match.TeamsAlive), int32(Match.Winner), Match.RadiusFrom, Match.RadiusTo, Match.DamagePercent);
            if (Match.Phase == uint8(Proto::MatchPhase::Waiting)) bEliminated = false; // 다음 판
        }
        break;
    }
    case Proto::PacketId::S_PlayerProgress:
    {
        Proto::S_PlayerProgress Packet;
        if (!ReadFixed(Bytes, Packet)) break;
        if (Packet.playerId == MyId)
        {
            MyProgress.bValid = true;
            MyProgress.Level = Packet.level;
            MyProgress.Xp = Packet.xp;
            MyProgress.XpToNext = Packet.xpToNext;
            MyProgress.SkillPoints = Packet.skillPoints;
            MyProgress.AwakeningPoints = Packet.awakeningPoints;
            MyProgress.StatPoints = Packet.statPoints;
            if (ACLHero* Hero = LocalHero(); Hero && Packet.levelsGained > 0)
                Hero->Notice = FString::Printf(TEXT("레벨 업! %d레벨 · 스킬 포인트 %d · 스탯 포인트 %d%s"), Packet.level, Packet.skillPoints, Packet.statPoints,
                                               Packet.awakeningPoints ? *FString::Printf(TEXT(" · 각성 포인트 %d"), Packet.awakeningPoints) : TEXT(""));
            if (Packet.gainedXp > 0)
                UE_LOG(LogTemp, Display, TEXT("ASCENSION_NET_PROGRESS level=%d xp=%u/%u gained=%u reason=%d skill=%d stat=%d awaken=%d"), int32(Packet.level), Packet.xp,
                       Packet.xpToNext, Packet.gainedXp, int32(Packet.reason), int32(Packet.skillPoints), int32(Packet.statPoints), int32(Packet.awakeningPoints));
        }
        else if (const TObjectPtr<ACLRemoteHero>* Remote = Remotes.Find(Packet.playerId); Remote && IsValid(*Remote))
        {
            (*Remote)->Level = Packet.level;
        }
        break;
    }
    case Proto::PacketId::S_PlayerDied:
    {
        Proto::S_PlayerDied Packet;
        if (!ReadFixed(Bytes, Packet)) break;
        const TCHAR* Cause = Packet.cause == Proto::DeathCause::Zone ? TEXT("자기장") : Packet.cause == Proto::DeathCause::Monster ? TEXT("몬스터") : TEXT("플레이어");
        if (Packet.playerId == MyId)
        {
            MyRespawnAtMs = Packet.respawnServerMs;
            bEliminated = Packet.respawnServerMs == 0;
            if (ACLHero* Hero = LocalHero())
                Hero->Notice = bEliminated ? FString::Printf(TEXT("%s에게 쓰러져 탈락했습니다. 남은 팀원을 지켜보세요."), Cause)
                                           : FString::Printf(TEXT("%s에게 쓰러졌습니다. 잠시 뒤 자기장 안에서 부활합니다."), Cause);
        }
        else if (const TObjectPtr<ACLRemoteHero>* Remote = Remotes.Find(Packet.playerId); Remote && IsValid(*Remote))
        {
            if (ACLHero* Hero = LocalHero())
                Hero->Notice = FString::Printf(TEXT("%s 쓰러짐(%s)%s"), *(*Remote)->DisplayName, Cause, Packet.respawnServerMs == 0 ? TEXT(" · 탈락") : TEXT(""));
        }
        UE_LOG(LogTemp, Display, TEXT("ASCENSION_NET_DIED id=%u killer=%u cause=%d respawn_in=%.1fs"), Packet.playerId, Packet.killerId, int32(Packet.cause),
               Packet.respawnServerMs ? SecondsUntil(Packet.respawnServerMs) : -1.0);
        break;
    }
    case Proto::PacketId::S_Teleport:
    {
        Proto::S_Teleport Packet;
        if (!ReadFixed(Bytes, Packet)) break;
        const bool bEnter = Packet.reason == Proto::TeleportReason::RiftEnter;
        if (Packet.playerId == MyId)
        {
            // 균열 입장·퇴장: 서버가 옮겼다. 이미 보낸 입력의 예측은 옛 위치 기준이라 다음 확정과 비교하지 않는다.
            bSkipNextAckCompare = true;
            StartPrediction(ToFVector(Packet.position));
            bInRift = bEnter;
            if (ACLHero* Hero = LocalHero())
            {
                if (Hero->Controller) Hero->Controller->SetControlRotation(FRotator(-30, Packet.yaw, 0));
                Hero->Notice = bEnter ? TEXT("균열 전투장에 들어왔습니다!") : TEXT("균열에서 나왔습니다");
            }
        }
        else if (TObjectPtr<ACLRemoteHero>* Remote = Remotes.Find(Packet.playerId); Remote && IsValid(*Remote))
        {
            (*Remote)->ResetTo(ToFVector(Packet.position), Packet.yaw);
        }
        UE_LOG(LogTemp, Display, TEXT("ASCENSION_NET_TELEPORT id=%u reason=%d pos=(%.0f,%.0f)"), Packet.playerId, int32(Packet.reason), Packet.position.x, Packet.position.y);
        break;
    }
    case Proto::PacketId::S_RiftState:
    {
        Proto::S_RiftState Packet;
        if (!ReadFixed(Bytes, Packet)) break;
        const uint8 Previous = Rift.bValid ? Rift.Phase : 0;
        Rift.bValid = true;
        Rift.Phase = uint8(Packet.phase);
        for (int32 I = 0; I < 2; ++I) { Rift.GateTeam[I] = Packet.gateTeam[I]; Rift.Gate[I] = FVector2D(Packet.gateX[I], Packet.gateY[I]); }
        Rift.GateRadius = Packet.gateRadius;
        Rift.Arena = FVector2D(Packet.arenaX, Packet.arenaY);
        Rift.ArenaRadius = Packet.arenaRadius;
        Rift.PhaseStartMs = Packet.phaseStartServerMs; Rift.PhaseEndMs = Packet.phaseEndServerMs;
        Rift.ShrinkStartMs = Packet.zoneShrinkStartServerMs; Rift.ShrinkEndMs = Packet.zoneShrinkEndServerMs; Rift.LateMs = Packet.zoneLateServerMs;
        Rift.DamageEarly = Packet.zoneDamageEarly; Rift.DamageLate = Packet.zoneDamageLate;
        Rift.BossId = Packet.bossId; Rift.Winner = Packet.winnerTeam; Rift.RewardStats = Packet.rewardStats; Rift.RewardXp = Packet.rewardXp;
        if (Previous != Rift.Phase)
        {
            if (Rift.Phase == uint8(Proto::RiftPhase::Closed)) bInRift = false;
            ACLHero* Hero = LocalHero();
            const TCHAR* Text = nullptr;
            switch (Proto::RiftPhase(Rift.Phase))
            {
            case Proto::RiftPhase::Open: Text = TEXT("균열이 열렸습니다! 중앙 광장 보라색 입구에 1초 서 있으면 들어갑니다"); break;
            case Proto::RiftPhase::Waiting: Text = TEXT("한 팀이 균열에 들어갔습니다. 다른 입구로 도전할 수 있습니다"); break;
            case Proto::RiftPhase::Pvp: Text = TEXT("균열 대결 시작! 상대 팀을 전멸시키세요"); break;
            case Proto::RiftPhase::Boss: Text = TEXT("상대가 오지 않아 균열 보스가 나타났습니다"); break;
            default: break;
            }
            if (Rift.Phase == uint8(Proto::RiftPhase::Finished))
                Text = Rift.Winner == MyTeam ? TEXT("균열 승리! 경험치와 스탯 포인트 +3") : Rift.Winner ? TEXT("균열 대결이 끝났습니다") : TEXT("균열이 보상 없이 끝났습니다");
            if (Hero && Text) Hero->Notice = Text;
            UE_LOG(LogTemp, Display, TEXT("ASCENSION_NET_RIFT phase=%d gates=%d,%d boss=%u winner=%d"), int32(Rift.Phase), int32(Rift.GateTeam[0]), int32(Rift.GateTeam[1]),
                   uint32(Rift.BossId), int32(Rift.Winner));
        }
        break;
    }
    case Proto::PacketId::S_Loadout:
    {
        Proto::S_Loadout Packet;
        if (!ReadFixed(Bytes, Packet) || Packet.playerId != MyId) break;
        ACLHero* Hero = LocalHero();
        if (!Hero || !Hero->Skills) break;
        uint8 Numbers[4], Ranks[4], Awakened[4];
        uint16 Stats[4];
        for (int32 I = 0; I < 4; ++I) { Numbers[I] = Packet.slots[I].skillNumber; Ranks[I] = Packet.slots[I].rank; Awakened[I] = Packet.slots[I].awakened; Stats[I] = Packet.stats[I]; }
        Hero->Skills->Level = MyProgress.bValid ? MyProgress.Level : 1;
        Hero->Skills->SetLoadout(Numbers, Ranks, Awakened, Stats, Packet.skillPointsLeft, Packet.awakeningPointsLeft, Packet.statPointsLeft);
        if (Packet.result != Proto::LoadoutResult::Ok)
        {
            const TCHAR* Why = Packet.result == Proto::LoadoutResult::NoPoints ? TEXT("포인트 부족") : Packet.result == Proto::LoadoutResult::SlotsFull ? TEXT("스킬 칸이 가득 참")
                             : Packet.result == Proto::LoadoutResult::Locked ? TEXT("해금 조건 부족") : Packet.result == Proto::LoadoutResult::InCombat ? TEXT("전투 중에는 지울 수 없음")
                             : Packet.result == Proto::LoadoutResult::MaxRank ? TEXT("최대 단계") : Packet.result == Proto::LoadoutResult::NotNow ? TEXT("지금은 할 수 없음") : TEXT("서버가 거부함");
            Hero->Skills->LastMessage = FString::Printf(TEXT("서버: %s"), Why);
            Hero->Skills->LastMessageUntil = GetWorld()->GetTimeSeconds() + 2;
        }
        UE_LOG(LogTemp, Display, TEXT("ASCENSION_NET_LOADOUT result=%d action=%d skill=%d slots=%d,%d,%d,%d stats=%d,%d,%d,%d left=%d/%d/%d"), int32(Packet.result), int32(Packet.action),
               int32(Packet.skillNumber), int32(Numbers[0]), int32(Numbers[1]), int32(Numbers[2]), int32(Numbers[3]), int32(Stats[0]), int32(Stats[1]), int32(Stats[2]), int32(Stats[3]),
               int32(Packet.skillPointsLeft), int32(Packet.statPointsLeft), int32(Packet.awakeningPointsLeft));
        break;
    }
    default:
        UE_LOG(LogTemp, Warning, TEXT("ASCENSION_NET_UNKNOWN_PACKET id=%d"), int32(Header.id));
        break;
    }
}

void ACLNetSession::BindSkillCommands()
{
    ACLHero* Hero = LocalHero();
    if (!Hero || !Hero->Skills) return;
    TWeakObjectPtr<ACLNetSession> Self(this);
    Hero->Skills->SendSkillCommand = [Self](uint8 Action, uint8 Number) { if (Self.IsValid()) Self->SendSkillCommand(Action, Number); };
    Hero->Skills->SendStatCommand = [Self](uint8 Stat, uint8 Amount) { if (Self.IsValid()) Self->SendStatCommand(Stat, Amount); };
}

void ACLNetSession::SendSkillCommand(uint8 Action, uint8 Number)
{
    if (State != EState::InRoom) return;
    Proto::C_SkillCommand Packet = Proto::Make<Proto::C_SkillCommand>();
    Packet.action = Proto::SkillAction(Action);
    Packet.skillNumber = Number;
    SendPacket(Packet);
}

void ACLNetSession::SendStatCommand(uint8 Stat, uint8 Amount)
{
    if (State != EState::InRoom) return;
    Proto::C_StatCommand Packet = Proto::Make<Proto::C_StatCommand>();
    Packet.stat = Stat;
    Packet.amount = Amount;
    SendPacket(Packet);
}

float ACLNetSession::RiftZoneRadiusNow() const
{
    if (Rift.Phase != uint8(Proto::RiftPhase::Pvp) || !Rift.ShrinkStartMs) return Rift.ArenaRadius;
    const double Into = -SecondsUntil(Rift.ShrinkStartMs), Length = FMath::Max(.001, (double(Rift.ShrinkEndMs) - double(Rift.ShrinkStartMs)) / 1000.0);
    return Rift.ArenaRadius * float(1.0 - FMath::Clamp(Into / Length, 0.0, 1.0));
}

bool ACLNetSession::RiftGateOpenFor(int32 Gate, uint8 Team) const
{
    const uint8 GateOwner = Rift.GateTeam[Gate], Other = Rift.GateTeam[1 - Gate];
    switch (Proto::RiftPhase(Rift.Phase))
    {
    case Proto::RiftPhase::Open: return GateOwner == 0;
    case Proto::RiftPhase::Waiting: return GateOwner == Team || (GateOwner == 0 && Other != Team);
    case Proto::RiftPhase::Pvp: return GateOwner == Team && SecondsUntil(Rift.PhaseStartMs) > -10.0; // 시작 뒤 잠시 팀원 합류(정확한 시간은 서버가 판단)
    default: return false;
    }
}

FString ACLNetSession::RiftText() const
{
    if (!InMatch() || !Rift.bValid) return FString();
    const auto Clock = [this](uint32 Ms) { const int32 L = FMath::Max(0, FMath::CeilToInt(SecondsUntil(Ms))); return FString::Printf(TEXT("%d:%02d"), L / 60, L % 60); };
    switch (Proto::RiftPhase(Rift.Phase))
    {
    case Proto::RiftPhase::Open: return TEXT("균열 열림 · 중앙 광장 입구 2곳 (입구 하나에 한 팀)");
    case Proto::RiftPhase::Waiting:
        return FString::Printf(TEXT("균열: 팀 %d 입장, 상대 대기 %s · 안 오면 보스전"), FMath::Max(Rift.GateTeam[0], Rift.GateTeam[1]), *Clock(Rift.PhaseEndMs));
    case Proto::RiftPhase::Pvp:
    {
        const bool bLate = Rift.LateMs && SecondsUntil(Rift.LateMs) <= 0, bShrink = Rift.ShrinkStartMs && SecondsUntil(Rift.ShrinkStartMs) <= 0;
        return FString::Printf(TEXT("균열 대결: 팀 %d 대 팀 %d · %s · 전투장 자기장 %s"), Rift.GateTeam[0], Rift.GateTeam[1], *Clock(Rift.PhaseEndMs),
                               bShrink ? *FString::Printf(TEXT("%.0fm (초당 %.0f%%)"), RiftZoneRadiusNow() / 100.f, bLate ? Rift.DamageLate : Rift.DamageEarly)
                                       : *FString::Printf(TEXT("%s 뒤 축소"), *Clock(Rift.ShrinkStartMs)));
    }
    case Proto::RiftPhase::Boss: return FString::Printf(TEXT("균열 보스전: 팀 %d · 남은 시간 %s"), FMath::Max(Rift.GateTeam[0], Rift.GateTeam[1]), *Clock(Rift.PhaseEndMs));
    case Proto::RiftPhase::Finished:
        return Rift.Winner ? FString::Printf(TEXT("균열 결과: 팀 %d 승리 (경험치 %u · 스탯 +%d)"), Rift.Winner, Rift.RewardXp, Rift.RewardStats) : FString(TEXT("균열 결과: 보상 없음"));
    default: return FString();
    }
}

void ACLNetSession::DrawRift() const
{
    if (!InMatch() || !Rift.bValid) return;
    UWorld* World = GetWorld();
    const double T = World->GetTimeSeconds();
    const bool bGatesOpen = Rift.Phase == uint8(Proto::RiftPhase::Open) || Rift.Phase == uint8(Proto::RiftPhase::Waiting) || Rift.Phase == uint8(Proto::RiftPhase::Pvp);
    if (bGatesOpen)
        for (int32 I = 0; I < 2; ++I)
        {
            // 보라색 소용돌이 입구: 들어갈 수 있으면 밝게, 다른 팀이 차지했으면 붉게
            const bool bMine = RiftGateOpenFor(I, MyTeam);
            const FColor Color = Rift.GateTeam[I] && Rift.GateTeam[I] != MyTeam ? FColor(200, 60, 80) : bMine ? FColor(190, 90, 255) : FColor(110, 70, 140);
            const FVector C(Rift.Gate[I].X, Rift.Gate[I].Y, 6);
            for (int32 Ring = 0; Ring < 3; ++Ring)
            {
                const float R = Rift.GateRadius * (.45f + .27f * Ring) + 10.f * FMath::Sin(float(T * 3 + Ring));
                DrawDebugCircle(World, C, R, 40, Color, false, -1, 0, 4 - Ring, FVector(1, 0, 0), FVector(0, 1, 0), false);
            }
            for (int32 K = 0; K < 6; ++K)
            {
                const float A = float(T * 1.7) + K * PI / 3;
                const FVector Base = C + FVector(FMath::Cos(A), FMath::Sin(A), 0) * Rift.GateRadius * .8f;
                DrawDebugLine(World, Base, Base + FVector(0, 0, 220 + 40 * FMath::Sin(float(T * 2 + K))), Color, false, -1, 0, 3);
            }
            const FString Label = Rift.GateTeam[I] ? FString::Printf(TEXT("균열 입구 %d · 팀 %d"), I + 1, Rift.GateTeam[I]) : FString::Printf(TEXT("균열 입구 %d"), I + 1);
            DrawDebugString(World, C + FVector(0, 0, 280), Label, nullptr, Color, 0.f, true, 1.3f);
        }
    if (Rift.Phase == uint8(Proto::RiftPhase::Pvp))
    {
        const FVector C(Rift.Arena.X, Rift.Arena.Y, 12);
        DrawDebugCircle(World, C, FMath::Max(RiftZoneRadiusNow(), 20.f), 96, FColor(200, 80, 255), false, -1, 0, 10, FVector(1, 0, 0), FVector(0, 1, 0), false);
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
        if (!AutoHuntMove.IsNearlyZero()) Direction = AutoHuntMove; // -AscensionNetAutoHunt: 캠프까지 걸어간다
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

void ACLNetSession::SendWave(float Yaw, bool bHeavy, float Charge)
{
    if (State != EState::InRoom) return; // 검기는 두 모드 모두 서버 몬스터를 친다
    auto Attack = Proto::Make<Proto::C_Attack>();
    Attack.sequence = ++AttackSequence;
    Attack.pressServerMs = ServerNowMs();
    Attack.yaw = Yaw;
    Attack.kind = bHeavy ? Proto::AttackKind::HeavyWave : Proto::AttackKind::Wave;
    Attack.charge = bHeavy ? Charge : 0.f;
    SendPacket(Attack);
    ++WavesSent;
}

void ACLNetSession::SendRespawn()
{
    if (State != EState::InRoom) return;
    SendPacket(Proto::Make<Proto::C_Respawn>());
}

void ACLNetSession::UpdateServerClock(uint32 ServerTick, double ReceivedAt)
{
    // 서버 틱 번호 × 50ms = 서버 시간. 늦게 도착한 패킷일수록 차이가 크므로 가장 작은 값을 따라가되 시계 차이를 조금씩 허용한다.
    const double Offset = ReceivedAt - ServerTick * (Proto::kServerTickMs / 1000.0);
    ServerClockOffset = bHasClock ? FMath::Min(Offset, ServerClockOffset + .001) : Offset;
    bHasClock = true;
}

ACLEnemy* ACLNetSession::FindMonster(uint16 Id) const
{
    const TObjectPtr<ACLEnemy>* Found = Monsters.Find(Id);
    return Found && IsValid(*Found) ? Found->Get() : nullptr;
}

void ACLNetSession::TickMonsters(double RenderTime, float Delta)
{
    for (const auto& Pair : Monsters)
    {
        if (IsValid(Pair.Value)) Pair.Value->UpdatePuppet(RenderTime, Delta);
    }
    // 다른 플레이어의 검기: 판정은 서버가 하고 여기서는 날아가는 모습만 그린다(1인 데모의 검기와 같은 모양).
    const double Now = FPlatformTime::Seconds();
    for (int32 I = RemoteWaves.Num() - 1; I >= 0; --I)
    {
        FRemoteWave& Wave = RemoteWaves[I];
        if (Now >= Wave.EndTime)
        {
            RemoteWaves.RemoveAtSwap(I);
            continue;
        }
        Wave.Position += Wave.Direction * Wave.Speed * Delta;
        const FColor Color = Wave.Radius > 60 ? FColor(255, 200, 70) : FColor::Cyan;
        for (int32 Step = 0; Step < 12; ++Step)
        {
            const FVector A = Wave.Position + Wave.Direction.RotateAngleAxis(-65 + 130.f * Step / 12, FVector::UpVector) * Wave.Radius;
            const FVector B = Wave.Position + Wave.Direction.RotateAngleAxis(-65 + 130.f * (Step + 1) / 12, FVector::UpVector) * Wave.Radius;
            DrawDebugLine(GetWorld(), A, B, Color, false, .05f, 0, 7 + Wave.Charge * 6);
        }
    }
}

// 시험 옵션 -AscensionNetAutoHunt: 가장 가까운 몬스터를 향해 0.8초마다 실제 공격 입력을 내고, 죽으면 1초 뒤 다시 시작한다.
void ACLNetSession::TickAutoHunt(double Now)
{
    // 시험 옵션 -AscensionNetAutoLearn: 포인트가 생기면 서버에 배우기·강화·스탯 분배를 보낸다(포인트 쓰기 접속 시험)
    static const bool bAutoLearn = FParse::Param(FCommandLine::Get(), TEXT("AscensionNetAutoLearn"));
    if (bAutoLearn && Now >= NextAutoLearn && State == EState::InRoom)
        if (ACLHero* Hero = LocalHero(); Hero && Hero->Skills && Hero->HP > 0)
        {
            NextAutoLearn = Now + 1.0;
            UCLSkillComponent* S = Hero->Skills;
            if (S->StatPointsLeft > 0) S->RequestStat(ECLStat::Strength, 1);
            else if (S->SkillPointsLeft > 0)
            {
                using A = UCLSkillComponent::EAction;
                const FCLSkillDef* Pick = nullptr; A Act = A::Learn;
                for (const FCLSkillDef& Def : UCLSkillComponent::AllSkills())
                {
                    if (S->CheckAction(A::Learn, Def).IsEmpty()) { Pick = &Def; Act = A::Learn; break; }
                    if (S->CheckAction(A::Upgrade, Def).IsEmpty() && !Pick) { Pick = &Def; Act = A::Upgrade; }
                }
                if (Pick) S->RequestSkill(Act, *Pick);
            }
        }
    static const bool bAutoHunt = FParse::Param(FCommandLine::Get(), TEXT("AscensionNetAutoHunt"));
    ACLHero* Hero = LocalHero();
    if (!bAutoHunt || !Hero) return;
    if (Hero->HP <= 0)
    {
        if (DeadSince == 0) DeadSince = Now;
        else if (Now - DeadSince > 1.0)
        {
            DeadSince = 0;
            Hero->ResetLife();
        }
        return;
    }
    DeadSince = 0;
    if (Now < NextAutoHunt) return;
    NextAutoHunt = Now + .3;
    // 가장 가까운 몬스터. 캠프 몬스터는 먼저 오지 않으므로 6m 밖이면 그쪽으로 걸어간다(TickPrediction이 AutoHuntMove를 쓴다).
    const ACLEnemy* Nearest = nullptr;
    double Best = 1e9;
    for (const auto& Pair : Monsters)
    {
        if (!IsValid(Pair.Value)) continue;
        const double Distance = FVector::Dist2D(Pair.Value->GetActorLocation(), Hero->GetActorLocation());
        if (Distance < Best) Best = Distance, Nearest = Pair.Value;
    }
    AutoHuntMove = FVector::ZeroVector;
    if (!Nearest || !Hero->GetController()) return;
    const FVector To = (Nearest->GetActorLocation() - Hero->GetActorLocation()).GetSafeNormal2D();
    if (Best > 600) AutoHuntMove = To;
    Hero->GetController()->SetControlRotation(FRotator(-30, To.Rotation().Yaw, 0));
    if (Best <= 1300) Hero->Attack();
}

void ACLNetSession::SendAttack(float Yaw)
{
    if (State != EState::InRoom || !bMatch) return; // 근접 공격은 매치에서 다른 팀 플레이어를 친다
    auto Attack = Proto::Make<Proto::C_Attack>();
    Attack.sequence = ++AttackSequence;
    Attack.pressServerMs = ServerNowMs();
    Attack.yaw = Yaw;
    Attack.kind = Proto::AttackKind::Slash;
    SendPacket(Attack);
}

void ACLNetSession::SpawnRemote(uint32 Id, const char* Name, const FVector& Position, float Yaw, uint8 Team)
{
    if (Id == MyId || Remotes.Contains(Id)) return;
    char Terminated[Proto::kNameLength + 1] = {};
    FMemory::Memcpy(Terminated, Name, Proto::kNameLength);

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if (ACLRemoteHero* Remote = GetWorld()->SpawnActor<ACLRemoteHero>(Position, FRotator(0, Yaw, 0), Params))
    {
        Remote->Init(Id, UTF8_TO_TCHAR(Terminated), Team, !bMatch || Team == MyTeam);
        Remotes.Add(Id, Remote);
        if (ACLHero* Hero = LocalHero()) Hero->Notice = FString::Printf(TEXT("%s 님이 들어왔습니다."), *Remote->DisplayName);
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
    for (const auto& Pair : Monsters)
    {
        if (IsValid(Pair.Value)) Pair.Value->Destroy();
    }
    Monsters.Empty();
    RemoteWaves.Empty();
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
    case EState::Entering: return FString::Printf(TEXT("온라인 · 판 %u 입장 중..."), TeamCode);
    case EState::InRoom:
        return FString::Printf(TEXT("온라인 %s · 판 %u · 같은 방 %d/%d명 · 지연 %ums · 무적 %.2f초%s%s"),
                               bMatch ? *FString::Printf(TEXT("매치 · 내 팀 %d"), MyTeam) : TEXT("연습 방"), TeamCode,
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

// ---- 매치 진행 표시 ----

double ACLNetSession::SecondsUntil(uint32 ServerMs) const
{
    return (int64(ServerMs) - int64(ServerNowMs())) / 1000.0;
}

float ACLNetSession::ZoneRadiusNow() const
{
    // 서버와 같은 계산: 단계 시작~끝 동안 From → To로 일정하게 줄어든다
    if (!Match.bValid) return 0.f;
    const int64 Length = int64(Match.PhaseEndMs) - int64(Match.PhaseStartMs);
    if (Length <= 0 || Match.RadiusFrom == Match.RadiusTo) return Match.RadiusTo;
    const double T = FMath::Clamp((int64(ServerNowMs()) - int64(Match.PhaseStartMs)) / double(Length), 0.0, 1.0);
    return float(Match.RadiusFrom + (Match.RadiusTo - Match.RadiusFrom) * T);
}

bool ACLNetSession::IsOutsideZone(const FVector& Position) const
{
    return InMatch() && FVector2D::Distance(FVector2D(Position.X, Position.Y), Match.Center) > ZoneRadiusNow();
}

void ACLNetSession::DrawZone()
{
    const bool bShow = InMatch() && !bInRift && Match.Phase != uint8(Proto::MatchPhase::Waiting);
    if (!Zone && bShow)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Zone = GetWorld()->SpawnActor<ACLZone>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
        if (Zone && !Zone->HasMaterials()) UE_LOG(LogTemp, Warning, TEXT("ASCENSION_ZONE 재질(M_ZoneWall·M_ZoneNext)이 없어 선으로 그립니다. CreateMap.py를 다시 실행하세요."));
    }
    if (!bShow)
    {
        if (Zone) Zone->Show(FVector2D::ZeroVector, 0, 0, false);
        return;
    }
    const float Radius = ZoneRadiusNow(), Next = ZoneNextRadius();
    if (Zone && Zone->HasMaterials())
    {
        Zone->Show(Match.Center, Radius, Next, true);
        return;
    }
    // 재질이 없는 예전 맵: 지금 자기장(파랑)과 다음 자리(흰색)를 선으로
    const FVector Center(Match.Center.X, Match.Center.Y, 15);
    DrawDebugCircle(GetWorld(), Center, FMath::Max(Radius, 20.f), 256, FColor(60, 140, 255), false, -1, 0, 10, FVector(1, 0, 0), FVector(0, 1, 0), false);
    if (Next < Radius - 1)
        DrawDebugCircle(GetWorld(), Center, FMath::Max(Next, 20.f), 192, FColor(230, 230, 230), false, -1, 0, 3, FVector(1, 0, 0), FVector(0, 1, 0), false);
}

FString ACLNetSession::MatchText() const
{
    if (!InMatch()) return FString();
    const double Left = Match.PhaseEndMs ? FMath::Max(0.0, SecondsUntil(Match.PhaseEndMs)) : 0.0;
    const int32 L = FMath::CeilToInt(Left);
    const FString Time = FString::Printf(TEXT("%d:%02d"), L / 60, L % 60);
    FString Phase;
    switch (Proto::MatchPhase(Match.Phase))
    {
    case Proto::MatchPhase::Waiting: Phase = TEXT("다른 팀을 기다리는 중"); break;
    case Proto::MatchPhase::Countdown: Phase = FString::Printf(TEXT("시작까지 %s"), *Time); break;
    case Proto::MatchPhase::Round:
        Phase = Match.Round == 4 ? FString::Printf(TEXT("4라운드 · 자기장 계속 축소 %s"), *Time) : FString::Printf(TEXT("%d라운드 · 자기장 축소까지 %s"), Match.Round, *Time);
        break;
    case Proto::MatchPhase::Shrink: Phase = FString::Printf(TEXT("%d라운드 끝 · 자기장 축소 중 %s"), Match.Round, *Time); break;
    case Proto::MatchPhase::Ended: Phase = Match.Winner ? FString::Printf(TEXT("팀 %d 승리"), Match.Winner) : FString(TEXT("무승부")); break;
    }
    return FString::Printf(TEXT("매치 · %s · 자기장 %.0fm · 밖 피해 초당 %.0f%% · 남은 팀 %d · 내 팀 %d%s"), *Phase, ZoneRadiusNow() / 100.f, Match.DamagePercent,
                           Match.TeamsAlive, MyTeam, Match.bRespawnAllowed ? TEXT("") : TEXT(" · 부활 없음"));
}

FString ACLNetSession::MatchBanner() const
{
    if (!InMatch()) return FString();
    if (Match.Phase == uint8(Proto::MatchPhase::Ended))
        return Match.Winner == 0 ? FString(TEXT("무승부 (동시 전멸)")) : Match.Winner == MyTeam ? FString(TEXT("승리! 최후의 1팀")) : FString::Printf(TEXT("팀 %d 승리"), Match.Winner);
    if (bEliminated) return TEXT("탈락 · 남은 팀원을 지켜보세요");
    if (MyRespawnAtMs) return FString::Printf(TEXT("부활까지 %.0f초"), FMath::Max(0.0, FMath::CeilToDouble(SecondsUntil(MyRespawnAtMs))));
    if (Match.Phase == uint8(Proto::MatchPhase::Countdown)) return FString::Printf(TEXT("%.0f"), FMath::Max(1.0, FMath::CeilToDouble(SecondsUntil(Match.PhaseEndMs))));
    if (const ACLHero* Hero = LocalHero(); Hero && Hero->HP > 0 && IsOutsideZone(Hero->GetActorLocation()) && Match.DamagePercent > 0)
        return TEXT("자기장 밖! 안으로 들어가세요");
    return FString();
}

FString ACLNetSession::ProgressText() const
{
    if (!MyProgress.bValid) return FString();
    const FString Xp = MyProgress.XpToNext ? FString::Printf(TEXT("경험치 %u/%u"), MyProgress.Xp, MyProgress.XpToNext) : FString(TEXT("최대 레벨"));
    return FString::Printf(TEXT("Lv %d · %s · 받은 포인트: 스킬 %d · 스탯 %d%s"), MyProgress.Level, *Xp, MyProgress.SkillPoints, MyProgress.StatPoints,
                           MyProgress.AwakeningPoints ? *FString::Printf(TEXT(" · 각성 %d"), MyProgress.AwakeningPoints) : TEXT(""));
}
