#include "CLSkillComponent.h"
#include "CombatLab.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

UCLSkillComponent::UCLSkillComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

const TArray<FCLSkillDef>& UCLSkillComponent::AllSkills()
{
    static const TArray<FCLSkillDef> Skills = []()
    {
        TArray<FCLSkillDef> Out;
        CLRegisterSwordSkills(Out);
        CLRegisterBowSkills(Out);
        return Out;
    }();
    return Skills;
}

const FCLSkillDef* UCLSkillComponent::FindSkill(FName Id)
{
    for (const FCLSkillDef& Def : AllSkills()) if (Def.Id == Id) return &Def;
    return nullptr;
}

void UCLSkillComponent::BeginPlay()
{
    Super::BeginPlay();
    AssignDefaultSlots();
    ResetForNewLife();
}

void UCLSkillComponent::AssignDefaultSlots()
{
    for (const FCLSkillDef*& Slot : Slots) Slot = nullptr;
    FString Option;
    if (FParse::Value(FCommandLine::Get(), TEXT("AscensionSkills="), Option, false))
    {
        TArray<FString> Ids; Option.ParseIntoArray(Ids, TEXT(","), false);
        for (int32 I = 0; I < SlotCount && I < Ids.Num(); ++I)
        {
            Slots[I] = FindSkill(FName(*Ids[I].TrimStartAndEnd()));
            if (!Slots[I] && Ids[I] != TEXT("None") && !Ids[I].IsEmpty())
                UE_LOG(LogTemp, Warning, TEXT("ASCENSION_SKILL unknown id '%s'"), *Ids[I]);
        }
        bDevSlots = true;
        for (int32 I = 0; I < SlotCount; ++I) if (Slots[I]) Learned[I] = {Slots[I], 1, false};
        return;
    }
    SyncSlots(); // 배운 스킬이 슬롯이 된다(처음에는 비어 있다: K 스킬 창에서 배운다)
}

void UCLSkillComponent::SyncSlots()
{
    if (bDevSlots) return;
    for (int32 I = 0; I < SlotCount; ++I) Slots[I] = Learned[I].Def;
}

// ---- 배우기·강화·각성·지우기 ----

uint8 UCLSkillComponent::SkillNumber(const FCLSkillDef& Def)
{
    return uint8(int32(Def.Weapon) * 12 + int32(Def.Stat) * 3 + int32(Def.Tier) + 1);
}

const FCLSkillDef* UCLSkillComponent::SkillByNumber(uint8 Number)
{
    for (const FCLSkillDef& Def : AllSkills()) if (SkillNumber(Def) == Number) return &Def;
    return nullptr;
}

const UCLSkillComponent::FLearned* UCLSkillComponent::FindLearned(const FCLSkillDef& Def) const
{
    for (const FLearned& L : Learned) if (L.Def == &Def) return &L;
    return nullptr;
}

static const TCHAR* StatName(ECLStat S)
{
    switch (S) { case ECLStat::Strength: return TEXT("힘"); case ECLStat::Agility: return TEXT("민첩"); case ECLStat::Intelligence: return TEXT("지능"); default: return TEXT("체력"); }
}

bool UCLSkillComponent::IsUnlocked(const FCLSkillDef& Def) const { return LockReason(Def).IsEmpty(); }

FString UCLSkillComponent::LockReason(const FCLSkillDef& Def) const
{
    const int32 Need = UnlockStat(Def.Tier);
    FString Reason;
    if (Stat(Def.Stat) < Need) Reason = FString::Printf(TEXT("%s %d 필요"), StatName(Def.Stat), Need);
    if (Def.Tier == ECLSkillTier::Advanced && Level < 12) Reason += FString::Printf(TEXT("%s12레벨 필요"), Reason.IsEmpty() ? TEXT("") : TEXT(" · "));
    return Reason;
}

