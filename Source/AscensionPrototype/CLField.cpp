#include "CLField.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

const FCLField& FCLField::Get()
{
    static const FCLField Field = []()
    {
        FCLField Out;
        FString Text;
        const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/ServerCollision/CombatLab.txt"));
        if (!FFileHelper::LoadFileToString(Text, *Path)) return Out;
        TArray<FString> Lines; Text.ParseIntoArrayLines(Lines);
        for (const FString& Line : Lines)
        {
            if (!Line.StartsWith(TEXT("area "))) continue;
            TArray<FString> W; Line.ParseIntoArrayWS(W);
            if (W.Num() != 6) continue;
            FCLFieldArea A;
            A.Kind = W[1]; A.Index = FCString::Atoi(*W[2]);
            A.Center = FVector2D(FCString::Atof(*W[3]), FCString::Atof(*W[4])); A.Radius = FCString::Atof(*W[5]);
            Out.Areas.Add(A);
        }
        return Out;
    }();
    return Field;
}

int32 FCLField::BushAt(const FVector& Position) const
{
    for (const FCLFieldArea& A : Areas)
        if (A.Kind == TEXT("bush") && FVector2D::Distance(A.Center, FVector2D(Position)) <= A.Radius) return A.Index;
    return -1;
}

FVector2D FCLField::TeamBase(int32 Team)
{
    const float Angle = FMath::DegreesToRadians(45.f + 90.f * (FMath::Clamp(Team, 1, 4) - 1));
    return FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 4000.f;
}
