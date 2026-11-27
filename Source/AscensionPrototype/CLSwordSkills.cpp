// 검 스킬 — 검 스킬 담당 파일.
// 기준: Docs/11.26기획안.md "검 스킬" 표. 수치는 기획안 초안이며, 기획안에 없는 판정 크기(반경·폭)는 임시값이다.
//
// 스킬 추가 방법
//   1. 아래 표에서 해당 스킬의 Execute에 함수 이름을 넣는다(nullptr이면 "아직 구현되지 않음"으로 표시된다).
//   2. 함수 안에서는 "판정(CLSkill::EnemiesIn...)" → "효과(CLSkill::Hit / Stagger ...)" → "표시(CLSkill::Draw...)" 순서로 쓴다.
//      판정과 표시를 나눠 두면 온라인에서 판정만 서버로 옮길 수 있다.
//   3. 수치는 표(FCLSkillDef)에만 두고 함수 안에 숫자를 흩지 않는다. 표에 없는 값은 이 파일 위쪽 상수로 둔다.
//   4. 시험: 실행 옵션 -AscensionSkills=Sword.Smash,Sword.Spin,None,None 으로 원하는 스킬을 Z/X/C/V 슬롯에 넣는다.
#include "CLSkillTypes.h"
#include "CLSkillLibrary.h"
#include "CLSkillComponent.h"
#include "CombatLab.h"

namespace
{
    // ---- 강타 (힘 초급): 전방 근접 단일, 적 경직 0.5초 — 구현 예시 ----
    constexpr float SmashStagger = .5f;
    void Smash(const FCLSkillContext& Ctx)
    {
        // 판정: 부채꼴 안에서 가장 가까운 한 명
        TArray<ACLEnemy*> Targets = CLSkill::EnemiesInCone(Ctx.Hero, Ctx.Origin, Ctx.Aim, Ctx.Def->Range, Ctx.Def->HalfAngle);
        Targets.Sort([&](const ACLEnemy& A, const ACLEnemy& B)
            { return FVector::DistSquared(A.GetActorLocation(), Ctx.Origin) < FVector::DistSquared(B.GetActorLocation(), Ctx.Origin); });
        // 효과
        if (Targets.Num() > 0) { CLSkill::Hit(Ctx, Targets[0], -1, 7); CLSkill::Stagger(Targets[0], SmashStagger); }
        // 표시
        CLSkill::DrawCone(Ctx.Hero, Ctx.Origin, Ctx.Aim, Ctx.Def->Range, Ctx.Def->HalfAngle, FColor(255, 170, 60));
    }
}

