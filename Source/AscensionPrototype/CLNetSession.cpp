#include "CLNetSession.h"

#include "AscensionProtocol.h"
#include "CLRemoteHero.h"
#include "CombatLab.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Queue.h"
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
                Inbox.Enqueue(TArray<uint8>(Pending.GetData() + Offset, Header.size));
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

    TQueue<TArray<uint8>, EQueueMode::Spsc> Inbox;
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

void ACLNetSession::Connect(const FString& HostPort, const FString& InPlayerName, uint32 InTeamCode)
{
    FString Host = HostPort, PortText;
    int32 Port = Proto::kDefaultPort;
    if (HostPort.Split(TEXT(":"), &Host, &PortText)) Port = FCString::Atoi(*PortText);

    ServerText = FString::Printf(TEXT("%s:%d"), *Host, Port);
    PlayerName = InPlayerName;
    TeamCode = InTeamCode;
    State = EState::Connecting;

    Receiver = new FCLNetReceiver(Host, Port);
    Thread = FRunnableThread::Create(Receiver, TEXT("AscensionNetReceiver"));
    UE_LOG(LogTemp, Display, TEXT("ASCENSION_NET_CONNECT %s name=%s team=%u"), *ServerText, *PlayerName, TeamCode);
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

    TArray<uint8> Bytes;
    while (Receiver->Inbox.Dequeue(Bytes)) HandlePacket(Bytes);

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
        NextPing = Now + 1.0;
        auto Ping = Proto::Make<Proto::C_Ping>();
        Ping.clientTimeMs = NowMs();
        SendPacket(Ping);
    }

    const double RenderTime = Now - ServerClockOffset - InterpolationDelay;
    for (const auto& Pair : Remotes)
    {
        if (IsValid(Pair.Value)) Pair.Value->UpdateVisual(RenderTime, Delta);
    }
}

void ACLNetSession::HandlePacket(const TArray<uint8>& Bytes)
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
            Fail(Packet.result == Proto::EnterResult::RoomFull ? FString(TEXT("팀 방이 가득 찼습니다 (최대 3명). 다른 팀 코드를 쓰세요."))
                                                         : FString(TEXT("방 입장 실패")));
            break;
        }
        State = EState::InRoom;
        if (ACLHero* Hero = LocalHero())
        {
            Hero->SetActorLocation(ToFVector(Packet.spawnPosition), false, nullptr, ETeleportType::TeleportPhysics);
            Hero->GetCharacterMovement()->StopMovementImmediately();
        }
        for (int32 I = 0; I < Packet.count; ++I)
        {
            const Proto::PlayerInfo Info = ReadEntry<Proto::PlayerInfo>(Bytes, sizeof(Packet) + I * sizeof(Proto::PlayerInfo));
            SpawnRemote(Info.playerId, Info.name, ToFVector(Info.position), Info.yaw);
        }
        NextMoveSend = FPlatformTime::Seconds();
        UE_LOG(LogTemp, Display, TEXT("ASCENSION_NET_ENTER team=%u id=%u others=%d"), TeamCode, MyId, Packet.count);
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
        const double Offset = FPlatformTime::Seconds() - ServerTime;
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
        if (ReadFixed(Bytes, Packet)) RttMs = NowMs() - Packet.clientTimeMs;
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
        return FString::Printf(TEXT("온라인 · 팀 %u · 같은 층 %d/%d명 · 지연 %ums%s"), TeamCode, Remotes.Num() + 1, Proto::kMaxRoomPlayers, RttMs,
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
