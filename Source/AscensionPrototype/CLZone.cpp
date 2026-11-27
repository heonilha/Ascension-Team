#include "CLZone.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ACLZone::ACLZone()
{
    PrimaryActorTick.bCanEverTick = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    const auto Make = [&](const TCHAR* Prefix, int32 I)
    {
        UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("%s%03d"), Prefix, I));
        C->SetupAttachment(RootComponent);
        C->SetStaticMesh(Cube.Object);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetCastShadow(false);
        C->SetMobility(EComponentMobility::Movable);
        C->SetHiddenInGame(true);
        return C;
    };
    for (int32 I = 0; I < Panels; ++I)
    {
        Wall.Add(Make(TEXT("Wall"), I));
        Next.Add(Make(TEXT("Next"), I));
    }
}

void ACLZone::BeginPlay()
{
    Super::BeginPlay();
    // 재질은 맵 스크립트(CreateMap.py)가 만든다. 없으면 HasMaterials()가 false가 되고 세션이 선으로 그린다.
    WallMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Environment/M_ZoneWall.M_ZoneWall"), nullptr, LOAD_NoWarn | LOAD_Quiet);
    NextMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Environment/M_ZoneNext.M_ZoneNext"), nullptr, LOAD_NoWarn | LOAD_Quiet);
    for (UStaticMeshComponent* C : Wall) if (WallMaterial) C->SetMaterial(0, WallMaterial);
    for (UStaticMeshComponent* C : Next) if (NextMaterial) C->SetMaterial(0, NextMaterial);
}

void ACLZone::Place(TArray<TObjectPtr<UStaticMeshComponent>>& Ring, const FVector2D& Center, float Radius, float Z, float Width, float Height, bool bVisible)
{
    // 판 하나는 원을 Panels 조각으로 나눈 현의 길이보다 조금 길게 해서 이음매가 벌어지지 않게 한다.
    const float Chord = 2 * Radius * FMath::Sin(PI / Panels) * 1.03f + 2;
    for (int32 I = 0; I < Ring.Num(); ++I)
    {
        UStaticMeshComponent* C = Ring[I];
        C->SetHiddenInGame(!bVisible);
        if (!bVisible) continue;
        const float A = (I + .5f) * 2 * PI / Panels;
        const FVector At(Center.X + FMath::Cos(A) * Radius, Center.Y + FMath::Sin(A) * Radius, Z);
        C->SetWorldLocationAndRotation(At, FRotator(0, FMath::RadiansToDegrees(A) + 90, 0));
        C->SetWorldScale3D(FVector(Chord / 100, Width / 100, Height / 100));
    }
}

void ACLZone::Show(const FVector2D& Center, float Radius, float NextRadius, bool bShow)
{
    // 반지름이 거의 그대로면 다시 놓지 않는다(축소 중에는 매 프레임 1cm 넘게 바뀐다)
    if (bShow == bLastShow && FMath::Abs(Radius - LastRadius) < 1 && FMath::Abs(NextRadius - LastNext) < 1 && Center.Equals(LastCenter, 1)) return;
    bLastShow = bShow;
    LastRadius = Radius;
    LastNext = NextRadius;
    LastCenter = Center;
    // 작아지면 판이 겹쳐 보이므로 1m 아래에서는 감춘다(4라운드 끝)
    Place(Wall, Center, Radius, WallHeight * .5f, 12, WallHeight, bShow && Radius > 100);
    Place(Next, Center, NextRadius, 4, 70, 4, bShow && NextRadius > 100 && NextRadius < Radius - 50);
}

void ACLZone::LogState() const
{
    const UStaticMeshComponent* C = Wall.Num() ? Wall[0].Get() : nullptr;
    if (!C) return;
    UE_LOG(LogTemp, Display, TEXT("ASCENSION_ZONE_STATE wall0 hidden=%d visible=%d registered=%d mesh=%s material=%s location=%s scale=%s bounds=%s"),
           C->bHiddenInGame ? 1 : 0, C->IsVisible() ? 1 : 0, C->IsRegistered() ? 1 : 0, C->GetStaticMesh() ? *C->GetStaticMesh()->GetName() : TEXT("none"),
           C->GetMaterial(0) ? *C->GetMaterial(0)->GetName() : TEXT("none"), *C->GetComponentLocation().ToString(), *C->GetComponentScale().ToString(),
           *C->Bounds.BoxExtent.ToString());
}