FString UCLSkillComponent::CheckAction(EAction Action, const FCLSkillDef& Def) const
{
    const FLearned* L = FindLearned(Def);
    switch (Action)
    {
    case EAction::Learn:
        if (L) return TEXT("이미 배운 스킬");
        if (!IsUnlocked(Def)) return LockReason(Def);
        { int32 Used = 0; for (const FLearned& X : Learned) Used += X.Def != nullptr; if (Used >= SlotCount) return TEXT("스킬 칸 4개가 모두 찼습니다"); }
        if (SkillPointsLeft <= 0) return TEXT("스킬 포인트 부족");
        return FString();
    case EAction::Upgrade:
        if (!L) return TEXT("먼저 배워야 합니다");
        if (L->Rank >= 3) return TEXT("최대 강화");
        if (SkillPointsLeft <= 0) return TEXT("스킬 포인트 부족");
        return FString();
    case EAction::Awaken:
        if (!L) return TEXT("먼저 배워야 합니다");
        if (Def.Tier != ECLSkillTier::Advanced) return TEXT("고급 스킬만 각성");
        if (L->Rank < 3) return TEXT("3단계까지 강화 필요");
        if (L->bAwakened) return TEXT("이미 각성");
        if (AwakeningPointsLeft <= 0) return TEXT("각성 포인트 부족");
        return FString();
    case EAction::Remove:
        if (!L) return TEXT("배우지 않은 스킬");
        if (InCombat()) return TEXT("전투 중에는 지울 수 없습니다");
        return FString();
    }
    return TEXT("알 수 없는 명령");
}

bool UCLSkillComponent::RequestSkill(EAction Action, const FCLSkillDef& Def)
{
    const FString Reason = CheckAction(Action, Def);
    if (!Reason.IsEmpty()) { Message(Reason); return false; }
    if (SendSkillCommand) { SendSkillCommand(uint8(Action), SkillNumber(Def)); return true; } // 서버 답을 기다린다
    return ApplyLocal(Action, Def);
}

bool UCLSkillComponent::ApplyLocal(EAction Action, const FCLSkillDef& Def)
{
    FLearned* L = nullptr;
    for (FLearned& X : Learned) if (X.Def == &Def) L = &X;
    switch (Action)
    {
    case EAction::Learn:
        for (FLearned& X : Learned) if (!X.Def) { X = {&Def, 1, false}; break; }
        --SkillPointsLeft; Message(FString::Printf(TEXT("%s 배움"), Def.DisplayName)); break;
    case EAction::Upgrade: ++L->Rank; --SkillPointsLeft; Message(FString::Printf(TEXT("%s %d단계"), Def.DisplayName, L->Rank)); break;
    case EAction::Awaken: L->bAwakened = true; --AwakeningPointsLeft; Message(FString::Printf(TEXT("%s 각성!"), Def.DisplayName)); break;
    case EAction::Remove:
        SkillPointsLeft += L->Rank; AwakeningPointsLeft += L->bAwakened;
        *L = FLearned(); Message(FString::Printf(TEXT("%s 지움 · 포인트 돌려받음"), Def.DisplayName)); break;
    }
    // 빈 칸을 앞으로 당긴다(배운 순서 유지)
    TArray<FLearned> Kept; for (const FLearned& X : Learned) if (X.Def) Kept.Add(X);
    for (int32 I = 0; I < SlotCount; ++I) Learned[I] = I < Kept.Num() ? Kept[I] : FLearned();
    SyncSlots();
    UE_LOG(LogTemp, Display, TEXT("ASCENSION_SKILL loadout action=%d skill=%s skill_points=%d"), int32(Action), *Def.Id.ToString(), SkillPointsLeft);
    return true;
}

bool UCLSkillComponent::RequestStat(ECLStat S, int32 Amount)
{
    if (Amount <= 0 || StatPointsLeft < Amount) { Message(TEXT("스탯 포인트 부족")); return false; }
    if (SendStatCommand) { SendStatCommand(uint8(S), uint8(Amount)); return true; }
    Stats[int32(S)] += Amount; StatPointsLeft -= Amount;
    Mana = FMath::Min(Mana, MaxMana());
    Message(FString::Printf(TEXT("%s +%d → %d"), StatName(S), Amount, Stats[int32(S)]));
    return true;
}

