#include "CLSkillLibrary.h"
#include "CombatLab.h"
#include "CLSkillComponent.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"

namespace
{
    FVector Flat(FVector V) { V.Z = 0; return V; }
    bool Alive(const ACLEnemy* E) { return IsValid(E) && !E->bPuppet && E->HP > 0; }
}

namespace CLSkill
{
    bool Clear(const AActor* Source, const FVector& From, const FVector& To)
    {
        FHitResult Hit; FCollisionQueryParams Params; Params.AddIgnoredActor(Source);
        return !Source->GetWorld()->LineTraceSingleByObjectType(Hit, From, To, FCollisionObjectQueryParams(ECC_WorldStatic), Params);
    }

    TArray<ACLEnemy*> EnemiesInCircle(const AActor* Source, const FVector& Center, float Radius)
    {
        TArray<ACLEnemy*> Out;
        for (TActorIterator<ACLEnemy> It(Source->GetWorld()); It; ++It)
            if (Alive(*It) && FVector::DistSquared2D(Center, It->GetActorLocation()) <= FMath::Square(Radius) && Clear(Source, Center + FVector(0, 0, 40), It->GetActorLocation()))
                Out.Add(*It);
        return Out;
    }

    TArray<ACLEnemy*> EnemiesInCone(const AActor* Source, const FVector& Origin, const FVector& Aim, float Range, float HalfAngleDegrees)
    {
        TArray<ACLEnemy*> Out; const FVector Forward = Flat(Aim).GetSafeNormal();
        const float MinDot = FMath::Cos(FMath::DegreesToRadians(HalfAngleDegrees));
        for (TActorIterator<ACLEnemy> It(Source->GetWorld()); It; ++It)
        {
            if (!Alive(*It)) continue;
            const FVector To = Flat(It->GetActorLocation() - Origin);
            if (To.SizeSquared() <= FMath::Square(Range) && FVector::DotProduct(To.GetSafeNormal(), Forward) >= MinDot && Clear(Source, Origin, It->GetActorLocation()))
                Out.Add(*It);
        }
        return Out;
    }

    TArray<ACLEnemy*> EnemiesInLine(const AActor* Source, const FVector& Origin, const FVector& Aim, float Range, float HalfWidth)
    {
        TArray<ACLEnemy*> Out; const FVector Forward = Flat(Aim).GetSafeNormal();
        for (TActorIterator<ACLEnemy> It(Source->GetWorld()); It; ++It)
        {
            if (!Alive(*It)) continue;
            const FVector To = Flat(It->GetActorLocation() - Origin);
            const float Along = FVector::DotProduct(To, Forward);
            const float Side = (To - Forward * Along).Size();
            if (Along >= 0 && Along <= Range && Side <= HalfWidth + 35 && Clear(Source, Origin, It->GetActorLocation()))
                Out.Add(*It);
        }
        return Out;
    }

    void Hit(const FCLSkillContext& Ctx, ACLEnemy* Target, float Damage, float Feedback)
    {
        if (!Alive(Target)) return;
        const FVector Location = Target->GetActorLocation();
        Target->ReceiveHit(Damage < 0 ? Ctx.Damage : Damage, Location - Ctx.Origin);
        if (Ctx.Hero) Ctx.Hero->ImpactFeedback(Feedback, Location);
    }

    int32 HitAll(const FCLSkillContext& Ctx, const TArray<ACLEnemy*>& Targets, float Damage, float Feedback)
    {
        int32 Count = 0;
        for (ACLEnemy* Target : Targets) if (Alive(Target)) { Hit(Ctx, Target, Damage, Feedback); ++Count; }
        return Count;
    }

    void Stagger(ACLEnemy* Target, float Seconds)
    {
        if (!Alive(Target) || Target->bBoss) return;
        const double Until = Target->GetWorld()->GetTimeSeconds() + Seconds;
        Target->StaggerUntil = FMath::Max(Target->StaggerUntil, Until);
        if (Target->Kind != 2) { Target->Phase = 3; Target->Deadline = FMath::Max(Target->Deadline, Until); }
        else Target->Deadline += Seconds; // 돌진병은 기존 피격 규칙처럼 공격 시각만 늦춘다
    }

