#pragma once
// 스킬 공통 틀 자동 시험. 실행 옵션 -AscensionSkillVerify 에서만 생성된다(Scripts/VerifySkills.ps1).
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CLSkillVerify.generated.h"

class ACLEnemy;

UCLASS()
class ACLSkillVerifier : public AActor
{
    GENERATED_BODY()
public:
    ACLSkillVerifier();
    virtual void Tick(float Delta) override;
private:
    void Check(bool Condition, const TCHAR* Name);
    void Finish();
    int32 Stage = 0;
    double NextAt = .5;
    bool bFailed = false;
    float SavedHP = 0;
    UPROPERTY() TObjectPtr<ACLEnemy> Target;
};
