#include "CombatLab.h"
#include "HeroMotion.h"
#include "CLNetSession.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
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
    const TCHAR* Names[] = {TEXT("번개 반격"), TEXT("연격 마무리"), TEXT("넓은 파동"), TEXT("탐욕 · 폭발 회피"), TEXT("탐욕 · 파괴의 일격"), TEXT("빠른 파동"),TEXT("자동 번개 강화"),TEXT("회전 칼날 강화"),TEXT("검기 관통 강화")};
    const TCHAR* Details[] = {TEXT("정확한 회피 후 반격 피해 추가 +15"), TEXT("일반 공격의 세 번째 타격 피해 +12"), TEXT("Q 스킬 반경 320 → 440"), TEXT("회피 폭발 피해 18 / 대가: 주변 적의 경계 확대"), TEXT("강공격 피해 +25 / 대가: 공격 후 빈틈 +0.45초"), TEXT("Q 스킬 재사용 대기시간 6초 → 4초"),TEXT("가까운 적 자동 공격 · 피해 +5 / 주기 단축"),TEXT("주변 적 자동 공격 · 피해 +3"),TEXT("일반 검기 대상 +1 (최대 4명) · 검 피해 +3 · 체력 +15 회복")};
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
    Input->BindAction("Heavy",IE_Released,this,&ACLHero::ReleaseHeavy);
    Input->BindAction("Dodge",IE_Pressed,this,&ACLHero::Dodge); Input->BindAction("Skill",IE_Pressed,this,&ACLHero::Skill);
    Input->BindAction("Lock",IE_Pressed,this,&ACLHero::ToggleLock); Input->BindAction("Restart",IE_Pressed,this,&ACLHero::ResetLife).bExecuteWhenPaused=true;
    Input->BindAction("Altar",IE_Pressed,this,&ACLHero::UseAltar);
    Input->BindAction("Inventory",IE_Pressed,this,&ACLHero::ToggleInventory);
    Input->BindAction("Previous",IE_Pressed,this,&ACLHero::SelectPrevious);
    Input->BindAction("Next",IE_Pressed,this,&ACLHero::SelectNext);
    Input->BindAction("Equip",IE_Pressed,this,&ACLHero::EquipSelected);
    Input->BindAction("NextEncounter",IE_Pressed,this,&ACLHero::NextEncounter);
    Input->BindAction("Choice1",IE_Pressed,this,&ACLHero::Choice1).bExecuteWhenPaused=true; Input->BindAction("Choice2",IE_Pressed,this,&ACLHero::Choice2).bExecuteWhenPaused=true;
    Input->BindAction("Choice3",IE_Pressed,this,&ACLHero::Choice3).bExecuteWhenPaused=true; Input->BindAction("Skip",IE_Pressed,this,&ACLHero::Skip).bExecuteWhenPaused=true;
}
bool ACLHero::UsesBladeWave() const { const AAscensionMode* Mode=Cast<AAscensionMode>(GetWorld()->GetAuthGameMode()); return Mode && Mode->bSurvival; }
void ACLHero::Forward(float V) { MoveForward=V; if(HP>0 && Lab::Now(this)>=DodgeEnd && (UsesBladeWave() || Lab::Now(this)>=AttackEnd)) AddMovementInput(FRotator(0,GetControlRotation().Yaw,0).Vector(),V); }
void ACLHero::Right(float V) { MoveRight=V; if(HP>0 && Lab::Now(this)>=DodgeEnd && (UsesBladeWave() || Lab::Now(this)>=AttackEnd)) AddMovementInput(FRotationMatrix(FRotator(0,GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::Y),V); }
void ACLHero::LookX(float V) { if(!IsValid(Locked)) AddControllerYawInput(V); }
void ACLHero::LookY(float V) { AddControllerPitchInput(V); }
void ACLHero::Tick(float Delta)
{
    Super::Tick(Delta); const double T=Lab::Now(this); if(HP<=0) return;
    TryLevelUp(); if(!Offers.IsEmpty() || bInventoryOpen) bCharging=false;
    if(bLevelChoice) return;
    TickAuxiliaryCombat();
    const float Impact=FMath::Clamp(float((CameraImpactUntil-T)/.12),0.f,1.f)*CameraImpactStrength;
    Camera->SetRelativeLocation(FVector(0,FMath::Sin(T*135)*Impact,FMath::Sin(T*175)*Impact*.55f));
    // Show the sword motion while the capsule keeps moving. Charging holds
    // the wind-up pose; release plays the remainder of the heavy slash.
    const int32 State=T<DodgeEnd?4:bCharging?3:T<AttackEnd?(bHeavy?3:2):GetVelocity().SizeSquared2D()>100?1:0;
    if(UsesBladeWave() && bHasModel)
    {
        if(State==4 && VisualState!=4) GetMesh()->SetRelativeRotation(FRotator(0,270,0));
        UCLHeroMotion* Motion=Cast<UCLHeroMotion>(GetMesh()->GetAnimInstance());
        if(!Motion) { GetMesh()->SetAnimInstanceClass(UCLHeroMotion::StaticClass()); Motion=Cast<UCLHeroMotion>(GetMesh()->GetAnimInstance()); }
        if(Motion && Clips.Num()==5)
        {
            Motion->IdleClip=Clips[0]; Motion->RunClip=Clips[1];
            Motion->BaseTime+=Delta; Motion->MovingWeight=FMath::Clamp(GetVelocity().Size2D()/220.f,0.f,1.f);
            Motion->ActionWeight=State>=2?1.f:0.f;
            if(State>=2 && Clips[State])
            {
                Motion->ActionClip=Clips[State]; Motion->bFullBodyAction=State==4;
                if(State==4) Motion->ActionTime=float((T-DodgeStart)/(DodgeEnd-DodgeStart))*Clips[4]->GetPlayLength();
                else if(bCharging) Motion->ActionTime=.55f+ChargeProgress()*.55f;
                else
                {
                    const float Start=bHeavy?1.1f:.1f,Padding=bHeavy?.9f:.2f;
                    Motion->ActionTime=Start+float((T-AttackStart)/(AttackEnd-AttackStart))*(Clips[State]->GetPlayLength()-Start-Padding);
                }
            }
        }
        VisualState=State;
    }
    else Lab::Visual(this,Clips,State,VisualState,T<DodgeEnd?float(DodgeEnd-DodgeStart):T<AttackEnd?float(AttackEnd-AttackStart):0);
    if(bCharging)
    {
        const FVector Center=GetActorLocation()+FVector(0,0,30)+GetActorForwardVector()*50;
        const float Charge=ChargeProgress(),Width=70+Charge*160;
        const FColor Color=FLinearColor::LerpUsingHSV(FLinearColor(1,.25f,.02f),FLinearColor(1,.95f,.35f),Charge).ToFColor(true);
        DrawDebugSphere(GetWorld(),Center,15+Charge*55,20,Color,false,-1,0,2+Charge*3);
        const FVector Side=FRotationMatrix(FRotator(0,GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::Y);
        DrawDebugLine(GetWorld(),Center-Side*Width,Center+Side*Width,Color,false,-1,0,2+Charge*4);
        for(int32 I=0;I<8;++I) { const FVector Ray=FVector(1,0,.35f).GetSafeNormal().RotateAngleAxis(float(T*160)+I*45,FVector::UpVector); DrawDebugLine(GetWorld(),Center+Ray*(75+Charge*50),Center+Ray*(20+Charge*15),Color,false,-1,0,2); }
    }
    // Slash clips have their own forward axis too; keep the visible torso on
    // the actor's locked swing direction throughout normal and heavy attacks.
    if(bHasModel && T>=DodgeEnd) Lab::AlignBodyForward(GetMesh(),T<AttackEnd || bCharging?Delta:0.f);
    if(bHasModel)
    {
        const FBoxSphereBounds Bounds=GetMesh()->GetSkeletalMeshAsset()->GetImportedBounds();
        FVector Offset(0,0,-88-(Bounds.Origin.Z-Bounds.BoxExtent.Z)*GetMesh()->GetRelativeScale3D().Z);
        if(T<DodgeEnd || T<AttackEnd || bCharging)
        {
            // Keep the animated pelvis over the capsule; gameplay owns horizontal roll travel.
            const FVector Hips=GetMesh()->GetBoneLocation(TEXT("hips"),EBoneSpaces::ComponentSpace);
            Offset-=GetMesh()->GetRelativeRotation().RotateVector(FVector(Hips.X,Hips.Y,0)*GetMesh()->GetRelativeScale3D());
        }
        GetMesh()->SetRelativeLocation(Offset);
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("AscensionCollisionView"))) DrawDebugBox(GetWorld(),GetActorLocation(),FVector(27,27,75),GetActorQuat(),FColor::Cyan,false,-1,0,2);
    if(IsValid(Locked) && (Locked->HP<=0 || FVector::DistSquared(GetActorLocation(),Locked->GetActorLocation())>FMath::Square(1500.f))) Locked=nullptr;
    if(IsValid(Locked))
    {
        FRotator R=GetControlRotation(); R.Yaw=(Locked->GetActorLocation()-GetActorLocation()).Rotation().Yaw; Controller->SetControlRotation(R);
        DrawDebugSphere(GetWorld(),Locked->GetActorLocation()+FVector(0,0,120),25,12,FColor::Cyan,false,-1,0,2);
    }
    if((UsesBladeWave() || T>=AttackEnd) && T>=DodgeEnd)
    {
        const FRotator Facing(0,GetControlRotation().Yaw,0);
        SetActorRotation(FMath::RInterpTo(GetActorRotation(),Facing,Delta,18.f));
    }
    if(T<AttackEnd && !UsesBladeWave()) { ConsumeMovementInputVector(); GetCharacterMovement()->StopMovementImmediately(); }
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
    if(!bResolved && T>=HitTime)
    {
        if(UsesBladeWave()) { SwingDirection=FRotator(0,GetControlRotation().Yaw,0).Vector(); SetActorRotation(SwingDirection.Rotation()); }
        bResolved=true; ResolveSwing();
    }
    if(T<AttackEnd && !UsesBladeWave()) { Lab::Arc(GetWorld(),GetActorLocation(),SwingDirection,bHeavy?230:190,bHeavy?65:50,bResolved?FColor::White:FColor::Cyan); }
    const float Swing=T<AttackEnd ? FMath::Sin(float((T-AttackStart)/(AttackEnd-AttackStart))*PI)*120-60 : 0;
    if(!bHasModel) Sword->SetRelativeRotation(FRotator(0,Swing,0));
}
void ACLHero::Attack() { StartAttack(false); }
float ACLHero::ChargeProgress() const { return bCharging?FMath::Clamp(float((Lab::Now(this)-ChargeStart)/MaximumChargeSeconds),0.f,1.f):0.f; }
bool ACLHero::CanReleaseHeavy() const { return bCharging && Lab::Now(this)-ChargeStart+1.e-6>=MinimumChargeSeconds; }
void ACLHero::Heavy()
{
    if(!UsesBladeWave()) { StartAttack(true); return; }
    const double T=Lab::Now(this);
    if(HP<=0 || bCharging || T<AttackEnd || T<DodgeEnd || !Offers.IsEmpty() || bInventoryOpen) return;
    bCharging=true; ChargeStart=T; ReleasedCharge=0; VisualState=-1;
}
void ACLHero::ReleaseHeavy()
{
    if(!bCharging) return;
    const bool Ready=CanReleaseHeavy();
    ReleasedCharge=ChargeProgress(); bCharging=false; VisualState=-1;
    if(!Ready) { ReleasedCharge=0; Notice=TEXT("충전 취소 · 강화공격은 0.6초 이상 모아야 발사됩니다"); return; }
    if(HP<=0 || !Offers.IsEmpty() || bInventoryOpen) return;
    StartAttack(true);
}
void ACLHero::StartAttack(bool HeavyAttack)
{
    const double T=Lab::Now(this); if(HP<=0 || bCharging || T<AttackEnd || T<DodgeEnd || !Offers.IsEmpty() || bInventoryOpen) return;
    bHeavy=HeavyAttack; Combo=HeavyAttack?0:(T-LastCombo<1.1?Combo%3+1:1); LastCombo=T;
    bEmpowered=T<PerfectUntil; PerfectUntil=0;
    AttackStart=T; HitTime=T+(bHeavy?.46:.28);
    AttackEnd=T+(bHeavy?.95:.68)+(bHeavy && HasAwakening(4)?.45:0); bResolved=false;
    if(!UsesBladeWave()) { ConsumeMovementInputVector(); GetCharacterMovement()->StopMovementImmediately(); }
    VisualState=-1;
    SwingDirection=FRotator(0,GetControlRotation().Yaw,0).Vector(); SetActorRotation(SwingDirection.Rotation());
    if(UsesBladeWave()) { HitTime=T+(bHeavy?.34:.28); AttackEnd=T+(bHeavy?.85:.72)+(bHeavy && HasAwakening(4)?.45:0); }
}
void ACLHero::ResolveSwing()
{
    float Damage=(bHeavy?35:20)+WeaponBonus()+PowerRank*3; if(bEmpowered) Damage+=HasAwakening(0)?25:10;
    if(!bHeavy && Combo==3 && HasAwakening(1)) Damage+=12;
    if(bHeavy && HasAwakening(4)) Damage+=25;
    if(UsesBladeWave())
    {
        if(bHeavy) Damage*=1.f+ReleasedCharge*2.f;
        FActorSpawnParameters Params; Params.Owner=this;
        if(ACLBladeWave* Wave=GetWorld()->SpawnActor<ACLBladeWave>(GetActorLocation()+SwingDirection*45,SwingDirection.Rotation(),Params))
        { Wave->Hero=this; Wave->Direction=SwingDirection; Wave->Damage=Damage; Wave->Radius=bHeavy?70+ReleasedCharge*160:55; Wave->Pierces=bHeavy?4+FMath::RoundToInt(ReleasedCharge*6):NormalWaveTargets(); Wave->Charge=bHeavy?ReleasedCharge:0; Wave->SetLifeSpan(bHeavy?1.2f+ReleasedCharge*.8f:1.2f); }
        return;
    }
    bool Hit=false; FVector Contact;
    for(TActorIterator<ACLEnemy> It(GetWorld());It;++It)
        if(It->HP>0 && Lab::InArc(this,*It,SwingDirection,bHeavy?230:190,bHeavy?65:50)) { Contact=It->GetActorLocation(); It->ReceiveHit(Damage); Hit=true; }
    if(Hit) ImpactFeedback(bHeavy?7.f:4.f,Contact);
}
void ACLHero::ImpactFeedback(float Strength,const FVector& Position)
{
    const double T=Lab::Now(this); CameraImpactUntil=T+.12; CameraImpactStrength=Strength; HitMarkerUntil=T+.16;
    if(!UsesBladeWave())
    { CustomTimeDilation=.18f; GetWorldTimerManager().SetTimer(ImpactPauseTimer,FTimerDelegate::CreateWeakLambda(this,[this]() { CustomTimeDilation=1.f; }),.045f,false); }
    if(USoundBase* Sound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Audio/SwordImpact.SwordImpact"))) UGameplayStatics::PlaySoundAtLocation(this,Sound,Position,.45f,FMath::FRandRange(.94f,1.06f));
    for(int32 I=0;I<10;++I) { const FVector Start=Position+FVector(0,0,15); DrawDebugLine(GetWorld(),Start,Start+FMath::VRand()*FMath::FRandRange(20.f,65.f),FColor(255,220,100),false,.1f,0,2); }
}
void ACLHero::NextEncounter()
{
    if(AAscensionMode* Mode=Cast<AAscensionMode>(GetWorld()->GetAuthGameMode())) Mode->StartNextEncounter();
}
void ACLHero::GainExperience(int32 Amount)
{
    const AAscensionMode* Mode=Cast<AAscensionMode>(GetWorld()->GetAuthGameMode());
    if(HP<=0 || Amount<=0 || !Mode || !Mode->bSurvival) return;
    Experience+=Amount; TryLevelUp();
}
void ACLHero::TryLevelUp()
{
    if(HP<=0 || !Offers.IsEmpty() || Experience<ExperienceRequired()) return;
    Experience-=ExperienceRequired(); ++Level; bLevelChoice=true; Offers={6,7,8}; PendingReplacement=-1;
    bCharging=false;
    Notice=TEXT("레벨 업! 시간이 멈췄습니다. 1 · 2 · 3: 성장 선택 / Esc: 포기");
    UGameplayStatics::SetGamePaused(this,true);
}
void ACLHero::TickAuxiliaryCombat()
{
    const AAscensionMode* Mode=Cast<AAscensionMode>(GetWorld()->GetAuthGameMode());
    if(!Mode || !Mode->bSurvival || HP<=0 || Mode->bVictory || bLevelChoice) return;
    const double T=Lab::Now(this);
    for(int32 I=0;I<2;++I)
    {
        const float Angle=float(T*180)+I*180;
        const FVector Point=GetActorLocation()+FVector(220,0,0).RotateAngleAxis(Angle,FVector::UpVector);
        DrawDebugSolidBox(GetWorld(),Point,FVector(4,4,25),FRotator(0,Angle,35).Quaternion(),FColor(100,220,255),false,-1);
    }
    if(T>=LightningReady)
    {
        ACLEnemy* Target=nullptr; float Best=FMath::Square(700.f);
        for(TActorIterator<ACLEnemy> It(GetWorld());It;++It)
        { const float D=FVector::DistSquared2D(GetActorLocation(),It->GetActorLocation()); if(It->HP>0 && D<Best && Lab::Clear(this,*It)) { Best=D; Target=*It; } }
        if(Target)
        {
            const FVector At=Target->GetActorLocation(); Target->ReceiveHit(12+LightningRank*5);
            DrawDebugLine(GetWorld(),At+FVector(0,0,350),At,FColor::Cyan,false,.2f,0,8);
            DrawDebugSphere(GetWorld(),At,25,8,FColor::Cyan,false,.2f,0,2);
            LightningReady=T+FMath::Max(.7f,2.2f-LightningRank*.15f);
        }
    }
    if(T>=BladeReady)
    {
        for(TActorIterator<ACLEnemy> It(GetWorld());It;++It)
            if(It->HP>0 && FVector::DistSquared2D(GetActorLocation(),It->GetActorLocation())<=FMath::Square(240.f) && Lab::Clear(this,*It)) It->ReceiveHit(5+BladeRank*3);
        BladeReady=T+1.2;
    }
}
void ACLHero::Dodge()
{
    double T=Lab::Now(this); if(HP<=0 || T<DodgeReady || T<DodgeEnd || (!bResolved && T<HitTime)) return;
    AttackEnd=T; bResolved=true; bCharging=false; ReleasedCharge=0;
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
    double T=Lab::Now(this); if(HP<=0 || bCharging || T<SkillReady || T<AttackEnd || T<DodgeEnd || !Offers.IsEmpty() || bInventoryOpen) return;
    SkillReady=T+(HasAwakening(5)?4:6); float Range=HasAwakening(2)?440:320;
    DrawDebugSphere(GetWorld(),GetActorLocation(),Range,32,FColor::Cyan,false,.35,0,3);
    for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) if(It->HP>0 && FVector::DistSquared(GetActorLocation(),It->GetActorLocation())<=FMath::Square(Range) && Lab::Clear(this,*It)) It->ReceiveHit(25);
}
void ACLHero::ReceiveHit(float Damage)
{
    if(const AAscensionMode* Mode=Cast<AAscensionMode>(GetWorld()->GetAuthGameMode()); Mode && Mode->bRestArea) return;
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
        Level=1; Experience=PowerRank=LightningRank=BladeRank=0; bLevelChoice=false; bCharging=false; ReleasedCharge=0;
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
    bCharging=false;
    TArray<int32> Pool; for(int32 I=0;I<6;++I) if(!HasAwakening(I)) Pool.Add(I);
    while(!Pool.IsEmpty() && Offers.Num()<3) { int32 Index=FMath::RandRange(0,Pool.Num()-1); Offers.Add(Pool[Index]); Pool.RemoveAt(Index); }
    Notice=TEXT("1 · 2 · 3: 각성 선택 / Esc: 포기. 선택 중에도 적은 움직입니다.");
}
void ACLHero::UseAltar()
{
    if(HP<=0 || !Offers.IsEmpty()) return;
    if(AAscensionMode* Mode=Cast<AAscensionMode>(GetWorld()->GetAuthGameMode()); Mode && Mode->bSurvival)
    {
        if(Mode->bRestArea) { Notice=TEXT("휴식 구역에 도착했습니다. 이번 등반 완료! R: 새 도전"); return; }
        if(FVector::Dist2D(GetActorLocation(),FVector::ZeroVector)<300)
        { if(Mode->bBossDefeated) Mode->AdvanceFloor(); else Mode->SummonGatekeeper(); return; }
    }
    ACLDrop* Nearest=nullptr; float Best=FMath::Square(200.f);
    for(TActorIterator<ACLDrop> It(GetWorld());It;++It)
    {
        float D=FVector::DistSquared(GetActorLocation(),It->GetActorLocation());
        if(!It->IsHidden() && !It->Items.IsEmpty() && D<Best && Lab::Clear(this,*It)) { Nearest=*It; Best=D; }
    }
    if(Nearest) { TakeItem(Nearest); return; }
    if(FVector::Dist2D(GetActorLocation(),FVector(650,650,0))>180) { Notice=TEXT("엘리트 근처의 금색 제단으로 이동한 뒤 E를 누르세요."); return; }
    if(AltarUses>=3) { Notice=TEXT("이번 생명에서는 제단을 모두 사용했습니다. 최대 3회 이용 가능"); return; }
    ++AltarUses; OfferAwakening();
}
void ACLHero::Choose(int32 Index)
{
    if(HP<=0) return;
    if(bLevelChoice)
    {
        if(!Offers.IsValidIndex(Index)) return;
        if(Offers[Index]==6) ++LightningRank;
        else if(Offers[Index]==7) ++BladeRank;
        else { ++PowerRank; HP=FMath::Min(100.f,HP+15); }
        Offers.Empty(); bLevelChoice=false; UGameplayStatics::SetGamePaused(this,false);
        Notice=TEXT("성장 완료! 경험치를 모아 다음 강화를 선택하세요."); TryLevelUp(); return;
    }
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
void ACLHero::Skip() { if(bInventoryOpen) { bInventoryOpen=false; return; } Offers.Empty(); PendingReplacement=-1; if(bLevelChoice) { bLevelChoice=false; UGameplayStatics::SetGamePaused(this,false); } Notice=TEXT("새 강화를 포기했습니다. 기존 능력은 유지됩니다."); }
float ACLHero::WeaponBonus() const { return Weapon.ID.IsValid()?Weapon.Tier*4.f:0; }
float ACLHero::ArmorReduction() const { return Armor.ID.IsValid() && Armor.Tier==1?3.f:0; }
void ACLHero::ToggleInventory() { if(HP>0 && Offers.IsEmpty()) { bInventoryOpen=!bInventoryOpen; SelectedItem=0; if(bInventoryOpen) bCharging=false; } }
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
    if(HP<=0 || !IsValid(Drop) || Drop->IsHidden() || Drop->Items.IsEmpty() || FVector::DistSquared(GetActorLocation(),Drop->GetActorLocation())>FMath::Square(200.f) || !Lab::Clear(this,Drop)) return false;
    if(Inventory.Num()>=12) { Notice=TEXT("가방이 가득 찼습니다. 장비를 더 가져올 수 없습니다."); return false; }
    FCLItem Item=Drop->Items[0]; Inventory.Add(Item); Drop->Items.RemoveAt(0);
    Notice=FString::Printf(TEXT("획득: %s / I: 가방 열기 / 위·아래: 선택 / Enter: 장착"),*Lab::ItemName(Item));
    UE_LOG(LogTemp,Display,TEXT("ASCENSION_LOOT item=%s life=%s"),*Item.ID.ToString(),*LifeID.ToString());
    if(Drop->Items.IsEmpty()) Drop->Destroy(); return true;
}
void ACLHero::ResetLife()
{
    UGameplayStatics::SetGamePaused(this,false); bLevelChoice=false;
    Level=1; Experience=PowerRank=0; LightningRank=BladeRank=1; LightningReady=BladeReady=0;
    HP=100; Awakenings.Empty(); Offers.Empty(); PendingReplacement=-1; LifeID=FGuid::NewGuid(); Locked=nullptr;
    Inventory.Empty(); Weapon=Lab::Item(0,0); Armor=Lab::Item(1,0); bInventoryOpen=false; SelectedItem=0;
    GetMesh()->SetVisibility(bHasModel); Sword->SetVisibility(true); Body->SetVisibility(!bHasModel); VisualState=-1;
    bResolved=true; bCharging=false; ReleasedCharge=0; AttackEnd=DodgeEnd=ImmuneEnd=DodgeReady=SkillReady=PerfectUntil=0; Combo=0; AltarUses=0; LastCombo=-100;
    MoveForward=MoveRight=0; HurtUntil=0;
    GetCharacterMovement()->StopMovementImmediately(); SetActorLocation(FVector(-750,0,100));
    GetWorldTimerManager().ClearTimer(ImpactPauseTimer); CustomTimeDilation=1.f; CameraImpactUntil=HitMarkerUntil=0; Camera->SetRelativeLocation(FVector::ZeroVector);
    if(Controller) Controller->SetControlRotation(FRotator(-30,0,0));
    if(AAscensionMode* Mode=Cast<AAscensionMode>(GetWorld()->GetAuthGameMode())) { Mode->Wave=1; Mode->ResetSurvival(); Mode->SpawnEncounter(); }
    Notice=TEXT("새 전투를 시작합니다. 장비와 각성 없이 다시 도전하세요.");
}

ACLEnemy::ACLEnemy()
{
    PrimaryActorTick.bCanEverTick=true; GetCapsuleComponent()->InitCapsuleSize(35,80);
    GetCharacterMovement()->MaxWalkSpeed=220; GetCharacterMovement()->bOrientRotationToMovement=false;
    // These prototype enemies steer themselves without an AIController.
    GetCharacterMovement()->bRunPhysicsWithNoController=true;
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
const TCHAR* ACLEnemy::BossHint() const
{
    if(Phase==3) return TEXT("공격 후 빈틈 · 차징 검기로 반격!");
    if(Phase==0) return TEXT("수문장 접근 중 · 공격 방향을 확인하세요");
    return BossAttack==0?TEXT("전방 베기 · 등 뒤나 바깥으로 회피"):
        BossAttack==1?TEXT("직선 돌진 · 옆으로 회피"):TEXT("주변 내려찍기 · 원 밖으로 이동");
}
void ACLEnemy::BeginBossAttack(int32 Pattern)
{
    ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0));
    if(!bBoss || HP<=0 || !P || P->HP<=0) return;
    BossAttack=FMath::Clamp(Pattern,0,2); Phase=1; BossTellStart=Lab::Now(this);
    BossOrigin=GetActorLocation(); BossAim=(P->GetActorLocation()-BossOrigin).GetSafeNormal2D();
    if(BossAim.IsNearlyZero()) BossAim=GetActorForwardVector();
    SetActorRotation(BossAim.Rotation()); VisualState=-1; bDashHit=false;
    const float Tell=BossAttack==0?1.05f:BossAttack==1?1.25f:1.4f;
    Deadline=BossTellStart+Tell*(bEnraged?.85f:1.f);
    ConsumeMovementInputVector(); GetCharacterMovement()->StopMovementImmediately();
    UE_LOG(LogTemp,Display,TEXT("ASCENSION_BOSS_TELL pattern=%d enraged=%d"),BossAttack,bEnraged);
}
void ACLEnemy::ResolveBossAttack()
{
    ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0));
    if(!bBoss || HP<=0 || Phase!=1 || !P || P->HP<=0) return;
    const double T=Lab::Now(this);
    if(BossAttack==1)
    {
        Phase=2; BossDashRemaining=900; Deadline=T+1.2; return;
    }
    const bool Hit=BossAttack==0?Lab::InArc(this,P,BossAim,280,70):
        FVector::Dist2D(BossOrigin,P->GetActorLocation())<=420 && Lab::Clear(this,P);
    if(Hit) P->ReceiveHit(BossAttack==0?20:28);
    BossImpactUntil=T+.45; Phase=3;
    Deadline=T+(bEnraged?1.15:1.6); BossAttackCount=(BossAttack+1)%3;
}
void ACLEnemy::TickBoss(float Delta,ACLHero* P)
{
    const double T=Lab::Now(this);
    if(!bEnraged && HP<=MaxHP*.5f)
    {
        bEnraged=true; P->Notice=TEXT("수문장 격노! 공격 예고와 빈틈이 짧아집니다.");
        UE_LOG(LogTemp,Display,TEXT("ASCENSION_BOSS_ENRAGED"));
    }
    // Keep attack timing in world time: ordinary hit-stop must not desync the tells.
    Lab::Visual(this,Clips,Phase==1?2:Phase==2 || GetVelocity().SizeSquared2D()>100?1:0,VisualState,
        Phase==1?float(Deadline-BossTellStart):0);
    const FColor Color=bEnraged?FColor(255,80,50):FColor(255,175,50);
    if(T<BossImpactUntil)
    {
        if(BossAttack==2) DrawDebugCircle(GetWorld(),BossOrigin-FVector(0,0,BossOrigin.Z-12),420,64,Color,false,-1,0,8,FVector::ForwardVector,FVector::RightVector,false);
        else Lab::Arc(GetWorld(),BossOrigin,BossAim,280,70,Color);
    }
    if(Phase==0)
    {
        const FVector To=P->GetActorLocation()-GetActorLocation();
        const int32 Next=To.Size2D()>600?1:BossAttackCount%3;
        const float Range=Next==0?260.f:Next==1?1100.f:440.f;
        if(To.Size2D()<=Range && Lab::Clear(this,P)) { BeginBossAttack(Next); return; }
        FCollisionQueryParams Params; Params.AddIgnoredActor(this); FHitResult Obstacle;
        for(float Angle:{0.f,45.f,-45.f,90.f,-90.f,135.f,-135.f})
        {
            const FVector Candidate=To.GetSafeNormal2D().RotateAngleAxis(Angle,FVector::UpVector);
            if(!GetWorld()->SweepSingleByChannel(Obstacle,GetActorLocation(),GetActorLocation()+Candidate*110,FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeCapsule(35,80),Params))
            { AddMovementInput(Candidate); SetActorRotation(Candidate.Rotation()); break; }
        }
    }
    else if(Phase==1)
    {
        FVector Ground=BossOrigin; Ground.Z=12;
        const float Progress=FMath::Clamp(float((T-BossTellStart)/FMath::Max(.01,Deadline-BossTellStart)),0.f,1.f);
        if(BossAttack==0)
        {
            Lab::Arc(GetWorld(),BossOrigin,BossAim,280,70,Color);
            Lab::Arc(GetWorld(),BossOrigin,BossAim,280*Progress,70,Color);
        }
        else if(BossAttack==1)
        {
            const FVector Side=FVector::CrossProduct(BossAim,FVector::UpVector)*90;
            DrawDebugLine(GetWorld(),Ground+Side,Ground+BossAim*900+Side,Color,false,-1,0,5);
            DrawDebugLine(GetWorld(),Ground-Side,Ground+BossAim*900-Side,Color,false,-1,0,5);
            DrawDebugLine(GetWorld(),Ground+BossAim*900+Side,Ground+BossAim*900-Side,Color,false,-1,0,5);
            DrawDebugDirectionalArrow(GetWorld(),Ground,Ground+BossAim*(100+800*Progress),35,Color,false,-1,0,5);
        }
        else
        {
            DrawDebugCircle(GetWorld(),Ground,420,64,Color,false,-1,0,5,FVector::ForwardVector,FVector::RightVector,false);
            DrawDebugCircle(GetWorld(),Ground,420*Progress,48,Color,false,-1,0,4,FVector::ForwardVector,FVector::RightVector,false);
        }
        if(T>=Deadline) ResolveBossAttack();
    }
    else if(Phase==2)
    {
        const FVector Start=GetActorLocation();
        const float Step=FMath::Min(BossDashRemaining,1500*Delta);
        FHitResult Wall; SetActorLocation(Start+BossAim*Step,true,&Wall);
        const FVector End=GetActorLocation(); BossDashRemaining=FMath::Max(0.f,BossDashRemaining-float(FVector::Dist2D(Start,End)));
        // Swept distance avoids skipping the player during a slow frame.
        const FVector Closest=FMath::ClosestPointOnSegment(P->GetActorLocation(),Start,End);
        if(!bDashHit && FVector::Dist2D(Closest,P->GetActorLocation())<=90 && Lab::Clear(this,P))
        { P->ReceiveHit(24); bDashHit=true; }
        if(Wall.bBlockingHit || BossDashRemaining<=1 || T>=Deadline)
        {
            Phase=3; Deadline=T+(bEnraged?1.35:1.9); BossAttackCount=(BossAttack+1)%3;
            GetCharacterMovement()->StopMovementImmediately();
        }
    }
    else if(T>=Deadline) Phase=0;
}
void ACLEnemy::Tick(float Delta)
{
    Super::Tick(Delta); ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0)); if(HP<=0 || !P || P->HP<=0) { GetCharacterMovement()->StopMovementImmediately(); return; }
    const double T=Lab::Now(this); FVector To=P->GetActorLocation()-GetActorLocation();
    if(!bBoss) Lab::Visual(this,Clips,Phase==1 && Kind!=1?2:Phase==2?1:GetVelocity().SizeSquared2D()>100?1:0,VisualState,Phase==1 && Kind!=1?(StrikeNumber==1?.6f:.75f):0);
    Lab::AlignBodyForward(GetMesh());
    if(T<DamageTextUntil) DrawDebugString(GetWorld(),DamageTextOrigin+FVector(0,0,130+(1-(DamageTextUntil-T)/.65)*65),FString::Printf(TEXT("-%.0f"),LastDamage),nullptr,FColor(255,220,100),0,true,1.3f);
    FColor C=Kind==2?FColor::Purple:Kind==1?FColor::Orange:FColor::Red;
    if(bBoss || Kind==2 || HP<MaxHP || P->Locked==this)
        DrawDebugString(GetWorld(),GetActorLocation()+FVector(0,0,Kind==2?150:110),FString::Printf(TEXT("%s %.0f/%.0f"),bBoss?TEXT("심연의 수문장"):Kind==2?TEXT("망령 지휘관"):Kind==1?TEXT("폭주 망령"):TEXT("망령 검병"),HP,MaxHP),nullptr,C,0,true);
    if(FParse::Param(FCommandLine::Get(),TEXT("AscensionCollisionView"))) DrawDebugBox(GetWorld(),GetActorLocation(),Kind==2?FVector(45,45,90):FVector(30,40,70),GetActorQuat(),C,false,-1,0,3);
    if(bFrozenForView) { ConsumeMovementInputVector(); GetCharacterMovement()->StopMovementImmediately(); return; }
    if(bBoss) { TickBoss(Delta,P); Lab::AlignBodyForward(GetMesh()); return; }
    if(T<StaggerUntil)
    {
        ConsumeMovementInputVector(); GetCharacterMovement()->StopMovementImmediately();
        const float Step=FMath::Min(KnockbackRemaining,KnockbackRemaining*Delta/FMath::Max(Delta,float(StaggerUntil-T)+Delta));
        FHitResult Hit; SetActorLocation(GetActorLocation()+KnockbackDirection*Step,true,&Hit);
        KnockbackRemaining=Hit.bBlockingHit?0:KnockbackRemaining-Step; return;
    }
    KnockbackRemaining=0;
    const AAscensionMode* Mode=Cast<AAscensionMode>(GetWorld()->GetAuthGameMode());
    const bool Pursue=Mode && Mode->bSurvival;
    const auto Chase=[&]()
    {
        FVector Desired=To.GetSafeNormal2D(); FCollisionQueryParams Params; Params.AddIgnoredActor(this);
        FHitResult Obstacle;
        for(float Angle:{0.f,45.f,-45.f,90.f,-90.f,135.f,-135.f})
        {
            const FVector Candidate=Desired.RotateAngleAxis(Angle,FVector::UpVector);
            if(!GetWorld()->SweepSingleByChannel(Obstacle,GetActorLocation(),GetActorLocation()+Candidate*110,FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeCapsule(30,65),Params))
            { AddMovementInput(Candidate); SetActorRotation(Candidate.Rotation()); break; }
        }
    };
    if(Phase==0)
    {
        if(!Pursue && To.Size2D()>700 && T>=AlertUntil) return;
        if(To.Size2D()<(Kind==1?(Pursue?360:650):155) && Lab::Clear(this,P)) BeginTell();
        else if(Pursue) Chase();
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
    else { if(Pursue && Kind!=2 && To.Size2D()>170) Chase(); if(T>=Deadline) Phase=0; }
}
void ACLEnemy::ReceiveHit(float Damage,FVector PushDirection)
{
    if(HP<=0) return; HP=FMath::Max(0.f,HP-Damage);
    LastDamage=Damage; DamageTextUntil=Lab::Now(this)+.65; DamageTextOrigin=GetActorLocation();
    if(HP>0 && !bBoss)
    {
        CustomTimeDilation=.18f;
        GetWorldTimerManager().SetTimer(ImpactPauseTimer,FTimerDelegate::CreateWeakLambda(this,[this]() { CustomTimeDilation=1.f; }),.045f,false);
        const double T=Lab::Now(this);
        if(T>=ReactionReady)
        {
            const float Duration=bBoss?.04f:Kind==2?.08f:.16f;
            StaggerUntil=T+Duration; ReactionReady=T+.35;
            if(PushDirection.IsNearlyZero()) if(ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0))) PushDirection=GetActorLocation()-P->GetActorLocation();
            KnockbackDirection=PushDirection.GetSafeNormal2D();
            KnockbackRemaining=bBoss?5.f:Kind==2?15.f:FMath::Clamp(Damage*1.5f,25.f,65.f);
            ConsumeMovementInputVector(); GetCharacterMovement()->StopMovementImmediately();
            if(Kind!=2) { Phase=3; Deadline=StaggerUntil; }
            else Deadline+=Duration;
        }
    }
    UE_LOG(LogTemp,Display,TEXT("ASCENSION_HIT kind=%d damage=%.0f hp=%.0f"),Kind,Damage,HP);
    if(HP==0)
    {
        if(AAscensionMode* Mode=Cast<AAscensionMode>(GetWorld()->GetAuthGameMode()); Mode && Mode->bSurvival)
        {
            if(ACLExperience* Orb=GetWorld()->SpawnActor<ACLExperience>(GetActorLocation(),FRotator::ZeroRotator)) Orb->Value=bBoss?120:Kind==2?40:Kind==1?12:8;
            if(bBoss) { Mode->bBossDefeated=true; if(ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0))) P->Notice=TEXT("수문장 처치! 중앙의 초록 출구에서 E: 다음 층"); }
        }
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
    // Equipment drops are temporarily hidden while the combat demo is refined.
    SetActorHiddenInGame(true);
}
void ACLDrop::Tick(float Delta)
{
    Super::Tick(Delta);
    if(IsHidden()) return;
    DrawDebugBox(GetWorld(),GetActorLocation(),FVector(27,22,22),FColor::Yellow,false,-1,0,3);
    DrawDebugString(GetWorld(),GetActorLocation()+FVector(0,0,50),FString::Printf(TEXT("장비 가방 / E / %d개"),Items.Num()),nullptr,FColor::Yellow,0,true);
}
ACLBladeWave::ACLBladeWave()
{
    PrimaryActorTick.bCanEverTick=true; InitialLifeSpan=1.2f;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("WaveRoot")));
}
void ACLBladeWave::Tick(float Delta)
{
    Super::Tick(Delta); if(!Hero.IsValid() || Hero->HP<=0) { Destroy(); return; }
    const FVector Start=GetActorLocation(); FVector End=Start+Direction*Speed*Delta;
    FHitResult Wall; FCollisionQueryParams Params; Params.AddIgnoredActor(this); Params.AddIgnoredActor(Hero.Get());
    const FCollisionObjectQueryParams StaticObjects(ECC_WorldStatic);
    const bool Blocked=GetWorld()->SweepSingleByObjectType(Wall,Start,End,FQuat::Identity,StaticObjects,FCollisionShape::MakeSphere(15),Params);
    if(Blocked) End=Wall.Location;
    struct FWaveContact { ACLEnemy* Enemy; float Entry; };
    TArray<FWaveContact> Contacts;
    const FVector Travel=(End-Start).GetSafeNormal(); const float Length=FVector::Dist(Start,End);
    for(TActorIterator<ACLEnemy> It(GetWorld());It;++It)
    {
        if(It->HP<=0 || HitEnemies.Contains(TWeakObjectPtr<ACLEnemy>(*It))) continue;
        const FVector Nearest=FMath::ClosestPointOnSegment(It->GetActorLocation(),Start,End);
        FHitResult Occluder;
        if(FVector::DistSquared(Nearest,It->GetActorLocation())<=FMath::Square(Radius+32) && !GetWorld()->LineTraceSingleByObjectType(Occluder,Start,It->GetActorLocation(),StaticObjects,Params))
        {
            const FVector Offset=It->GetActorLocation()-Start;
            const float Along=FVector::DotProduct(Offset,Travel);
            const float SideSquared=FMath::Max(0.f,Offset.SizeSquared()-Along*Along);
            const float Entry=FMath::Clamp(Along-FMath::Sqrt(FMath::Max(0.f,FMath::Square(Radius+32)-SideSquared)),0.f,Length);
            Contacts.Add({*It,Entry});
        }
    }
    Contacts.Sort([](const FWaveContact& A,const FWaveContact& B) { return A.Entry==B.Entry?A.Enemy->GetUniqueID()<B.Enemy->GetUniqueID():A.Entry<B.Entry; });
    for(const FWaveContact& Contact:Contacts)
    {
        ACLEnemy* Enemy=Contact.Enemy; if(!IsValid(Enemy) || Enemy->HP<=0) continue;
        HitEnemies.Add(Enemy); Enemy->ReceiveHit(Damage,Direction); Hero->ImpactFeedback(3,Enemy->GetActorLocation());
        if(--Pierces<=0) { Destroy(); return; }
    }
    SetActorLocation(End);
    const FColor Color=Radius>60?FColor(255,200,70):FColor::Cyan;
    for(int32 I=0;I<12;++I)
    {
        const FVector A=End+Direction.RotateAngleAxis(-65+130.f*I/12,FVector::UpVector)*Radius;
        const FVector B=End+Direction.RotateAngleAxis(-65+130.f*(I+1)/12,FVector::UpVector)*Radius;
        DrawDebugLine(GetWorld(),A,B,Color,false,.05f,0,7+Charge*6);
        if(Radius>60)
        {
            const FVector InnerA=End+(A-End)*.82f,InnerB=End+(B-End)*.82f;
            DrawDebugLine(GetWorld(),InnerA,InnerB,FColor(255,245,210),false,.05f,0,3+Charge*4);
        }
    }
    if(Blocked) Destroy();
}
ACLExperience::ACLExperience()
{
    PrimaryActorTick.bCanEverTick=true; InitialLifeSpan=180;
    UStaticMeshComponent* Mesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ExperienceOrb")); SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Mesh->SetStaticMesh(Sphere.Object); Mesh->SetRelativeScale3D(FVector(.14f)); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void ACLExperience::Tick(float Delta)
{
    Super::Tick(Delta); ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0)); if(!P || P->HP<=0) return;
    const float Distance=FVector::Dist2D(GetActorLocation(),P->GetActorLocation());
    DrawDebugSphere(GetWorld(),GetActorLocation(),10,8,FColor::Cyan,false,-1,0,2);
    if(Distance<90 && Lab::Clear(this,P)) { const int32 Amount=Value; Value=0; SetActorTickEnabled(false); Destroy(); P->GainExperience(Amount); }
    else if(Distance<350 && Lab::Clear(this,P)) SetActorLocation(FMath::VInterpTo(GetActorLocation(),P->GetActorLocation(),Delta,8));
}
AAscensionMode::AAscensionMode() { PrimaryActorTick.bCanEverTick=true; DefaultPawnClass=ACLHero::StaticClass(); HUDClass=ACLHUD::StaticClass(); }
void AAscensionMode::BeginPlay()
{
    Super::BeginPlay(); bVerify=FParse::Param(FCommandLine::Get(),TEXT("AscensionVerify")); bView=FParse::Param(FCommandLine::Get(),TEXT("AscensionView")) || FParse::Param(FCommandLine::Get(),TEXT("AscensionMotionView")) || FParse::Param(FCommandLine::Get(),TEXT("AscensionCombatView")) || FParse::Param(FCommandLine::Get(),TEXT("AscensionCombatFeelView")); bSpawnVerify=FParse::Param(FCommandLine::Get(),TEXT("AscensionSpawnVerify"));
    bSurvival=(!bVerify && !bView && !bSpawnVerify) || FParse::Param(FCommandLine::Get(),TEXT("AscensionSurvivalVerify")) || FParse::Param(FCommandLine::Get(),TEXT("AscensionCombatVerify")) || FParse::Param(FCommandLine::Get(),TEXT("AscensionCombatFeelView")) || FParse::Param(FCommandLine::Get(),TEXT("AscensionSurvivalView")) || FParse::Param(FCommandLine::Get(),TEXT("AscensionWaveView"));
    if(FParse::Param(FCommandLine::Get(),TEXT("AscensionHordeView")) || FParse::Param(FCommandLine::Get(),TEXT("AscensionFloorView"))) bView=bSurvival=true;
    if(FParse::Param(FCommandLine::Get(),TEXT("AscensionBossVerify"))) bSurvival=true;
    if(FParse::Param(FCommandLine::Get(),TEXT("AscensionBossView"))) bView=bSurvival=true;
    FString ServerAddress;
    bOnline=!bVerify && !bView && !bSpawnVerify && FParse::Value(FCommandLine::Get(),TEXT("AscensionServer="),ServerAddress);
    ResetSurvival(); SpawnEncounter(); NextCheck=Lab::Now(this)+1;
    if(bSurvival) if(ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0))) P->Notice=TEXT("사냥하며 강화 → 중앙 제단 E로 수문장 소환 → 처치 후 중앙 E로 다음 층");
    if(bOnline)
    {
        // 이름을 안 주면 "컴퓨터이름-프로세스번호": 한 PC에서 클라이언트 여러 개를 띄워도 구분된다
        FString Name=FString::Printf(TEXT("%s-%u"),FPlatformProcess::ComputerName(),FPlatformProcess::GetCurrentProcessId()); FParse::Value(FCommandLine::Get(),TEXT("AscensionName="),Name);
        uint32 Team=1; FParse::Value(FCommandLine::Get(),TEXT("AscensionTeam="),Team);
        Net=GetWorld()->SpawnActor<ACLNetSession>();
        if(Net) Net->Connect(ServerAddress,Name,Team);
        if(ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0))) P->Notice=TEXT("온라인 P0: 같은 팀 코드로 접속한 플레이어와 함께 이동합니다. 몬스터는 서버 이전 후 추가됩니다.");
    }
}
void AAscensionMode::Check(bool Condition,const TCHAR* Name)
{
    if(!Condition) bFailed=true;
    UE_LOG(LogTemp,Display,TEXT("ASCENSION_CHECK %s %s"),Name,Condition?TEXT("PASS"):TEXT("FAIL"));
}
void AAscensionMode::Tick(float Delta)
{
    Super::Tick(Delta);
    if(bSurvival && !bVerify && !bView && !bOnline) TickSurvival();
    if(bView && FParse::Param(FCommandLine::Get(),TEXT("AscensionFloorView"))) TickSurvival();
    if(!bSurvival && !bVerify && !bView && !bSpawnVerify && !bEncounterCleared && SpawnedEnemies>0 && RemainingEnemies()==0)
    {
        bEncounterCleared=true;
        if(ACLHero* Hero=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0)); Hero && Hero->HP>0)
        { Hero->HP=FMath::Min(100.f,Hero->HP+20); if(Hero->Offers.IsEmpty()) Hero->Notice=TEXT("전투 완료! 체력 +20 / N: 다음 전투"); }
    }
    if(!bRestArea)
    {
        DrawDebugCircle(GetWorld(),FVector(650,650,8),100,32,FColor::Yellow,false,-1,0,4,FVector::ForwardVector,FVector::RightVector,false);
        DrawDebugString(GetWorld(),FVector(650,650,90),TEXT("각성 제단 / E / 생명당 3회"),nullptr,FColor::Yellow,0,true);
    }
    if(bVerify && VerifyStage==4 && !FParse::Param(FCommandLine::Get(),TEXT("AscensionCombatVerify")))
        if(ACLHero* Mover=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0))) Mover->AddMovementInput(FVector(-1,0,0),1);
    if(bVerify && VerifyStage==99) if(ACLHero* Mover=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0))) Mover->Forward(1);
    if(bVerify && VerifyStage==4 && FParse::Param(FCommandLine::Get(),TEXT("AscensionCombatVerify"))) if(ACLHero* Mover=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0))) Mover->Forward(1);
    if(bView && (VerifyStage==2 || VerifyStage==9) && FParse::Param(FCommandLine::Get(),TEXT("AscensionCombatFeelView"))) if(ACLHero* Mover=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0))) Mover->Right(1);
    if(bView && VerifyStage==9 && FParse::Param(FCommandLine::Get(),TEXT("AscensionCombatFeelView"))) ++MotionFrames;
    if(bView && VerifyStage==3 && FParse::Param(FCommandLine::Get(),TEXT("AscensionWaveView"))) if(ACLHero* Mover=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0))) Mover->Right(1);
    const bool MotionView=FParse::Param(FCommandLine::Get(),TEXT("AscensionMotionView"));
    if(bView && MotionView && VerifyStage==3)
        if(ACLHero* Mover=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0))) Mover->Right(1);
    if((!bVerify && !bView && !bSpawnVerify) || Lab::Now(this)<NextCheck) return;
    ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0));
    if(!P) { Check(false,TEXT("player_spawn")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    double T=Lab::Now(this);
    if(bVerify && FParse::Param(FCommandLine::Get(),TEXT("AscensionBossVerify")))
    {
        if(VerifyStage==0)
        {
        for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) It->Destroy();
        P->DisableInput(Cast<APlayerController>(P->Controller));
        P->LightningReady=P->BladeReady=T+1000; P->Armor=Lab::Item(1,0);
        ACLEnemy* Boss=GetWorld()->SpawnActor<ACLEnemy>(FVector(0,0,100),FRotator::ZeroRotator);
        if(!Boss) { Check(false,TEXT("boss_fixture_spawn")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
        Boss->Configure(2); Boss->bBoss=true; Boss->HP=Boss->MaxHP=300; Boss->SetActorTickEnabled(false);
        const auto Place=[&](FVector PlayerAt,FVector BossAt=FVector(0,0,100))
        {
            P->HP=100; P->ImmuneEnd=0; P->SetActorLocation(PlayerAt); P->GetCharacterMovement()->StopMovementImmediately();
            Boss->SetActorLocation(BossAt); Boss->GetCharacterMovement()->StopMovementImmediately(); Boss->Phase=0;
        };
        Place(FVector(220,0,100)); Boss->BeginBossAttack(0); const double TellEnd=Boss->Deadline;
        Boss->Tick(.1f); Boss->ReceiveHit(5);
        Check(P->HP==100 && Boss->Phase==1 && Boss->Deadline==TellEnd && Boss->CustomTimeDilation==1,TEXT("boss_tell_deals_no_early_damage_and_hits_cannot_delay_it"));
        P->SetActorLocation(FVector(0,220,100)); Boss->Deadline=T-.01; Boss->Tick(.01f);
        Check(P->HP==100 && Boss->Phase==3 && Boss->BossAim.Equals(FVector::ForwardVector),TEXT("cleave_locks_direction_and_side_step_avoids_damage"));
        Place(FVector(220,0,100)); Boss->BeginBossAttack(0); Boss->ResolveBossAttack(); Boss->ResolveBossAttack();
        Check(P->HP==80 && Boss->Phase==3 && Boss->Deadline>T+1.5,TEXT("cleave_hits_once_and_opens_counterattack_window"));
        Place(FVector(220,0,100)); Boss->BeginBossAttack(0); P->SetActorLocation(FVector(-150,0,100)); Boss->ResolveBossAttack();
        Check(P->HP==100,TEXT("cleave_rear_is_safe"));
        Place(FVector(400,0,100)); Boss->BeginBossAttack(2); Boss->ResolveBossAttack();
        Check(P->HP==72 && Boss->Phase==3,TEXT("slam_hits_inside_420cm"));
        Place(FVector(430,0,100)); Boss->BeginBossAttack(2); Boss->ResolveBossAttack();
        Check(P->HP==100,TEXT("slam_outside_ring_is_safe"));
        Place(FVector(300,0,100)); Boss->BeginBossAttack(2); P->ImmuneEnd=T+1; Boss->ResolveBossAttack();
        Check(P->HP==100 && P->PerfectUntil>T,TEXT("boss_slam_respects_dodge_invulnerability"));
        Place(FVector(600,0,100)); Boss->BeginBossAttack(1); P->SetActorLocation(FVector(0,400,100)); Boss->ResolveBossAttack();
        Boss->Tick(.2f); const float Travel=Boss->GetActorLocation().X; Boss->Tick(1.f);
        Check(P->HP==100 && Travel>250 && Boss->GetActorLocation().X<=901 && FMath::Abs(Boss->GetActorLocation().Y)<1 && Boss->Phase==3,TEXT("charge_commits_to_lane_caps_distance_and_recovers"));
        Place(FVector(400,0,100)); Boss->BeginBossAttack(1); Boss->ResolveBossAttack(); Boss->Tick(.4f); Boss->Tick(.1f);
        Check(P->HP==76,TEXT("charge_sweeps_player_during_large_frame_and_hits_once"));
        Place(FVector(ArenaHalfWidth-100,500,100),FVector(ArenaHalfWidth-100,0,100));
        Boss->BeginBossAttack(1); Boss->BossAim=FVector::ForwardVector; Boss->ResolveBossAttack(); Boss->Tick(.4f);
        Check(Boss->GetActorLocation().X<=ArenaHalfWidth-34 && Boss->Phase==3 && P->HP==100,TEXT("boss_charge_stops_at_arena_wall"));
        Place(FVector(220,0,100)); Boss->HP=150; Boss->Phase=3; Boss->Deadline=T+10; Boss->Tick(.01f);
        Boss->BeginBossAttack(0); const double EnragedTell=Boss->Deadline-T; Boss->ResolveBossAttack();
        Check(Boss->bEnraged && EnragedTell>=.85 && EnragedTell<1.05 && Boss->Deadline>T+1.1 && Boss->Deadline<T+1.2,TEXT("half_health_enrage_shortens_future_tells_and_recovery"));
        Place(FVector(0,600,100)); Boss->BossAttackCount=1; Boss->Tick(.01f);
        Check(Boss->Phase==1 && Boss->BossAttack==1,TEXT("boss_selects_charge_after_cleave"));
        Place(FVector(400,0,100)); Boss->BossAttackCount=2; Boss->Tick(.01f);
        Check(Boss->Phase==1 && Boss->BossAttack==2,TEXT("boss_selects_slam_after_charge"));
        Place(FVector(900,0,100)); Boss->BossAttackCount=0; Boss->Tick(.01f);
        Check(Boss->Phase==1 && Boss->BossAttack==1,TEXT("boss_uses_charge_against_ranged_kiting"));
        bBossSpawned=true; Boss->ReceiveHit(10000);
        Check(bBossDefeated && Boss->IsActorBeingDestroyed(),TEXT("pattern_boss_death_still_unlocks_floor_exit"));
        P->Skip();
        // Also exercise the normal frame loop, without forcing a deadline or attack.
        P->HP=100; P->ImmuneEnd=0; P->SetActorLocation(FVector(900,0,100));
        VerifyEnemy=GetWorld()->SpawnActor<ACLEnemy>(FVector(0,0,100),FRotator::ZeroRotator);
        if(!VerifyEnemy) { Check(false,TEXT("timed_boss_spawn")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
        VerifyEnemy->Configure(2); VerifyEnemy->bBoss=true; VerifyEnemy->HP=VerifyEnemy->MaxHP=300;
        VerifyStage=1; NextCheck=T+.3; return;
        }
        if(VerifyStage==1)
        {
            Check(VerifyEnemy->Phase==1 && VerifyEnemy->BossAttack==1 && P->HP==100,TEXT("live_boss_waits_in_telegraph_before_charge"));
            P->SetActorLocation(FVector(0,450,100)); VerifyStage=2; NextCheck=T+2; return;
        }
        Check(VerifyEnemy->Phase==3 && VerifyEnemy->GetActorLocation().X>650 && VerifyEnemy->GetActorLocation().X<=910 && FMath::Abs(VerifyEnemy->GetActorLocation().Y)<20 && P->HP==100,TEXT("live_boss_charge_commits_after_tell_then_enters_recovery"));
        UE_LOG(LogTemp,Display,TEXT("ASCENSION_BOSS_VERIFY %s"),bFailed?TEXT("FAIL"):TEXT("PASS"));
        FPlatformMisc::RequestExitWithStatus(false,bFailed?1:0); return;
    }
    if(bView && FParse::Param(FCommandLine::Get(),TEXT("AscensionBossView")))
    {
        if(VerifyStage==0)
        {
            for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) It->Destroy();
            P->DisableInput(Cast<APlayerController>(P->Controller)); P->SetActorLocation(FVector(-500,-450,100));
            P->Controller->SetControlRotation(FRotator(-40,45,0)); P->Arm->TargetArmLength=1100;
            P->LightningReady=P->BladeReady=T+1000; P->ImmuneEnd=T+1000;
            VerifyEnemy=GetWorld()->SpawnActor<ACLEnemy>(FVector(0,0,100),FRotator::ZeroRotator);
            if(!VerifyEnemy) { Check(false,TEXT("boss_view_spawn")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
            VerifyEnemy->Configure(2); VerifyEnemy->bBoss=true; VerifyEnemy->HP=VerifyEnemy->MaxHP=300;
            bBossSpawned=true; VerifyEnemy->BeginBossAttack(0); VerifyStage=1; NextCheck=T+.45;
        }
        else if(VerifyStage==1 || VerifyStage==3 || VerifyStage==5)
        {
            const TCHAR* Name=VerifyStage==1?TEXT("BossCleave.png"):VerifyStage==3?TEXT("BossCharge.png"):TEXT("BossSlamEnraged.png");
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/Name,true,false); ++VerifyStage; NextCheck=T+.2;
        }
        else if(VerifyStage==2 || VerifyStage==4)
        {
            VerifyEnemy->SetActorLocation(FVector(0,0,100));
            if(VerifyStage==4) { VerifyEnemy->HP=140; VerifyEnemy->bEnraged=true; }
            VerifyEnemy->BeginBossAttack(VerifyStage==2?1:2); ++VerifyStage; NextCheck=T+.55;
        }
        else { UE_LOG(LogTemp,Display,TEXT("ASCENSION_BOSS_VIEW %s"),bFailed?TEXT("FAIL"):TEXT("PASS")); FPlatformMisc::RequestExitWithStatus(false,bFailed?1:0); }
        return;
    }
    if(bView && FParse::Param(FCommandLine::Get(),TEXT("AscensionFloorView")))
    {
        if(VerifyStage==0)
        {
            for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) It->Destroy();
            P->DisableInput(Cast<APlayerController>(P->Controller)); P->SetActorLocation(FVector(0,-250,100));
            P->Arm->TargetArmLength=1000; P->Controller->SetControlRotation(FRotator(-30,90,0));
            P->LightningReady=P->BladeReady=T+1000; P->UseAltar();
            for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) It->bFrozenForView=true;
            Check(bBossSpawned && !bBossDefeated,TEXT("render_gatekeeper_summoned")); VerifyStage=1; NextCheck=T+1;
        }
        else if(VerifyStage==1)
        { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("GatekeeperFight.png"),true,false); VerifyStage=2; NextCheck=T+.6; }
        else if(VerifyStage==2)
        {
            for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) if(It->bBoss) It->ReceiveHit(10000);
            for(TActorIterator<ACLExperience> It(GetWorld());It;++It) It->Destroy(); P->Skip();
            VerifyStage=3; NextCheck=T+.4;
        }
        else if(VerifyStage==3)
        { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("FloorExitOpen.png"),true,false); VerifyStage=4; NextCheck=T+.6; }
        else if(VerifyStage==4)
        {
            bView=false; P->UseAltar(); bView=true; ReinforcementReady=RefillReady=EliteReady=T+1000;
            for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) It->bFrozenForView=true;
            P->Arm->TargetArmLength=650; P->Controller->SetControlRotation(FRotator(-30,0,0));
            Check(CurrentFloor==2 && RemainingEnemies()==14,TEXT("render_second_floor")); VerifyStage=5; NextCheck=T+.7;
        }
        else if(VerifyStage==5)
        { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("SecondFloor.png"),true,false); VerifyStage=6; NextCheck=T+.6; }
        else if(VerifyStage==6)
        {
            P->SetActorLocation(FVector(0,-250,100)); P->UseAltar();
            for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) if(It->bBoss) It->ReceiveHit(10000);
            for(TActorIterator<ACLExperience> It(GetWorld());It;++It) It->Destroy(); P->Skip(); P->UseAltar();
            Check(bRestArea && CurrentFloor==3 && RemainingEnemies()==0,TEXT("render_rest_area")); VerifyStage=7; NextCheck=T+.7;
        }
        else if(VerifyStage==7)
        { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("RestArea.png"),true,false); VerifyStage=8; NextCheck=T+.5; }
        else
        { UE_LOG(LogTemp,Display,TEXT("ASCENSION_FLOOR_VIEW %s"),bFailed?TEXT("FAIL"):TEXT("PASS")); FPlatformMisc::RequestExitWithStatus(false,bFailed?1:0); }
        return;
    }
    if(bView && FParse::Param(FCommandLine::Get(),TEXT("AscensionHordeView")))
    {
        if(VerifyStage==0)
        {
            P->DisableInput(Cast<APlayerController>(P->Controller)); P->SetActorLocation(FVector(0,0,100));
            P->LightningReady=P->BladeReady=T+1000; bView=false; SpawnEncounter(20261011); bView=true;
            SpawnSurvivalBatch(36); Check(RemainingEnemies()==48,TEXT("render_horde_spawns_48"));
            P->Arm->bDoCollisionTest=false; P->Arm->TargetArmLength=4500; P->Controller->SetControlRotation(FRotator(-65,0,0));
            VerifyStage=1; NextCheck=T+2.5;
        }
        else if(VerifyStage==1)
        {
            int32 Moving=0;
            for(TActorIterator<ACLEnemy> It(GetWorld());It;++It)
            { Moving+=It->GetVelocity().Size2D()>10; It->bFrozenForView=true; It->GetCharacterMovement()->StopMovementImmediately(); }
            Check(RemainingEnemies()==48 && Moving>=30,TEXT("render_horde_actual_pursuit"));
            UE_LOG(LogTemp,Display,TEXT("ASCENSION_HORDE_RENDER moving=%d count=%d"),Moving,RemainingEnemies());
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("ExpandedArena.png"),true,false); VerifyStage=2; NextCheck=T+1;
        }
        else if(VerifyStage==2)
        {
            P->Arm->TargetArmLength=650; P->Controller->SetControlRotation(FRotator(-30,0,0)); VerifyStage=3; NextCheck=T+1;
        }
        else if(VerifyStage==3)
        { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("HordeCombat.png"),true,false); VerifyStage=4; NextCheck=T+1; }
        else
        { UE_LOG(LogTemp,Display,TEXT("ASCENSION_HORDE_VIEW %s"),bFailed?TEXT("FAIL"):TEXT("PASS")); FPlatformMisc::RequestExitWithStatus(false,bFailed?1:0); }
        return;
    }
    if(bVerify && FParse::Param(FCommandLine::Get(),TEXT("AscensionCombatVerify")))
    {
        if(VerifyStage==0)
        {
            for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) It->Destroy();
            P->DisableInput(Cast<APlayerController>(P->Controller)); P->SetActorLocation(FVector(0,0,100)); P->Controller->SetControlRotation(FRotator(-30,0,0));
            P->LightningReady=P->BladeReady=T+1000;
            VerifyEnemy=GetWorld()->SpawnActor<ACLEnemy>(FVector(900,0,100),FRotator::ZeroRotator);
            Check(IsValid(VerifyEnemy),TEXT("combat_test_enemy_spawn"));
            if(!VerifyEnemy) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
            VerifyEnemyPosition=VerifyEnemy->GetActorLocation(); VerifyStage=1; NextCheck=T+.8;
        }
        else if(VerifyStage==1)
        {
            UE_LOG(LogTemp,Display,TEXT("ASCENSION_CHASE displacement=%.1f speed=%.1f"),VerifyEnemyPosition.X-VerifyEnemy->GetActorLocation().X,VerifyEnemy->GetVelocity().Size2D());
            Check(VerifyEnemy->GetActorLocation().X<VerifyEnemyPosition.X-60 && VerifyEnemy->GetVelocity().Size2D()>100,TEXT("unpossessed_monster_actually_chases_from_900cm"));
            VerifyEnemyPosition=VerifyEnemy->GetActorLocation(); VerifyEnemy->ReceiveHit(10,FVector::ForwardVector);
            const double Reaction=VerifyEnemy->StaggerUntil; VerifyEnemy->ReceiveHit(1,FVector::ForwardVector);
            Check(VerifyEnemy->StaggerUntil==Reaction,TEXT("repeated_hits_do_not_extend_stagger"));
            VerifyStage=2; NextCheck=T+.1;
        }
        else if(VerifyStage==2)
        {
            Check(VerifyEnemy->GetActorLocation().X>VerifyEnemyPosition.X+2 && VerifyEnemy->GetVelocity().Size2D()<1 && T<VerifyEnemy->StaggerUntil,TEXT("hit_staggers_and_moves_enemy_backward"));
            VerifyStage=3; NextCheck=T+.65;
        }
        else if(VerifyStage==3)
        {
            Check(VerifyEnemy->GetActorLocation().X<VerifyEnemyPosition.X-30 && VerifyEnemy->GetVelocity().Size2D()>100,TEXT("monster_resumes_chase_after_reaction"));
            VerifyEnemy->bFrozenForView=true; VerifyEnemy->SetActorLocation(FVector(0,600,100));
            P->ConsumeMovementInputVector(); P->GetCharacterMovement()->StopMovementImmediately(); VerifyPosition=P->GetActorLocation();
            P->Attack(); VerifyStage=4; NextCheck=T+.18;
        }
        else if(VerifyStage==4)
        {
            Check(P->VisualState==2 && P->GetActorLocation().X>VerifyPosition.X+2 && P->GetVelocity().Size2D()>10,TEXT("moving_shot_plays_slash_without_stopping"));
            const double Started=P->AttackStart; P->Attack();
            Check(P->AttackStart==Started && FMath::IsNearlyEqual(float(P->AttackEnd-P->AttackStart),.72f),TEXT("normal_shot_has_720ms_cooldown"));
            P->ConsumeMovementInputVector(); P->GetCharacterMovement()->StopMovementImmediately(); VerifyStage=5; NextCheck=T+.6;
        }
        else if(VerifyStage==5)
        {
            int32 Waves=0; for(TActorIterator<ACLBladeWave> It(GetWorld());It;++It) { ++Waves; It->Destroy(); }
            Check(P->bResolved && Waves==1,TEXT("slash_fires_one_delayed_blade_wave"));
            // Spawn the far enemy first so actor iteration order cannot mask
            // an incorrect single-target collision order.
            ACLEnemy* Far=GetWorld()->SpawnActor<ACLEnemy>(P->GetActorLocation()+FVector(340,0,0),FRotator::ZeroRotator);
            ACLEnemy* Near=GetWorld()->SpawnActor<ACLEnemy>(P->GetActorLocation()+FVector(130,0,0),FRotator::ZeroRotator);
            ACLEnemy* Middle=GetWorld()->SpawnActor<ACLEnemy>(P->GetActorLocation()+FVector(250,0,0),FRotator::ZeroRotator);
            if(!Far || !Near || !Middle) { Check(false,TEXT("piercing_fixture_spawn")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
            for(ACLEnemy* Enemy:{Far,Near,Middle}) { Enemy->bFrozenForView=true; Enemy->HP=500; }
            ACLBladeWave* Single=GetWorld()->SpawnActor<ACLBladeWave>(P->GetActorLocation(),FRotator::ZeroRotator); Single->Hero=P; Single->Tick(.35f);
            Check(Single->IsActorBeingDestroyed() && Single->HitEnemies.Num()==1 && Near->HP==480 && Middle->HP==500 && Far->HP==500,TEXT("base_wave_hits_only_first_enemy_in_path"));
            Near->HP=500;
            const FGuid Gear=P->Weapon.ID; const int32 OldLevel=P->Level,OldExperience=P->Experience;
            P->GainExperience(P->ExperienceRequired()); P->Choose(2);
            Check(P->PowerRank==1 && P->NormalWaveTargets()==2 && !UGameplayStatics::IsGamePaused(this) && P->Weapon.ID==Gear,TEXT("levelup_unlocks_second_normal_target"));
            P->ResolveSwing(); ACLBladeWave* Upgraded=nullptr; for(TActorIterator<ACLBladeWave> It(GetWorld());It;++It) Upgraded=*It;
            if(Upgraded) { Upgraded->Tick(.35f); Check(Upgraded->IsActorBeingDestroyed() && Upgraded->HitEnemies.Num()==2 && Near->HP==477 && Middle->HP==477 && Far->HP==500,TEXT("upgraded_wave_hits_two_targets_then_stops")); }
            else Check(false,TEXT("upgraded_wave_spawn"));
            for(ACLEnemy* Enemy:{Far,Near,Middle}) Enemy->Destroy();
            P->PowerRank=0; P->Level=OldLevel; P->Experience=OldExperience;
            P->Heavy(); VerifyStage=6; NextCheck=T+.6;
        }
        else if(VerifyStage==6)
        {
            int32 Waves=0; for(TActorIterator<ACLBladeWave> It(GetWorld());It;++It) ++Waves;
            Check(P->IsCharging() && P->ChargeProgress()>.15f && P->ChargeProgress()<.3f && P->VisualState==3 && Waves==0,TEXT("heavy_holds_windup_without_firing"));
            const double Started=P->AttackStart; P->Attack(); Check(P->AttackStart==Started,TEXT("normal_attack_cannot_interrupt_charge"));
            VerifyStage=7; NextCheck=T+2.5;
        }
        else if(VerifyStage==7)
        {
            Check(P->ChargeProgress()==1.f,TEXT("charge_caps_at_3000ms"));
            P->Controller->SetControlRotation(FRotator(-30,90,0)); P->ReleaseHeavy();
            Check(!P->IsCharging() && !P->bResolved && FVector::DotProduct(P->SwingDirection,FVector::RightVector)>.99f,TEXT("heavy_release_uses_current_camera_aim"));
            VerifyStage=8; NextCheck=T+.4;
        }
        else if(VerifyStage==8)
        {
            int32 Waves=0; bool Charged=false;
            for(TActorIterator<ACLBladeWave> It(GetWorld());It;++It) { ++Waves; Charged=It->Damage==105 && It->Radius==230 && It->Pierces==10; It->Destroy(); }
            Check(P->bResolved && Waves==1 && Charged,TEXT("full_charge_fires_single_105_damage_230_radius_wave"));
            VerifyStage=9; NextCheck=T+.6;
        }
        else if(VerifyStage==9)
        {
            P->Heavy(); Check(P->IsCharging(),TEXT("charge_can_restart_after_recovery"));
            P->Dodge(); P->ReleaseHeavy(); Check(!P->IsCharging() && P->bResolved,TEXT("dodge_cancels_charge_without_release_shot"));
            VerifyStage=10; NextCheck=T+.75;
        }
        else if(VerifyStage==10)
        {
            VerifyEnemy->bFrozenForView=false; VerifyEnemy->SetActorLocation(FVector(ArenaHalfWidth-40,0,100)); VerifyEnemy->HP=100; VerifyEnemy->ReactionReady=0;
            VerifyEnemy->ReceiveHit(45,FVector::ForwardVector); VerifyStage=11; NextCheck=T+.1;
        }
        else if(VerifyStage==11)
        {
            Check(VerifyEnemy->GetActorLocation().X>=ArenaHalfWidth-40 && VerifyEnemy->GetActorLocation().X<=ArenaHalfWidth-34.8f,TEXT("knockback_sweep_stops_at_wall"));
            VerifyEnemy->bFrozenForView=true; VerifyEnemy->Configure(2); VerifyEnemy->Phase=1; VerifyEnemy->ReactionReady=0; VerifyEnemy->ReceiveHit(10);
            Check(VerifyEnemy->Phase==1 && FMath::IsNearlyEqual(float(VerifyEnemy->StaggerUntil-T),.08f),TEXT("elite_has_short_reaction_preserves_attack"));
            VerifyEnemy->bBoss=true; VerifyEnemy->StaggerUntil=0; VerifyEnemy->ReactionReady=0;
            const double BossDeadline=VerifyEnemy->Deadline; VerifyEnemy->ReceiveHit(10);
            Check(VerifyEnemy->Phase==1 && VerifyEnemy->StaggerUntil==0 && VerifyEnemy->Deadline==BossDeadline,TEXT("boss_hits_preserve_telegraph_timing"));
            P->Heavy(); P->ToggleInventory(); P->ReleaseHeavy(); Check(!P->IsCharging() && P->bInventoryOpen && P->bResolved,TEXT("inventory_cancels_charge_without_shot")); P->ToggleInventory();
            P->Heavy(); P->GainExperience(25); P->ReleaseHeavy(); Check(!P->IsCharging() && P->bLevelChoice && P->bResolved,TEXT("levelup_cancels_charge_while_paused")); P->Choose(0);
            const double PreviousAttackStart=P->AttackStart,PreviousAttackEnd=P->AttackEnd;
            P->Heavy(); P->ReleaseHeavy();
            Check(!P->IsCharging() && P->bResolved && P->ReleasedCharge==0 && P->AttackStart==PreviousAttackStart && P->AttackEnd==PreviousAttackEnd,TEXT("instant_release_cancels_without_attack_or_recovery"));
            P->Heavy(); P->ChargeStart=T-.599; P->ReleaseHeavy();
            Check(!P->IsCharging() && P->bResolved && P->AttackStart==PreviousAttackStart && P->AttackEnd==PreviousAttackEnd,TEXT("release_below_minimum_cancels_without_cooldown"));
            P->Heavy(); P->ChargeStart=T-.601;
            Check(P->CanReleaseHeavy(),TEXT("minimum_charge_unlocks_release")); P->ReleaseHeavy();
            Check(!P->bResolved && P->AttackStart==T && FMath::IsNearlyEqual(float(P->AttackEnd-T),.85f),TEXT("minimum_charge_starts_existing_release_recovery"));
            VerifyStage=14; NextCheck=T+.4;
        }
        else if(VerifyStage==14)
        {
            int32 Waves=0; bool Minimum=false;
            for(TActorIterator<ACLBladeWave> It(GetWorld());It;++It)
            { ++Waves; Minimum=FMath::IsNearlyEqual(It->Damage,49.0233f,.05f) && FMath::IsNearlyEqual(It->Radius,102.0533f,.05f); It->Destroy(); }
            Check(Waves==1 && Minimum,TEXT("minimum_charge_fires_one_scaled_wave"));
            VerifyStage=15; NextCheck=T+.5;
        }
        else if(VerifyStage==15)
        {
            P->Heavy(); P->ChargeStart=T-1.5; P->ReleaseHeavy(); VerifyStage=13; NextCheck=T+.4;
        }
        else if(VerifyStage==13)
        {
            int32 Waves=0; bool Partial=false;
            for(TActorIterator<ACLBladeWave> It(GetWorld());It;++It)
            {
                ++Waves; Partial=FMath::IsNearlyEqual(It->Damage,70.f,.05f) && FMath::IsNearlyEqual(It->Radius,150.f,.05f);
                const FVector Sideways=FVector::CrossProduct(FVector::UpVector,It->Direction);
                ACLEnemy* Side=GetWorld()->SpawnActor<ACLEnemy>(It->GetActorLocation()+It->Direction*80+Sideways*190,FRotator::ZeroRotator);
                if(Side)
                {
                    Side->bFrozenForView=true; Side->HP=500;
                    const FVector At=It->GetActorLocation(); It->Tick(.15f); Check(Side->HP==500,TEXT("half_charge_does_not_hit_outside_its_width"));
                    ACLBladeWave* Wide=GetWorld()->SpawnActor<ACLBladeWave>(At,FRotator::ZeroRotator); Wide->Hero=P; Wide->Direction=It->Direction; Wide->Radius=230; Wide->Damage=105; Wide->Tick(.15f);
                    Check(Side->HP==395,TEXT("full_charge_width_hits_additional_side_enemy")); Wide->Destroy(); Side->Destroy();
                }
                else Check(false,TEXT("charge_width_fixture_spawn"));
                It->Destroy(); break;
            }
            Check(Waves==1 && Partial,TEXT("half_charge_has_intermediate_damage_and_width"));
            VerifyStage=12; NextCheck=T+.6;
        }
        else if(VerifyStage==12)
        {
            P->PowerRank=2; P->Heavy(); P->ReceiveHit(10000); P->ReleaseHeavy(); Check(!P->IsCharging() && P->HP==0 && P->PowerRank==0,TEXT("death_cancels_charge_and_piercing_growth"));
            P->ResetLife(); Check(!P->IsCharging() && P->HP==100 && P->ReleasedCharge==0 && P->NormalWaveTargets()==1,TEXT("reset_clears_charge_and_restores_single_target"));
            UE_LOG(LogTemp,Display,TEXT("ASCENSION_COMBAT_VERIFY %s"),bFailed?TEXT("FAIL"):TEXT("PASS")); FPlatformMisc::RequestExitWithStatus(false,bFailed?1:0);
        }
        return;
    }
    if(bView && FParse::Param(FCommandLine::Get(),TEXT("AscensionCombatFeelView")))
    {
        if(VerifyStage==0)
        {
            P->DisableInput(Cast<APlayerController>(P->Controller)); P->SetActorLocation(FVector(-750,0,100)); P->Controller->SetControlRotation(FRotator(-30,0,0));
            P->LightningReady=P->BladeReady=T+1000;
            for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) { It->bFrozenForView=true; It->GetCharacterMovement()->StopMovementImmediately(); }
            VerifyStage=1; NextCheck=T+1;
        }
        else if(VerifyStage==1) { P->Attack(); VerifyStage=2; NextCheck=T+.18; }
        else if(VerifyStage==2)
        {
            Check(P->VisualState==2 && P->GetVelocity().Size2D()>10,TEXT("render_moving_slash"));
            VerifyPosition=P->GetActorTransform().InverseTransformPosition(P->GetMesh()->GetSocketLocation(TEXT("RightFoot")));
            VerifyEnemyPosition=P->GetActorTransform().InverseTransformPosition(P->GetMesh()->GetSocketLocation(TEXT("RightHand")));
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("MovingSlashEarly.png"),true,false); VerifyStage=9; MotionFrames=0; NextCheck=T+.18;
        }
        else if(VerifyStage==9)
        {
            // A screenshot/shader hitch can advance world time in a single frame.
            // Compare poses after several evaluated frames, not adjacent buffers.
            if(MotionFrames<8) { NextCheck=T+.001; return; }
            const FVector Foot=P->GetActorTransform().InverseTransformPosition(P->GetMesh()->GetSocketLocation(TEXT("RightFoot")));
            const FVector Hand=P->GetActorTransform().InverseTransformPosition(P->GetMesh()->GetSocketLocation(TEXT("RightHand")));
            Check(Cast<UCLHeroMotion>(P->GetMesh()->GetAnimInstance()) && FVector::Dist(Foot,VerifyPosition)>3 && FVector::Dist(Hand,VerifyEnemyPosition)>3,TEXT("render_feet_and_sword_hand_animate_while_moving"));
            const FVector Side=P->GetMesh()->GetSocketLocation(TEXT("RightShoulder"))-P->GetMesh()->GetSocketLocation(TEXT("LeftShoulder"));
            const FVector Up=P->GetMesh()->GetSocketLocation(TEXT("Head"))-P->GetMesh()->GetSocketLocation(TEXT("Hips"));
            const float Dot=FVector::DotProduct(FVector::CrossProduct(Side,Up).GetSafeNormal2D(),P->GetActorForwardVector());
            UE_LOG(LogTemp,Display,TEXT("ASCENSION_BLENDED_MOTION foot_delta=%.1f hand_delta=%.1f facing_dot=%.3f"),FVector::Dist(Foot,VerifyPosition),FVector::Dist(Hand,VerifyEnemyPosition),Dot);
            Check(Dot>.65f,TEXT("render_blended_slash_faces_forward"));
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("MovingSlash.png"),true,false); VerifyStage=3; NextCheck=T+.6;
        }
        else if(VerifyStage==3) { P->ConsumeMovementInputVector(); P->GetCharacterMovement()->StopMovementImmediately(); P->Heavy(); VerifyStage=4; NextCheck=T+.85; }
        else if(VerifyStage==4) { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("ChargingWave.png"),true,false); VerifyStage=11; NextCheck=T+1; }
        else if(VerifyStage==11) { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("ChargingWaveMid.png"),true,false); VerifyStage=12; NextCheck=T+1.3; }
        else if(VerifyStage==12) { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("ChargingWaveFull.png"),true,false); VerifyStage=5; NextCheck=T+.1; }
        else if(VerifyStage==5) { P->ReleaseHeavy(); VerifyStage=6; NextCheck=T+.4; }
        else if(VerifyStage==6) { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("ChargedRelease.png"),true,false); VerifyStage=7; NextCheck=T+1; }
        else if(VerifyStage==7) { VerifyPosition=P->GetActorLocation(); P->Dodge(); VerifyStage=8; NextCheck=T+.3; }
        else if(VerifyStage==8)
        {
            const float Travel=FVector::Dist2D(VerifyPosition,P->GetActorLocation());
            Check(P->VisualState==4 && Travel>50 && Travel<250 && FVector::DotProduct(P->GetActorForwardVector(),P->DashDirection)>.99f,TEXT("render_full_body_roll_keeps_input_direction"));
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("WaveRoll.png"),true,false); VerifyStage=10; NextCheck=T+1;
        }
        else { UE_LOG(LogTemp,Display,TEXT("ASCENSION_COMBAT_FEEL_VIEW %s"),bFailed?TEXT("FAIL"):TEXT("PASS")); FPlatformMisc::RequestExitWithStatus(false,bFailed?1:0); }
        return;
    }
    if(bVerify && FParse::Param(FCommandLine::Get(),TEXT("AscensionSurvivalVerify")))
    {
        if(VerifyStage==99)
        {
            Check(P->GetActorLocation().X>VerifyPosition.X+2 && P->GetVelocity().Size2D()>10 && T<P->AttackEnd,TEXT("blade_attack_keeps_actual_movement"));
            UE_LOG(LogTemp,Display,TEXT("ASCENSION_SURVIVAL_VERIFY %s"),bFailed?TEXT("FAIL"):TEXT("PASS")); FPlatformMisc::RequestExitWithStatus(false,bFailed?1:0); return;
        }
        for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) It->Destroy();
        const FGuid Gear=P->Weapon.ID;
        const float CornerX=ArenaHalfWidth-130,CornerY=ArenaHalfDepth-130;
        const FVector Corners[]={FVector(CornerX,CornerY,100),FVector(-CornerX,CornerY,100),FVector(CornerX,-CornerY,100),FVector(-CornerX,-CornerY,100)};
        for(const FVector& Corner:Corners)
        {
            P->SetActorLocation(Corner); FHitResult Ground;
            const bool Supported=GetWorld()->LineTraceSingleByChannel(Ground,Corner,Corner-FVector(0,0,200),ECC_Visibility);
            SpawnSurvivalBatch(6); bool Safe=RemainingEnemies()==6;
            for(TActorIterator<ACLEnemy> It(GetWorld());It;++It)
            {
                const FVector At=It->GetActorLocation();
                Safe&=FMath::Abs(At.X)<=ArenaHalfWidth-100 && FMath::Abs(At.Y)<=ArenaHalfDepth-100 && FVector::Dist2D(At,Corner)>=640;
                It->Destroy();
            }
            Check(Supported && Safe,TEXT("expanded_arena_corner_has_ground_and_safe_reinforcements"));
        }
        P->SetActorLocation(FVector(-750,0,100));
        bVerify=false; SpawnEncounter(20261011); bVerify=true;
        Check(RemainingEnemies()==12,TEXT("survival_starts_with_twelve_enemies"));
        for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) It->Destroy();
        ResetSurvival(); ReinforcementReady=T+100; RefillReady=T; TickSurvival();
        Check(RemainingEnemies()==4 && RefillReady>T,TEXT("low_population_refills_between_scheduled_batches"));
        for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) It->Destroy();
        ResetSurvival(); ReinforcementReady=T; TickSurvival();
        Check(RemainingEnemies()==4 && FMath::IsNearlyEqual(float(ReinforcementReady-T),2.5f),TEXT("early_reinforcements_every_2500ms"));
        SurvivalStart=T-240; ReinforcementReady=T; RefillReady=T+100; EliteReady=T+100; const int32 Early=RemainingEnemies(); TickSurvival();
        Check(RemainingEnemies()==Early+8 && FMath::IsNearlyEqual(float(ReinforcementReady-T),1.5f),TEXT("late_reinforcements_every_1500ms"));
        for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) It->Destroy(); ResetSurvival();
        P->GainExperience(25);
        Check(P->Level==2 && P->Experience==0 && P->Offers.Num()==3 && UGameplayStatics::IsGamePaused(this),TEXT("experience_level_choice_pauses_world"));
        P->Choose(0); Check(P->LightningRank==2 && !UGameplayStatics::IsGamePaused(this) && P->Weapon.ID==Gear,TEXT("growth_selection_resumes_preserves_gear"));
        P->SetActorLocation(FVector(0,0,100));
        ACLEnemy* Target=GetWorld()->SpawnActor<ACLEnemy>(P->GetActorLocation()+FVector(130,0,0),FRotator::ZeroRotator);
        if(Target)
        {
            Target->Configure(0); Target->SetActorTickEnabled(false); P->LightningReady=P->BladeReady=0; P->TickAuxiliaryCombat();
            Check(Target->HP==20,TEXT("automatic_lightning_and_blade_damage"));
            ACLBladeWave* TestWave=GetWorld()->SpawnActor<ACLBladeWave>(P->GetActorLocation(),FRotator::ZeroRotator);
            ACLEnemy* Behind=GetWorld()->SpawnActor<ACLEnemy>(P->GetActorLocation()+FVector(280,0,0),FRotator::ZeroRotator);
            if(Behind) Behind->Configure(0);
            TestWave->Hero=P; TestWave->Damage=5; TestWave->Pierces=3; TestWave->Tick(.2f); TestWave->Tick(.01f);
            Check(Target->HP==15 && Behind && Behind->HP==45 && TestWave->HitEnemies.Num()==2,TEXT("blade_wave_pierces_and_hits_each_once")); TestWave->Destroy();
            // Actual enemy displacement and post-hit pursuit are covered by VerifyCombat.
        }
        ACLBladeWave* WallWave=GetWorld()->SpawnActor<ACLBladeWave>(FVector(ArenaHalfWidth-100,0,100),FRotator::ZeroRotator);
        WallWave->Hero=P; WallWave->Tick(.5f); Check(WallWave->IsActorBeingDestroyed(),TEXT("blade_wave_stops_at_wall"));
        ACLExperience* Orb=GetWorld()->SpawnActor<ACLExperience>(P->GetActorLocation(),FRotator::ZeroRotator);
        if(Orb) { Orb->Tick(.016f); Check(P->Experience==8 && Orb->Value==0 && !Orb->IsActorTickEnabled(),TEXT("experience_orb_collected_once")); }
        SurvivalStart=T-180; ReinforcementReady=EliteReady=0; const int32 Before=RemainingEnemies(); TickSurvival();
        Check(RemainingEnemies()>Before && Wave==7,TEXT("elapsed_time_increases_enemy_pressure"));
        SpawnSurvivalBatch(100); Check(RemainingEnemies()==MaxSurvivalEnemies,TEXT("survival_fills_and_respects_48_enemy_cap"));
        SurvivalStart=T-300; TickSurvival(); const int32 WithBoss=RemainingEnemies(); TickSurvival();
        Check(!bBossSpawned && CurrentFloor==1 && WithBoss==MaxSurvivalEnemies,TEXT("elapsed_time_never_auto_summons_or_advances_floor"));
        bool FixedStats=true; for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) FixedStats&=It->MaxHP==(It->Kind==2?150:It->Kind==1?70:50);
        Check(FixedStats,TEXT("floor_one_monster_health_does_not_scale_with_time"));
        AdvanceFloor(); Check(CurrentFloor==1,TEXT("floor_exit_locked_until_gatekeeper_defeated"));
        P->SetActorLocation(FVector(-750,0,100)); SummonGatekeeper(); Check(!bBossSpawned,TEXT("gatekeeper_summon_requires_central_range"));
        P->SetActorLocation(FVector(0,-250,100)); P->UseAltar(); const int32 Summoned=RemainingEnemies(); P->UseAltar(); TickSurvival();
        Check(bBossSpawned && Summoned==49 && RemainingEnemies()==Summoned && CurrentFloor==1,TEXT("central_gatekeeper_summons_once_without_new_reinforcements"));
        for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) if(It->bBoss)
        { Check(It->HP==300 && FVector::Dist2D(It->GetActorLocation(),FVector::ZeroVector)<=331,TEXT("first_gatekeeper_has_fixed_health_and_central_position")); It->ReceiveHit(10000); break; }
        AdvanceFloor(); Check(CurrentFloor==1,TEXT("pending_reward_choice_blocks_floor_transition"));
        P->Skip(); P->PowerRank=2; const int32 SavedLevel=P->Level,SavedXP=P->Experience,SavedLightning=P->LightningRank;
        bVerify=false; P->UseAltar(); bVerify=true;
        Check(CurrentFloor==2 && !bBossSpawned && !bBossDefeated && !bVictory && RemainingEnemies()==14 && P->Weapon.ID==Gear && P->Level==SavedLevel && P->Experience==SavedXP && P->PowerRank==2 && P->LightningRank==SavedLightning,TEXT("second_floor_preserves_growth_and_gear_resets_encounter"));
        bool SecondStats=true; for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) SecondStats&=FMath::IsNearlyEqual(It->MaxHP,(It->Kind==1?70.f:50.f)*1.25f);
        Check(SecondStats,TEXT("second_floor_has_fixed_higher_monster_health"));
        P->SetActorLocation(FVector(0,-250,100)); P->UseAltar();
        for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) if(It->bBoss) { Check(It->HP==450,TEXT("second_gatekeeper_has_fixed_450_health")); It->ReceiveHit(10000); break; }
        P->Skip(); P->UseAltar(); ReinforcementReady=RefillReady=EliteReady=0; TickSurvival(); P->ReceiveHit(10000);
        Check(CurrentFloor==3 && bRestArea && bVictory && RemainingEnemies()==0 && P->HP==100 && P->PowerRank==2 && P->Weapon.ID==Gear,TEXT("rest_area_clears_combat_preserves_growth_and_prevents_damage"));
        P->ResetLife(); P->ReceiveHit(10000);
        Check(P->HP==0 && P->Experience==0 && P->PowerRank==0 && P->LightningRank==0 && P->Awakenings.IsEmpty(),TEXT("death_clears_survival_growth"));
        P->ResetLife(); Check(P->HP==100 && P->Level==1 && P->Experience==0 && P->LightningRank==1 && CurrentFloor==1 && !bRestArea && !bBossSpawned && !bVictory,TEXT("new_life_resets_survival_run"));
        P->DisableInput(Cast<APlayerController>(P->Controller)); P->Controller->SetControlRotation(FRotator(-30,0,0));
        for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) It->bFrozenForView=true;
        P->LightningReady=P->BladeReady=T+1000; P->ConsumeMovementInputVector(); VerifyPosition=P->GetActorLocation(); P->Attack(); P->Forward(1);
        VerifyStage=99; NextCheck=T+.12; return;
    }
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
        if(FParse::Param(FCommandLine::Get(),TEXT("AscensionWaveView")) && VerifyStage>=2)
        {
            if(VerifyStage==2) { P->Attack(); P->Right(1); ++VerifyStage; NextCheck=T+.08; }
            else if(VerifyStage==3) { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BladeWave.png"),true,false); ++VerifyStage; NextCheck=T+.5; }
            else FPlatformMisc::RequestExit(false);
            return;
        }
        if(FParse::Param(FCommandLine::Get(),TEXT("AscensionSurvivalView")) && VerifyStage>=2)
        {
            P->GainExperience(25); FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("GrowthChoice.png"),true,false);
            ++VerifyStage; NextCheck=T+100; return;
        }
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
            P->SetActorLocation(DeathBag->GetActorLocation()+FVector(0,0,70));
            Check(DeathBag->IsHidden() && !P->TakeItem(DeathBag) && DeathBag->Items.Num()==2,TEXT("hidden_equipment_drop_cannot_be_looted"));
            // Explicitly reveal this fixture to keep the preserved equipment rules covered.
            DeathBag->SetActorHiddenInGame(false); P->SetActorLocation(DeathBag->GetActorLocation()+FVector(500,0,70));
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
        if(WallTrials>0) Check(P->GetActorLocation().X<ArenaHalfWidth-20 && P->GetActorLocation().X>ArenaHalfWidth-100,TEXT("wall_dodge_blocked"));
        if(WallTrials==20)
        {
            VerifyPosition=P->GetActorLocation(); P->AddMovementInput(FVector(-1,0,0),1);
            VerifyStage=4; NextCheck=T+.2; PreviousLife=P->LifeID;
            // CharacterMovement is checked on the next frame, after dodge has ended.
            VerifyEnemy=nullptr;
            return;
        }
        P->SetActorLocation(FVector(ArenaHalfWidth-80,0,100)); P->Controller->SetControlRotation(FRotator(-30,0,0)); P->DodgeReady=0; P->Dodge();
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
    if(bSurvival) { P->Notice=TEXT("중앙 수문장을 처치해야 다음 층으로 갈 수 있습니다. 중앙 제단 E: 소환/이동"); return; }
    if(RemainingEnemies()>0) { P->Notice=TEXT("남은 몬스터를 처치한 뒤 다음 전투를 시작할 수 있습니다."); return; }
    if(!P->Offers.IsEmpty()) { P->Notice=TEXT("각성을 선택하거나 Esc로 포기한 뒤 N을 누르세요."); return; }
    ++Wave; SpawnEncounter(); P->Notice=FString::Printf(TEXT("%d번째 전투 시작! 장비와 각성은 유지됩니다."),Wave);
}
float AAscensionMode::SurvivalElapsed() const { return FMath::Max(0.f,float(Lab::Now(this)-SurvivalStart)); }
void AAscensionMode::ResetSurvival()
{
    SurvivalStart=Lab::Now(this); ReinforcementReady=SurvivalStart+2.5; RefillReady=SurvivalStart+1; EliteReady=SurvivalStart+60;
    bBossSpawned=bBossDefeated=bVictory=false;
    CurrentFloor=1; bRestArea=false;
    for(TActorIterator<ACLExperience> It(GetWorld());It;++It) It->Destroy();
}
void AAscensionMode::SummonGatekeeper()
{
    ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0));
    if(bOnline) { if(P) P->Notice=TEXT("온라인 P0에는 아직 수문장이 없습니다. 몬스터·보스는 서버 이전 후 추가됩니다."); return; }
    if(!P || P->HP<=0 || !bSurvival || bRestArea || bBossDefeated || FVector::Dist2D(P->GetActorLocation(),FVector::ZeroVector)>=300) return;
    if(bBossSpawned) { P->Notice=TEXT("수문장과 전투 중입니다. 처치해야 중앙 출구가 열립니다."); return; }
    SpawnSurvivalBatch(1,false,true);
    if(!bBossSpawned) P->Notice=TEXT("중앙에 적이 너무 많습니다. 주변을 정리한 뒤 E로 다시 소환하세요.");
}
void AAscensionMode::AdvanceFloor()
{
    ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0));
    if(!P || P->HP<=0 || !bSurvival || bRestArea || !bBossDefeated || !P->Offers.IsEmpty() || FVector::Dist2D(P->GetActorLocation(),FVector::ZeroVector)>=300) return;
    for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) { It->SetActorEnableCollision(false); It->Destroy(); }
    for(TActorIterator<ACLExperience> It(GetWorld());It;++It) It->Destroy();
    for(TActorIterator<ACLBladeWave> It(GetWorld());It;++It) It->Destroy();
    for(TActorIterator<ACLDrop> It(GetWorld());It;++It) It->Destroy();
    const int32 NextFloor=CurrentFloor+1; ResetSurvival(); CurrentFloor=NextFloor; Wave=1;
    P->Locked=nullptr; P->bCharging=false; P->ReleasedCharge=0; P->bResolved=true; P->bInventoryOpen=false; P->VisualState=-1;
    P->AttackEnd=P->DodgeEnd=Lab::Now(this); P->ImmuneEnd=Lab::Now(this)+2; P->PerfectUntil=0;
    P->ConsumeMovementInputVector(); P->GetCharacterMovement()->StopMovementImmediately();
    P->SetActorLocation(NextFloor==3?FVector(-1100,900,100):FVector(-750,0,100));
    if(NextFloor==3)
    { bRestArea=bVictory=true; P->HP=100; P->Notice=TEXT("휴식 구역 도착! 두 층 등반 완료 · 성장과 장비 유지 · R: 새 도전"); }
    else
    { SpawnEncounter(); P->Notice=TEXT("2층 상층 정원 도착! 성장과 장비를 유지합니다. 사냥 후 중앙 수문장에 도전하세요."); }
    UE_LOG(LogTemp,Display,TEXT("ASCENSION_FLOOR_ADVANCE floor=%d rest=%d level=%d"),CurrentFloor,bRestArea,P->Level);
}
void AAscensionMode::TickSurvival()
{
    ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0)); if(!P || P->HP<=0 || bVictory) return;
    const double T=Lab::Now(this); const float Elapsed=SurvivalElapsed(); Wave=1+int32(Elapsed/30);
    if(!bBossSpawned)
    {
        if(T>=ReinforcementReady) { SpawnSurvivalBatch(FMath::Min(4+int32(Elapsed/60),8)); ReinforcementReady=T+FMath::Max(1.5f,2.5f-Elapsed/240); }
        if(T>=RefillReady)
        {
            const int32 Target=FMath::Min(12+int32(Elapsed/30)*2,28);
            SpawnSurvivalBatch(FMath::Clamp(Target-RemainingEnemies(),0,4)); RefillReady=T+1;
        }
        if(T>=EliteReady) { SpawnSurvivalBatch(1,true); EliteReady=T+60; }
    }
    if(!bRestArea)
    {
        const FVector Exit(0,0,10);
        const FColor GateColor=bBossDefeated?FColor::Green:bBossSpawned?FColor::Red:FColor(255,190,70);
        DrawDebugCircle(GetWorld(),Exit,200,40,GateColor,false,-1,0,5,FVector::ForwardVector,FVector::RightVector,false);
        DrawDebugLine(GetWorld(),Exit,Exit+FVector(0,0,bBossDefeated?250:120),GateColor,false,-1,0,5);
        DrawDebugString(GetWorld(),Exit+FVector(0,0,180),bBossDefeated?TEXT("출구 개방 · E: 다음 층"):bBossSpawned?TEXT("수문장 전투 중 · 출구 봉인"):TEXT("중앙 제단 · E: 수문장 소환"),nullptr,GateColor,0,true);
        FVector From=P->GetActorLocation(); From.Z=10;
        if(bBossDefeated) DrawDebugDirectionalArrow(GetWorld(),From,From+(Exit-From).GetSafeNormal2D()*180,30,FColor::Green,false,-1,0,4);
    }
}
void AAscensionMode::SpawnSurvivalBatch(int32 Count,bool Elite,bool Boss)
{
    if(bOnline) return;
    ACLHero* P=Cast<ACLHero>(UGameplayStatics::GetPlayerPawn(this,0)); if(!P || P->HP<=0 || bRestArea || (Boss && bBossSpawned)) return;
    if(Boss) Count=FMath::Min(Count,1);
    for(int32 I=0;I<Count;++I)
    {
        if(!Boss && RemainingEnemies()>=MaxSurvivalEnemies) break;
        for(int32 Attempt=0;Attempt<150;++Attempt)
        {
            FVector At=(Boss?FVector::ZeroVector:P->GetActorLocation())+FVector(Boss?FMath::FRandRange(0.f,330.f):FMath::FRandRange(650.f,1250.f),0,0).RotateAngleAxis(FMath::FRandRange(0.f,360.f),FVector::UpVector); At.Z=100;
            if(Boss && FVector::Dist2D(At,P->GetActorLocation())<160) continue;
            if(FMath::Abs(At.X)>ArenaHalfWidth-100 || FMath::Abs(At.Y)>ArenaHalfDepth-100 || GetWorld()->OverlapBlockingTestByChannel(At,FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeCapsule(45,88))) continue;
            bool Safe=true; for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) if(FVector::DistSquared2D(At,It->GetActorLocation())<FMath::Square(160.f)) { Safe=false; break; }
            if(!Safe) continue;
            FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
            if(ACLEnemy* Enemy=GetWorld()->SpawnActor<ACLEnemy>(At,FRotator::ZeroRotator,Params))
            {
                Enemy->Configure(Elite || Boss?2:FMath::FRand()<(SurvivalElapsed()<120?.25f:.5f)?1:0);
                Enemy->HP=Enemy->MaxHP*=1.f+.25f*(CurrentFloor-1);
                Enemy->AlertUntil=Lab::Now(this)+600; ++SpawnedEnemies;
                if(Boss) { Enemy->bBoss=true; Enemy->HP=Enemy->MaxHP=CurrentFloor==1?300:450; Enemy->GetMesh()->SetRelativeScale3D(Enemy->GetMesh()->GetRelativeScale3D()*1.25f); bBossSpawned=true; P->Notice=TEXT("중앙 수문장 소환! 추가 증원은 멈춥니다. 처치 후 중앙 E: 다음 층"); }
                break;
            }
        }
    }
}
void AAscensionMode::SpawnEncounter(int32 Seed)
{
    if(bOnline) return; // 온라인 P0: 몬스터는 서버가 관리하기 전까지 만들지 않는다
    TArray<ACLEnemy*> Existing; for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) Existing.Add(*It);
    for(ACLEnemy* Enemy:Existing) { Enemy->SetActorEnableCollision(false); Enemy->Destroy(); }
    bEncounterCleared=false; SpawnedEnemies=0;
    const bool Fixed=bVerify || (bView && !FParse::Param(FCommandLine::Get(),TEXT("AscensionCombatView")));
    const FVector FixedPositions[]={FVector(-250,300,100),FVector(400,-350,100),FVector(700,400,100)};
    const int32 Count=Fixed?3:bSurvival?12+2*(CurrentFloor-1):FMath::Clamp(6+Wave,7,10);
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
        const int32 Kind=Fixed?I:bSurvival?Random.RandRange(0,1):I==Count-1?2:I<2?I:Random.RandRange(0,1);
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
        if(ACLEnemy* Enemy=GetWorld()->SpawnActor<ACLEnemy>(At,FRotator::ZeroRotator,Params))
        {
            Enemy->Configure(Kind); if(!Fixed) Enemy->HP=Enemy->MaxHP*=bSurvival?1.f+.25f*(CurrentFloor-1):1.f+.08f*FMath::Min(Wave-1,5);
            if(bSurvival) Enemy->AlertUntil=Lab::Now(this)+600;
            Occupied.Add(Enemy->GetActorLocation()); ++SpawnedEnemies;
        }
    }
    UE_LOG(LogTemp,Display,TEXT("ASCENSION_ENCOUNTER wave=%d count=%d seed=%d"),Wave,SpawnedEnemies,Seed);
}
void ACLHUD::DrawHUD()
{
    Super::DrawHUD(); ACLHero* P=Cast<ACLHero>(GetOwningPawn()); if(!P || !Canvas) return;
    const float BarWidth=FMath::Min(360.f,Canvas->SizeX-48.f),BarX=(Canvas->SizeX-BarWidth)*.5f,BarY=Canvas->SizeY-85.f;
    const float Health=FMath::Clamp(P->HP/100.f,0.f,1.f);
    DrawRect(FLinearColor(0,0,0,.85f),BarX-12,BarY-28,BarWidth+24,76);
    DrawText(FString::Printf(TEXT("체력  %.0f / 100%s"),P->HP,P->HP>0 && P->HP<=30?TEXT("  · 위험"):TEXT("")),FLinearColor::White,BarX,BarY-23,nullptr,1.1f);
    DrawRect(FLinearColor(.2f,.2f,.23f,1),BarX,BarY,BarWidth,16);
    DrawRect(Lab::Now(P)<P->HurtUntil?FLinearColor::White:Health<=.3f?FLinearColor(.9f,.12f,.1f):FLinearColor(.12f,.8f,.65f),BarX,BarY,BarWidth*Health,16);
    DrawRect(FLinearColor(0,0,0,.75f),12,12,FMath::Min(1000.f,Canvas->SizeX-24.f),190);
    AAscensionMode* Mode=Cast<AAscensionMode>(GetWorld()->GetAuthGameMode());
    if(Mode && IsValid(Mode->Net)) DrawText(Mode->Net->StatusText(),FLinearColor(.55f,.85f,1.f),BarX-12,BarY-58,nullptr,1.f);
    if(Mode && Mode->bSurvival)
    {
        const int32 Seconds=int32(Mode->SurvivalElapsed());
        DrawText(Mode->bRestArea?TEXT("휴식 구역 · 두 층 등반 완료"):FString::Printf(TEXT("%d층 %s · 체류 %d:%02d · 적 %d · %s"),Mode->CurrentFloor,Mode->CurrentFloor==1?TEXT("폐허 정원"):TEXT("상층 정원"),Seconds/60,Seconds%60,Mode->RemainingEnemies(),Mode->bBossDefeated?TEXT("중앙 출구 개방"):Mode->bBossSpawned?TEXT("수문장 전투"):TEXT("사냥 / 중앙 E 소환")),FLinearColor::White,25,22,nullptr,1.15);
        DrawRect(FLinearColor(.12f,.15f,.22f),BarX,BarY+25,BarWidth,6);
        DrawRect(FLinearColor(.35f,.6f,1.f),BarX,BarY+25,BarWidth*FMath::Clamp(float(P->Experience)/P->ExperienceRequired(),0.f,1.f),6);
    }
    else DrawText(FString::Printf(TEXT("각성과 탐욕의 탑 · 전투 %d · 남은 몬스터 %d"),Mode?Mode->Wave:1,Mode?Mode->RemainingEnemies():0),FLinearColor::White,25,22,nullptr,1.15);
    DrawText(FString::Printf(TEXT("체력 %.0f / 100    회피 %.1f초    Q 스킬 %.1f초    락온 %s"),P->HP,FMath::Max(0.,P->DodgeReady-Lab::Now(P)),FMath::Max(0.,P->SkillReady-Lab::Now(P)),IsValid(P->Locked)?TEXT("켜짐"):TEXT("꺼짐")),FLinearColor::White,25,49);
    DrawText(Mode && Mode->bSurvival?TEXT("WASD 이동 · 좌클릭 검기 · 우클릭 누르기: 충전 / 놓기: 발사 · Space 회피"):TEXT("WASD 이동 · 마우스 시점 · 좌클릭 검 공격 · 우클릭 강공격 · Space 회피"),FLinearColor::White,25,74);
    if(P->IsCharging())
    {
        const float X=Canvas->SizeX*.5f-145,Y=FMath::Min(Canvas->SizeY*.5f+65,Canvas->SizeY-250.f),Charge=P->ChargeProgress();
        DrawRect(FLinearColor(0,0,0,.85f),X-8,Y-45,306,69);
        const bool Ready=P->CanReleaseHeavy();
        DrawText(!Ready?TEXT("최소 0.6초 충전 필요 · 지금 놓으면 취소"):(Charge>=1?TEXT("최대 충전 · 놓으면 발사"):TEXT("발사 가능 · 더 모으면 피해와 범위 증가")),Ready?FLinearColor(1,.8f,.2f):FLinearColor(.7f,.8f,1.f),X,Y-42);
        DrawText(FString::Printf(TEXT("%.1f / %.1f초 · 피해 %.1f배 · 폭 %.1f배"),Charge*ACLHero::MaximumChargeSeconds,ACLHero::MaximumChargeSeconds,1+Charge*2,(70+Charge*160)/70),FLinearColor::White,X,Y-20);
        DrawRect(FLinearColor(.2f,.15f,.08f),X,Y+4,290,10);
        DrawRect(FLinearColor(1,.35f+Charge*.55f,.1f),X,Y+4,290*Charge,10);
        for(int32 I=1;I<3;++I) DrawLine(X+290.f*I/3,Y+4,X+290.f*I/3,Y+14,FLinearColor::White,1);
        const float MinimumX=X+290.f*ACLHero::MinimumChargeSeconds/ACLHero::MaximumChargeSeconds;
        DrawLine(MinimumX,Y+2,MinimumX,Y+16,FLinearColor(.7f,.8f,1.f),2);
    }
    DrawText(Mode && Mode->bSurvival?TEXT("Tab 락온 · Q 스킬 · E 중앙 소환/층 이동 · I 가방 · R 새 도전"):TEXT("Tab 락온 · Q 스킬 · E 제단 · I 가방 · N 다음 전투 · R 다시 시작"),FLinearColor::White,25,97);
    if(Mode && Mode->bSurvival && P->Offers.IsEmpty())
        for(TActorIterator<ACLEnemy> It(GetWorld());It;++It) if(It->bBoss && It->HP>0)
        {
            const float X=Canvas->SizeX*.5f-210,Y=Canvas->SizeY-205.f;
            DrawRect(FLinearColor(0,0,0,.85f),X-8,Y-4,436,76);
            DrawText(FString::Printf(TEXT("%d층 중앙 수문장%s · %.0f / %.0f"),Mode->CurrentFloor,It->bEnraged?TEXT(" [격노]"):TEXT(""),It->HP,It->MaxHP),FLinearColor(1,.75f,.3f),X,Y);
            DrawRect(FLinearColor(.25f,.12f,.1f),X,Y+25,420,10);
            DrawRect(FLinearColor(.9f,.28f,.12f),X,Y+25,420*FMath::Clamp(It->HP/It->MaxHP,0.f,1.f),10);
            DrawText(It->BossHint(),It->Phase==3?FLinearColor(.3f,1.f,.6f):FLinearColor(1,.8f,.4f),X,Y+45); break;
        }
    DrawText(P->Notice,FLinearColor::Yellow,25,123);
    FString Owned=TEXT("보유 각성: "); if(P->Awakenings.IsEmpty()) Owned+=TEXT("없음");
    for(int32 I=0;I<P->Awakenings.Num();++I) Owned+=FString::Printf(TEXT("[%d] %s   "),I+1,Lab::Names[P->Awakenings[I]]);
    DrawText(Owned,FLinearColor::White,25,148);
    DrawText(Mode && Mode->bSurvival?FString::Printf(TEXT("Lv %d · 경험치 %d/%d · 번개 %d · 칼날 %d · 검 강화 %d · 일반 검기 %d명"),P->Level,P->Experience,P->ExperienceRequired(),P->LightningRank,P->BladeRank,P->PowerRank,P->NormalWaveTargets()):TEXT("개발 중: 탑 등반 · 파티 · 멀티플레이"),FLinearColor(.7f,.8f,1.f),25,174);
    if(Mode && Mode->bVictory) DrawText(TEXT("휴식 구역 도착! 등반 완료 · R: 새 도전"),FLinearColor::Green,Canvas->SizeX*.5f-245,Canvas->SizeY*.5f,nullptr,1.3f);
    if(Mode && Mode->bEncounterCleared) DrawText(TEXT("전투 완료 · N: 다음 전투"),FLinearColor::Yellow,Canvas->SizeX*.5f-170,Canvas->SizeY-45);
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
        DrawText(P->bLevelChoice?TEXT("레벨 업 · 시간 정지 · 1 / 2 / 3: 성장 선택 · Esc: 포기"):P->PendingReplacement>=0?TEXT("보유 각성 교체 · 1 / 2 / 3: 교체할 슬롯 · Esc: 포기"):TEXT("각성 선택 · 1 / 2 / 3: 획득 · Esc: 포기"),FLinearColor::Yellow,35,241);
        if(P->PendingReplacement>=0) DrawText(FString::Printf(TEXT("새로 얻을 각성: %s"),Lab::Names[P->PendingReplacement]),FLinearColor::White,35,267);
        for(int32 I=0;I<P->Offers.Num();++I)
        {
            int32 ID=P->PendingReplacement>=0?P->Awakenings[I]:P->Offers[I];
            FLinearColor Color=ID==3 || ID==4?FLinearColor(1,.55f,.1f):FLinearColor::White;
            DrawText(FString::Printf(TEXT("%d. %s"),I+1,Lab::Names[ID]),Color,35,300+I*55);
            const FString Detail=ID==8 && P->bLevelChoice?FString::Printf(TEXT("일반 검기 %d → %d명 · 검 피해 +3 · 체력 +15 회복%s"),P->NormalWaveTargets(),FMath::Min(4,P->NormalWaveTargets()+1),P->NormalWaveTargets()==4?TEXT(" (대상 수 최대)"):TEXT("")):FString(Lab::Details[ID]);
            DrawText(Detail,Color,55,323+I*55);
        }
    }
}
