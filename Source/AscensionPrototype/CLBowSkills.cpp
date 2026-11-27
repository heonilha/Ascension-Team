// 활 스킬 — 활 스킬 담당 파일.
// 기준: Docs/11.26기획안.md "활 스킬" 표. 기획안에 상세 수치가 아직 없어, 아래 수치는 모두 임시값(0 = 미정)이다.
// 수치를 정하면 기획안의 활 스킬 표에도 적는다. 티어 기준: 초급 배율 150~250%·쿨 2~5초·마나 5~15 /
// 중급 300~500%·쿨 8~12초·마나 20~35 / 고급 1000~1500%·쿨 35~60초·마나 50~100.
//
// 스킬 추가 방법은 CLSwordSkills.cpp 맨 위 설명과 같다.
// 시험: 실행 옵션 -AscensionSkills=None,None,Bow.Rapid,Bow.Ice 로 원하는 스킬을 C/V 슬롯에 넣는다.
// 화살은 지금 검기 투사체(CLSkill::FireProjectile)를 임시로 재사용한다. 화살 모양 투사체는 활 담당이 만든다.
#include "CLSkillTypes.h"
#include "CLSkillLibrary.h"
#include "CLSkillComponent.h"
#include "CombatLab.h"

namespace
{
    // ---- 속사 (민첩 초급): 빠른 발사 — 구현 예시(임시 수치) ----
    void Rapid(const FCLSkillContext& Ctx)
    {
        CLSkill::FireProjectile(Ctx, Ctx.Def->Speed, Ctx.Def->Range, Ctx.Def->Radius, 1);
    }
}

void CLRegisterBowSkills(TArray<FCLSkillDef>& Out)
{
    using S = ECLStat; using T = ECLSkillTier; using P = ECLSkillShape;
    auto Add = [&](const TCHAR* Id, const TCHAR* Name, S Stat, T Tier, P Shape, float Range, float Radius, float Speed,
                   float Multiplier, int32 Hits, float Cooldown, float Mana, float Cast, float Action, FCLSkillExecute Execute, const TCHAR* Effect = TEXT(""))
    {
        FCLSkillDef D; D.Id = FName(Id); D.DisplayName = Name; D.Weapon = ECLWeapon::Bow; D.Stat = Stat; D.Tier = Tier; D.Shape = Shape;
        D.Range = Range; D.Radius = Radius; D.Speed = Speed; D.Multiplier = Multiplier; D.Hits = Hits;
        D.Cooldown = Cooldown; D.ManaCost = Mana; D.CastTime = Cast; D.ActionTime = Action; D.Execute = Execute; D.Effect = Effect;
        Out.Add(D);
    };
    //   Id                 이름               스탯             티어             모양            거리  반경  속도  배율 타수 쿨 마나 준비 동작 실행
    Add(TEXT("Bow.Power"),  TEXT("강궁 사격"),  S::Strength,     T::Basic,        P::Projectile,     0,   0,    0, 0.f, 1, 0, 0, 0, .5f, nullptr, TEXT("충전할수록 강해짐")); // 충전할수록 강함
    Add(TEXT("Bow.Pierce"), TEXT("꿰뚫기"),    S::Strength,     T::Intermediate, P::Line,           0,   0,    0, 0.f, 1, 0, 0, 0, .5f, nullptr, TEXT("직선상 모든 적 관통")); // 직선상 모든 적
    Add(TEXT("Bow.Giant"),  TEXT("거대 화살"),  S::Strength,     T::Advanced,     P::Projectile,     0,   0,    0, 0.f, 1, 0, 0, 0, .5f, nullptr, TEXT("넉백 + 착탄 경직")); // 넉백 + 착탄 경직
    Add(TEXT("Bow.Rapid"),  TEXT("속사"),      S::Agility,      T::Basic,        P::Projectile,  1500,  30, 2600, 1.5f, 1, 2, 5, 0, .3f, Rapid, TEXT("빠른 발사"));
    Add(TEXT("Bow.Multi"),  TEXT("다연발"),    S::Agility,      T::Intermediate, P::Cone,           0,   0,    0, 0.f, 1, 0, 0, 0, .5f, nullptr, TEXT("범위 연속 공격")); // 범위 연속 공격
    Add(TEXT("Bow.Shadow"), TEXT("그림자 사격"), S::Agility,     T::Advanced,     P::Projectile,     0,   0,    0, 0.f, 1, 0, 0, 0, .5f, nullptr, TEXT("동작 미정")); // 동작 미정
    Add(TEXT("Bow.Ice"),    TEXT("얼음 화살"),  S::Intelligence, T::Basic,        P::Projectile,     0,   0,    0, 0.f, 1, 0, 0, 0, .5f, nullptr, TEXT("맞은 적 슬로우")); // 슬로우
    Add(TEXT("Bow.Homing"), TEXT("추적 화살"),  S::Intelligence, T::Intermediate, P::Projectile,     0,   0,    0, 0.f, 1, 0, 0, 0, .5f, nullptr, TEXT("대상을 따라감"));
    Add(TEXT("Bow.Storm"),  TEXT("마력 폭풍"),  S::Intelligence, T::Advanced,     P::Circle,         0,   0,    0, 0.f, 1, 0, 0, 0, .5f, nullptr, TEXT("광역 마력 폭발")); // 광역
    // 체력 계열은 기획안에서 재검토 중(현재 세트 vs "흔들리지 않는 사수" 세트). 확정되면 바꾼다.
    Add(TEXT("Bow.Shield"), TEXT("보호막(활)"), S::Vitality,     T::Basic,        P::Self,           0,   0,    0, 0.f, 0, 0, 0, 0, .3f, nullptr, TEXT("피해 흡수(체력 계열 재검토 중)"));
    Add(TEXT("Bow.Bind"),   TEXT("속박 화살"),  S::Vitality,     T::Intermediate, P::Projectile,     0,   0,    0, 0.f, 1, 0, 0, 0, .5f, nullptr, TEXT("맞은 적 속박"));
    Add(TEXT("Bow.Heal"),   TEXT("치유의 비"),  S::Vitality,     T::Advanced,     P::Circle,         0,   0,    0, 0.f, 0, 0, 0, 0, .5f, nullptr, TEXT("범위 안 아군 치유"));
}
