// 배틀로얄 HUD와 스킬 창(K). ACLHUD::DrawHUD(CombatLab.cpp)가 부른다.
//
// 화면 구성
// - 아래 가운데: 레벨 배지 · 체력 · 마나 · 경험치 / 그 위 스킬 바(무기 + Z X C V)와 남은 포인트 알림
// - 위 가운데: 매치 진행(단계·남은 시간·자기장·남은 팀)과 균열 상태, 가운데 큰 안내(카운트다운·부활·승리)
// - 오른쪽 위: 미니맵(자기장·팀 기지·캠프·풀숲·균열 입구·나와 아군). 균열 안에서는 전투장
// - 스킬 창: 왼쪽 스탯 분배, 가운데 검/활 스킬 표(스탯 × 티어), 오른쪽 고른 스킬 설명과 배우기·강화·각성·지우기
// 그리기는 Canvas(AHUD) 사각형·글자만 쓴다. 판정·포인트는 UCLSkillComponent(오프라인)와 서버(온라인)가 정한다.
#include "CombatLab.h"
#include "CLSkillComponent.h"
#include "CLNetSession.h"
#include "CLRemoteHero.h"
#include "CLField.h"
#include "Engine/Canvas.h"
#include "GameFramework/PlayerController.h"

namespace
{
    const FLinearColor BgDeep(.025f, .03f, .05f, .9f);
    const FLinearColor BgPanel(.05f, .06f, .09f, .82f);
    const FLinearColor BgCard(.09f, .1f, .14f, .95f);
    const FLinearColor Edge(.32f, .38f, .52f, .7f);
    const FLinearColor Gold(1.f, .78f, .35f);
    const FLinearColor TextMain(.93f, .95f, .99f);
    const FLinearColor TextDim(.6f, .65f, .75f);
    const FLinearColor Good(.45f, .95f, .6f);
    const FLinearColor Bad(1.f, .45f, .4f);
    const FLinearColor ManaBlue(.3f, .55f, 1.f);

    FLinearColor StatColor(ECLStat S)
    {
        switch (S)
        {
        case ECLStat::Strength: return FLinearColor(.95f, .45f, .3f);
        case ECLStat::Agility: return FLinearColor(.4f, .9f, .5f);
        case ECLStat::Intelligence: return FLinearColor(.45f, .62f, 1.f);
        default: return FLinearColor(.95f, .8f, .35f);
        }
    }
    FLinearColor TeamColor(int32 Team)
    {
        switch (Team)
        {
        case 1: return FLinearColor(.92f, .28f, .22f);
        case 2: return FLinearColor(.28f, .52f, 1.f);
        case 3: return FLinearColor(.3f, .85f, .38f);
        default: return FLinearColor(.95f, .8f, .2f);
        }
    }
    const TCHAR* StatName(ECLStat S)
    {
        switch (S) { case ECLStat::Strength: return TEXT("힘"); case ECLStat::Agility: return TEXT("민첩"); case ECLStat::Intelligence: return TEXT("지능"); default: return TEXT("체력"); }
    }
    const TCHAR* StatConcept(ECLStat S)
    {
        switch (S) { case ECLStat::Strength: return TEXT("물리 · 한 방"); case ECLStat::Agility: return TEXT("빠름 · 공속·치명"); case ECLStat::Intelligence: return TEXT("마법 · 마나"); default: return TEXT("탱커 · 체력·방어"); }
    }
    const TCHAR* StatEffect(ECLStat S)
    {
        switch (S)
        {
        case ECLStat::Strength: return TEXT("포인트당 공격력 +2, 치명타 피해 +1%");
        case ECLStat::Agility: return TEXT("공격 속도 +1.5%, 치명 +0.5%, 이동 +0.2%");
        case ECLStat::Intelligence: return TEXT("최대 마나 +5, 마나 회복 +1%, 쿨감 +0.3%");
        default: return TEXT("최대 체력 +20, 방어력 +1, 넉백 저항 +1%");
        }
    }
    const TCHAR* TierName(ECLSkillTier T) { return T == ECLSkillTier::Basic ? TEXT("초급") : T == ECLSkillTier::Intermediate ? TEXT("중급") : TEXT("고급"); }
    const TCHAR* RomanRank(int32 Rank) { return Rank >= 3 ? TEXT("III") : Rank == 2 ? TEXT("II") : TEXT("I"); }

    FString ShapeText(const FCLSkillDef& D)
    {
        switch (D.Shape)
        {
        case ECLSkillShape::Self: return TEXT("자기 자신");
        case ECLSkillShape::Circle: return FString::Printf(TEXT("주변 반경 %.1fm"), D.Radius / 100.f);
        case ECLSkillShape::Cone: return FString::Printf(TEXT("전방 부채꼴 %.1fm"), D.Range / 100.f);
        case ECLSkillShape::Line: return FString::Printf(TEXT("전방 직선 %.0fm"), D.Range / 100.f);
        case ECLSkillShape::Projectile: return D.Range > 0 ? FString::Printf(TEXT("투사체 %.0fm"), D.Range / 100.f) : FString(TEXT("투사체"));
        case ECLSkillShape::Dash: return FString::Printf(TEXT("전방 %.0fm 돌진"), D.Range / 100.f);
        }
        return FString();
    }
    FString Clock(double Seconds)
    {
        const int32 L = FMath::Max(0, FMath::CeilToInt(Seconds));
        return FString::Printf(TEXT("%d:%02d"), L / 60, L % 60);
    }
}

