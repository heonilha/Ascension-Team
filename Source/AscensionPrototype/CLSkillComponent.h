#pragma once
// 무기 교체·스킬 슬롯 4개·쿨타임·마나·스탯·버프를 맡는 공통 틀.
// 검·활 담당은 이 파일을 고치지 않고 CLSwordSkills.cpp / CLBowSkills.cpp에 스킬을 추가한다.
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CLSkillTypes.h"
#include "CLSkillComponent.generated.h"

UCLASS()
class UCLSkillComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UCLSkillComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    static constexpr int32 SlotCount = 4;

    // ---- 무기 ----
    ECLWeapon Weapon = ECLWeapon::Sword;
    void SwapWeapon();
    void SetWeapon(ECLWeapon NewWeapon);

    // ---- 스탯 (데모 임시: 모두 5에서 시작, 분배 UI는 아직 없음) ----
    int32 Stats[4] = {5, 5, 5, 5};
    int32 Stat(ECLStat S) const { return Stats[int32(S)]; }
    float AttackPower() const;          // 기본 공격력 20 + 힘 × 2
    float SkillDamage(const FCLSkillDef& Def) const; // 공격력 × 배율 × (1 + 해당 스탯 × 0.01)

    // ---- 마나 (데모 임시: 기본 100 + 지능 × 5, 초당 4 × (1 + 지능 × 1%)) ----
    float Mana = 0;
    float MaxMana() const;
    float ManaRegen() const;

    // ---- 슬롯 ----
    // 기본: 1·2번 = 등록된 검 스킬 앞 2개, 3·4번 = 활 스킬 앞 2개.
    // 실행 옵션 -AscensionSkills=Sword.Smash,Sword.Spin,Bow.Rapid,Bow.Ice 로 바꿀 수 있다(빈 칸은 None).
    const FCLSkillDef* Slots[SlotCount] = {};
    bool UseSlot(int32 Index);
    double CooldownLeft(int32 Index) const;

    // ---- 버프: 스킬이 자유롭게 쓰는 이름 붙은 지속 시간 ----
    void AddBuff(FName Id, float Seconds);
    bool HasBuff(FName Id) const;
    double BuffLeft(FName Id) const;

    // 사망·새 도전 시 마나·쿨타임·버프 초기화
    void ResetForNewLife();

    // 등록된 모든 스킬(검 + 활)
    static const TArray<FCLSkillDef>& AllSkills();
    static const FCLSkillDef* FindSkill(FName Id);

    FString LastMessage; // HUD에 잠깐 보여 줄 안내(마나 부족 등)
    double LastMessageUntil = 0;

private:
    class ACLHero* Hero() const;
    double Now() const;
    void Message(const FString& Text);
    void AssignDefaultSlots();
    TMap<FName, double> CooldownUntil;
    TMap<FName, double> Buffs;
    // 준비 시간이 있는 스킬: 조준 방향을 시전 순간에 고정하고 시간이 되면 실행한다
    const FCLSkillDef* Pending = nullptr;
    double PendingAt = 0;
    FVector PendingAim = FVector::ForwardVector;
    void Execute(const FCLSkillDef& Def, const FVector& Aim);
};
