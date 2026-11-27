#pragma once
// 배틀로얄 필드 배치(캠프·풀숲·균열 입구·전투장). 서버와 같은 충돌 파일(Data/ServerCollision/CombatLab.txt)의
// "area <종류> <번호> <x> <y> <반지름>" 줄을 읽는다. 미니맵과 풀숲·균열 표시가 쓴다(판정은 서버).
#include "CoreMinimal.h"

struct FCLFieldArea
{
    FString Kind; // bush, camp_outer, camp_ruin, camp_elite, rift_gate, rift_arena
    int32 Index = 0;
    FVector2D Center = FVector2D::ZeroVector;
    float Radius = 0;
};

struct FCLField
{
    TArray<FCLFieldArea> Areas;
    static const FCLField& Get(); // 처음 부를 때 한 번 읽는다
    // 그 자리를 덮는 풀숲 번호(-1 = 없음)
    int32 BushAt(const FVector& Position) const;
    // 팀 기지(서버 room_layout.rs: 대각선 네 방향, 중심에서 160m)
    static FVector2D TeamBase(int32 Team);
    static constexpr float HalfSize = 25000.f; // 필드 반폭
};
