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

    // ---- 스탯: 모두 5에서 시작, 스킬 창(K)에서 받은 포인트를 분배한다. 온라인은 서버 값(S_Loadout) ----
    int32 Stats[4] = {5, 5, 5, 5};
    int32 Stat(ECLStat S) const { return Stats[int32(S)]; }
    float AttackPower() const;          // 기본 공격력 20 + 힘 × 2
    float SkillDamage(const FCLSkillDef& Def) const; // 공격력 × 배율 × (1 + 해당 스탯 × 0.01) × 강화·각성 보정
    float SkillCooldown(const FCLSkillDef& Def) const; // 강화 단계만큼 줄어든 쿨타임

    // ---- 마나 (데모 임시: 기본 100 + 지능 × 5, 초당 4 × (1 + 지능 × 1%)) ----
    float Mana = 0;
    float MaxMana() const;
    float ManaRegen() const;

    // ---- 슬롯 ----
    // 배운 스킬이 배운 순서대로 Z·X·C·V에 들어간다(스킬 창 K).
    // 개발 시험: 실행 옵션 -AscensionSkills=Sword.Smash,Sword.Spin,Bow.Rapid,Bow.Ice 로 포인트 없이 슬롯을 정할 수 있다(빈 칸은 None).
    const FCLSkillDef* Slots[SlotCount] = {};
    bool bDevSlots = false; // -AscensionSkills로 정했다(배우기 규칙을 쓰지 않는다)

    // ---- 배운 스킬과 포인트 (기획안 "레벨과 스킬 포인트") ----
    // 규칙은 서버(asc_game::loadout)와 같다: 검·활 합쳐 4개, 배우기 1 + 강화 2, 해금 스탯 5/15/25(고급은 12레벨),
    // 각성은 3단계 고급 스킬에 각성 포인트 1, 지우면 전부 돌려받지만 전투 중(5초) 불가.
    // 강화·각성 효과(임시, 기획안 미정): 강화 1단계마다 피해 +20%·쿨타임 -10%, 각성 피해 +50%.
    struct FLearned { const FCLSkillDef* Def = nullptr; int32 Rank = 0; bool bAwakened = false; };
    FLearned Learned[SlotCount];
    int32 Level = 1;                 // 해금 조건에 쓰는 레벨(온라인은 서버, 오프라인은 1인 데모 레벨)
    int32 SkillPointsLeft = 1, AwakeningPointsLeft = 0, StatPointsLeft = 0;
    enum class EAction : uint8 { Learn = 1, Upgrade = 2, Awaken = 3, Remove = 4 }; // 프로토콜 SkillAction과 같은 값
    static uint8 SkillNumber(const FCLSkillDef& Def);       // 무기 × 12 + 스탯 × 3 + 티어 + 1 (서버와 같은 번호)
    static const FCLSkillDef* SkillByNumber(uint8 Number);
    const FLearned* FindLearned(const FCLSkillDef& Def) const;
    int32 RankOf(const FCLSkillDef& Def) const { const FLearned* L = FindLearned(Def); return L ? L->Rank : 0; }
    static int32 UnlockStat(ECLSkillTier Tier) { return Tier == ECLSkillTier::Basic ? 5 : Tier == ECLSkillTier::Intermediate ? 15 : 25; }
    bool IsUnlocked(const FCLSkillDef& Def) const;
    FString LockReason(const FCLSkillDef& Def) const; // 해금 안 됐으면 이유("힘 15 필요" 등)
    FString CheckAction(EAction Action, const FCLSkillDef& Def) const; // 지금 할 수 없으면 이유(빈 문자열 = 가능)
    // 스킬 창에서 부른다. 온라인은 서버에 보내고 답(S_Loadout)을 기다리고, 오프라인은 바로 적용한다.
    bool RequestSkill(EAction Action, const FCLSkillDef& Def);
    bool RequestStat(ECLStat Stat, int32 Amount);
    // 온라인: 서버가 정한 값으로 바꾼다
    void SetLoadout(const uint8 Numbers[SlotCount], const uint8 Ranks[SlotCount], const uint8 Awakened[SlotCount], const uint16 InStats[4],
                    int32 InSkillPoints, int32 InAwakeningPoints, int32 InStatPoints);
    // 온라인 연결이 채운다(없으면 오프라인 규칙)
    TFunction<void(uint8 Action, uint8 Number)> SendSkillCommand;
    TFunction<void(uint8 Stat, uint8 Amount)> SendStatCommand;
    // 오프라인: 1인 데모 레벨에서 받은 포인트를 다시 센다(레벨이 오르면 부른다)
    void RefreshOfflinePoints(int32 HeroLevel);
    void NoteCombat() { LastCombat = Now(); }
    bool InCombat() const { return Now() - LastCombat < 5.0; }
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
    void SyncSlots();
    bool ApplyLocal(EAction Action, const FCLSkillDef& Def);
    double LastCombat = -100;
    TMap<FName, double> CooldownUntil;
    TMap<FName, double> Buffs;
    // 준비 시간이 있는 스킬: 조준 방향을 시전 순간에 고정하고 시간이 되면 실행한다
    const FCLSkillDef* Pending = nullptr;
    double PendingAt = 0;
    FVector PendingAim = FVector::ForwardVector;
    void Execute(const FCLSkillDef& Def, const FVector& Aim);
};
