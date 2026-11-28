#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "HeroMotion.generated.h"

class UAnimSequence;

// 주인공 동작.
// - 이동: 대기 → 걷기 → 달리기를 실제 속도(Speed)로 섞는다. 걷기·달리기는 같은 발 위상으로 돌고, 재생 속도를 이동 속도에 맞춰
//   발이 바닥에서 미끄러지지 않게 한다(클립 발 속도: 걷기 156, 달리기 390 cm/s).
// - 공격은 척추 위만, 회피는 몸 전체. 동작이 바뀌거나(공격→회피) 같은 동작을 다시 시작하면(연속 공격) 짧게 섞는다.
// - 몸 전체 동작(회피)은 클립 안 골반 이동을 지워 제자리로 만들고 몸 정면을 모델 정면(+Y)으로 돌린다(이동은 게임이 한다).
//   ActionTravel(0~1)을 주면 게임이 실제로 옮긴 거리만큼 클립도 굴러가도록 재생 위치를 정한다.
UCLASS(Transient)
class UCLHeroMotion : public UAnimInstance
{
    GENERATED_BODY()
public:
    UCLHeroMotion();
    UPROPERTY() TObjectPtr<UAnimSequence> IdleClip;
    UPROPERTY() TObjectPtr<UAnimSequence> WalkClip;
    UPROPERTY() TObjectPtr<UAnimSequence> RunClip;
    UPROPERTY() TObjectPtr<UAnimSequence> ActionClip;
    float BaseTime = 0;   // 대기 클립 시간
    float Speed = 0;      // 수평 이동 속도(cm/s)
    float ActionTime = 0, ActionWeight = 0;
    float ActionTravel = -1; // 회피: 게임 이동 거리 비율(0~1). 음수면 ActionTime을 쓴다
    bool bFullBodyAction = false;

    // 동작 클립 재생 위치(초). State 2 공격·3 강공격·4 회피, Elapsed는 동작 시작 뒤 지난 시간, Duration은 동작 전체 시간,
    // Hit은 공격 판정 시각(시작 기준). 공격은 클립에서 칼이 가장 빠른 순간이 판정 시각에 오도록 앞뒤를 따로 늘이고 줄인다.
    static float ActionClipTime(int32 State, float Elapsed, float Duration, float Hit);
    // 강공격을 모으는 동안(0~1): 칼을 들어 올린 자세까지 천천히 간다
    static float ChargeClipTime(float Charge);
    // 회피 이동 곡선(0~1 → 0~1): 게임 회피 300cm가 이 곡선으로 움직인다(ACLHero::Tick·공유 이동 코드와 같은 식)
    static float DodgeTravel(float Progress);
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};