    void FireProjectile(const FCLSkillContext& Ctx, float Speed, float Range, float Radius, int32 Pierces, float Damage)
    {
        if (!Ctx.Hero) return;
        const FVector Aim = Flat(Ctx.Aim).GetSafeNormal();
        FActorSpawnParameters Params; Params.Owner = Ctx.Hero;
        if (ACLBladeWave* Shot = Ctx.Hero->GetWorld()->SpawnActor<ACLBladeWave>(Ctx.Origin + Aim * 45, Aim.Rotation(), Params))
        {
            Shot->Hero = Ctx.Hero; Shot->Direction = Aim; Shot->Damage = Damage < 0 ? Ctx.Damage : Damage;
            Shot->Speed = Speed; Shot->Radius = Radius; Shot->Pierces = Pierces; Shot->Charge = 0;
            Shot->SetLifeSpan(FMath::Max(.05f, Range / FMath::Max(1.f, Speed)));
        }
    }

    FVector Dash(const FCLSkillContext& Ctx, float Distance)
    {
        if (!Ctx.Hero) return FVector::ZeroVector;
        const FVector Start = Ctx.Hero->GetActorLocation();
        FHitResult Hit; Ctx.Hero->SetActorLocation(Start + Flat(Ctx.Aim).GetSafeNormal() * Distance, true, &Hit);
        return Ctx.Hero->GetActorLocation() - Start;
    }

    void DrawCircle(const AActor* Source, const FVector& Center, float Radius, FColor Color, float Seconds)
    {
        DrawDebugCircle(Source->GetWorld(), FVector(Center.X, Center.Y, 8), Radius, 48, Color, false, Seconds, 0, 4, FVector(1, 0, 0), FVector(0, 1, 0), false);
    }

    void DrawCone(const AActor* Source, const FVector& Origin, const FVector& Aim, float Range, float HalfAngleDegrees, FColor Color, float Seconds)
    {
        const FVector O(Origin.X, Origin.Y, 8); const FVector F = Flat(Aim).GetSafeNormal();
        for (int32 I = 0; I < 16; ++I)
        {
            const FVector A = O + F.RotateAngleAxis(-HalfAngleDegrees + 2 * HalfAngleDegrees * I / 16, FVector::UpVector) * Range;
            const FVector B = O + F.RotateAngleAxis(-HalfAngleDegrees + 2 * HalfAngleDegrees * (I + 1) / 16, FVector::UpVector) * Range;
            DrawDebugLine(Source->GetWorld(), A, B, Color, false, Seconds, 0, 4);
        }
        DrawDebugLine(Source->GetWorld(), O, O + F.RotateAngleAxis(-HalfAngleDegrees, FVector::UpVector) * Range, Color, false, Seconds, 0, 3);
        DrawDebugLine(Source->GetWorld(), O, O + F.RotateAngleAxis(HalfAngleDegrees, FVector::UpVector) * Range, Color, false, Seconds, 0, 3);
    }

    void DrawLine(const AActor* Source, const FVector& Origin, const FVector& Aim, float Range, float HalfWidth, FColor Color, float Seconds)
    {
        const FVector O(Origin.X, Origin.Y, 8); const FVector F = Flat(Aim).GetSafeNormal();
        const FVector S = FVector::CrossProduct(FVector::UpVector, F) * HalfWidth;
        UWorld* W = Source->GetWorld();
        DrawDebugLine(W, O - S, O + S, Color, false, Seconds, 0, 3);
        DrawDebugLine(W, O - S, O - S + F * Range, Color, false, Seconds, 0, 4);
        DrawDebugLine(W, O + S, O + S + F * Range, Color, false, Seconds, 0, 4);
        DrawDebugLine(W, O - S + F * Range, O + S + F * Range, Color, false, Seconds, 0, 3);
    }
}
