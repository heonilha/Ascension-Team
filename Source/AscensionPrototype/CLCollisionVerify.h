// 서버 충돌 지형 파일(Data/ServerCollision/CombatLab.txt)이 실제 게임 충돌과 같은지 확인한다.
// -AscensionCollisionVerify 옵션으로 실행했을 때만 쓴다.
#pragma once

#include "CoreMinimal.h"

class UWorld;

// 놀이 영역 40cm 격자마다 주인공 캡슐(반지름 32, 반높이 88)의 실제 겹침 결과와 파일의 다각형 판정을 비교한다.
// 결과를 ASCENSION_COLLISION_VERIFY 줄로 남기고 일치하면 true.
bool AscensionVerifyServerCollision(UWorld* World);
