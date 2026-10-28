// 서버 연결 하나를 담당하는 액터. -AscensionServer=IP:포트 옵션으로 실행했을 때만 생성된다.
//
// 스레드 구조
// - 수신 스레드(FCLNetReceiver): 접속 → 바이트 수신 → 패킷 단위로 잘라 Inbox 큐에 넣는다.
// - 게임 스레드(Tick): Inbox의 패킷을 처리하고, 15Hz로 내 이동을 보낸다.
//   액터·월드는 게임 스레드에서만 건드린다.
//
// 판정 원칙: 이 클래스는 입력(이동 상태)을 보내고 서버 결과를 화면에 반영만 한다.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CLNetSession.generated.h"

class ACLHero;
class ACLRemoteHero;
class FCLNetReceiver;
class FRunnableThread;

UCLASS()
class ACLNetSession : public AActor
{
    GENERATED_BODY()
public:
    ACLNetSession();
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void BeginDestroy() override;

    // HostPort 예: "127.0.0.1:7777". TeamCode가 같은 플레이어끼리 같은 층에 들어간다.
    void Connect(const FString& HostPort, const FString& InPlayerName, uint32 InTeamCode);
    FString StatusText() const;

    // 원격 플레이어를 서버 시간보다 이만큼 늦게 그려서 두 스냅샷 사이를 보간한다(서버 2틱).
    static constexpr double InterpolationDelay = .1;
    static constexpr double MoveSendInterval = 1. / 15.;

private:
    enum class EState : uint8 { Idle, Connecting, LoggingIn, Entering, InRoom, Failed };

    void HandlePacket(const TArray<uint8>& Bytes);
    void SendBytes(const void* Data, int32 Size);
    template <class T> void SendPacket(const T& Packet) { SendBytes(&Packet, sizeof(T)); }
    void SendMove();
    void SpawnRemote(uint32 Id, const char* Name, const FVector& Position, float Yaw);
    void RemoveRemote(uint32 Id);
    void Fail(const FString& Reason);
    void Shutdown();
    ACLHero* LocalHero() const;

    FCLNetReceiver* Receiver = nullptr; // Shutdown()에서 스레드를 멈춘 뒤 지운다
    FRunnableThread* Thread = nullptr;

    EState State = EState::Idle;
    FString ServerText, PlayerName, Error;
    uint32 TeamCode = 0, MyId = 0, MoveSequence = 0, RttMs = 0, Corrections = 0;
    double NextMoveSend = 0, NextPing = 0;
    // 로컬 시계 - 서버 시계. 가장 작은(지연이 가장 적은) 값을 쓴다.
    double ServerClockOffset = 0;
    bool bHasClock = false;

    UPROPERTY() TMap<uint32, TObjectPtr<ACLRemoteHero>> Remotes;
};