// ---- 그리기 도우미 ----

void ACLHUD::Panel(float X, float Y, float W, float H, const FLinearColor& Fill, const FLinearColor& EdgeColor)
{
    DrawRect(Fill, X, Y, W, H);
    const float T = FMath::Max(1.f, UI);
    DrawRect(EdgeColor, X, Y, W, T); DrawRect(EdgeColor, X, Y + H - T, W, T);
    DrawRect(EdgeColor, X, Y, T, H); DrawRect(EdgeColor, X + W - T, Y, T, H);
    DrawRect(FLinearColor(1, 1, 1, .04f), X + T, Y + T, W - 2 * T, FMath::Min(H * .35f, 18.f * UI)); // 위쪽 은은한 빛
}

void ACLHUD::Bar(float X, float Y, float W, float H, float Ratio, const FLinearColor& Fill, const FLinearColor& Back)
{
    DrawRect(Back, X, Y, W, H);
    const float Filled = W * FMath::Clamp(Ratio, 0.f, 1.f);
    DrawRect(Fill, X, Y, Filled, H);
    DrawRect(FLinearColor(1, 1, 1, .18f), X, Y, Filled, FMath::Max(1.f, H * .3f));
}

float ACLHUD::TextWidth(const FString& S, float Size)
{
    float W = 0, H = 0;
    GetTextSize(S, W, H, nullptr, Size * UI);
    return W;
}

void ACLHUD::Text(const FString& S, float X, float Y, const FLinearColor& C, float Size, int32 Align)
{
    if (S.IsEmpty()) return;
    const float W = Align ? TextWidth(S, Size) : 0;
    const float DX = Align == 1 ? -W * .5f : Align == 2 ? -W : 0;
    DrawText(S, FLinearColor(0, 0, 0, C.A * .7f), X + DX + UI, Y + UI, nullptr, Size * UI); // 그림자
    DrawText(S, C, X + DX, Y, nullptr, Size * UI);
}

bool ACLHUD::Hover(float X, float Y, float W, float H) const
{
    return MouseX >= X && MouseX <= X + W && MouseY >= Y && MouseY <= Y + H;
}

bool ACLHUD::Button(const FString& Label, float X, float Y, float W, float H, bool bEnabled, const FLinearColor& Accent)
{
    const bool bHover = bEnabled && Hover(X, Y, W, H);
    const FLinearColor Fill = !bEnabled ? FLinearColor(.08f, .08f, .1f, .9f) : bHover ? Accent * FLinearColor(.55f, .55f, .55f, 1) : Accent * FLinearColor(.32f, .32f, .32f, 1);
    Panel(X, Y, W, H, FLinearColor(Fill.R, Fill.G, Fill.B, .95f), bEnabled ? Accent : FLinearColor(.25f, .26f, .3f));
    Text(Label, X + W * .5f, Y + (H - 16 * UI) * .5f, bEnabled ? TextMain : TextDim, 1.f, 1);
    return bHover && bClick;
}

// ---- 전체 ----

void ACLHUD::DrawBattleHud(ACLHero* P, AAscensionMode* Mode)
{
    UI = FMath::Clamp(Canvas->SizeY / 720.f, .85f, 1.6f);
    MouseX = MouseY = -1; bClick = false;
    if (APlayerController* PC = GetOwningPlayerController())
    {
        if (P->bSkillWindowOpen) PC->GetMousePosition(MouseX, MouseY);
        bClick = P->bSkillWindowOpen && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton);
    }
    ACLNetSession* Net = Mode && IsValid(Mode->Net) ? Mode->Net.Get() : nullptr;
    DrawVitals(P, Net);
    DrawSkillBar(P);
    DrawMatchPanel(P, Net);
    DrawMinimap(P, Net);
}

void ACLHUD::DrawVitals(ACLHero* P, ACLNetSession* Net)
{
    UCLSkillComponent* S = P->Skills;
    const float W = 470 * UI, H = 76 * UI, X = (Canvas->SizeX - W) * .5f, Y = Canvas->SizeY - H - 12 * UI;
    Panel(X, Y, W, H, BgPanel, Edge);
    // 레벨 배지
    const bool bOnline = Net && Net->MyProgress.bValid;
    const int32 Level = bOnline ? Net->MyProgress.Level : P->Level;
    Panel(X + 8 * UI, Y + 8 * UI, 58 * UI, 60 * UI, FLinearColor(.12f, .1f, .05f, .95f), Gold);
    Text(TEXT("LV"), X + 37 * UI, Y + 12 * UI, Gold, .8f, 1);
    Text(FString::FromInt(Level), X + 37 * UI, Y + 29 * UI, TextMain, 1.6f, 1);
    // 체력
    const float BX = X + 72 * UI, BW = W - 82 * UI;
    const float Health = FMath::Clamp(P->HP / 100.f, 0.f, 1.f);
    const bool bHurt = P->GetWorld()->GetTimeSeconds() < P->HurtUntil;
    Bar(BX, Y + 10 * UI, BW, 20 * UI, Health, bHurt ? FLinearColor::White : Health <= .3f ? FLinearColor(.9f, .15f, .12f) : FLinearColor(.16f, .78f, .55f), FLinearColor(.14f, .15f, .18f));
    Text(FString::Printf(TEXT("%.0f / 100"), P->HP), BX + BW * .5f, Y + 11 * UI, TextMain, .95f, 1);
    // 마나
    if (S)
    {
        Bar(BX, Y + 33 * UI, BW, 9 * UI, S->Mana / FMath::Max(1.f, S->MaxMana()), ManaBlue, FLinearColor(.08f, .1f, .2f));
        Text(FString::Printf(TEXT("마나 %.0f / %.0f"), S->Mana, S->MaxMana()), BX + BW, Y + 55 * UI, FLinearColor(.75f, .85f, 1.f), .78f, 2);
    }
    // 경험치
    float XpRatio = 0; FString XpText;
    if (bOnline)
    {
        const auto& Pr = Net->MyProgress;
        XpRatio = Pr.XpToNext ? float(Pr.Xp) / Pr.XpToNext : 1.f;
        XpText = Pr.XpToNext ? FString::Printf(TEXT("경험치 %u / %u"), Pr.Xp, Pr.XpToNext) : FString(TEXT("최대 레벨"));
    }
    else
    {
        XpRatio = float(P->Experience) / FMath::Max(1, P->ExperienceRequired());
        XpText = FString::Printf(TEXT("경험치 %d / %d"), P->Experience, P->ExperienceRequired());
    }
    Bar(BX, Y + 46 * UI, BW, 6 * UI, XpRatio, Gold, FLinearColor(.16f, .13f, .07f));
    Text(XpText, BX, Y + 55 * UI, FLinearColor(1.f, .85f, .55f), .78f);
}

