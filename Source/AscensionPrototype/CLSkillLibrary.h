#pragma once
// 스킬 구현에 쓰는 공통 판정·피해·표시 도우미.
// "누가 맞는가"(판정)와 "어떻게 보이는가"(표시)를 나눠 두면, 온라인에서 판정만 서버로 옮길 수 있다.
//
// 현재 한계: 1인(오프라인) 플레이의 몬스터(ACLEnemy)만 대상으로 한다. 온라인에서는 몬스터가 서버 꼭두각시라
// 로컬 피해가 무시되고, 다른 플레이어 피해도 서버 판정이 붙기 전까지 없다.
#include "CoreMinimal.h"
#include "CLSkillTypes.h"

class ACLEnemy;

namespace CLSkill
{
    // ---- 판정: 살아 있고 벽에 가리지 않은 몬스터 목록 ----
    TArray<ACLEnemy*> EnemiesInCircle(const AActor* Source, const FVector& Center, float Radius);
    TArray<ACLEnemy*> EnemiesInCone(const AActor* Source, const FVector& Origin, const FVector& Aim, float Range, float HalfAngleDegrees);
    TArray<ACLEnemy*> EnemiesInLine(const AActor* Source, const FVector& Origin, const FVector& Aim, float Range, float HalfWidth);
    // 시야 확인: From에서 To까지 고정 지형(벽·기둥)이 가리지 않는가
    bool Clear(const AActor* Source, const FVector& From, const FVector& To);

    // ---- 효과 ----
    // 피해를 주고 명중 연출(카메라 흔들림·소리·불꽃)을 낸다. Damage < 0 이면 Ctx.Damage를 쓴다.
    void Hit(const FCLSkillContext& Ctx, ACLEnemy* Target, float Damage = -1, float Feedback = 4);
    int32 HitAll(const FCLSkillContext& Ctx, const TArray<ACLEnemy*>& Targets, float Damage = -1, float Feedback = 4);
    // 경직: 몬스터가 Seconds 동안 움직이거나 공격하지 못한다(수문장은 무시)
    void Stagger(ACLEnemy* Target, float Seconds);
    // 직선 투사체(첫 적부터 Pierces명까지 맞고 사라짐, 벽에서 소멸). 현재는 검기 투사체를 재사용하는 임시 모양이다.
    void FireProjectile(const FCLSkillContext& Ctx, float Speed, float Range, float Radius, int32 Pierces, float Damage = -1);
    // 시전자를 Aim 방향으로 Distance만큼 즉시 이동(벽에서 멈춤). 돌진·순간이동용
    FVector Dash(const FCLSkillContext& Ctx, float Distance);

    // ---- 표시(개발용 선) ----
    void DrawCircle(const AActor* Source, const FVector& Center, float Radius, FColor Color, float Seconds = .35f);
    void DrawCone(const AActor* Source, const FVector& Origin, const FVector& Aim, float Range, float HalfAngleDegrees, FColor Color, float Seconds = .35f);
    void DrawLine(const AActor* Source, const FVector& Origin, const FVector& Aim, float Range, float HalfWidth, FColor Color, float Seconds = .35f);
}
