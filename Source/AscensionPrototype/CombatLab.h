#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "CombatLab.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UStaticMeshComponent;
class ACLEnemy;
class ACLHero;
class UAnimSequence;

USTRUCT()
struct FCLItem
{
    GENERATED_BODY()
    UPROPERTY() FGuid ID;
    UPROPERTY() int32 Kind = 0;
    UPROPERTY() int32 Tier = 0;
};

UCLASS()
class ACLDrop : public AActor
{
    GENERATED_BODY()
public:
    ACLDrop();
    virtual void Tick(float Delta) override;
    TArray<FCLItem> Items;
};

UCLASS()
class ACLExperience : public AActor
{
    GENERATED_BODY()
public:
    ACLExperience();
    virtual void Tick(float Delta) override;
    int32 Value = 8;
};

UCLASS()
class ACLBladeWave : public AActor
{
    GENERATED_BODY()
public:
    ACLBladeWave();
    virtual void Tick(float Delta) override;
    FVector Direction = FVector::ForwardVector;
    float Damage = 20, Speed = 1400, Radius = 55;
    int32 Pierces = 3;
    TWeakObjectPtr<ACLHero> Hero;
    TSet<TWeakObjectPtr<ACLEnemy>> HitEnemies;
};

UCLASS()
class ACLHero : public ACharacter
{
    GENERATED_BODY()
public:
    ACLHero();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    void ReceiveHit(float Damage);
    void OfferAwakening();
    void ResetLife();
    void Attack(); void Heavy(); void ReleaseHeavy(); void Dodge(); void Skill(); void ToggleLock();
    bool IsCharging() const { return bCharging; }
    float ChargeProgress() const;
    void NextEncounter();
    void ImpactFeedback(float Strength, const FVector& Position);
    double HitMarkerUntil = 0;
    double HurtUntil = 0;
    void GainExperience(int32 Amount);
    void TryLevelUp();
    void TickAuxiliaryCombat();
    bool UsesBladeWave() const;
    int32 Level = 1, Experience = 0, LightningRank = 1, BladeRank = 1, PowerRank = 0;
    int32 ExperienceRequired() const { return 25+15*(Level-1); }
    bool bLevelChoice = false;
    void UseAltar();
    void ToggleInventory(); void SelectPrevious(); void SelectNext(); void EquipSelected();
    bool TakeItem(ACLDrop* Drop);
    FCLItem Weapon, Armor;
    TArray<FCLItem> Inventory;
    bool bInventoryOpen = false;
    int32 SelectedItem = 0;
    float WeaponBonus() const;
    float ArmorReduction() const;
    void Choose(int32 Index);
    float HP = 100;
    double DodgeReady = 0, SkillReady = 0, PerfectUntil = 0;
    FGuid LifeID;
    TArray<int32> Awakenings, Offers;
    int32 PendingReplacement = -1;
    int32 Combo = 0;
    int32 AltarUses = 0;
    bool HasAwakening(int32 ID) const { return Awakenings.Contains(ID); }
    FString Notice;
    UPROPERTY() TObjectPtr<ACLEnemy> Locked;
private:
    friend class AAscensionMode;
    void Forward(float Value); void Right(float Value);
    void LookX(float Value); void LookY(float Value);
    void StartAttack(bool HeavyAttack);
    void ResolveSwing();
    void Choice1() { Choose(0); } void Choice2() { Choose(1); } void Choice3() { Choose(2); }
    void Skip();
    UPROPERTY() TObjectPtr<USpringArmComponent> Arm;
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Sword;
    double AttackStart = -100, AttackEnd = 0, HitTime = 0, DodgeStart = -100, DodgeEnd = 0, ImmuneEnd = 0, LastCombo = -100;
    float DodgeProgress = 0;
    bool bResolved = true, bHeavy = false, bEmpowered = false;
    bool bCharging = false;
    double ChargeStart = 0;
    float ReleasedCharge = 0;
    FVector DashDirection = FVector::ForwardVector;
    FVector SwingDirection = FVector::ForwardVector;
    UPROPERTY() TArray<TObjectPtr<UAnimSequence>> Clips;
    int32 VisualState = -1;
    bool bHasModel = false;
    float MoveForward = 0, MoveRight = 0;
    double LightningReady = 0, BladeReady = 0;
    double CameraImpactUntil = 0;
    float CameraImpactStrength = 0;
    FTimerHandle ImpactPauseTimer;
};

UCLASS()
class ACLEnemy : public ACharacter
{
    GENERATED_BODY()
public:
    ACLEnemy();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    void Configure(int32 EnemyKind);
    void ReceiveHit(float Damage, FVector PushDirection = FVector::ZeroVector);
    int32 Kind = 0, Phase = 0;
    float HP = 50, MaxHP = 50;
    double Deadline = 0;
    double AlertUntil = 0;
    bool bFrozenForView = false;
    bool bBoss = false;
    double StaggerUntil = 0, ReactionReady = 0;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Body;
private:
    void BeginTell(); void Strike();
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Sword;
    FVector Direction;
    int32 StrikeNumber = 0;
    bool bDashHit = false;
    UPROPERTY() TArray<TObjectPtr<UAnimSequence>> Clips;
    int32 VisualState = -1;
    FTimerHandle ImpactPauseTimer;
    double DamageTextUntil = 0;
    float LastDamage = 0;
    FVector DamageTextOrigin;
    FVector KnockbackDirection = FVector::ZeroVector;
    float KnockbackRemaining = 0;
};

UCLASS()
class AAscensionMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AAscensionMode();
    virtual void BeginPlay() override;
    void SpawnEncounter(int32 Seed = 0);
    void StartNextEncounter();
    int32 RemainingEnemies() const;
    int32 Wave = 1, SpawnedEnemies = 0;
    bool bEncounterCleared = false;
    bool bSurvival = false, bBossSpawned = false, bBossDefeated = false, bVictory = false;
    double SurvivalStart = 0, ReinforcementReady = 0, EliteReady = 0;
    float SurvivalElapsed() const;
    void ResetSurvival();
    void TickSurvival();
    void SpawnSurvivalBatch(int32 Count,bool Elite=false,bool Boss=false);
    virtual void Tick(float Delta) override;
private:
    void Check(bool Condition, const TCHAR* Name);
    bool bVerify = false, bView = false, bFailed = false, bSpawnVerify = false;
    int32 VerifyStage = 0, WallTrials = 0;
    int32 MotionFrames = 0;
    double NextCheck = 0;
    FGuid PreviousLife;
    FVector VerifyPosition;
    FVector VerifyEnemyPosition;
    UPROPERTY() TObjectPtr<ACLEnemy> VerifyEnemy;
};

UCLASS()
class ACLHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