void ACLHUD::DrawSkillBar(ACLHero* P)
{
    UCLSkillComponent* S = P->Skills;
    if (!S) return;
    const float Size = 60 * UI, Gap = 8 * UI;
    const float Total = Size * 5 + Gap * 4 + 10 * UI;
    const float X0 = (Canvas->SizeX - Total) * .5f, Y = Canvas->SizeY - 88 * UI - Size - 24 * UI;
    const double Now = P->GetWorld()->GetTimeSeconds();

    // 무기(F 교체)
    const bool bSword = S->Weapon == ECLWeapon::Sword;
    Panel(X0, Y, Size, Size, BgDeep, Gold);
    Text(bSword ? TEXT("검") : TEXT("활"), X0 + Size * .5f, Y + 10 * UI, Gold, 1.5f, 1);
    Text(TEXT("F 교체"), X0 + Size * .5f, Y + 40 * UI, TextDim, .75f, 1);

    const TCHAR* Keys[] = {TEXT("Z"), TEXT("X"), TEXT("C"), TEXT("V")};
    for (int32 I = 0; I < UCLSkillComponent::SlotCount; ++I)
    {
        const float SX = X0 + Size + 10 * UI + I * (Size + Gap);
        const FCLSkillDef* Def = S->Slots[I];
        if (!Def)
        {
            Panel(SX, Y, Size, Size, FLinearColor(.04f, .045f, .06f, .7f), FLinearColor(.22f, .24f, .3f, .8f));
            Text(Keys[I], SX + 5 * UI, Y + 3 * UI, TextDim, .8f);
            Text(TEXT("+"), SX + Size * .5f, Y + 14 * UI, TextDim, 1.6f, 1);
            Text(TEXT("K"), SX + Size * .5f, Y + 42 * UI, FLinearColor(.45f, .5f, .6f), .7f, 1);
            continue;
        }
        const FLinearColor C = StatColor(Def->Stat);
        const double Left = S->CooldownLeft(I);
        const bool bReady = Left <= 0 && S->Mana >= Def->ManaCost && Def->Execute;
        Panel(SX, Y, Size, Size, FLinearColor(C.R * .22f, C.G * .22f, C.B * .22f, .95f), bReady ? C : FLinearColor(.3f, .3f, .35f));
        // 티어 표시(왼쪽 위 띠)와 이름 첫 글자 크게
        DrawRect(C, SX, Y, Size, 3 * UI);
        Text(FString(Def->DisplayName).Left(2), SX + Size * .5f, Y + 15 * UI, TextMain, 1.25f, 1);
        Text(Keys[I], SX + 5 * UI, Y + 4 * UI, Gold, .8f);
        const int32 Rank = S->RankOf(*Def);
        if (Rank > 0) Text(RomanRank(Rank), SX + Size - 5 * UI, Y + 4 * UI, TextDim, .7f, 2);
        if (!Def->Execute) { DrawRect(FLinearColor(0, 0, 0, .55f), SX, Y, Size, Size); Text(TEXT("준비 중"), SX + Size * .5f, Y + 40 * UI, TextDim, .7f, 1); }
        else if (Left > 0)
        {
            const float Cd = FMath::Max(.01f, S->SkillCooldown(*Def));
            DrawRect(FLinearColor(0, 0, 0, .62f), SX, Y, Size, Size * FMath::Clamp(float(Left / Cd), 0.f, 1.f));
            Text(FString::Printf(TEXT("%.1f"), Left), SX + Size * .5f, Y + 36 * UI, TextMain, 1.05f, 1);
        }
        else Text(FString::Printf(TEXT("%.0f"), Def->ManaCost), SX + Size * .5f, Y + 40 * UI, S->Mana >= Def->ManaCost ? FLinearColor(.7f, .82f, 1.f) : Bad, .75f, 1);
        Text(Def->DisplayName, SX + Size * .5f, Y + Size + 2 * UI, FLinearColor(TextMain.R, TextMain.G, TextMain.B, .85f), .7f, 1);
    }
    // 남은 포인트 알림
    const int32 Points = S->SkillPointsLeft + S->StatPointsLeft + S->AwakeningPointsLeft;
    if (Points > 0 && !P->bSkillWindowOpen)
    {
        FString Line = TEXT("K  스킬 창");
        if (S->SkillPointsLeft) Line += FString::Printf(TEXT("  ·  스킬 포인트 %d"), S->SkillPointsLeft);
        if (S->StatPointsLeft) Line += FString::Printf(TEXT("  ·  스탯 포인트 %d"), S->StatPointsLeft);
        if (S->AwakeningPointsLeft) Line += FString::Printf(TEXT("  ·  각성 %d"), S->AwakeningPointsLeft);
        const float LW = TextWidth(Line, .85f) + 24 * UI;
        const float Pulse = .65f + .35f * FMath::Sin(float(Now * 4));
        Panel((Canvas->SizeX - LW) * .5f, Y - 32 * UI, LW, 24 * UI, FLinearColor(.16f, .12f, .03f, .9f), Gold * FLinearColor(1, 1, 1, Pulse));
        Text(Line, Canvas->SizeX * .5f, Y - 28 * UI, Gold, .85f, 1);
    }
    if (Now < S->LastMessageUntil) Text(S->LastMessage, Canvas->SizeX * .5f, Y - 56 * UI, FLinearColor(1, .88f, .5f), .95f, 1);
}

