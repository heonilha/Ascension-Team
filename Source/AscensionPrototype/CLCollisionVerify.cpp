#include "CLCollisionVerify.h"

#include "CollisionQueryParams.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
    struct FServerObstacle
    {
        FString Name;
        bool bBlocking = false;
        double ZMin = 0, ZMax = 0;
        TArray<FVector2D> Polygon; // 반시계 방향 볼록 다각형
    };

    // 점에서 볼록 다각형 경계까지 거리. 안쪽이면 음수.
    double SignedDistance(const FVector2D& P, const TArray<FVector2D>& Polygon)
    {
        bool bInside = true;
        double Best = TNumericLimits<double>::Max();
        for (int32 I = 0; I < Polygon.Num(); ++I)
        {
            const FVector2D A = Polygon[I], B = Polygon[(I + 1) % Polygon.Num()], E = B - A;
            if (FVector2D::CrossProduct(E, P - A) < 0) bInside = false;
            const double T = FMath::Clamp(FVector2D::DotProduct(P - A, E) / E.SizeSquared(), 0.0, 1.0);
            Best = FMath::Min(Best, FVector2D::Distance(P, A + E * T));
        }
        return bInside ? -Best : Best;
    }

    bool Load(const FString& Path, TArray<FServerObstacle>& Out, double& StepHeight)
    {
        TArray<FString> Lines;
        if (!FFileHelper::LoadFileToStringArray(Lines, *Path)) return false;
        for (const FString& Line : Lines)
        {
            TArray<FString> T;
            Line.ParseIntoArrayWS(T);
            if (T.Num() == 2 && T[0] == TEXT("step_height")) StepHeight = FCString::Atod(*T[1]);
            if (T.Num() < 6 || T[0] != TEXT("obstacle")) continue;
            FServerObstacle O;
            O.Name = T[1];
            O.bBlocking = T[2] == TEXT("1");
            O.ZMin = FCString::Atod(*T[3]);
            O.ZMax = FCString::Atod(*T[4]);
            const int32 Count = FCString::Atoi(*T[5]);
            if (T.Num() != 6 + Count * 2) return false;
            for (int32 I = 0; I < Count; ++I) O.Polygon.Add(FVector2D(FCString::Atod(*T[6 + I * 2]), FCString::Atod(*T[7 + I * 2])));
            Out.Add(MoveTemp(O));
        }
        return !Out.IsEmpty();
    }
}

bool AscensionVerifyServerCollision(UWorld* World)
{
    constexpr double HalfWidth = 2800, HalfDepth = 2400, Grid = 40, EdgeBand = 6;
    constexpr double Radius = 32, HalfHeight = 88, CenterZ = 92; // 바닥(z=0)에 선 주인공 캡슐

    const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/ServerCollision/CombatLab.txt"));
    TArray<FServerObstacle> Obstacles;
    double StepHeight = 45;
    if (!Load(Path, Obstacles, StepHeight))
    {
        UE_LOG(LogTemp, Error, TEXT("ASCENSION_COLLISION_VERIFY FAIL cannot read %s"), *Path);
        return false;
    }

    // 캡슐 높이 범위와 겹치고 계단으로 오를 수 없는 장애물만 막는다(서버 판정과 같은 기준).
    TArray<const FServerObstacle*> Relevant;
    for (const FServerObstacle& O : Obstacles)
    {
        if (O.bBlocking && O.ZMin < CenterZ + HalfHeight && O.ZMax > CenterZ - HalfHeight) Relevant.Add(&O);
    }

    // 실제 질의도 같은 기준: 오를 수 있는 낮은 구조물(기둥 받침 등)은 제외한다.
    FCollisionQueryParams Query(TEXT("AscensionCollisionVerify"));
    for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
    {
        if (It->GetComponentsBoundingBox().Max.Z <= StepHeight) Query.AddIgnoredActor(*It);
    }
    const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
    const FCollisionShape Capsule = FCollisionShape::MakeCapsule(Radius, HalfHeight);

    int32 Points = 0, Blocked = 0, Mismatched = 0, Skipped = 0;
    for (double Y = -HalfDepth + Grid / 2; Y < HalfDepth; Y += Grid)
    {
        for (double X = -HalfWidth + Grid / 2; X < HalfWidth; X += Grid)
        {
            double Nearest = TNumericLimits<double>::Max();
            for (const FServerObstacle* O : Relevant) Nearest = FMath::Min(Nearest, SignedDistance(FVector2D(X, Y), O->Polygon));
            Nearest -= Radius;
            if (FMath::Abs(Nearest) < EdgeBand)
            {
                ++Skipped; // 타원을 24각형으로 근사한 차이(1cm 미만)와 경계 반올림을 피한다
                continue;
            }
            const bool bReal = World->OverlapAnyTestByObjectType(FVector(X, Y, CenterZ), FQuat::Identity, Objects, Capsule, Query);
            const bool bExported = Nearest < 0;
            ++Points;
            Blocked += bReal ? 1 : 0;
            if (bReal != bExported && ++Mismatched <= 8)
                UE_LOG(LogTemp, Warning, TEXT("ASCENSION_COLLISION_MISMATCH (%.0f,%.0f) real=%d exported=%d"), X, Y, bReal, bExported);
        }
    }
    const bool bOk = Mismatched == 0 && Blocked > 0;
    UE_LOG(LogTemp, Display, TEXT("ASCENSION_COLLISION_VERIFY %s points=%d blocked=%d mismatched=%d edge_skipped=%d obstacles=%d relevant=%d"),
           bOk ? TEXT("PASS") : TEXT("FAIL"), Points, Blocked, Mismatched, Skipped, Obstacles.Num(), Relevant.Num());
    return bOk;
}
