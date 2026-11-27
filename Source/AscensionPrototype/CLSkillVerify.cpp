#include "CLSkillVerify.h"
#include "CLSkillComponent.h"
#include "CombatLab.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"

ACLSkillVerifier::ACLSkillVerifier() { PrimaryActorTick.bCanEverTick = true; }

void ACLSkillVerifier::Check(bool Condition, const TCHAR* Name)
{
    if (!Condition) bFailed = true;
    UE_LOG(LogTemp, Display, TEXT("ASCENSION_CHECK %s %s"), Name, Condition ? TEXT("PASS") : TEXT("FAIL"));
}

void ACLSkillVerifier::Finish()
{
    UE_LOG(LogTemp, Display, TEXT("ASCENSION_SKILL_VERIFY %s"), bFailed ? TEXT("FAIL") : TEXT("PASS"));
    FPlatformMisc::RequestExitWithStatus(false, bFailed ? 1 : 0);
    Stage = 100;
}

void ACLSkillVerifier::Tick(float Delta)
{
    Super::Tick(Delta);
    const double T = GetWorld()->GetTimeSeconds();
    if (T < NextAt || Stage >= 100) return;
    ACLHero* P = Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (!P || !P->Skills) { Check(false, TEXT("hero_with_skill_component")); Finish(); return; }
    UCLSkillComponent* S = P->Skills;
    const FCLSkillDef* Smash = UCLSkillComponent::FindSkill(TEXT("Sword.Smash"));
    const FCLSkillDef* Rapid = UCLSkillComponent::FindSkill(TEXT("Bow.Rapid"));

    if (Stage == 0)
    {
        for (TActorIterator<ACLEnemy> It(GetWorld()); It; ++It) It->Destroy();
        P->DisableInput(Cast<APlayerController>(P->Controller));
        P->SetActorLocation(FVector(0, 0, 100)); P->Controller->SetControlRotation(FRotator(-30, 0, 0)); P->SetActorRotation(FRotator::ZeroRotator);
        Check(UCLSkillComponent::AllSkills().Num() == 24, TEXT("registers_12_sword_and_12_bow_skills"));
        Check(Smash && Rapid && S->Slots[0] == Smash && S->Slots[2] == Rapid, TEXT("default_slots_prefer_implemented_sword_then_bow"));
        Check(S->Slots[1] && S->Slots[1]->Weapon == ECLWeapon::Sword && !S->Slots[1]->Execute, TEXT("unimplemented_skill_fills_remaining_slot"));
        Check(S->Weapon == ECLWeapon::Sword && FMath::IsNearlyEqual(S->Mana, S->MaxMana()), TEXT("starts_with_sword_and_full_mana"));
        Target = GetWorld()->SpawnActor<ACLEnemy>(FVector(150, 0, 100), FRotator::ZeroRotator);
        if (!Target) { Check(false, TEXT("skill_test_enemy_spawn")); Finish(); return; }
        Target->bFrozenForView = true; Target->HP = Target->MaxHP = 500;
        Stage = 1; NextAt = T + .3;
    }
    else if (Stage == 1)
    {
        // 강타: (20 + 힘5×2) × 200% × (1 + 힘5×1%) = 63
        const float Mana = S->Mana;
        Check(S->UseSlot(0), TEXT("smash_used"));
        Check(FMath::IsNearlyEqual(Target->HP, 500.f - 63.f, .01f), TEXT("smash_damage_follows_formula_63"));
        Check(Target->StaggerUntil >= T + .45, TEXT("smash_staggers_500ms"));
        Check(FMath::IsNearlyEqual(S->Mana, Mana - 10.f, .01f), TEXT("smash_spends_10_mana"));
        Check(P->IsInAction() && !S->UseSlot(0), TEXT("cannot_recast_during_action"));
        Stage = 2; NextAt = T + .7;
    }
    else if (Stage == 2)
    {
        Check(!P->IsInAction(), TEXT("skill_action_ends"));
        const float HP = Target->HP;
        Check(!S->UseSlot(0) && S->CooldownLeft(0) > 3.0 && Target->HP == HP, TEXT("smash_blocked_by_4s_cooldown"));
        const float Mana = S->Mana;
        Check(!S->UseSlot(1) && S->Mana == Mana && S->Weapon == ECLWeapon::Sword, TEXT("unimplemented_skill_does_nothing"));
        Target->SetActorLocation(FVector(500, 0, 100)); SavedHP = Target->HP;
        Check(S->UseSlot(2), TEXT("rapid_shot_used"));
        Check(S->Weapon == ECLWeapon::Bow, TEXT("skill_auto_switches_to_its_weapon"));
        Stage = 3; NextAt = T + .6;
    }
    else if (Stage == 3)
    {
        // 속사: 30 × 150% × (1 + 민첩5×1%) = 47.25
        Check(FMath::IsNearlyEqual(Target->HP, SavedHP - 47.25f, .01f), TEXT("rapid_projectile_hits_once_for_47_25"));
        int32 Shots = 0; for (TActorIterator<ACLBladeWave> It(GetWorld()); It; ++It) ++Shots;
        Check(Shots == 0, TEXT("projectile_removed_after_single_hit"));
        S->SwapWeapon();
        Check(S->Weapon == ECLWeapon::Sword, TEXT("f_swaps_weapon"));
        S->Mana = 0;
        S->ResetForNewLife();
        Check(FMath::IsNearlyEqual(S->Mana, S->MaxMana()) && S->CooldownLeft(0) == 0 && S->CooldownLeft(2) == 0, TEXT("new_life_resets_mana_and_cooldowns"));
        S->Mana = 3;
        Check(!S->UseSlot(0) && S->Mana == 3, TEXT("insufficient_mana_blocks_skill"));
        Finish();
    }
}