void UCLSkillComponent::SetLoadout(const uint8 Numbers[SlotCount], const uint8 Ranks[SlotCount], const uint8 Awakened[SlotCount], const uint16 InStats[4],
                                   int32 InSkillPoints, int32 InAwakeningPoints, int32 InStatPoints)
{
    if (!bDevSlots)
        for (int32 I = 0; I < SlotCount; ++I)
        {
            const FCLSkillDef* Def = Numbers[I] ? SkillByNumber(Numbers[I]) : nullptr;
            Learned[I] = Def ? FLearned{Def, Ranks[I], Awakened[I] != 0} : FLearned();
        }
    for (int32 I = 0; I < 4; ++I) Stats[I] = InStats[I];
    SkillPointsLeft = InSkillPoints; AwakeningPointsLeft = InAwakeningPoints; StatPointsLeft = InStatPoints;
    Mana = FMath::Min(Mana, MaxMana());
    SyncSlots();
}

void UCLSkillComponent::RefreshOfflinePoints(int32 HeroLevel)
{
    if (SendSkillCommand) return; // 온라인은 서버가 센다
    Level = FMath::Clamp(HeroLevel, 1, 20);
    int32 SpentSkill = 0, SpentAwaken = 0, SpentStat = 0;
    for (const FLearned& L : Learned) if (L.Def) { SpentSkill += bDevSlots ? 0 : L.Rank; SpentAwaken += L.bAwakened; }
    for (int32 V : Stats) SpentStat += V - 5;
    SkillPointsLeft = FMath::Max(0, FMath::Min(Level, 12) - SpentSkill);
    AwakeningPointsLeft = FMath::Max(0, (Level >= 15) + (Level >= 17) - SpentAwaken);
    StatPointsLeft = FMath::Max(0, 3 * (Level - 1) - SpentStat);
}

ACLHero* UCLSkillComponent::Hero() const { return Cast<ACLHero>(GetOwner()); }
double UCLSkillComponent::Now() const { return GetWorld() ? GetWorld()->GetTimeSeconds() : 0; }
void UCLSkillComponent::Message(const FString& Text) { LastMessage = Text; LastMessageUntil = Now() + 1.5; }

float UCLSkillComponent::AttackPower() const { return 20.f + Stat(ECLStat::Strength) * 2.f; }
float UCLSkillComponent::SkillDamage(const FCLSkillDef& Def) const
{
    const FLearned* L = FindLearned(Def);
    const float Rank = L ? 1.f + .2f * (FMath::Max(1, L->Rank) - 1) : 1.f; // 강화 1단계마다 +20% (임시)
    const float Awaken = L && L->bAwakened ? 1.5f : 1.f;                  // 각성 +50% (기획안)
    return AttackPower() * Def.Multiplier * (1.f + Stat(Def.Stat) * .01f) * Rank * Awaken;
}
float UCLSkillComponent::SkillCooldown(const FCLSkillDef& Def) const
{
    const int32 Rank = FMath::Max(1, RankOf(Def));
    return Def.Cooldown * (1.f - .1f * (Rank - 1)); // 강화 1단계마다 -10% (임시)
}
float UCLSkillComponent::MaxMana() const { return 100.f + Stat(ECLStat::Intelligence) * 5.f; }
float UCLSkillComponent::ManaRegen() const { return 4.f * (1.f + Stat(ECLStat::Intelligence) * .01f); }

void UCLSkillComponent::SetWeapon(ECLWeapon NewWeapon)
{
    if (Weapon == NewWeapon) return;
    Weapon = NewWeapon;
    if (ACLHero* H = Hero()) H->SetWeaponVisual(Weapon);
}

void UCLSkillComponent::SwapWeapon()
{
    if (Pending) return; // 시전 준비 중에는 바꾸지 않는다
    SetWeapon(Weapon == ECLWeapon::Sword ? ECLWeapon::Bow : ECLWeapon::Sword);
    Message(Weapon == ECLWeapon::Sword ? TEXT("검으로 교체") : TEXT("활로 교체"));
}