void ACLHUD::DrawMatchPanel(ACLHero* P, ACLNetSession* Net)
{
    if (Net) Text(Net->StatusText(), 14 * UI, Canvas->SizeY - 22 * UI, FLinearColor(.5f, .7f, .9f, .8f), .75f);
    if (!Net || !Net->InMatch()) return;
    const FString Line = Net->MatchText();
    // MatchText: "매치 · <단계> · 자기장 …" → 큰 줄(단계)과 작은 줄(나머지)로 나눠 보여 준다
    TArray<FString> Parts; Line.ParseIntoArray(Parts, TEXT(" · "));
    const FString Head = Parts.Num() > 1 ? Parts[1] : Line;
    FString Rest; for (int32 I = 2; I < Parts.Num(); ++I) Rest += (I > 2 ? TEXT("  ·  ") : TEXT("")) + Parts[I];
    const bool bOutside = Net->IsOutsideZone(P->GetActorLocation()) && !Net->bInRift;
    const float W = FMath::Max(TextWidth(Head, 1.3f), TextWidth(Rest, .85f)) + 40 * UI, H = 56 * UI;
    const float X = (Canvas->SizeX - W) * .5f, Y = 12 * UI;
    Panel(X, Y, W, H, BgPanel, bOutside ? Bad : Edge);
    Text(Head, Canvas->SizeX * .5f, Y + 6 * UI, bOutside ? Bad : TextMain, 1.3f, 1);
    Text(Rest, Canvas->SizeX * .5f, Y + 34 * UI, TextDim, .85f, 1);
    const FString RiftLine = Net->RiftText();
    if (!RiftLine.IsEmpty())
    {
        const float RW = TextWidth(RiftLine, .85f) + 28 * UI;
        Panel((Canvas->SizeX - RW) * .5f, Y + H + 6 * UI, RW, 24 * UI, FLinearColor(.12f, .04f, .18f, .88f), FLinearColor(.7f, .4f, 1.f, .9f));
        Text(RiftLine, Canvas->SizeX * .5f, Y + H + 10 * UI, FLinearColor(.88f, .75f, 1.f), .85f, 1);
    }
    const FString Banner = Net->MatchBanner();
    if (!Banner.IsEmpty())
    {
        const float BW = TextWidth(Banner, 2.f) + 60 * UI, BY = Canvas->SizeY * .3f;
        Panel((Canvas->SizeX - BW) * .5f, BY - 10 * UI, BW, 56 * UI, FLinearColor(0, 0, 0, .6f), Gold * FLinearColor(1, 1, 1, .6f));
        Text(Banner, Canvas->SizeX * .5f, BY, FLinearColor(1, .86f, .4f), 2.f, 1);
    }
    // 풀숲 안: 나는 숨어 있다
    if (!Net->bInRift && FCLField::Get().BushAt(P->GetActorLocation()) >= 0)
    {
        const FString Hide = TEXT("풀숲 속 · 다른 팀에게 보이지 않음 (공격하면 잠깐 드러남)");
        const float HW = TextWidth(Hide, .85f) + 24 * UI;
        Panel((Canvas->SizeX - HW) * .5f, Canvas->SizeY - 210 * UI, HW, 24 * UI, FLinearColor(.03f, .12f, .05f, .85f), FLinearColor(.35f, .85f, .45f, .9f));
        Text(Hide, Canvas->SizeX * .5f, Canvas->SizeY - 206 * UI, FLinearColor(.7f, 1.f, .75f), .85f, 1);
    }
}

