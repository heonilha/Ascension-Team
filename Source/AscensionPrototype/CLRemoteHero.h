// 같은 층에 있는 다른 플레이어의 겉모습. 입력·판정 없이 서버가 보낸 위치를 보간해서 보여준다.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CLRemoteHero.generated.h"

class UAnimSequence;
class UStaticMeshComponent;

UCLASS()
class ACLRemoteHero : public ACharacter
{
    GENERATED_BODY()
public:
    ACLRemoteHero();
    virtual void BeginPlay() override;

    void Init(uint32 InPlayerId, const FString& InName);
    // 서버 시간 ServerTime(초)의 위치를 기록한다.
    void PushSample(double ServerTime, const FVector& Position, float Yaw, uint8 State);
    // ACLNetSession이 매 프레임 부른다. RenderTime(서버 시간)의 위치를 보간해 그린다.
    void UpdateVisual(double RenderTime, float Delta);

    uint32 PlayerId = 0;
    FString DisplayName;

private:
    struct FSample
    {
        double Time;
        FVector Position;
        float Yaw;
        uint8 State;
    };
    TArray<FSample> Samples;

    UPROPERTY() TObjectPtr<UStaticMeshComponent> Sword;
    UPROPERTY() TArray<TObjectPtr<UAnimSequence>> Clips;
    bool bHasModel = false;
    uint8 ShownState = 0;
    double ActionStart = 0, AnimTime = 0;
    FVector LastPosition = FVector::ZeroVector;
    float Speed = 0;
};
