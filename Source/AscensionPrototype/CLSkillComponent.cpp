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
        return;
    }
    // 구현된 스킬(Execute가 있는 것)을 먼저 채우고, 남은 칸은 미구현 스킬로 채운다
    int32 Sword = 0, Bow = 2;
    for (int32 Pass = 0; Pass < 2; ++Pass)
        for (const FCLSkillDef& Def : AllSkills())
        {
            if ((Def.Execute != nullptr) != (Pass == 0)) continue;
            if (Def.Weapon == ECLWeapon::Sword && Sword < 2) Slots[Sword++] = &Def;
            if (Def.Weapon == ECLWeapon::Bow && Bow < 4) Slots[Bow++] = &Def;
        }
}

ACLHero* UCLSkillComponent::Hero() const { return Cast<ACLHero>(GetOwner()); }
double UCLSkillComponent::Now() const { return GetWorld() ? GetWorld()->GetTimeSeconds() : 0; }
void UCLSkillComponent::Message(const FString& Text) { LastMessage = Text; LastMessageUntil = Now() + 1.5; }

float UCLSkillComponent::AttackPower() const { return 20.f + Stat(ECLStat::Strength) * 2.f; }
float UCLSkillComponent::SkillDamage(const FCLSkillDef& Def) const
{
    return AttackPower() * Def.Multiplier * (1.f + Stat(Def.Stat) * .01f);
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
    CooldownUntil.Add(Def->Id, Now() + Def->Cooldown);
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
