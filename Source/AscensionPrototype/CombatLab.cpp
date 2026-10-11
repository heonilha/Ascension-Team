#include "CombatLab.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceNull.h"
#include "UnrealClient.h"
#include "HAL/PlatformMisc.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "Sound/SoundBase.h"

namespace Lab
{
    const TCHAR* Names[] = {TEXT("번개 반격"), TEXT("연격 마무리"), TEXT("넓은 파동"), TEXT("탐욕 · 폭발 회피"), TEXT("탐욕 · 파괴의 일격"), TEXT("빠른 파동")};
    const TCHAR* Details[] = {TEXT("정확한 회피 후 반격 피해 추가 +15"), TEXT("일반 공격의 세 번째 타격 피해 +12"), TEXT("Q 스킬 반경 320 → 440"), TEXT("회피 폭발 피해 18 / 대가: 주변 적의 경계 확대"), TEXT("강공격 피해 +25 / 대가: 공격 후 빈틈 +0.45초"), TEXT("Q 스킬 재사용 대기시간 6초 → 4초")};
    FCLItem Item(int32 Kind,int32 Tier) { FCLItem I; I.ID=FGuid::NewGuid(); I.Kind=Kind; I.Tier=Tier; return I; }
    FString ItemName(const FCLItem& I)
    {
        if(!I.ID.IsValid()) return TEXT("없음");
        if(I.Kind==0) return I.Tier==2?TEXT("묵직한 검 (+8 피해)"):I.Tier==1?TEXT("예리한 검 (+4 피해)"):TEXT("기본 검");
        return I.Tier==1?TEXT("철제 방어구 (피해 -3)"):TEXT("기본 방어구");
    }
    bool SetModel(ACharacter* Character,TArray<TObjectPtr<UAnimSequence>>& Clips,bool Monster=false)
    {
        USkeletalMesh* Model=LoadObject<USkeletalMesh>(nullptr,Monster?TEXT("/Game/Models/UndeadSentinel/UndeadSentinelRig.UndeadSentinelRig"):TEXT("/Game/Models/SharedKnightRig.SharedKnightRig"));
        if(!Model) return false;
        USkeletalMeshComponent* Mesh=Character->GetMesh(); Mesh->SetSkeletalMeshAsset(Model); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        FBoxSphereBounds Bounds=Model->GetImportedBounds(); float Scale=180.f/FMath::Max(1.f,float(Bounds.BoxExtent.Z*2));
        Mesh->SetRelativeScale3D(FVector(Scale)); Mesh->SetRelativeRotation(FRotator(0,180,0));
        Mesh->SetRelativeLocation(FVector(0,0,-88-(Bounds.Origin.Z-Bounds.BoxExtent.Z)*Scale));
        if(UMaterialInterface* Material=LoadObject<UMaterialInterface>(nullptr,Monster?TEXT("/Game/Models/UndeadSentinel/M_UndeadSentinel.M_UndeadSentinel"):TEXT("/Game/Models/M_SharedKnight.M_SharedKnight")))
            for(int32 I=0;I<Mesh->GetNumMaterials();++I) Mesh->SetMaterial(I,Material);
        if(Monster)
        {
            const TCHAR* MonsterPaths[]={TEXT("/Game/Models/UndeadSentinel/UndeadSentinelActions_Idle.UndeadSentinelActions_Idle"),TEXT("/Game/Models/UndeadSentinel/UndeadSentinelRun.UndeadSentinelRun"),TEXT("/Game/Models/UndeadSentinel/UndeadSentinelActions_Right_Hand_Sword_Slash.UndeadSentinelActions_Right_Hand_Sword_Slash")};
            for(const TCHAR* Path:MonsterPaths) Clips.Add(LoadObject<UAnimSequence>(nullptr,Path));
            int32 Loaded=0; for(const auto& Clip:Clips) if(Clip) ++Loaded;
            UE_LOG(LogTemp,Display,TEXT("ASCENSION_MONSTER_MODEL_LOADED clips=%d/3"),Loaded); return Loaded==3;
        }
        const TCHAR* Paths[]={TEXT("/Game/Models/SharedKnightActions_Idle.SharedKnightActions_Idle"),TEXT("/Game/Models/SharedKnightRun.SharedKnightRun"),TEXT("/Game/Models/SharedKnightActions_Right_Hand_Sword_Slash.SharedKnightActions_Right_Hand_Sword_Slash"),TEXT("/Game/Models/SharedKnightActions_Charged_Upward_Slash.SharedKnightActions_Charged_Upward_Slash"),TEXT("/Game/Models/SharedKnightActions_Roll_Dodge.SharedKnightActions_Roll_Dodge")};
        for(const TCHAR* Path:Paths) Clips.Add(LoadObject<UAnimSequence>(nullptr,Path));
        int32 Loaded=0; for(const auto& Clip:Clips) if(Clip) ++Loaded;
        UE_LOG(LogTemp,Display,TEXT("ASCENSION_MODEL_LOADED scale=%.3f height=%.1f clips=%d/5"),Scale,Bounds.BoxExtent.Z*2*Scale,Loaded);
        return true;
    }
    void Visual(ACharacter* Character,const TArray<TObjectPtr<UAnimSequence>>& Clips,int32 State,int32& Previous,float Duration=0)
    {
        if(State==Previous || !Clips.IsValidIndex(State) || !Clips[State]) return;
        Character->GetMesh()->SetRelativeRotation(FRotator(0,270,0));
        Previous=State; Character->GetMesh()->PlayAnimation(Clips[State],State<=1);
        const float Start=State==3?.55f:State==2?.1f:0.f;
        const float EndPadding=State==3?.9f:State==2?.2f:0.f;
        Character->GetMesh()->SetPosition(Start,false);
        Character->GetMesh()->SetPlayRate(Duration>0?FMath::Max(.1f,Clips[State]->GetPlayLength()-Start-EndPadding)/Duration:1.f);
    }
    void AttachSword(ACharacter* Character,UStaticMeshComponent* Sword)
    {
        USkeletalMeshComponent* Mesh=Character->GetMesh();
        for(int32 I=0;I<Mesh->GetNumBones();++I)
        {
            FName Bone=Mesh->GetBoneName(I); FString Name=Bone.ToString().ToLower();
            if(Name.Contains(TEXT("righthand")) || Name==TEXT("hand_r") || Name==TEXT("r_hand"))
            {
                Sword->AttachToComponent(Mesh,FAttachmentTransformRules::SnapToTargetNotIncludingScale,Bone);
                Sword->SetRelativeLocation(FVector(40,0,0)); Sword->SetRelativeRotation(FRotator(0,0,0));
                Sword->SetRelativeScale3D(FVector(.9f,.05f,.1f)/Mesh->GetRelativeScale3D());
                UE_LOG(LogTemp,Display,TEXT("ASCENSION_SWORD_BONE %s"),*Bone.ToString()); return;
            }
        }
        UE_LOG(LogTemp,Warning,TEXT("ASCENSION_SWORD_BONE_NOT_FOUND"));
    }
    void AlignBodyForward(USkeletalMeshComponent* Mesh,float Delta=0)
    {
        if(Mesh->GetBoneIndex(TEXT("LeftShoulder"))==INDEX_NONE || Mesh->GetBoneIndex(TEXT("RightShoulder"))==INDEX_NONE || Mesh->GetBoneIndex(TEXT("Head"))==INDEX_NONE || Mesh->GetBoneIndex(TEXT("Hips"))==INDEX_NONE) return;
        const FVector Right=Mesh->GetBoneLocation(TEXT("RightShoulder"),EBoneSpaces::ComponentSpace)-Mesh->GetBoneLocation(TEXT("LeftShoulder"),EBoneSpaces::ComponentSpace);
        const FVector Up=Mesh->GetBoneLocation(TEXT("Head"),EBoneSpaces::ComponentSpace)-Mesh->GetBoneLocation(TEXT("Hips"),EBoneSpaces::ComponentSpace);
        const FVector Forward=FVector::CrossProduct(Right,Up).GetSafeNormal2D();
        if(!Forward.IsNearlyZero())
        {
            const float Twist=Delta>0?FMath::Clamp(FMath::FindDeltaAngleDegrees(90.f,Forward.Rotation().Yaw),-25.f,25.f):0.f;
            const FRotator Target(0,-Forward.Rotation().Yaw+Twist,0);
            Mesh->SetRelativeRotation(Delta>0?FMath::RInterpTo(Mesh->GetRelativeRotation(),Target,Delta,25.f):Target);
        }
    }
    double Now(const AActor* A) { return A->GetWorld()->GetTimeSeconds(); }
    bool Clear(const AActor* Source, const AActor* Target)
    {
        FHitResult Hit; FCollisionQueryParams Params; Params.AddIgnoredActor(Source); Params.AddIgnoredActor(Target);
        return !Source->GetWorld()->LineTraceSingleByChannel(Hit, Source->GetActorLocation(), Target->GetActorLocation(), ECC_Visibility, Params);
    }
    bool InArc(const AActor* Source, const AActor* Target, FVector Facing, float Range, float HalfAngle)
    {
        FVector To = Target->GetActorLocation() - Source->GetActorLocation(); To.Z = 0;
        return To.SizeSquared() <= FMath::Square(Range) && FVector::DotProduct(To.GetSafeNormal(), Facing) >= FMath::Cos(FMath::DegreesToRadians(HalfAngle)) && Clear(Source, Target);
    }
    void Arc(UWorld* World, FVector Origin, FVector Facing, float Range, float HalfAngle, FColor Color)
    {
        Origin.Z = 8;
        for (int32 I = 0; I < 16; ++I)
        {
            FVector A = Origin + Facing.RotateAngleAxis(-HalfAngle + 2*HalfAngle*I/16, FVector::UpVector)*Range;
            FVector B = Origin + Facing.RotateAngleAxis(-HalfAngle + 2*HalfAngle*(I+1)/16, FVector::UpVector)*Range;
            DrawDebugLine(World, A, B, Color, false, -1, 0, 3);
        }
        DrawDebugLine(World, Origin, Origin + Facing.RotateAngleAxis(-HalfAngle, FVector::UpVector)*Range, Color, false, -1, 0, 2);
        DrawDebugLine(World, Origin, Origin + Facing.RotateAngleAxis(HalfAngle, FVector::UpVector)*Range, Color, false, -1, 0, 2);
    }
}

