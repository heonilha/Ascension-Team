#include "CLRemoteHero.h"

#include "HeroMotion.h"

#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

// CombatLab.cpp에 있는 모델 적용 함수를 그대로 재사용한다(플레이어와 같은 모델·동작).
namespace Lab
{
    bool SetModel(ACharacter* Character, TArray<TObjectPtr<UAnimSequence>>& Clips, bool Monster);
    void AttachSword(ACharacter* Character, UStaticMeshComponent* Sword);
    void AlignBodyForward(USkeletalMeshComponent* Mesh, float Delta);
}

ACLRemoteHero::ACLRemoteHero()
{
    // 스스로 움직이지 않는다. ACLNetSession이 매 프레임 UpdateVisual을 부른다.
    PrimaryActorTick.bCanEverTick = false;
    AutoPossessAI = EAutoPossessAI::Disabled;
    GetCapsuleComponent()->InitCapsuleSize(32, 88);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore); // P0: 팀원끼리 몸으로 막지 않는다
    GetCharacterMovement()->GravityScale = 0;

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Sword = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RemoteSword"));
    Sword->SetupAttachment(RootComponent);
    Sword->SetStaticMesh(Cube.Object);
    Sword->SetRelativeScale3D(FVector(1.1, .07, .12));
    Sword->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ACLRemoteHero::BeginPlay()
{
    Super::BeginPlay();
    GetCharacterMovement()->DisableMovement();
    GetCharacterMovement()->SetComponentTickEnabled(false);
    bHasModel = Lab::SetModel(this, Clips, false);
    if (bHasModel)
    {
        Lab::AttachSword(this, Sword);
        GetMesh()->SetAnimInstanceClass(UCLHeroMotion::StaticClass());
    }
    LastPosition = GetActorLocation();
}

void ACLRemoteHero::Init(uint32 InPlayerId, const FString& InName)
{
    PlayerId = InPlayerId;
    DisplayName = InName;
}

void ACLRemoteHero::PushSample(double ServerTime, const FVector& Position, float Yaw, uint8 State)
{
    if (!Samples.IsEmpty())
    {
        const FSample Last = Samples.Last();
        if (ServerTime <= Last.Time) return; // 순서가 뒤집힌 패킷은 버린다(TCP라 보통 없음)
        // 서버는 움직인 플레이어만 보낸다. 오래 멈췄다가 다시 움직이면 멈춘 위치를 직전에 한 번 더 넣어,
        // 긴 공백 전체에 걸쳐 천천히 미끄러지듯 보간되는 것을 막는다.
        if (ServerTime - Last.Time > .25) Samples.Add({ServerTime - .05, Last.Position, Last.Yaw, Last.State});
    }
    Samples.Add({ServerTime, Position, Yaw, State});
    if (Samples.Num() > 32) Samples.RemoveAt(0);
}

void ACLRemoteHero::UpdateVisual(double RenderTime, float Delta)
{
    if (Samples.IsEmpty()) return;

    // RenderTime 직전·직후 두 샘플 사이를 보간한다. 샘플이 하나뿐이면 그 자리에 둔다(P0는 외삽 없음).
    while (Samples.Num() >= 2 && Samples[1].Time <= RenderTime) Samples.RemoveAt(0);
    const FSample& A = Samples[0];
    FVector Position = A.Position;
    float Yaw = A.Yaw;
    uint8 State = A.State;
    if (Samples.Num() >= 2 && RenderTime > A.Time)
    {
        const FSample& B = Samples[1];
        const float Alpha = FMath::Clamp(float((RenderTime - A.Time) / (B.Time - A.Time)), 0.f, 1.f);
        Position = FMath::Lerp(A.Position, B.Position, Alpha);
        Yaw = A.Yaw + FMath::FindDeltaAngleDegrees(A.Yaw, B.Yaw) * Alpha;
        if (Alpha > .5f) State = B.State;
    }
    SetActorLocationAndRotation(Position, FRotator(0, Yaw, 0));

    Speed = FMath::FInterpTo(Speed, float((Position - LastPosition).Size2D()) / FMath::Max(Delta, 1.e-3f), Delta, 10.f);
    LastPosition = Position;
    AnimTime += Delta;

    // 상태 0 대기 1 달리기 2 공격 3 강공격 4 회피 5 사망. 클립 순서는 CombatLab.cpp SetModel과 같다.
    if (State != ShownState)
    {
        if (State >= 2 && State <= 4) ActionStart = AnimTime;
        if (State == 4 && bHasModel) GetMesh()->SetRelativeRotation(FRotator(0, 270, 0));
        ShownState = State;
    }
    if (bHasModel && Clips.Num() == 5)
    {
        if (UCLHeroMotion* Motion = Cast<UCLHeroMotion>(GetMesh()->GetAnimInstance()))
        {
            Motion->IdleClip = Clips[0];
            Motion->RunClip = Clips[1];
            Motion->BaseTime = AnimTime;
            Motion->MovingWeight = FMath::Clamp(Speed / 220.f, 0.f, 1.f);
            const bool bAction = State >= 2 && State <= 4 && Clips[State];
            Motion->ActionWeight = bAction ? 1.f : 0.f;
            if (bAction)
            {
                Motion->ActionClip = Clips[State];
                Motion->bFullBodyAction = State == 4;
                // 플레이어 쪽과 같은 구간을 비슷한 속도로 재생한다(공격 약 1.8배속, 회피 0.65초).
                const float Start = State == 3 ? 1.1f : State == 2 ? .1f : 0.f;
                const float Rate = State == 4 ? Clips[4]->GetPlayLength() / .65f : 1.8f;
                Motion->ActionTime = Start + float(AnimTime - ActionStart) * Rate;
            }
        }
        if (State != 4) Lab::AlignBodyForward(GetMesh(), 0.f);
    }

    DrawDebugString(GetWorld(), FVector(0, 0, 125), DisplayName, this, FColor(120, 220, 255), 0, true);
}