void CLRegisterSwordSkills(TArray<FCLSkillDef>& Out)
{
    using S = ECLStat; using T = ECLSkillTier; using P = ECLSkillShape;
    auto Add = [&](const TCHAR* Id, const TCHAR* Name, S Stat, T Tier, P Shape, float Range, float Radius, float HalfAngle,
                   float Multiplier, int32 Hits, float Cooldown, float Mana, float Cast, float Action, FCLSkillExecute Execute, const TCHAR* Effect = TEXT(""))
    {
        FCLSkillDef D; D.Id = FName(Id); D.DisplayName = Name; D.Weapon = ECLWeapon::Sword; D.Stat = Stat; D.Tier = Tier; D.Shape = Shape;
        D.Range = Range; D.Radius = Radius; D.HalfAngle = HalfAngle; D.Multiplier = Multiplier; D.Hits = Hits;
        D.Cooldown = Cooldown; D.ManaCost = Mana; D.CastTime = Cast; D.ActionTime = Action; D.Execute = Execute; D.Effect = Effect;
        Out.Add(D);
    };
    //   Id                    이름             스탯          티어            모양          거리   반경 반각  배율  타수 쿨  마나 준비 동작 실행
    Add(TEXT("Sword.Smash"),   TEXT("강타"),     S::Strength,     T::Basic,        P::Cone,       220,    0, 40,  2.0f, 1,  4,  10, 0,   .5f, Smash, TEXT("적 경직 0.5초"));
    Add(TEXT("Sword.Spin"),    TEXT("회전베기"), S::Strength,     T::Intermediate, P::Circle,       0,  300,  0,  1.2f, 3, 10,  25, 0,   .9f, nullptr, TEXT("시전 중 슈퍼아머")); // 시전 중 슈퍼아머
    Add(TEXT("Sword.Quake"),   TEXT("지진참"),   S::Strength,     T::Advanced,     P::Line,       800,  120,  0, 12.0f, 1, 40,  60, 1,  1.4f, nullptr, TEXT("검을 들어 1초 준비 후 지면 강타, 넘어뜨림 1.5초")); // 넘어뜨림 1.5초
    Add(TEXT("Sword.Quick"),   TEXT("빠른 베기"), S::Agility,     T::Basic,        P::Cone,       200,    0, 45,  .9f, 2,  2,   5, 0,   .4f, nullptr, TEXT("공격 속도의 영향을 받음")); // 공격 속도 영향
    Add(TEXT("Sword.Shadow"),  TEXT("그림자 이동"), S::Agility,   T::Intermediate, P::Dash,       600,   80,  0,  1.5f, 1,  8,  20, 0,   .3f, nullptr, TEXT("이동 중 무적 0.3초, 이동 후 3초간 치명타 +20%")); // 무적 0.3초, 3초 치명 +20%
    Add(TEXT("Sword.Thunder"), TEXT("벽력일섬"), S::Agility,      T::Advanced,     P::Dash,      1000,   90,  0, 10.0f, 1, 35,  50, 0,   .5f, nullptr, TEXT("돌진 중 무적")); // 돌진 중 무적
    Add(TEXT("Sword.Wave"),    TEXT("검기 발사"), S::Intelligence, T::Basic,       P::Projectile, 1200,  55,  0,  1.8f, 1,  3,  15, 0,   .4f, nullptr, TEXT("첫 적에게만 적중")); // 첫 적에게만
    Add(TEXT("Sword.Arcane"),  TEXT("마검 발현"), S::Intelligence, T::Intermediate, P::Self,        0,    0,  0,  0.f, 0, 30,  40, 0,   .4f, nullptr, TEXT("10초간 사거리 +50%, 평타마다 마법 피해 40% 추가")); // 10초: 사거리 +50%, 평타 추가 40%
    Add(TEXT("Sword.Excalibur"), TEXT("엑스칼리버"), S::Intelligence, T::Advanced, P::Circle,       0,  600,  0, 15.0f, 1, 60, 100, 1.5f, 1.9f, nullptr, TEXT("1.5초 준비 후 거대 빛의 검 낙하"));
    Add(TEXT("Sword.Shield"),  TEXT("보호막"),   S::Vitality,     T::Basic,        P::Self,         0,    0,  0,  0.f, 0, 15,  20, 0,   .3f, nullptr, TEXT("5초간 최대 체력 20% 피해 흡수")); // 최대 체력 20% 흡수 5초
    Add(TEXT("Sword.Thirst"),  TEXT("피의 갈증"), S::Vitality,    T::Intermediate, P::Self,         0,    0,  0,  0.f, 0, 30,  30, 0,   .4f, nullptr, TEXT("10초간 잃은 체력 10%당 공격력 +5%(최대 40%), 피흡 15%")); // 10초
    Add(TEXT("Sword.Giant"),   TEXT("거대화"),   S::Vitality,     T::Advanced,     P::Self,         0,    0,  0,  0.f, 0, 60,  50, 0,   .6f, nullptr, TEXT("15초간 크기 1.5배, 최대 체력 +30%, 받는 피해 -30%, 넉백 면역, 주변 8m 도발 3초")); // 15초, 주변 8m 도발 3초
}