double UCLSkillComponent::CooldownLeft(int32 Index) const
{
    if (Index < 0 || Index >= SlotCount || !Slots[Index]) return 0;
    const double* Until = CooldownUntil.Find(Slots[Index]->Id);
    return Until ? FMath::Max(0.0, *Until - Now()) : 0;
}

bool UCLSkillComponent::UseSlot(int32 Index)
{
    ACLHero* H = Hero();
    if (!H || Index < 0 || Index >= SlotCount) return false;
    const FCLSkillDef* Def = Slots[Index];
    if (!Def) { Message(TEXT("빈 스킬 슬롯")); return false; }
    if (Pending || !H->CanStartSkill()) return false;
    if (CooldownLeft(Index) > 0) { Message(FString::Printf(TEXT("%s 재사용 대기 %.1f초"), Def->DisplayName, CooldownLeft(Index))); return false; }
    if (Mana < Def->ManaCost) { Message(FString::Printf(TEXT("마나 부족 (%s: %.0f 필요)"), Def->DisplayName, Def->ManaCost)); return false; }
    if (!Def->Execute) { Message(FString::Printf(TEXT("%s: 아직 구현되지 않음"), Def->DisplayName)); return false; }

    SetWeapon(Def->Weapon); // 스킬을 쓰면 해당 무기로 자동 교체
    Mana -= Def->ManaCost;
    CooldownUntil.Add(Def->Id, Now() + SkillCooldown(*Def));
    const FVector Aim = FRotator(0, H->GetControlRotation().Yaw, 0).Vector();
    H->PlaySkillAction(FMath::Max(Def->ActionTime, Def->CastTime), Def->Tier != ECLSkillTier::Basic);
    UE_LOG(LogTemp, Display, TEXT("ASCENSION_SKILL use id=%s slot=%d mana=%.0f"), *Def->Id.ToString(), Index + 1, Mana);
    if (Def->CastTime > 0) { Pending = Def; PendingAt = Now() + Def->CastTime; PendingAim = Aim; }
    else Execute(*Def, Aim);
    return true;
}

void UCLSkillComponent::Execute(const FCLSkillDef& Def, const FVector& Aim)
{
    FCLSkillContext Ctx;
    Ctx.Hero = Hero(); Ctx.Skills = this; Ctx.Def = &Def;
    Ctx.Origin = Ctx.Hero ? Ctx.Hero->GetActorLocation() : FVector::ZeroVector;
    Ctx.Aim = Aim; Ctx.Damage = SkillDamage(Def);
    Def.Execute(Ctx);
}

void UCLSkillComponent::TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(Delta, TickType, ThisTickFunction);
    ACLHero* H = Hero(); if (!H) return;
    RefreshOfflinePoints(H->Level); // 오프라인: 1인 데모 레벨에서 포인트를 센다(온라인은 서버 값)
    if (H->HP <= 0) { Pending = nullptr; return; }
    Mana = FMath::Min(MaxMana(), Mana + ManaRegen() * Delta);
    if (Pending)
    {
        if (Now() >= PendingAt) { const FCLSkillDef* Def = Pending; Pending = nullptr; Execute(*Def, PendingAim); }
        else if (!H->IsInAction()) { Message(FString::Printf(TEXT("%s 시전 취소"), Pending->DisplayName)); Pending = nullptr; } // 회피 등으로 동작이 끊김
    }
}

void UCLSkillComponent::AddBuff(FName Id, float Seconds) { Buffs.Add(Id, FMath::Max(BuffLeft(Id), double(Seconds)) + Now()); }
bool UCLSkillComponent::HasBuff(FName Id) const { return BuffLeft(Id) > 0; }
double UCLSkillComponent::BuffLeft(FName Id) const
{
    const double* Until = Buffs.Find(Id);
    return Until ? FMath::Max(0.0, *Until - Now()) : 0;
}

void UCLSkillComponent::ResetForNewLife()
{
    Mana = MaxMana(); CooldownUntil.Empty(); Buffs.Empty(); Pending = nullptr;
    Weapon = ECLWeapon::Sword;
    if (ACLHero* H = Hero()) H->SetWeaponVisual(Weapon);
}
