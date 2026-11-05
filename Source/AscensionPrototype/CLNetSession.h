// 서버 연결 하나를 담당하는 액터. -AscensionServer=IP:포트 옵션으로 실행했을 때만 생성된다.
//
// 스레드 구조
// - 수신 스레드(FCLNetReceiver): 접속 → 바이트 수신 → 패킷 단위로 잘라 도착 시각과 함께 Inbox 큐에 넣는다.
//   시계 맞추기는 처리 시각이 아니라 도착 시각으로 한다(게임 프레임이 길어도 서버 시각 추정이 밀리지 않게).
// - 게임 스레드(Tick): Inbox의 패킷을 처리하고, 60Hz 고정 틱으로 내 이동을 예측하며 입력을 30Hz로 묶어 보낸다.
//
// 이동(v3, 서버 권한): 서버와 같은 이동 코드(AscensionMovement.h)로 미리 움직이고, 서버 확정 상태(S_MoveAck)를 받으면
// 그 상태에서 아직 확정되지 않은 입력을 다시 돌려 맞춘다. 차이는 화면에서 순간이동하지 않도록 짧게 녹인다.
//   액터·월드는 게임 스레드에서만 건드린다.
//
// 판정 원칙: 이 클래스는 입력(이동 상태·회피·공격)을 보내고 서버 결과를 화면에 반영만 한다.
// 회피·공격에는 S_Pong으로 추정한 서버 시각을 붙인다. 서버는 그 시각을 모드별 상한 안에서만 인정한다.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CLNetSession.generated.h"

class ACLHero;
class ACLRemoteHero;
class FCLNetReceiver;
class FRunnableThread;
struct FCLPrediction;

UCLASS()
class ACLNetSession : public AActor
{
    GENERATED_BODY()
public:
    ACLNetSession();
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void BeginDestroy() override;

    // HostPort 예: "127.0.0.1:7777". 모드와 TeamCode가 같은 플레이어끼리 같은 방에 들어간다.
    void Connect(const FString& HostPort, const FString& InPlayerName, uint32 InTeamCode, bool bInPvp);
    FString StatusText() const;

    // 로컬에서 회피·공격을 시작한 순간 부른다. 판정은 서버 결과(S_HitResult)로 받는다.
    void SendDodge(const FVector& Direction);
    void SendAttack(float Yaw);

    // 원격 플레이어를 서버 시간보다 이만큼 늦게 그려서 두 스냅샷 사이를 보간한다(서버 2틱).
    static constexpr double InterpolationDelay = .1;
    static constexpr double InputSendInterval = 1. / 30.;

private:
    enum class EState : uint8 { Idle, Connecting, LoggingIn, Entering, InRoom, Failed };

    void HandlePacket(const TArray<uint8>& Bytes, double ReceivedAt);
    void SendBytes(const void* Data, int32 Size);
    template <class T> void SendPacket(const T& Packet) { SendBytes(&Packet, sizeof(T)); }
    void TickPrediction(float Delta);
    void SendInputs();
    void StartPrediction(const FVector& Spawn);
    uint32 ServerNowMs() const;
    void SpawnRemote(uint32 Id, const char* Name, const FVector& Position, float Yaw);
    void RemoveRemote(uint32 Id);
    void Fail(const FString& Reason);
    void Shutdown();
    ACLHero* LocalHero() const;

    FCLNetReceiver* Receiver = nullptr; // Shutdown()에서 스레드를 멈춘 뒤 지운다
    FRunnableThread* Thread = nullptr;

    EState State = EState::Idle;
    FString ServerText, PlayerName, Error;
    uint32 TeamCode = 0, MyId = 0, RttMs = 0;
    FCLPrediction* Prediction = nullptr; // Shutdown()에서 지운다
    double NextPredictionLog = 0;
    bool bPvp = false;
    uint8 Capacity = 3;
    uint16 IframeMs = 150, RewindCapMs = 200; // 서버가 S_EnterRoom으로 알려 준 모드 규칙
    uint32 DodgeSequence = 0, AttackSequence = 0, ServerDodges = 0, ServerHits = 0;
    // 로컬 시계(초) - 서버 시계(초). 최근 S_Pong 몇 개 중 왕복 시간이 가장 짧았던 것으로 정한다(회피·공격 시각용).
    // 가장 좋았던 값 하나를 계속 쓰면 두 시계의 속도 차이(드리프트)가 쌓이므로 오래된 측정은 버린다.
    double ServerTimeOffset = 0;
    struct FClockSample { uint32 RttMs; double Offset; };
    TArray<FClockSample> ClockSamples;
    // 0.2초 간격 Ping, 최근 1초. 두 시계 속도가 수 % 달라도(WSL 가상 머신 시계에서 실측) 오차가 약 10ms 안에 머문다.
    static constexpr double PingInterval = .2;
    static constexpr int32 ClockWindow = 5;
    uint32 AutoDodgeTurn = 0; // -AscensionNetAutoDodge 시험 옵션

    double NextInputSend = 0, NextPing = 0;
    // 로컬 시계 - 서버 시계. 가장 작은(지연이 가장 적은) 값을 쓴다.
    double ServerClockOffset = 0;
    bool bHasClock = false;

    UPROPERTY() TMap<uint32, TObjectPtr<ACLRemoteHero>> Remotes;
};
