#pragma once
// 스킬 공통 정의. 검·활 담당이 함께 쓰는 틀이며, 서버 판정으로 옮길 때도 이 수치를 그대로 쓴다.
// 기준: Docs/11.26기획안.md "스탯", "검 스킬", "활 스킬".
#include "CoreMinimal.h"

class ACLHero;
class UCLSkillComponent;

enum class ECLWeapon : uint8 { Sword, Bow };
enum class ECLStat : uint8 { Strength, Agility, Intelligence, Vitality }; // 힘 · 민첩 · 지능 · 체력
enum class ECLSkillTier : uint8 { Basic, Intermediate, Advanced };         // 초급 · 중급 · 고급

// 판정 모양. 서버에 같은 모양을 만들 수 있도록 실제 판정에 쓰는 모양을 고른다.
enum class ECLSkillShape : uint8
{
    Self,       // 자기 자신(버프·보호막)
    Circle,     // 중심 반경 Radius
    Cone,       // 전방 Range, 반각 HalfAngle
    Line,       // 전방 직선 Range, 폭 Radius(반폭)
    Projectile, // 투사체: 속도 Speed, 최대 거리 Range, 반경 Radius
    Dash,       // 시전자가 Range만큼 돌진, 경로 폭 Radius(반폭)
};

// 스킬이 실제로 효과를 낼 때(준비 시간이 끝난 순간) 받는 정보
struct FCLSkillContext
{
    ACLHero* Hero = nullptr;
    UCLSkillComponent* Skills = nullptr;
    const struct FCLSkillDef* Def = nullptr;
    FVector Origin = FVector::ZeroVector; // 시전자 위치
    FVector Aim = FVector::ForwardVector; // 카메라 기준 수평 조준 방향(길이 1)
    float Damage = 0;                     // 한 번 맞을 때의 피해 = 공격력 × 배율 × (1 + 해당 스탯 × 0.01)
};

using FCLSkillExecute = void (*)(const FCLSkillContext&);

struct FCLSkillDef
{
    FName Id;                  // 예: "Sword.Smash". 실행 옵션·저장·서버 패킷에서 쓰는 이름
    const TCHAR* DisplayName = TEXT(""); // 화면 표시 이름. 예: TEXT("강타")
    ECLWeapon Weapon = ECLWeapon::Sword;
    ECLStat Stat = ECLStat::Strength;
    ECLSkillTier Tier = ECLSkillTier::Basic;
    ECLSkillShape Shape = ECLSkillShape::Cone;
    float Range = 0;       // cm
    float Radius = 0;      // cm (원 반경·직선/돌진 반폭·투사체 반경)
    float HalfAngle = 0;   // 도 (Cone)
    float Speed = 0;       // cm/s (Projectile)
    float Multiplier = 0;  // 1타 배율. 200% → 2.0
    int32 Hits = 1;        // 타수. 120% × 3회 → Multiplier 1.2, Hits 3
    float Cooldown = 0;    // 초
    float ManaCost = 0;
    float CastTime = 0;    // 준비 시간(초). 이 시간이 지난 뒤 Execute를 부른다
    float ActionTime = .5f;// 시전 동작 시간(초). 이 동안 일반 공격·회피·다른 스킬을 막는다
    FCLSkillExecute Execute = nullptr;
    const TCHAR* Effect = TEXT(""); // 특수 효과 설명(스킬 창 표시용). 예: TEXT("적 경직 0.5초")
};

// 각 무기 파일이 자기 스킬을 등록한다. 공용 파일을 고치지 않고 스킬을 추가하기 위한 진입점이다.
void CLRegisterSwordSkills(TArray<FCLSkillDef>& Out); // CLSwordSkills.cpp
void CLRegisterBowSkills(TArray<FCLSkillDef>& Out);   // CLBowSkills.cpp
