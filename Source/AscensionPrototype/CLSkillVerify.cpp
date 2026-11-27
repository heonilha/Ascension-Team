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
        // 스킬 번호는 서버와 같다: 무기 × 12 + 스탯 × 3 + 티어 + 1
        const FCLSkillDef* Quick = UCLSkillComponent::FindSkill(TEXT("Sword.Quick"));
        const FCLSkillDef* Spin = UCLSkillComponent::FindSkill(TEXT("Sword.Spin"));
        Check(Smash && Rapid && Quick && Spin && UCLSkillComponent::SkillNumber(*Smash) == 1 && UCLSkillComponent::SkillNumber(*Rapid) == 16
              && UCLSkillComponent::SkillByNumber(9) == UCLSkillComponent::FindSkill(TEXT("Sword.Excalibur")), TEXT("skill_numbers_match_server"));
        // 배우기: 처음에는 빈 슬롯, 1레벨에 스킬 포인트 1
        P->Level = 1; S->RefreshOfflinePoints(P->Level);
        Check(!S->Slots[0] && S->SkillPointsLeft == 1 && S->StatPointsLeft == 0, TEXT("starts_with_no_skills_and_1_point"));
        Check(S->RequestSkill(UCLSkillComponent::EAction::Learn, *Smash) && S->Slots[0] == Smash && S->SkillPointsLeft == 0, TEXT("learn_smash_fills_slot_z"));
        Check(!S->RequestSkill(UCLSkillComponent::EAction::Learn, *Quick), TEXT("learn_needs_skill_point"));
        P->Level = 6; S->RefreshOfflinePoints(P->Level); // 6레벨: 스킬 5 남음, 스탯 15
        Check(S->SkillPointsLeft == 5 && S->StatPointsLeft == 15, TEXT("level_6_points"));
        Check(!S->RequestSkill(UCLSkillComponent::EAction::Learn, *Spin) && S->LockReason(*Spin) == TEXT("힘 15 필요"), TEXT("intermediate_locked_below_stat_15"));
        Check(S->RequestSkill(UCLSkillComponent::EAction::Learn, *Quick) && S->RequestSkill(UCLSkillComponent::EAction::Learn, *Rapid), TEXT("learn_quick_and_rapid"));
        Check(S->RequestStat(ECLStat::Vitality, 3) && S->Stats[3] == 8 && S->StatPointsLeft == 12, TEXT("allocate_vitality_3"));
        Check(S->Slots[1] == Quick && !Quick->Execute && S->Slots[2] == Rapid, TEXT("slots_follow_learn_order"));
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
        // 강화: 피해 +20%, 쿨타임 -10% (임시 규칙)
        Check(S->RequestSkill(UCLSkillComponent::EAction::Upgrade, *Smash) && S->RankOf(*Smash) == 2, TEXT("upgrade_smash_to_rank_2"));
        Check(FMath::IsNearlyEqual(S->SkillDamage(*Smash), 63.f * 1.2f, .01f) && FMath::IsNearlyEqual(S->SkillCooldown(*Smash), 3.6f, .001f), TEXT("rank_2_damage_and_cooldown"));
        Check(!S->RequestSkill(UCLSkillComponent::EAction::Remove, *Smash), TEXT("cannot_remove_in_combat"));
        Stage = 4; NextAt = T + 5.2;
    }
    else if (Stage == 4)
    {
        const int32 Points = S->SkillPointsLeft;
        Check(S->RequestSkill(UCLSkillComponent::EAction::Remove, *Smash) && S->SkillPointsLeft == Points + 2 && S->Slots[0] != Smash, TEXT("remove_refunds_all_points"));
        Finish();
    }
}
