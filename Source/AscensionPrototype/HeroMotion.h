#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "HeroMotion.generated.h"

class UAnimSequence;

// Reuses the existing clips: locomotion below the spine, sword above it.
UCLASS(Transient)
class UCLHeroMotion : public UAnimInstance
{
    GENERATED_BODY()
public:
    UCLHeroMotion();
    UPROPERTY() TObjectPtr<UAnimSequence> IdleClip;
    UPROPERTY() TObjectPtr<UAnimSequence> RunClip;
    UPROPERTY() TObjectPtr<UAnimSequence> ActionClip;
    float BaseTime = 0, ActionTime = 0, MovingWeight = 0, ActionWeight = 0;
    bool bFullBodyAction = false;
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};