ACLHero::ACLHero()
{
    PrimaryActorTick.bCanEverTick = true;
    GetCapsuleComponent()->InitCapsuleSize(32, 88);
    GetCharacterMovement()->MaxWalkSpeed = 450;
    GetCharacterMovement()->bOrientRotationToMovement = false;
    bUseControllerRotationYaw = false;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProxyBody")); Body->SetupAttachment(RootComponent);
    Body->SetStaticMesh(Cube.Object); Body->SetRelativeScale3D(FVector(.5,.5,1.45)); Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Sword = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProxySword")); Sword->SetupAttachment(RootComponent);
    Sword->SetStaticMesh(Cube.Object); Sword->SetRelativeLocation(FVector(50,40,0)); Sword->SetRelativeScale3D(FVector(1.1,.07,.12)); Sword->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm")); Arm->SetupAttachment(RootComponent);
    Arm->TargetArmLength = 650; Arm->bUsePawnControlRotation = true; Arm->bDoCollisionTest = true;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera")); Camera->SetupAttachment(Arm);
}
void ACLHero::BeginPlay()
{
    Super::BeginPlay(); LifeID = FGuid::NewGuid(); Weapon=Lab::Item(0,0); Armor=Lab::Item(1,0);
    bHasModel=Lab::SetModel(this,Clips); if(bHasModel) { Body->SetVisibility(false); Lab::AttachSword(this,Sword); }
    if (Controller) Controller->SetControlRotation(FRotator(-30,0,0));
    if (APlayerController* PC = Cast<APlayerController>(Controller))
    {
        PC->SetInputMode(FInputModeGameOnly()); PC->bShowMouseCursor = false;
        if(PC->PlayerCameraManager) { PC->PlayerCameraManager->ViewPitchMin=-65; PC->PlayerCameraManager->ViewPitchMax=-10; }
    }
    Notice = TEXT("엘리트를 처치하거나 금색 제단에서 각성을 얻으세요. R: 새 전투 시작");
}
void ACLHero::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindAxis("Forward",this,&ACLHero::Forward); Input->BindAxis("Right",this,&ACLHero::Right);
    Input->BindAxis("LookX",this,&ACLHero::LookX); Input->BindAxis("LookY",this,&ACLHero::LookY);
    Input->BindAction("Attack",IE_Pressed,this,&ACLHero::Attack); Input->BindAction("Heavy",IE_Pressed,this,&ACLHero::Heavy);
    Input->BindAction("Dodge",IE_Pressed,this,&ACLHero::Dodge); Input->BindAction("Skill",IE_Pressed,this,&ACLHero::Skill);
    Input->BindAction("Lock",IE_Pressed,this,&ACLHero::ToggleLock); Input->BindAction("Restart",IE_Pressed,this,&ACLHero::ResetLife);
    Input->BindAction("Altar",IE_Pressed,this,&ACLHero::UseAltar);
    Input->BindAction("Inventory",IE_Pressed,this,&ACLHero::ToggleInventory);
    Input->BindAction("Previous",IE_Pressed,this,&ACLHero::SelectPrevious);
    Input->BindAction("Next",IE_Pressed,this,&ACLHero::SelectNext);
    Input->BindAction("Equip",IE_Pressed,this,&ACLHero::EquipSelected);
    Input->BindAction("NextEncounter",IE_Pressed,this,&ACLHero::NextEncounter);
    Input->BindAction("Choice1",IE_Pressed,this,&ACLHero::Choice1); Input->BindAction("Choice2",IE_Pressed,this,&ACLHero::Choice2);
    Input->BindAction("Choice3",IE_Pressed,this,&ACLHero::Choice3); Input->BindAction("Skip",IE_Pressed,this,&ACLHero::Skip);
}
void ACLHero::Forward(float V) { MoveForward=V; if(HP>0 && Lab::Now(this)>=DodgeEnd && Lab::Now(this)>=AttackEnd) AddMovementInput(FRotator(0,GetControlRotation().Yaw,0).Vector(),V); }
void ACLHero::Right(float V) { MoveRight=V; if(HP>0 && Lab::Now(this)>=DodgeEnd && Lab::Now(this)>=AttackEnd) AddMovementInput(FRotationMatrix(FRotator(0,GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::Y),V); }
void ACLHero::LookX(float V) { if(!IsValid(Locked)) AddControllerYawInput(V); }
void ACLHero::LookY(float V) { AddControllerPitchInput(V); }
void ACLHero::Tick(float Delta)
{
    Super::Tick(Delta); const double T=Lab::Now(this); if(HP<=0) return;
    const float Impact=FMath::Clamp(float((CameraImpactUntil-T)/.12),0.f,1.f)*CameraImpactStrength;
    Camera->SetRelativeLocation(FVector(0,FMath::Sin(T*135)*Impact,FMath::Sin(T*175)*Impact*.55f));
    Lab::Visual(this,Clips,T<DodgeEnd?4:T<AttackEnd?(bHeavy?3:2):GetVelocity().SizeSquared2D()>100?1:0,VisualState,T<DodgeEnd?float(DodgeEnd-DodgeStart):T<AttackEnd?(bHeavy?.95f:.68f):0);
    // Slash clips have their own forward axis too; keep the visible torso on
    // the actor's locked swing direction throughout normal and heavy attacks.
    if(bHasModel && T>=DodgeEnd) Lab::AlignBodyForward(GetMesh(),T<AttackEnd?Delta:0.f);
    if(bHasModel)
    {
        const FBoxSphereBounds Bounds=GetMesh()->GetSkeletalMeshAsset()->GetImportedBounds();
        FVector Offset(0,0,-88-(Bounds.Origin.Z-Bounds.BoxExtent.Z)*GetMesh()->GetRelativeScale3D().Z);
        if(T<DodgeEnd || T<AttackEnd)
        {
            // Keep the animated pelvis over the capsule; gameplay owns horizontal roll travel.
            const FVector Hips=GetMesh()->GetBoneLocation(TEXT("hips"),EBoneSpaces::ComponentSpace);
            Offset-=GetMesh()->GetRelativeRotation().RotateVector(FVector(Hips.X,Hips.Y,0)*GetMesh()->GetRelativeScale3D());
        }
        GetMesh()->SetRelativeLocation(Offset);
    }
    DrawDebugBox(GetWorld(),GetActorLocation(),FVector(27,27,75),GetActorQuat(),FColor::Cyan,false,-1,0,2);
    if(IsValid(Locked) && (Locked->HP<=0 || FVector::DistSquared(GetActorLocation(),Locked->GetActorLocation())>FMath::Square(1500.f))) Locked=nullptr;
    if(IsValid(Locked))
    {
        FRotator R=GetControlRotation(); R.Yaw=(Locked->GetActorLocation()-GetActorLocation()).Rotation().Yaw; Controller->SetControlRotation(R);
        DrawDebugSphere(GetWorld(),Locked->GetActorLocation()+FVector(0,0,120),25,12,FColor::Cyan,false,-1,0,2);
    }
    if(T>=AttackEnd && T>=DodgeEnd)
    {
        const FRotator Facing(0,GetControlRotation().Yaw,0);
        SetActorRotation(FMath::RInterpTo(GetActorRotation(),Facing,Delta,18.f));
    }
    if(T<AttackEnd) { ConsumeMovementInputVector(); GetCharacterMovement()->StopMovementImmediately(); }
    if(T<DodgeEnd)
    {
        // Integrate a smooth 300cm roll over 0.65s, independent of frame rate.
        const float Progress=FMath::Clamp(float((T-DodgeStart)/(DodgeEnd-DodgeStart)),0.f,1.f);
        const auto Distance=[](float A) { return 300.f*(A-FMath::Sin(2*PI*A)/(2*PI)); };
        FHitResult Hit; SetActorLocation(GetActorLocation()+DashDirection*(Distance(Progress)-Distance(DodgeProgress)),true,&Hit);
        DodgeProgress=Progress;
        DrawDebugLine(GetWorld(),GetActorLocation(),GetActorLocation()-DashDirection*70,FColor::Cyan,false,.15,0,4);
        // A wall blocks travel while the roll animation still finishes.
    }
    if(!bResolved && T>=HitTime) { bResolved=true; ResolveSwing(); }
    if(T<AttackEnd) { Lab::Arc(GetWorld(),GetActorLocation(),SwingDirection,bHeavy?230:190,bHeavy?65:50,bResolved?FColor::White:FColor::Cyan); }
    const float Swing=T<AttackEnd ? FMath::Sin(float((T-AttackStart)/(AttackEnd-AttackStart))*PI)*120-60 : 0;
    if(!bHasModel) Sword->SetRelativeRotation(FRotator(0,Swing,0));
}
void ACLHero::Attack() { StartAttack(false); }
void ACLHero::Heavy() { StartAttack(true); }
void ACLHero::StartAttack(bool HeavyAttack)
{
    const double T=Lab::Now(this); if(HP<=0 || T<AttackEnd || T<DodgeEnd || !Offers.IsEmpty() || bInventoryOpen) return;
    bHeavy=HeavyAttack; Combo=HeavyAttack?0:(T-LastCombo<1.1?Combo%3+1:1); LastCombo=T;
    bEmpowered=T<PerfectUntil; PerfectUntil=0;
    AttackStart=T; HitTime=T+(bHeavy?.46:.28);
    AttackEnd=T+(bHeavy?.95:.68)+(bHeavy && HasAwakening(4)?.45:0); bResolved=false;
    ConsumeMovementInputVector(); GetCharacterMovement()->StopMovementImmediately(); VisualState=-1;
    SwingDirection=FRotator(0,GetControlRotation().Yaw,0).Vector(); SetActorRotation(SwingDirection.Rotation());
}
void ACLHero::ResolveSwing()
{
    float Damage=(bHeavy?35:20)+WeaponBonus(); if(bEmpowered) Damage+=HasAwakening(0)?25:10;
    if(!bHeavy && Combo==3 && HasAwakening(1)) Damage+=12;
    if(bHeavy && HasAwakening(4)) Damage+=25;
    bool Hit=false; FVector Contact;
    for(TActorIterator<ACLEnemy> It(GetWorld());It;++It)
        if(It->HP>0 && Lab::InArc(this,*It,SwingDirection,bHeavy?230:190,bHeavy?65:50)) { Contact=It->GetActorLocation(); It->ReceiveHit(Damage); Hit=true; }
    if(Hit) ImpactFeedback(bHeavy?7.f:4.f,Contact);
}
void ACLHero::ImpactFeedback(float Strength,const FVector& Position)
{
    const double T=Lab::Now(this); CameraImpactUntil=T+.12; CameraImpactStrength=Strength; HitMarkerUntil=T+.16;
    CustomTimeDilation=.18f;
    GetWorldTimerManager().SetTimer(ImpactPauseTimer,FTimerDelegate::CreateWeakLambda(this,[this]() { CustomTimeDilation=1.f; }),.045f,false);
    if(USoundBase* Sound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Audio/SwordImpact.SwordImpact"))) UGameplayStatics::PlaySoundAtLocation(this,Sound,Position,.45f,FMath::FRandRange(.94f,1.06f));
    for(int32 I=0;I<10;++I) { const FVector Start=Position+FVector(0,0,15); DrawDebugLine(GetWorld(),Start,Start+FMath::VRand()*FMath::FRandRange(20.f,65.f),FColor(255,220,100),false,.1f,0,2); }
}
void ACLHero::NextEncounter()
{
    if(AAscensionMode* Mode=Cast<AAscensionMode>(GetWorld()->GetAuthGameMode())) Mode->StartNextEncounter();
}
void ACLHero::Dodge()
{
    double T=Lab::Now(this); if(HP<=0 || T<DodgeReady || T<DodgeEnd || (!bResolved && T<HitTime)) return;
    AttackEnd=T; bResolved=true;
    const FRotationMatrix CameraAxes(FRotator(0,GetControlRotation().Yaw,0));
    DashDirection=(CameraAxes.GetUnitAxis(EAxis::X)*MoveForward+CameraAxes.GetUnitAxis(EAxis::Y)*MoveRight).GetSafeNormal2D();
    if(DashDirection.IsNearlyZero()) DashDirection=FRotator(0,GetControlRotation().Yaw,0).Vector();
    ConsumeMovementInputVector(); GetCharacterMovement()->StopMovementImmediately();
    DodgeStart=T; DodgeProgress=0; DodgeEnd=T+.65; ImmuneEnd=T+.15; DodgeReady=T+1.0;
    SetActorRotation(DashDirection.Rotation()); VisualState=-1;
    if(HasAwakening(3))
    {
        FVector P=GetActorLocation(); DrawDebugSphere(GetWorld(),P,260,24,FColor::Orange,false,1,0,3);
        for(TActorIterator<ACLEnemy> It(GetWorld());It;++It)
        {
            if(FVector::DistSquared(P,It->GetActorLocation())<FMath::Square(1500.f)) It->AlertUntil=T+5;
            if(It->HP>0 && FVector::DistSquared(P,It->GetActorLocation())<FMath::Square(260.f) && Lab::Clear(this,*It)) It->ReceiveHit(18);
        }
        // In the solo prototype the greed cost is an explicit alert: nearby enemies engage.
        Notice=TEXT("탐욕의 대가! 주변 적이 폭발을 감지했습니다.");
    }
}
void ACLHero::Skill()
{
    double T=Lab::Now(this); if(HP<=0 || T<SkillReady || T<AttackEnd || T<DodgeEnd || !Offers.IsEmpty() || bInventoryOpen) return;
    SkillReady=T+(HasAwakening(5)?4:6); float Range=HasAwakening(2)?440:320;
    DrawDebugSphere(GetWorld(),GetActorLocation(),Range,32,FColor::Cyan,false,.35,0,3);
    for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) if(It->HP>0 && FVector::DistSquared(GetActorLocation(),It->GetActorLocation())<=FMath::Square(Range) && Lab::Clear(this,*It)) It->ReceiveHit(25);
}
void ACLHero::ReceiveHit(float Damage)
{
    if(HP<=0) return;
    if(Lab::Now(this)<ImmuneEnd) { PerfectUntil=Lab::Now(this)+2; Notice=TEXT("정확한 회피! 2초 안에 검으로 공격하면 반격 피해가 증가합니다."); return; }
    HurtUntil=Lab::Now(this)+.2;
    HP=FMath::Max(0.f,HP-FMath::Max(1.f,Damage-ArmorReduction())); DrawDebugSphere(GetWorld(),GetActorLocation(),100,12,FColor::Red,false,.2,0,3);
    if(HP==0)
    {
        ACLDrop* Drop=GetWorld()->SpawnActor<ACLDrop>(GetActorLocation()-FVector(0,0,55),FRotator::ZeroRotator);
        if(Drop) { Drop->Items=Inventory; if(Weapon.ID.IsValid()) Drop->Items.Add(Weapon); if(Armor.ID.IsValid()) Drop->Items.Add(Armor); }
        Inventory.Empty(); Weapon=FCLItem(); Armor=FCLItem(); bInventoryOpen=false;
        GetMesh()->SetVisibility(false); Sword->SetVisibility(false); Body->SetVisibility(false);
        Awakenings.Empty(); Offers.Empty(); PendingReplacement=-1; Locked=nullptr; bResolved=true;
        CustomTimeDilation=1.f; Camera->SetRelativeLocation(FVector::ZeroVector);
        GetCharacterMovement()->StopMovementImmediately(); Notice=TEXT("사망했습니다. 각성이 초기화되었습니다. R: 다시 도전");
        UE_LOG(LogTemp,Display,TEXT("ASCENSION_DEATH life=%s"),*LifeID.ToString());
    }
}
void ACLHero::ToggleLock()
{
    if(HP<=0) return; if(IsValid(Locked)) { Locked=nullptr; return; }
    float Best=FMath::Square(1400.f);
    for(TActorIterator<ACLEnemy> It(GetWorld());It;++It)
    {
        float D=FVector::DistSquared(GetActorLocation(),It->GetActorLocation());
        if(It->HP>0 && D<Best && Lab::Clear(this,*It)) { Best=D; Locked=*It; }
    }
}
void ACLHero::OfferAwakening()
{
    if(HP<=0 || !Offers.IsEmpty()) return;
    TArray<int32> Pool; for(int32 I=0;I<6;++I) if(!HasAwakening(I)) Pool.Add(I);
    while(!Pool.IsEmpty() && Offers.Num()<3) { int32 Index=FMath::RandRange(0,Pool.Num()-1); Offers.Add(Pool[Index]); Pool.RemoveAt(Index); }
    Notice=TEXT("1 · 2 · 3: 각성 선택 / Esc: 포기. 선택 중에도 적은 움직입니다.");
}
void ACLHero::UseAltar()
{
    if(HP<=0 || !Offers.IsEmpty()) return;
    ACLDrop* Nearest=nullptr; float Best=FMath::Square(200.f);
    for(TActorIterator<ACLDrop> It(GetWorld());It;++It)
    {
        float D=FVector::DistSquared(GetActorLocation(),It->GetActorLocation());
        if(!It->Items.IsEmpty() && D<Best && Lab::Clear(this,*It)) { Nearest=*It; Best=D; }
    }
    if(Nearest) { TakeItem(Nearest); return; }
    if(FVector::Dist2D(GetActorLocation(),FVector(650,650,0))>180) { Notice=TEXT("엘리트 근처의 금색 제단으로 이동한 뒤 E를 누르세요."); return; }
    if(AltarUses>=3) { Notice=TEXT("이번 생명에서는 제단을 모두 사용했습니다. 최대 3회 이용 가능"); return; }
    ++AltarUses; OfferAwakening();
}
void ACLHero::Choose(int32 Index)
{
    if(HP<=0) return;
    if(PendingReplacement>=0)
    {
        if(!Awakenings.IsValidIndex(Index)) return; Awakenings[Index]=PendingReplacement;
    }
    else
    {
        if(!Offers.IsValidIndex(Index)) return;
        if(Awakenings.Num()==3) { PendingReplacement=Offers[Index]; Notice=TEXT("교체할 보유 각성의 번호를 누르세요. 1 · 2 · 3 / Esc: 교체 포기"); return; }
        Awakenings.Add(Offers[Index]);
    }
    PendingReplacement=-1; Offers.Empty(); Notice=TEXT("각성을 획득했습니다! 달라진 공격과 회피를 시험해보세요.");
}
void ACLHero::Skip() { if(bInventoryOpen) { bInventoryOpen=false; return; } Offers.Empty(); PendingReplacement=-1; Notice=TEXT("새 각성을 포기했습니다. 기존 각성은 유지됩니다."); }
float ACLHero::WeaponBonus() const { return Weapon.ID.IsValid()?Weapon.Tier*4.f:0; }
float ACLHero::ArmorReduction() const { return Armor.ID.IsValid() && Armor.Tier==1?3.f:0; }
void ACLHero::ToggleInventory() { if(HP>0 && Offers.IsEmpty()) { bInventoryOpen=!bInventoryOpen; SelectedItem=0; } }
void ACLHero::SelectPrevious() { if(bInventoryOpen && !Inventory.IsEmpty()) SelectedItem=(SelectedItem+Inventory.Num()-1)%Inventory.Num(); }
void ACLHero::SelectNext() { if(bInventoryOpen && !Inventory.IsEmpty()) SelectedItem=(SelectedItem+1)%Inventory.Num(); }
void ACLHero::EquipSelected()
{
    if(HP<=0 || !bInventoryOpen || !Inventory.IsValidIndex(SelectedItem) || Lab::Now(this)<AttackEnd) return;
    FCLItem& Slot=Inventory[SelectedItem].Kind==0?Weapon:Armor;
    Swap(Slot,Inventory[SelectedItem]);
    if(!Inventory[SelectedItem].ID.IsValid()) Inventory.RemoveAt(SelectedItem);
    SelectedItem=FMath::Clamp(SelectedItem,0,FMath::Max(0,Inventory.Num()-1)); Notice=TEXT("장비를 교체했습니다. 공격과 방어 수치에 바로 적용됩니다.");
}
bool ACLHero::TakeItem(ACLDrop* Drop)
{
    if(HP<=0 || !IsValid(Drop) || Drop->Items.IsEmpty() || FVector::DistSquared(GetActorLocation(),Drop->GetActorLocation())>FMath::Square(200.f) || !Lab::Clear(this,Drop)) return false;
    if(Inventory.Num()>=12) { Notice=TEXT("가방이 가득 찼습니다. 장비를 더 가져올 수 없습니다."); return false; }
    FCLItem Item=Drop->Items[0]; Inventory.Add(Item); Drop->Items.RemoveAt(0);
    Notice=FString::Printf(TEXT("획득: %s / I: 가방 열기 / 위·아래: 선택 / Enter: 장착"),*Lab::ItemName(Item));
    UE_LOG(LogTemp,Display,TEXT("ASCENSION_LOOT item=%s life=%s"),*Item.ID.ToString(),*LifeID.ToString());
    if(Drop->Items.IsEmpty()) Drop->Destroy(); return true;
}
void ACLHero::ResetLife()
{
    HP=100; Awakenings.Empty(); Offers.Empty(); PendingReplacement=-1; LifeID=FGuid::NewGuid(); Locked=nullptr;
    Inventory.Empty(); Weapon=Lab::Item(0,0); Armor=Lab::Item(1,0); bInventoryOpen=false; SelectedItem=0;
    GetMesh()->SetVisibility(bHasModel); Sword->SetVisibility(true); Body->SetVisibility(!bHasModel); VisualState=-1;
    bResolved=true; AttackEnd=DodgeEnd=ImmuneEnd=DodgeReady=SkillReady=PerfectUntil=0; Combo=0; AltarUses=0; LastCombo=-100;
    MoveForward=MoveRight=0; HurtUntil=0;
    GetCharacterMovement()->StopMovementImmediately(); SetActorLocation(FVector(-750,0,100));
    GetWorldTimerManager().ClearTimer(ImpactPauseTimer); CustomTimeDilation=1.f; CameraImpactUntil=HitMarkerUntil=0; Camera->SetRelativeLocation(FVector::ZeroVector);
    if(Controller) Controller->SetControlRotation(FRotator(-30,0,0));
    if(AAscensionMode* Mode=Cast<AAscensionMode>(GetWorld()->GetAuthGameMode())) { Mode->Wave=1; Mode->SpawnEncounter(); }
    Notice=TEXT("새 전투를 시작합니다. 장비와 각성 없이 다시 도전하세요.");
}

ACLEnemy::ACLEnemy()
{
    PrimaryActorTick.bCanEverTick=true; GetCapsuleComponent()->InitCapsuleSize(35,80);
    GetCharacterMovement()->MaxWalkSpeed=220; GetCharacterMovement()->bOrientRotationToMovement=false;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EnemyProxy")); Body->SetupAttachment(RootComponent);
    Body->SetStaticMesh(Cube.Object); Body->SetRelativeScale3D(FVector(.6,.6,1.4)); Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Sword=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EnemySword")); Sword->SetupAttachment(RootComponent);
    Sword->SetStaticMesh(Cube.Object); Sword->SetCollisionEnabled(ECollisionEnabled::NoCollision); Sword->SetVisibility(false);
}
void ACLEnemy::Configure(int32 K)
{
    Kind=K; MaxHP=HP=K==2?150:K==1?70:50; Body->SetRelativeScale3D(K==2?FVector(.9,.9,1.8):K==1?FVector(.5,.8,1.3):FVector(.6,.6,1.4));
    Sword->SetVisibility(K!=1 && GetMesh()->GetSkeletalMeshAsset()!=nullptr);
    if(USkeletalMesh* Model=GetMesh()->GetSkeletalMeshAsset())
    {
        const FBoxSphereBounds Bounds=Model->GetImportedBounds();
        const float Scale=(K==2?220.f:K==1?175.f:185.f)/FMath::Max(1.f,float(Bounds.BoxExtent.Z*2));
        GetMesh()->SetRelativeScale3D(FVector(Scale)); GetMesh()->SetRelativeLocation(FVector(0,0,-88-(Bounds.Origin.Z-Bounds.BoxExtent.Z)*Scale));
        for(int32 I=0;I<GetMesh()->GetNumMaterials();++I)
            if(UMaterialInstanceDynamic* Material=GetMesh()->CreateDynamicMaterialInstance(I)) Material->SetVectorParameterValue(TEXT("Tint"),K==2?FLinearColor(.7f,.6f,1.f):K==1?FLinearColor(1.f,.55f,.4f):FLinearColor(1.f,.95f,.8f));
    }
}
void ACLEnemy::BeginPlay()
{
    Super::BeginPlay(); if(Lab::SetModel(this,Clips,true)) { Body->SetVisibility(false); Lab::AttachSword(this,Sword); Configure(Kind); }
}
void ACLEnemy::BeginTell()
{
    ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0)); if(!P || P->HP<=0) return;
    Phase=1; Direction=(P->GetActorLocation()-GetActorLocation()).GetSafeNormal2D(); SetActorRotation(Direction.Rotation());
    VisualState=-1;
    Deadline=Lab::Now(this)+(Kind==1?.9:StrikeNumber==1?.45:.6); GetCharacterMovement()->StopMovementImmediately();
}
void ACLEnemy::Strike()
{
    ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0)); if(!P || P->HP<=0) return;
    if(Kind==1) { Phase=2; Deadline=Lab::Now(this)+.5; bDashHit=false; return; }
    if(Lab::InArc(this,P,Direction,180,55)) P->ReceiveHit(Kind==2?16:10);
    if(Kind==2 && StrikeNumber==0) { StrikeNumber=1; BeginTell(); }
    else { Phase=3; Deadline=Lab::Now(this)+(Kind==2?1.5:.8); StrikeNumber=0; }
}
void ACLEnemy::Tick(float Delta)
{
    Super::Tick(Delta); ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0)); if(HP<=0 || !P || P->HP<=0) { GetCharacterMovement()->StopMovementImmediately(); return; }
    const double T=Lab::Now(this); FVector To=P->GetActorLocation()-GetActorLocation();
    Lab::Visual(this,Clips,Phase==1 && Kind!=1?2:Phase==2?1:GetVelocity().SizeSquared2D()>100?1:0,VisualState,Phase==1 && Kind!=1?(StrikeNumber==1?.6f:.75f):0);
    Lab::AlignBodyForward(GetMesh());
    if(T<DamageTextUntil) DrawDebugString(GetWorld(),DamageTextOrigin+FVector(0,0,130+(1-(DamageTextUntil-T)/.65)*65),FString::Printf(TEXT("-%.0f"),LastDamage),nullptr,FColor(255,220,100),0,true,1.3f);
    FColor C=Kind==2?FColor::Purple:Kind==1?FColor::Orange:FColor::Red;
    DrawDebugString(GetWorld(),GetActorLocation()+FVector(0,0,Kind==2?150:110),FString::Printf(TEXT("%s %.0f/%.0f"),Kind==2?TEXT("망령 지휘관"):Kind==1?TEXT("폭주 망령"):TEXT("망령 검병"),HP,MaxHP),nullptr,C,0,true);
    DrawDebugBox(GetWorld(),GetActorLocation(),Kind==2?FVector(45,45,90):FVector(30,40,70),GetActorQuat(),C,false,-1,0,3);
    if(bFrozenForView) return;
    if(Phase==0)
    {
        if(To.Size2D()>700 && T>=AlertUntil) return;
        if(To.Size2D()<(Kind==1?650:155) && Lab::Clear(this,P)) BeginTell();
        else { AddMovementInput(To.GetSafeNormal2D()); SetActorRotation(To.GetSafeNormal2D().Rotation()); }
    }
    else if(Phase==1)
    {
        if(Kind==1)
        {
            FVector A=GetActorLocation(); A.Z=8;
            DrawDebugLine(GetWorld(),A+FVector::CrossProduct(Direction,FVector::UpVector)*70,A+Direction*600+FVector::CrossProduct(Direction,FVector::UpVector)*70,C,false,-1,0,4);
            DrawDebugLine(GetWorld(),A-FVector::CrossProduct(Direction,FVector::UpVector)*70,A+Direction*600-FVector::CrossProduct(Direction,FVector::UpVector)*70,C,false,-1,0,4);
        }
        else Lab::Arc(GetWorld(),GetActorLocation(),Direction,180,55,C);
        if(T>=Deadline) Strike();
    }
    else if(Phase==2)
    {
        FHitResult Hit; SetActorLocation(GetActorLocation()+Direction*1200*Delta,true,&Hit);
        if(!bDashHit && FVector::Dist2D(GetActorLocation(),P->GetActorLocation())<105 && Lab::Clear(this,P)) { P->ReceiveHit(15); bDashHit=true; }
        if(T>=Deadline || Hit.bBlockingHit) { Phase=3; Deadline=T+1.2; }
    }
    else if(T>=Deadline) Phase=0;
}
void ACLEnemy::ReceiveHit(float Damage)
{
    if(HP<=0) return; HP=FMath::Max(0.f,HP-Damage);
    LastDamage=Damage; DamageTextUntil=Lab::Now(this)+.65; DamageTextOrigin=GetActorLocation();
    if(HP>0)
    {
        CustomTimeDilation=.18f;
        GetWorldTimerManager().SetTimer(ImpactPauseTimer,FTimerDelegate::CreateWeakLambda(this,[this]() { CustomTimeDilation=1.f; }),.045f,false);
        if(Kind==0) { Phase=3; Deadline=Lab::Now(this)+.13; GetCharacterMovement()->StopMovementImmediately(); }
    }
    UE_LOG(LogTemp,Display,TEXT("ASCENSION_HIT kind=%d damage=%.0f hp=%.0f"),Kind,Damage,HP);
    if(HP==0)
    {
        DrawDebugString(GetWorld(),DamageTextOrigin+FVector(0,0,140),FString::Printf(TEXT("-%.0f"),Damage),nullptr,FColor(255,220,100),.65f,true,1.3f);
        ACLDrop* Drop=GetWorld()->SpawnActor<ACLDrop>(GetActorLocation()-FVector(0,0,55),FRotator::ZeroRotator);
        if(Drop) Drop->Items.Add(Lab::Item(Kind==1?1:0,Kind==2?2:1));
        if(Kind==2) if(ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0))) P->OfferAwakening();
        Destroy();
    }
}
ACLDrop::ACLDrop()
{
    PrimaryActorTick.bCanEverTick=true;
    UStaticMeshComponent* Mesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DropBag")); SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Mesh->SetStaticMesh(Cube.Object); Mesh->SetRelativeScale3D(FVector(.5,.4,.4)); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); InitialLifeSpan=180;
}
void ACLDrop::Tick(float Delta)
{
    Super::Tick(Delta);
    DrawDebugBox(GetWorld(),GetActorLocation(),FVector(27,22,22),FColor::Yellow,false,-1,0,3);
    DrawDebugString(GetWorld(),GetActorLocation()+FVector(0,0,50),FString::Printf(TEXT("장비 가방 / E / %d개"),Items.Num()),nullptr,FColor::Yellow,0,true);
}
AAscensionMode::AAscensionMode() { PrimaryActorTick.bCanEverTick=true; DefaultPawnClass=ACLHero::StaticClass(); HUDClass=ACLHUD::StaticClass(); }
void AAscensionMode::BeginPlay()
{
    Super::BeginPlay(); bVerify=FParse::Param(FCommandLine::Get(),TEXT("AscensionVerify")); bView=FParse::Param(FCommandLine::Get(),TEXT("AscensionView")) || FParse::Param(FCommandLine::Get(),TEXT("AscensionMotionView")) || FParse::Param(FCommandLine::Get(),TEXT("AscensionCombatView")); bSpawnVerify=FParse::Param(FCommandLine::Get(),TEXT("AscensionSpawnVerify"));
    SpawnEncounter(); NextCheck=Lab::Now(this)+1;
}
void AAscensionMode::Check(bool Condition,const TCHAR* Name)
{
    if(!Condition) bFailed=true;
    UE_LOG(LogTemp,Display,TEXT("ASCENSION_CHECK %s %s"),Name,Condition?TEXT("PASS"):TEXT("FAIL"));
}
void AAscensionMode::Tick(float Delta)
{
    Super::Tick(Delta);
    if(!bVerify && !bView && !bSpawnVerify && !bEncounterCleared && SpawnedEnemies>0 && RemainingEnemies()==0)
    {
        bEncounterCleared=true;
        if(ACLHero* Hero=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0)); Hero && Hero->HP>0)
        { Hero->HP=FMath::Min(100.f,Hero->HP+20); if(Hero->Offers.IsEmpty()) Hero->Notice=TEXT("전투 완료! 체력 +20 / 장비를 챙기고 N: 다음 전투"); }
    }
    DrawDebugCircle(GetWorld(),FVector(650,650,8),100,32,FColor::Yellow,false,-1,0,4,FVector::ForwardVector,FVector::RightVector,false);
    DrawDebugString(GetWorld(),FVector(650,650,90),TEXT("각성 제단 / E / 생명당 3회"),nullptr,FColor::Yellow,0,true);
    if(bVerify && VerifyStage==4)
        if(ACLHero* Mover=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0))) Mover->AddMovementInput(FVector(-1,0,0),1);
    const bool MotionView=FParse::Param(FCommandLine::Get(),TEXT("AscensionMotionView"));
    if(bView && MotionView && VerifyStage==3)
        if(ACLHero* Mover=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0))) Mover->Right(1);
    if((!bVerify && !bView && !bSpawnVerify) || Lab::Now(this)<NextCheck) return;
    ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0));
    if(!P) { Check(false,TEXT("player_spawn")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    double T=Lab::Now(this);
    if(bSpawnVerify)
    {
        TArray<FVector> First;
        for(int32 Seed=100;Seed<120;++Seed)
        {
            SpawnEncounter(Seed); TArray<FVector> Positions; int32 Guards=0,Chargers=0,Elites=0;
            bool Safe=true;
            for(TActorIterator<ACLEnemy> It(GetWorld());It;++It)
            {
                const FVector At=It->GetActorLocation(); Safe&=FVector::Dist2D(At,P->GetActorLocation())>=440;
                const USkeletalMesh* Model=It->GetMesh()->GetSkeletalMeshAsset();
                Safe&=Model && Model->GetName()==TEXT("UndeadSentinelRig") && !It->Body->IsVisible();
                for(const FVector& Other:Positions) Safe&=FVector::Dist2D(At,Other)>=160;
                Positions.Add(At); Guards+=It->Kind==0; Chargers+=It->Kind==1; Elites+=It->Kind==2;
            }
            Check(Positions.Num()==7 && Guards>0 && Chargers>0 && Elites==1 && Safe,TEXT("random_spawn_count_mix_spacing_monster_model"));
            if(Seed==100) First=Positions;
            if(Seed==101) Check(Positions!=First,TEXT("random_seed_changes_layout"));
        }
        const FGuid Gear=P->Weapon.ID; StartNextEncounter(); Check(Wave==1,TEXT("next_wave_rejected_while_enemies_alive"));
        for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) It->Destroy();
        StartNextEncounter(); Check(Wave==2 && RemainingEnemies()==8 && P->Weapon.ID==Gear,TEXT("next_wave_preserves_equipment"));
        UE_LOG(LogTemp,Display,TEXT("ASCENSION_RANDOM_VERIFY %s seeds=20"),bFailed?TEXT("FAIL"):TEXT("PASS")); FPlatformMisc::RequestExitWithStatus(false,bFailed?1:0); bSpawnVerify=false; return;
    }
    if(bView)
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("AscensionDodgeVerify")) && VerifyStage>=2)
        {
            const int32 Step=(VerifyStage-2)/3,Phase=(VerifyStage-2)%3;
            if(Step>=8) { UE_LOG(LogTemp,Display,TEXT("ASCENSION_DODGE_VERIFY %s cases=8"),bFailed?TEXT("FAIL"):TEXT("PASS")); FPlatformMisc::RequestExitWithStatus(false,bFailed?1:0); return; }
            if(Phase==0)
            {
                P->SetActorLocation(FVector(-700,-500,100)); P->DodgeReady=0;
                P->Controller->SetControlRotation(FRotator(-30,Step<4?0:90,0));
                const FVector2D Inputs[]={FVector2D(0,-1),FVector2D(0,1),FVector2D(-1,0),FVector2D(1,1)};
                P->Forward(Inputs[Step%4].X); P->Right(Inputs[Step%4].Y); P->ConsumeMovementInputVector(); P->Attack();
                NextCheck=T+.4;
            }
            else if(Phase==1)
            {
                const FRotationMatrix Axes(FRotator(0,P->GetControlRotation().Yaw,0));
                const FVector Expected=(Axes.GetUnitAxis(EAxis::X)*P->MoveForward+Axes.GetUnitAxis(EAxis::Y)*P->MoveRight).GetSafeNormal();
                VerifyPosition=P->GetActorLocation(); P->Dodge();
                Check(T<P->DodgeEnd && FVector::DotProduct(Expected,P->DashDirection)>.99f,TEXT("held_direction_survives_attack_lock"));
                P->Forward(0); P->Right(0); NextCheck=T+.7;
            }
            else
            {
                const FVector Travel=P->GetActorLocation()-VerifyPosition;
                Check(Travel.Size2D()>240 && FVector::DotProduct(Travel.GetSafeNormal2D(),P->DashDirection)>.99f,TEXT("post_attack_roll_travels_selected_direction")); NextCheck=T+.2;
            }
            ++VerifyStage; return;
        }
        if(FParse::Param(FCommandLine::Get(),TEXT("AscensionHealthView")) && VerifyStage>=2)
        {
            if(VerifyStage==2) { P->ReceiveHit(75); ++VerifyStage; NextCheck=T+.4; }
            else if(VerifyStage==3) { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("HealthLow.png"),true,false); ++VerifyStage; NextCheck=T+.5; }
            else FPlatformMisc::RequestExit(false);
            return;
        }
        if(FParse::Param(FCommandLine::Get(),TEXT("AscensionFacingVerify")) && VerifyStage>=2)
        {
            const int32 Step=(VerifyStage-2)/2;
            if(Step>=8) { UE_LOG(LogTemp,Display,TEXT("ASCENSION_FACING_VERIFY %s attacks=8"),bFailed?TEXT("FAIL"):TEXT("PASS")); FPlatformMisc::RequestExitWithStatus(false,bFailed?1:0); return; }
            if((VerifyStage-2)%2==0)
            {
                const float Yaws[]={0,90,-90,180};
                P->Controller->SetControlRotation(FRotator(-30,Yaws[Step/2],0));
                if(Step%2) P->Heavy(); else P->Attack();
                ++VerifyStage; NextCheck=T+(Step%2?.35:.18);
            }
            else
            {
                USkeletalMeshComponent* Mesh=P->GetMesh();
                const FVector Side=Mesh->GetSocketLocation(TEXT("RightShoulder"))-Mesh->GetSocketLocation(TEXT("LeftShoulder"));
                const FVector Up=Mesh->GetSocketLocation(TEXT("Head"))-Mesh->GetSocketLocation(TEXT("Hips"));
                const FVector Forward=FVector::CrossProduct(Side,Up).GetSafeNormal2D();
                const float Dot=FVector::DotProduct(Forward,P->GetActorForwardVector());
                UE_LOG(LogTemp,Display,TEXT("ASCENSION_ATTACK_FACING step=%d heavy=%d yaw=%.1f dot=%.3f"),Step,Step%2,P->GetActorRotation().Yaw,Dot);
                Check(Dot>.65f && FVector::DotProduct(P->GetActorForwardVector(),P->SwingDirection)>.99f,TEXT("attack_preserves_front_with_natural_twist"));
                if(Step<2) FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/(Step?TEXT("HeavyFacing.png"):TEXT("AttackFacing.png")),true,false);
                ++VerifyStage; NextCheck=T+1;
            }
            return;
        }
        if(FParse::Param(FCommandLine::Get(),TEXT("AscensionCombatView")) && VerifyStage>=2)
        {
            if(VerifyStage==2)
            {
                for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) if(It->Kind==0) { It->SetActorLocation(P->GetActorLocation()+FVector(130,0,0)); break; }
                P->Attack(); VerifyStage=3; NextCheck=T+.15;
            }
            else if(VerifyStage==3) { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("CombatImpact.png"),true,false); VerifyStage=4; NextCheck=T+1; }
            else FPlatformMisc::RequestExit(false);
            return;
        }
        if(MotionView && VerifyStage>=2)
        {
            if(VerifyStage==2) { VerifyStage=3; NextCheck=T+.5; }
            else if(VerifyStage==3) { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("MotionRun.png"),true,false); VerifyStage=4; NextCheck=T+.3; }
            else if(VerifyStage==4) { P->Attack(); VerifyStage=5; NextCheck=T+.18; }
            else if(VerifyStage==5) { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("MotionAttack.png"),true,false); VerifyStage=6; NextCheck=T+.5; }
            else if(VerifyStage==6) { P->Dodge(); VerifyStage=7; NextCheck=T+.3; }
            else if(VerifyStage==7) { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("MotionRoll.png"),true,false); VerifyStage=8; NextCheck=T+1; }
            else FPlatformMisc::RequestExit(false);
            return;
        }
        if(VerifyStage==0)
        {
            P->SetActorLocation(FVector(-750,0,100)); P->Controller->SetControlRotation(FRotator(-30,0,0));
            P->DisableInput(Cast<APlayerController>(P->Controller)); P->SetActorRotation(FRotator::ZeroRotator);
            for(TActorIterator<ACLEnemy> It(GetWorld());It;++It)
            {
                It->bFrozenForView=true; It->GetCharacterMovement()->StopMovementImmediately();
                if(FParse::Param(FCommandLine::Get(),TEXT("AscensionMonsterView")))
                {
                    const FVector Offset=It->Kind==2?FVector(750,140,0):It->Kind==1?FVector(500,210,0):FVector(450,-220,0);
                    It->SetActorLocation(P->GetActorLocation()+Offset); It->SetActorRotation((P->GetActorLocation()-It->GetActorLocation()).Rotation());
                }
            }
            NextCheck=T+2; VerifyStage=1;
        }
        else if(VerifyStage==1)
        {
            USkeletalMeshComponent* Mesh=P->GetMesh();
            FName Left,Right,Head,Hips;
            for(int32 I=0;I<Mesh->GetNumBones();++I)
            {
                FName Bone=Mesh->GetBoneName(I); FString Name=Bone.ToString().ToLower();
                if(Name.Contains(TEXT("leftshoulder"))) Left=Bone;
                if(Name.Contains(TEXT("rightshoulder"))) Right=Bone;
                if(Name==TEXT("head")) Head=Bone;
                if(Name==TEXT("hips")) Hips=Bone;
            }
            if(!Left.IsNone() && !Right.IsNone() && !Head.IsNone() && !Hips.IsNone())
            {
                const FVector Side=(Mesh->GetSocketLocation(Right)-Mesh->GetSocketLocation(Left)).GetSafeNormal();
                const FVector Up=(Mesh->GetSocketLocation(Head)-Mesh->GetSocketLocation(Hips)).GetSafeNormal();
                const FVector ModelForward=FVector::CrossProduct(Side,Up).GetSafeNormal2D();
                const FVector CameraForward=FRotator(0,P->GetControlRotation().Yaw,0).Vector();
                UE_LOG(LogTemp,Display,TEXT("ASCENSION_MODEL_FACING dot=%.3f model_yaw=%.1f camera_yaw=%.1f"),FVector::DotProduct(ModelForward,CameraForward),ModelForward.Rotation().Yaw,CameraForward.Rotation().Yaw);
                Check(FVector::DotProduct(ModelForward,CameraForward)>.99f,TEXT("visible_model_faces_camera"));
                DrawDebugDirectionalArrow(GetWorld(),P->GetActorLocation()+FVector(0,0,-80),P->GetActorLocation()+FVector(0,0,-80)+CameraForward*200,30,FColor::Green,false,2,0,4);
                DrawDebugDirectionalArrow(GetWorld(),P->GetActorLocation()+FVector(0,0,-75),P->GetActorLocation()+FVector(0,0,-75)+ModelForward*200,30,FColor::Red,false,2,0,4);
            }
            else
            {
                for(int32 I=0;I<Mesh->GetNumBones();++I) UE_LOG(LogTemp,Display,TEXT("ASCENSION_BONE %s"),*Mesh->GetBoneName(I).ToString());
            }
            FString Shot=FPaths::ProjectSavedDir()/TEXT("CombatLab.png");
            FScreenshotRequest::RequestScreenshot(Shot,true,false); NextCheck=T+3; VerifyStage=2;
        }
        else if(VerifyStage==2) { P->OfferAwakening(); NextCheck=T+1; VerifyStage=3; }
        else if(VerifyStage==3) { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("KoreanAwakening.png"),true,false); NextCheck=T+2; VerifyStage=4; }
        else FPlatformMisc::RequestExit(false);
        return;
    }
    if(VerifyStage==0)
    {
        P->ResetLife(); P->SetActorLocation(FVector(0,0,100)); P->Controller->SetControlRotation(FRotator(-30,0,0));
        for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) { It->SetActorTickEnabled(false); It->GetCharacterMovement()->StopMovementImmediately(); if(It->Kind==0) VerifyEnemy=*It; }
        Check(IsValid(VerifyEnemy),TEXT("encounter_spawn"));
        if(!IsValid(VerifyEnemy)) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
        VerifyEnemy->SetActorLocation(FVector(130,0,100)); P->AddMovementInput(FVector(1,0,0)); P->Attack();
        VerifyPosition=P->GetActorLocation(); P->Forward(1); P->Right(1);
        Check(P->GetPendingMovementInputVector().IsNearlyZero() && P->GetVelocity().IsNearlyZero(),TEXT("attack_clears_and_blocks_movement"));
        VerifyStage=1; NextCheck=T+.65;
    }
    else if(VerifyStage==1)
    {
        Check(VerifyEnemy->HP==30,TEXT("one_swing_one_hit"));
        Check(FVector::DistSquared2D(P->GetActorLocation(),VerifyPosition)<1,TEXT("attack_remains_stationary"));
        P->Dodge(); P->ReceiveHit(10); Check(P->HP==100 && P->PerfectUntil>T,TEXT("dodge_immunity_and_counter"));
        Check(FMath::IsNearlyEqual(float(P->DodgeEnd-P->DodgeStart),.65f),TEXT("roll_duration_matches_visual"));
        Check(FVector::DotProduct(P->GetActorForwardVector(),P->DashDirection)>.99f,TEXT("roll_faces_travel_direction"));
        VerifyStage=2; NextCheck=T+.8;
    }
    else if(VerifyStage==2)
    {
        P->ReceiveHit(10); Check(P->HP==90,TEXT("normal_damage_after_dodge"));
        P->OfferAwakening(); Check(P->Offers.Num()==3 && P->Offers[0]!=P->Offers[1] && P->Offers[1]!=P->Offers[2] && P->Offers[0]!=P->Offers[2],TEXT("three_unique_offers"));
        P->Choose(0); P->OfferAwakening(); P->Choose(0); P->OfferAwakening(); P->Choose(0);
        Check(P->Awakenings.Num()==3,TEXT("awakening_capacity"));
        P->OfferAwakening(); int32 NewID=P->Offers[0]; P->Choose(0); P->Choose(1);
        Check(P->Awakenings.Num()==3 && P->Awakenings[1]==NewID,TEXT("replace_without_duplication"));
        PreviousLife=P->LifeID; const FGuid OldWeapon=P->Weapon.ID, OldArmor=P->Armor.ID; P->ReceiveHit(1000);
        Check(P->HP==0 && P->Awakenings.IsEmpty() && P->Offers.IsEmpty(),TEXT("death_clears_awakenings"));
        ACLDrop* DeathBag=nullptr;
        for(TActorIterator<ACLDrop> It(GetWorld());It;++It) if(It->Items.ContainsByPredicate([&](const FCLItem& I){return I.ID==OldWeapon;})) DeathBag=*It;
        Check(IsValid(DeathBag) && DeathBag->Items.Num()==2 && !P->Weapon.ID.IsValid() && !P->Armor.ID.IsValid(),TEXT("death_transfers_equipment_once"));
        P->ResetLife(); Check(P->HP==100 && P->LifeID!=PreviousLife && P->Awakenings.IsEmpty(),TEXT("fresh_life_identity"));
        if(IsValid(DeathBag))
        {
            Check(!P->TakeItem(DeathBag),TEXT("loot_distance_rejected"));
            P->SetActorLocation(DeathBag->GetActorLocation()+FVector(0,0,70));
            Check(P->TakeItem(DeathBag) && P->Inventory.Num()==1 && P->Inventory[0].ID==OldWeapon && DeathBag->Items.Num()==1,TEXT("loot_atomic_transfer"));
            Check(P->TakeItem(DeathBag) && P->Inventory.Num()==2 && P->Inventory[1].ID==OldArmor,TEXT("loot_each_item_once"));
            P->bInventoryOpen=true; P->SelectedItem=0; P->EquipSelected();
            Check(P->Weapon.ID==OldWeapon && P->Inventory.Num()==2 && P->Inventory[0].ID!=OldWeapon,TEXT("equip_swap_preserves_identity"));
            P->Inventory.Add(Lab::Item(1,1)); P->SelectedItem=P->Inventory.Num()-1; P->EquipSelected(); P->ReceiveHit(10);
            Check(P->HP==93 && P->ArmorReduction()==3,TEXT("armor_reduces_real_damage")); P->bInventoryOpen=false;
        }
        for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) { It->SetActorTickEnabled(false); It->GetCharacterMovement()->StopMovementImmediately(); }
        VerifyStage=3; NextCheck=T+.2;
    }
    else if(VerifyStage==3)
    {
        if(WallTrials>0) Check(P->GetActorLocation().X<1480 && P->GetActorLocation().X>1400,TEXT("wall_dodge_blocked"));
        if(WallTrials==20)
        {
            VerifyPosition=P->GetActorLocation(); P->AddMovementInput(FVector(-1,0,0),1);
            VerifyStage=4; NextCheck=T+.2; PreviousLife=P->LifeID;
            // CharacterMovement is checked on the next frame, after dodge has ended.
            VerifyEnemy=nullptr;
            return;
        }
        P->SetActorLocation(FVector(1420,0,100)); P->Controller->SetControlRotation(FRotator(-30,0,0)); P->DodgeReady=0; P->Dodge();
        ++WallTrials; NextCheck=T+.75;
    }
    else
    {
        Check(P->GetActorLocation().X<VerifyPosition.X-1,TEXT("movement_restored_after_dodge"));
        Check(FVector::DotProduct(P->GetActorForwardVector(),FRotator(0,P->GetControlRotation().Yaw,0).Vector())>.99f,TEXT("movement_faces_camera_direction"));
        UE_LOG(LogTemp,Display,TEXT("ASCENSION_VERIFY %s wall_trials=%d"),bFailed?TEXT("FAIL"):TEXT("PASS"),WallTrials);
        FPlatformMisc::RequestExitWithStatus(false,bFailed?1:0); bVerify=false;
    }
}
int32 AAscensionMode::RemainingEnemies() const
{
    int32 Count=0; for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) if(It->HP>0) ++Count; return Count;
}
void AAscensionMode::StartNextEncounter()
{
    ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0)); if(!P || P->HP<=0) return;
    if(RemainingEnemies()>0) { P->Notice=TEXT("남은 몬스터를 처치한 뒤 다음 전투를 시작할 수 있습니다."); return; }
    if(!P->Offers.IsEmpty()) { P->Notice=TEXT("각성을 선택하거나 Esc로 포기한 뒤 N을 누르세요."); return; }
    ++Wave; SpawnEncounter(); P->Notice=FString::Printf(TEXT("%d번째 전투 시작! 장비와 각성은 유지됩니다."),Wave);
}
void AAscensionMode::SpawnEncounter(int32 Seed)
{
    TArray<ACLEnemy*> Existing; for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) Existing.Add(*It);
    for(ACLEnemy* Enemy:Existing) { Enemy->SetActorEnableCollision(false); Enemy->Destroy(); }
    bEncounterCleared=false; SpawnedEnemies=0;
    const bool Fixed=bVerify || (bView && !FParse::Param(FCommandLine::Get(),TEXT("AscensionCombatView")));
    const FVector FixedPositions[]={FVector(-250,300,100),FVector(400,-350,100),FVector(700,400,100)};
    const int32 Count=Fixed?3:FMath::Clamp(6+Wave,7,10);
    FRandomStream Random(Seed?Seed:FMath::Rand()); TArray<FVector> Occupied;
    ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0)); const FVector PlayerAt=P?P->GetActorLocation():FVector(-750,0,100);
    for(int32 I=0;I<Count;++I)
    {
        FVector At; bool Found=Fixed;
        if(Fixed) At=FixedPositions[I];
        else for(int32 Attempt=0;Attempt<500 && !Found;++Attempt)
        {
            At=FVector(Random.FRandRange(-1100,1100),Random.FRandRange(-1050,1050),100);
            if(FVector::DistSquared2D(At,PlayerAt)<FMath::Square(450.f)) continue;
            bool Spaced=true; for(const FVector& Other:Occupied) if(FVector::DistSquared2D(At,Other)<FMath::Square(180.f)) { Spaced=false; break; }
            Found=Spaced && !GetWorld()->OverlapBlockingTestByChannel(At,FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeCapsule(45,82));
        }
        if(!Found) { UE_LOG(LogTemp,Warning,TEXT("ASCENSION_SPAWN_NO_SAFE_POSITION index=%d"),I); continue; }
        const int32 Kind=Fixed?I:I==Count-1?2:I<2?I:Random.RandRange(0,1);
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
        if(ACLEnemy* Enemy=GetWorld()->SpawnActor<ACLEnemy>(At,FRotator::ZeroRotator,Params))
        {
            Enemy->Configure(Kind); if(!Fixed) Enemy->HP=Enemy->MaxHP*=1.f+.08f*FMath::Min(Wave-1,5);
            Occupied.Add(Enemy->GetActorLocation()); ++SpawnedEnemies;
        }
    }
    UE_LOG(LogTemp,Display,TEXT("ASCENSION_ENCOUNTER wave=%d count=%d seed=%d"),Wave,SpawnedEnemies,Seed);
}
void ACLHUD::DrawHUD()
{
    Super::DrawHUD(); ACLHero* P=Cast<ACLHero>(GetOwningPawn()); if(!P || !Canvas) return;
    DrawRect(FLinearColor(0,0,0,.75f),12,12,FMath::Min(1000.f,Canvas->SizeX-24.f),190);
    AAscensionMode* Mode=Cast<AAscensionMode>(GetWorld()->GetAuthGameMode());
    DrawText(FString::Printf(TEXT("각성과 탐욕의 탑 · 전투 %d · 남은 몬스터 %d"),Mode?Mode->Wave:1,Mode?Mode->RemainingEnemies():0),FLinearColor::White,25,22,nullptr,1.15);
    DrawText(FString::Printf(TEXT("체력 %.0f / 100    회피 %.1f초    Q 스킬 %.1f초    락온 %s"),P->HP,FMath::Max(0.,P->DodgeReady-Lab::Now(P)),FMath::Max(0.,P->SkillReady-Lab::Now(P)),IsValid(P->Locked)?TEXT("켜짐"):TEXT("꺼짐")),FLinearColor::White,25,49);
    DrawText(TEXT("WASD 이동 · 마우스 시점 · 좌클릭 검 공격 · 우클릭 강공격 · Space 회피"),FLinearColor::White,25,74);
    DrawText(TEXT("Tab 락온 · Q 스킬 · E 획득/제단 · I 가방 · N 다음 전투 · R 다시 시작"),FLinearColor::White,25,97);
    DrawText(P->Notice,FLinearColor::Yellow,25,123);
    FString Owned=TEXT("보유 각성: "); if(P->Awakenings.IsEmpty()) Owned+=TEXT("없음");
    for(int32 I=0;I<P->Awakenings.Num();++I) Owned+=FString::Printf(TEXT("[%d] %s   "),I+1,Lab::Names[P->Awakenings[I]]);
    DrawText(Owned,FLinearColor::White,25,148); DrawText(TEXT("개발 중: 탑 등반 · 파티 · 멀티플레이"),FLinearColor(.7f,.7f,.7f),25,174);
    if(Mode && Mode->bEncounterCleared) DrawText(TEXT("전투 완료 · 장비를 챙긴 뒤 N: 다음 전투"),FLinearColor::Yellow,Canvas->SizeX*.5f-170,Canvas->SizeY-45);
    if(Lab::Now(P)<P->HitMarkerUntil)
    {
        const float X=Canvas->SizeX*.5f,Y=Canvas->SizeY*.5f;
        for(int32 SX:{-1,1}) for(int32 SY:{-1,1}) DrawLine(X+SX*7,Y+SY*7,X+SX*17,Y+SY*17,FLinearColor(1,.85f,.3f),2);
    }
    if(P->bInventoryOpen)
    {
        DrawRect(FLinearColor(0,0,0,.9f),20,225,FMath::Min(1000.f,Canvas->SizeX-40.f),420);
        DrawText(TEXT("장비 가방 · 위/아래: 선택 · Enter: 장착 · I/Esc: 닫기 (전투는 계속됩니다)"),FLinearColor::Yellow,35,242);
        DrawText(TEXT("검: ")+Lab::ItemName(P->Weapon)+TEXT(" / 방어구: ")+Lab::ItemName(P->Armor),FLinearColor::White,35,270);
        DrawText(FString::Printf(TEXT("가방 %d / 12"),P->Inventory.Num()),FLinearColor::White,35,298);
        for(int32 I=0;I<P->Inventory.Num();++I) DrawText(FString::Printf(TEXT("%s %d. %s"),I==P->SelectedItem?TEXT("▶"):TEXT("  "),I+1,*Lab::ItemName(P->Inventory[I])),I==P->SelectedItem?FLinearColor::Yellow:FLinearColor::White,35,328+I*24);
    }
    if(!P->Offers.IsEmpty())
    {
        DrawRect(FLinearColor(0,0,0,.9f),20,225,FMath::Min(1000.f,Canvas->SizeX-40.f),260);
        DrawText(P->PendingReplacement>=0?TEXT("보유 각성 교체 · 1 / 2 / 3: 교체할 슬롯 · Esc: 포기"):TEXT("각성 선택 · 1 / 2 / 3: 획득 · Esc: 포기"),FLinearColor::Yellow,35,241);
        if(P->PendingReplacement>=0) DrawText(FString::Printf(TEXT("새로 얻을 각성: %s"),Lab::Names[P->PendingReplacement]),FLinearColor::White,35,267);
        for(int32 I=0;I<P->Offers.Num();++I)
        {
            int32 ID=P->PendingReplacement>=0?P->Awakenings[I]:P->Offers[I];
            FLinearColor Color=ID==3 || ID==4?FLinearColor(1,.55f,.1f):FLinearColor::White;
            DrawText(FString::Printf(TEXT("%d. %s"),I+1,Lab::Names[ID]),Color,35,300+I*55);
            DrawText(Lab::Details[ID],Color,55,323+I*55);
        }
    }
}