void ACLHUD::DrawMinimap(ACLHero* P, ACLNetSession* Net)
{
    const float Size = 210 * UI, X = Canvas->SizeX - Size - 14 * UI, Y = 12 * UI;
    const bool bArena = Net && Net->bInRift && Net->Rift.bValid;
    const FVector2D Center = bArena ? Net->Rift.Arena : FVector2D::ZeroVector;
    const float Half = bArena ? Net->Rift.ArenaRadius * 1.15f : FCLField::HalfSize;
    Panel(X - 4 * UI, Y - 4 * UI, Size + 8 * UI, Size + 24 * UI, BgPanel, Edge);
    DrawRect(FLinearColor(.06f, .1f, .07f, .95f), X, Y, Size, Size);
    // 월드 X(앞) → 화면 위, 월드 Y(오른쪽) → 화면 오른쪽
    const auto Map = [&](const FVector2D& W) { return FVector2D(X + (W.Y - Center.Y + Half) / (2 * Half) * Size, Y + (Half - (W.X - Center.X)) / (2 * Half) * Size); };
    const auto Inside = [&](const FVector2D& M) { return M.X >= X && M.X <= X + Size && M.Y >= Y && M.Y <= Y + Size; };
    const auto Circle = [&](const FVector2D& W, float R, const FLinearColor& C, float Thick)
    {
        const int32 N = 48; FVector2D Last = Map(W + FVector2D(R, 0));
        for (int32 I = 1; I <= N; ++I)
        {
            const float A = I * 2 * PI / N; const FVector2D Next = Map(W + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
            if (Inside(Last) && Inside(Next)) DrawLine(Last.X, Last.Y, Next.X, Next.Y, C, Thick);
            Last = Next;
        }
    };
    const auto Dot = [&](const FVector2D& W, float R, const FLinearColor& C) { const FVector2D M = Map(W); if (Inside(M)) DrawRect(C, M.X - R, M.Y - R, 2 * R, 2 * R); };
    if (!bArena)
    {
        for (const FCLFieldArea& A : FCLField::Get().Areas)
        {
            if (A.Kind == TEXT("bush")) Dot(A.Center, FMath::Max(2.f, A.Radius / Half * Size * .5f), FLinearColor(.18f, .5f, .2f, .9f));
            else if (A.Kind.StartsWith(TEXT("camp"))) Dot(A.Center, 3 * UI, A.Kind == TEXT("camp_elite") ? Gold : FLinearColor(.75f, .5f, .3f));
        }
        for (int32 Team = 1; Team <= 4; ++Team)
        {
            const FLinearColor C = TeamColor(Team);
            Dot(FCLField::TeamBase(Team), (Net && Team == Net->MyTeam ? 6.f : 4.f) * UI, C);
        }
        if (Net && Net->Rift.bValid && Net->Rift.Phase >= 1 && Net->Rift.Phase <= 3)
            for (int32 I = 0; I < 2; ++I) Dot(Net->Rift.Gate[I], 4 * UI, FLinearColor(.8f, .4f, 1.f));
        if (Net && Net->InMatch())
        {
            Circle(Net->ZoneCenter(), Net->ZoneRadiusNow(), FLinearColor(.35f, .6f, 1.f), 2 * UI);
        }
    }
    else
    {
        Circle(Center, Net->Rift.ArenaRadius, FLinearColor(.7f, .4f, 1.f), 2 * UI);
        if (Net->Rift.Phase == 3) Circle(Center, Net->RiftZoneRadiusNow(), FLinearColor(.95f, .5f, 1.f), 2 * UI);
    }
    // 다른 플레이어: 아군은 항상, 적은 30m 안에서 보일 때만
    if (Net)
        for (const auto& Pair : Net->RemoteHeroes())
        {
            const ACLRemoteHero* R = Pair.Value;
            if (!IsValid(R) || R->bHiddenInBush) continue;
            const FVector2D W(R->GetActorLocation());
            if (!R->bAlly && FVector2D::Distance(W, FVector2D(P->GetActorLocation())) > 3000) continue;
            Dot(W, 3.5f * UI, R->bAlly ? FLinearColor(.4f, 1.f, .55f) : FLinearColor(1.f, .3f, .25f));
        }
    // 나: 바라보는 방향 표시
    const FVector2D Me = Map(FVector2D(P->GetActorLocation()));
    const float Yaw = FMath::DegreesToRadians(P->GetControlRotation().Yaw);
    const FVector2D Dir(FMath::Sin(Yaw), -FMath::Cos(Yaw));
    DrawLine(Me.X, Me.Y, Me.X + Dir.X * 11 * UI, Me.Y + Dir.Y * 11 * UI, FLinearColor::White, 2 * UI);
    DrawRect(FLinearColor::White, Me.X - 3 * UI, Me.Y - 3 * UI, 6 * UI, 6 * UI);
    Text(bArena ? TEXT("균열 전투장") : TEXT("필드 100m"), X + 4 * UI, Y + Size + 2 * UI, TextDim, .72f);
    if (Net && Net->InMatch()) Text(FString::Printf(TEXT("팀 %d"), Net->MyTeam), X + Size - 4 * UI, Y + Size + 2 * UI, TeamColor(Net->MyTeam), .72f, 2);
}

// ---- 스킬 창 ----

void ACLHUD::DrawSkillWindow(ACLHero* P)
{
    UCLSkillComponent* S = P->Skills;
    if (!S) return;
    // 화면이 작으면 창 배율을 줄인다
    const float Base = UI;
    UI = FMath::Min(UI, FMath::Min((Canvas->SizeX - 20.f) / 1060.f, (Canvas->SizeY - 20.f) / 620.f));
    const float W = 1060 * UI, H = 620 * UI, X = (Canvas->SizeX - W) * .5f, Y = (Canvas->SizeY - H) * .5f;
    DrawRect(FLinearColor(0, 0, 0, .45f), 0, 0, Canvas->SizeX, Canvas->SizeY);
    Panel(X, Y, W, H, BgDeep, Gold * FLinearColor(1, 1, 1, .7f));

    // 머리줄: 제목·포인트·닫기
    Text(TEXT("스킬 · 스탯"), X + 22 * UI, Y + 14 * UI, TextMain, 1.55f);
    Text(FString::Printf(TEXT("Lv %d  ·  스킬은 배운 순서대로 Z X C V  ·  검·활 합쳐 4개"), S->Level), X + 24 * UI, Y + 44 * UI, TextDim, .8f);
    float ChipX = X + W - 60 * UI;
    const auto Chip = [&](const FString& Label, int32 Value, const FLinearColor& C)
    {
        const FString L = FString::Printf(TEXT("%s  %d"), *Label, Value);
        const float CW = TextWidth(L, .9f) + 22 * UI;
        ChipX -= CW + 8 * UI;
        Panel(ChipX, Y + 16 * UI, CW, 28 * UI, FLinearColor(C.R * .18f, C.G * .18f, C.B * .18f, .95f), Value > 0 ? C : FLinearColor(.3f, .3f, .35f));
        Text(L, ChipX + CW * .5f, Y + 21 * UI, Value > 0 ? C : TextDim, .9f, 1);
    };
    Chip(TEXT("각성 포인트"), S->AwakeningPointsLeft, FLinearColor(1.f, .55f, .9f));
    Chip(TEXT("스탯 포인트"), S->StatPointsLeft, Good);
    Chip(TEXT("스킬 포인트"), S->SkillPointsLeft, Gold);
    if (Button(TEXT("X"), X + W - 48 * UI, Y + 14 * UI, 32 * UI, 32 * UI, true, Bad)) P->SetSkillWindowOpen(false);

    // ---- 왼쪽: 스탯 ----
    const float SX = X + 18 * UI, SY = Y + 74 * UI, SW = 254 * UI, SH = 112 * UI;
    for (int32 I = 0; I < 4; ++I)
    {
        const ECLStat Stat = ECLStat(I);
        const FLinearColor C = StatColor(Stat);
        const float CY = SY + I * (SH + 8 * UI);
        Panel(SX, CY, SW, SH, BgCard, FLinearColor(C.R, C.G, C.B, .55f));
        DrawRect(C, SX, CY, 4 * UI, SH);
        Text(StatName(Stat), SX + 14 * UI, CY + 8 * UI, C, 1.3f);
        Text(StatConcept(Stat), SX + 14 * UI + TextWidth(StatName(Stat), 1.3f) + 10 * UI, CY + 14 * UI, TextDim, .78f);
        Text(FString::FromInt(S->Stats[I]), SX + SW - 14 * UI, CY + 4 * UI, TextMain, 1.8f, 2);
        Text(StatEffect(Stat), SX + 14 * UI, CY + 42 * UI, FLinearColor(.78f, .82f, .9f), .72f);
        FString Now;
        if (Stat == ECLStat::Strength) Now = FString::Printf(TEXT("공격력 %.0f"), S->AttackPower());
        else if (Stat == ECLStat::Intelligence) Now = FString::Printf(TEXT("최대 마나 %.0f · 초당 %.1f"), S->MaxMana(), S->ManaRegen());
        else Now = TEXT("효과는 서버 판정 연결 뒤 적용");
        const int32 Next = S->Stats[I] < 15 ? 15 : S->Stats[I] < 25 ? 25 : 0;
        Text(Next ? FString::Printf(TEXT("%s · %d에서 %s 해금"), *Now, Next, Next == 15 ? TEXT("중급") : TEXT("고급")) : Now, SX + 14 * UI, CY + 62 * UI, TextDim, .72f);
        if (Button(TEXT("+1"), SX + 14 * UI, CY + 80 * UI, 56 * UI, 24 * UI, S->StatPointsLeft >= 1, C)) S->RequestStat(Stat, 1);
        if (Button(TEXT("+5"), SX + 78 * UI, CY + 80 * UI, 56 * UI, 24 * UI, S->StatPointsLeft >= 5, C)) S->RequestStat(Stat, 5);
    }

    // ---- 가운데: 검·활 스킬 표 ----
    const float GX = X + 290 * UI, GY = Y + 74 * UI;
    for (int32 Tab = 0; Tab < 2; ++Tab)
    {
        // 탭: 고른 쪽은 금색으로 채우고, 다른 쪽은 어둡게(마우스를 올리면 밝게)
        const float TX = GX + Tab * 128 * UI, TW = 120 * UI, TH = 32 * UI;
        const bool bActive = SkillTab == Tab, bOver = Hover(TX, GY, TW, TH);
        Panel(TX, GY, TW, TH, bActive ? FLinearColor(.32f, .24f, .08f, .97f) : bOver ? FLinearColor(.14f, .15f, .2f, .95f) : BgCard, bActive ? Gold : Edge);
        Text(Tab == 0 ? TEXT("검") : TEXT("활"), TX + TW * .5f, GY + 7 * UI, bActive ? Gold : TextDim, 1.05f, 1);
        if (bOver && bClick) SkillTab = Tab;
    }
    Text(SkillTab == 0 ? TEXT("근접 · 버티며 싸운다") : TEXT("원거리 · 거리를 두고 견제"), GX + 262 * UI, GY + 8 * UI, TextDim, .8f);
    const float CW = 168 * UI, CH = 98 * UI, Gap = 8 * UI, TableY = GY + 70 * UI;
    const TCHAR* TierHead[] = {TEXT("초급 · 스탯 5"), TEXT("중급 · 스탯 15"), TEXT("고급 · 스탯 25 · Lv12")};
    for (int32 T = 0; T < 3; ++T) Text(TierHead[T], GX + T * (CW + Gap) + CW * .5f, GY + 46 * UI, T == 2 ? Gold : TextDim, .78f, 1);
    const ECLWeapon Weapon = SkillTab == 0 ? ECLWeapon::Sword : ECLWeapon::Bow;
    for (const FCLSkillDef& Def : UCLSkillComponent::AllSkills())
    {
        if (Def.Weapon != Weapon) continue;
        const float CX = GX + int32(Def.Tier) * (CW + Gap), CY = TableY + int32(Def.Stat) * (CH + Gap);
        const FLinearColor C = StatColor(Def.Stat);
        const int32 Rank = S->RankOf(Def);
        const UCLSkillComponent::FLearned* L = S->FindLearned(Def);
        const bool bUnlocked = S->IsUnlocked(Def), bSelected = SelectedSkill == &Def, bHover = Hover(CX, CY, CW, CH);
        const FLinearColor Fill = Rank > 0 ? FLinearColor(C.R * .2f, C.G * .2f, C.B * .2f, .97f) : bHover ? FLinearColor(.14f, .15f, .2f, .97f) : BgCard;
        Panel(CX, CY, CW, CH, Fill, bSelected ? Gold : Rank > 0 ? C : FLinearColor(.24f, .26f, .32f, .8f));
        if (bSelected) Panel(CX + 2 * UI, CY + 2 * UI, CW - 4 * UI, CH - 4 * UI, FLinearColor(0, 0, 0, 0), Gold * FLinearColor(1, 1, 1, .5f));
        DrawRect(bUnlocked || Rank ? C : FLinearColor(.3f, .3f, .33f), CX, CY, 4 * UI, CH);
        Text(Def.DisplayName, CX + 12 * UI, CY + 8 * UI, bUnlocked || Rank ? TextMain : TextDim, 1.02f);
        if (!Def.Execute) { Panel(CX + CW - 52 * UI, CY + 8 * UI, 44 * UI, 18 * UI, FLinearColor(.12f, .12f, .14f), FLinearColor(.35f, .35f, .4f)); Text(TEXT("준비 중"), CX + CW - 30 * UI, CY + 9 * UI, TextDim, .62f, 1); }
        Text(ShapeText(Def), CX + 12 * UI, CY + 32 * UI, TextDim, .7f);
        // 단계 칸 3개 + 각성
        for (int32 K = 0; K < 3; ++K)
            DrawRect(K < Rank ? Gold : FLinearColor(.2f, .2f, .24f), CX + 12 * UI + K * 18 * UI, CY + 54 * UI, 14 * UI, 6 * UI);
        if (L && L->bAwakened) Text(TEXT("각성"), CX + 70 * UI, CY + 48 * UI, FLinearColor(1.f, .55f, .9f), .75f);
        FString Status; FLinearColor SC = TextDim;
        if (Rank > 0) { Status = FString::Printf(TEXT("배움 %d/3"), Rank); SC = Gold; }
        else if (!bUnlocked) { Status = S->LockReason(Def); SC = Bad * FLinearColor(1, 1, 1, .85f); }
        else if (S->SkillPointsLeft > 0) { Status = TEXT("배울 수 있음"); SC = Good; }
        else Status = TEXT("스킬 포인트 필요");
        Text(Status, CX + 12 * UI, CY + 70 * UI, SC, .74f);
        if (bHover && bClick) SelectedSkill = &Def;
    }

    // ---- 오른쪽: 고른 스킬 ----
    const float DX = X + 818 * UI, DY = Y + 74 * UI, DW = W - (818 + 18) * UI, DH = 490 * UI;
    Panel(DX, DY, DW, DH, BgPanel, Edge);
    const FCLSkillDef* D = SelectedSkill && SelectedSkill->Weapon == Weapon ? SelectedSkill : nullptr;
    if (!D)
    {
        Text(TEXT("스킬을 고르세요"), DX + DW * .5f, DY + 24 * UI, TextMain, 1.1f, 1);
        float LY = DY + 64 * UI;
        for (const TCHAR* Line : {TEXT("· 배우기 1 + 강화 2 = 최대 3포인트"), TEXT("· 강화마다 피해 +20%, 쿨타임 -10%"), TEXT("  (임시 규칙)"), TEXT("· 고급 3단계는 각성 가능"),
                                  TEXT("  (각성 피해 +50%)"), TEXT("· 지우면 포인트를 모두 돌려받음"), TEXT("  (전투 중 불가)"), TEXT("· 스킬을 쓰면 그 무기로 바뀜")})
        { Text(Line, DX + 14 * UI, LY, TextDim, .8f); LY += 24 * UI; }
    }
    else
    {
        const FLinearColor C = StatColor(D->Stat);
        DrawRect(C, DX, DY, DW, 4 * UI);
        Text(D->DisplayName, DX + 14 * UI, DY + 12 * UI, TextMain, 1.4f);
        Text(FString::Printf(TEXT("%s · %s · %s"), D->Weapon == ECLWeapon::Sword ? TEXT("검") : TEXT("활"), StatName(D->Stat), TierName(D->Tier)), DX + 14 * UI, DY + 42 * UI, C, .85f);
        float LY = DY + 72 * UI;
        const auto Row = [&](const TCHAR* Label, const FString& Value)
        {
            Text(Label, DX + 14 * UI, LY, TextDim, .8f);
            Text(Value, DX + DW - 14 * UI, LY, TextMain, .8f, 2);
            LY += 22 * UI;
        };
        Row(TEXT("판정"), ShapeText(*D));
        if (D->Multiplier > 0)
            Row(TEXT("피해"), FString::Printf(TEXT("%.0f%s (%.0f%%)"), S->SkillDamage(*D), D->Hits > 1 ? *FString::Printf(TEXT(" × %d"), D->Hits) : TEXT(""), D->Multiplier * 100));
        Row(TEXT("쿨타임"), D->Cooldown > 0 ? FString::Printf(TEXT("%.1f초"), S->SkillCooldown(*D)) : FString(TEXT("미정")));
        Row(TEXT("마나"), D->ManaCost > 0 ? FString::Printf(TEXT("%.0f"), D->ManaCost) : FString(TEXT("미정")));
        if (D->CastTime > 0) Row(TEXT("준비"), FString::Printf(TEXT("%.1f초"), D->CastTime));
        // 효과 설명(폭에 맞춰 줄바꿈)
        LY += 4 * UI;
        TArray<FString> Words; FString(D->Effect).ParseIntoArray(Words, TEXT(" "));
        FString Current;
        for (const FString& Word : Words)
        {
            const FString Try = Current.IsEmpty() ? Word : Current + TEXT(" ") + Word;
            if (TextWidth(Try, .8f) > DW - 28 * UI && !Current.IsEmpty()) { Text(Current, DX + 14 * UI, LY, FLinearColor(.85f, .9f, 1.f), .8f); LY += 20 * UI; Current = Word; }
            else Current = Try;
        }
        if (!Current.IsEmpty()) { Text(Current, DX + 14 * UI, LY, FLinearColor(.85f, .9f, 1.f), .8f); LY += 20 * UI; }
        if (!D->Execute) { Text(TEXT("아직 구현 중인 스킬입니다"), DX + 14 * UI, LY + 4 * UI, TextDim, .75f); LY += 22 * UI; }

        // 행동 단추: 지금 가능한 것만 밝게, 아니면 이유를 아래에
        using A = UCLSkillComponent::EAction;
        float BY = DY + DH - 4 * 44 * UI - 34 * UI;
        FString FirstReason;
        const auto Action = [&](A Act, const FString& Label, const FLinearColor& Accent)
        {
            const FString Why = S->CheckAction(Act, *D);
            if (Button(Label, DX + 14 * UI, BY, DW - 28 * UI, 36 * UI, Why.IsEmpty(), Accent)) S->RequestSkill(Act, *D);
            if (!Why.IsEmpty() && FirstReason.IsEmpty() && (Act == A::Learn ? !S->FindLearned(*D) : S->FindLearned(*D) != nullptr)) FirstReason = Why;
            BY += 44 * UI;
        };
        const int32 Rank = S->RankOf(*D);
        Action(A::Learn, Rank ? TEXT("배움") : TEXT("배우기 (스킬 포인트 1)"), Good);
        Action(A::Upgrade, Rank >= 3 ? FString(TEXT("최대 강화")) : FString::Printf(TEXT("강화 → %d단계 (스킬 포인트 1)"), FMath::Max(2, Rank + 1)), Gold);
        Action(A::Awaken, D->Tier == ECLSkillTier::Advanced ? TEXT("각성 (각성 포인트 1)") : TEXT("각성: 고급 스킬만"), FLinearColor(1.f, .55f, .9f));
        Action(A::Remove, TEXT("지우기 (포인트 돌려받음)"), Bad);
        if (!FirstReason.IsEmpty()) Text(FirstReason, DX + DW * .5f, DY + DH - 26 * UI, Bad, .8f, 1);
    }

    // ---- 아래: 장착된 스킬 ----
    const float FY = Y + H - 44 * UI;
    Text(TEXT("장착"), X + 290 * UI, FY + 8 * UI, TextDim, .85f);
    const TCHAR* Keys[] = {TEXT("Z"), TEXT("X"), TEXT("C"), TEXT("V")};
    for (int32 I = 0; I < UCLSkillComponent::SlotCount; ++I)
    {
        const float EX = X + 340 * UI + I * 118 * UI;
        const FCLSkillDef* Def = S->Slots[I];
        Panel(EX, FY + 2 * UI, 110 * UI, 30 * UI, Def ? FLinearColor(.1f, .1f, .14f, .95f) : FLinearColor(.05f, .05f, .07f, .8f), Def ? StatColor(Def->Stat) : FLinearColor(.2f, .2f, .25f));
        Text(FString::Printf(TEXT("%s  %s"), Keys[I], Def ? Def->DisplayName : TEXT("비어 있음")), EX + 8 * UI, FY + 8 * UI, Def ? TextMain : TextDim, .78f);
    }
    Text(TEXT("K · Esc 닫기"), X + W - 18 * UI, FY + 8 * UI, TextDim, .8f, 2);
    if (P->GetWorld()->GetTimeSeconds() < S->LastMessageUntil) Text(S->LastMessage, X + 18 * UI, FY + 8 * UI, FLinearColor(1, .88f, .5f), .85f);
    UI = Base;
}
