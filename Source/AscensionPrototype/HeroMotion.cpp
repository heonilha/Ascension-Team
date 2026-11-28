#include "HeroMotion.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimNodeBase.h"
#include "BonePose.h"

namespace
{
    // 믹사모 클립 값(Scripts/ImportMixamo.py로 가져온 A_* 를 분석한 값)
    // A_Slash 1.5초: 0~0.37 칼을 들어 올림, 0.60 칼이 가장 빠름(내려 벰), 0.75 이후 되돌아옴
    constexpr float SlashImpact = .60f, SlashLightStart = .30f, SlashLightEnd = 1.10f, SlashHeavyStart = .05f, SlashHeavyEnd = 1.30f, SlashRaised = .37f;
    // A_Roll 2.53초: 0.40부터 몸을 던져 1.33에 바닥, 2.0에 거의 일어선다. 이 구간에 골반이 301cm 가서 게임 회피(300cm)와 같다
    constexpr float RollStart = .40f, RollEnd = 2.0f;
    // 디딘 발이 바닥을 미는 속도(클립 고유 이동 속도): A_Walk 156, A_Run 390 cm/s. 두 클립은 같은 위상에서 같은 발이 앞선다
    constexpr float WalkClipSpeed = 156.f, RunClipSpeed = 390.f;
    constexpr float SwitchBlendSeconds = .12f;
}

float UCLHeroMotion::ActionClipTime(int32 State, float Elapsed, float Duration, float Hit)
{
    if (State == 4) return RollStart + (RollEnd - RollStart) * FMath::Clamp(Elapsed / FMath::Max(Duration, .01f), 0.f, 1.f);
    const bool bHeavy = State == 3;
    const float Start = bHeavy ? SlashHeavyStart : SlashLightStart, End = bHeavy ? SlashHeavyEnd : SlashLightEnd;
    if (Elapsed < Hit) return Start + (SlashImpact - Start) * FMath::Clamp(Elapsed / FMath::Max(Hit, .01f), 0.f, 1.f);
    return SlashImpact + (End - SlashImpact) * FMath::Clamp((Elapsed - Hit) / FMath::Max(Duration - Hit, .01f), 0.f, 1.f);
}

float UCLHeroMotion::ChargeClipTime(float Charge)
{
    return SlashHeavyStart + (SlashRaised - SlashHeavyStart) * FMath::Clamp(Charge, 0.f, 1.f);
}

float UCLHeroMotion::DodgeTravel(float Progress)
{
    const float A = FMath::Clamp(Progress, 0.f, 1.f);
    return A - FMath::Sin(2 * PI * A) / (2 * PI);
}

class FCLMotionProxy : public FAnimInstanceProxy
{
public:
    explicit FCLMotionProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
    virtual void PreUpdate(UAnimInstance* Instance, float Delta) override
    {
        FAnimInstanceProxy::PreUpdate(Instance, Delta);
        const UCLHeroMotion* Motion = CastChecked<UCLHeroMotion>(Instance);
        // 다른 동작으로 바뀌거나 같은 동작이 처음부터 다시 시작하면, 직전 자세에서 짧게 넘어간다
        const bool bRestart = Motion->ActionClip == Action && Motion->ActionTime < ActionTime - .05f;
        if (Motion->ActionClip != Action || bRestart)
        {
            if (Action && ActionWeight > .05f) { Prev = Action; PrevTime = ResolvedTime; bPrevFullBody = bFullBody; Switch = 0; }
            else Switch = 1;
        }
        Switch = FMath::Min(1.f, Switch + Delta / SwitchBlendSeconds);
        Idle = Motion->IdleClip; Walk = Motion->WalkClip; Run = Motion->RunClip; Action = Motion->ActionClip;
        BaseTime = Motion->BaseTime; ActionTime = Motion->ActionTime; ActionTravel = Motion->ActionTravel;
        ActionWeight = FMath::FInterpTo(ActionWeight, Motion->ActionWeight, Delta, 18.f);
        bFullBody = Motion->bFullBodyAction;

        // 이동: 속도로 대기·걷기·달리기 비중을 정하고, 한 걸음 주기(두 발)를 실제 이동 거리로 돌린다
        Speed = FMath::FInterpTo(Speed, FMath::Max(0.f, Motion->Speed), Delta, 12.f);
        RunAlpha = Walk ? FMath::Clamp((Speed - WalkClipSpeed) / (RunClipSpeed - WalkClipSpeed), 0.f, 1.f) : 1.f;
        MoveWeight = FMath::Clamp(Speed / (Walk ? WalkClipSpeed : RunClipSpeed * .5f), 0.f, 1.f);
        const float WalkCycle = Walk ? WalkClipSpeed * Walk->GetPlayLength() : 0, RunCycle = Run ? RunClipSpeed * Run->GetPlayLength() : 0;
        const float Cycle = FMath::Max(1.f, Walk && Run ? FMath::Lerp(WalkCycle, RunCycle, RunAlpha) : FMath::Max(WalkCycle, RunCycle));
        Phase = FMath::Fmod(Phase + Delta * Speed / Cycle, 1.f);
    }
    virtual bool Evaluate(FPoseContext& Output) override
    {
        Sample(Idle, BaseTime, true, Output);
        if (MoveWeight > .001f && (Walk || Run))
        {
            UAnimSequence* Main = Walk && RunAlpha < .5f ? Walk : (Run ? Run : Walk);
            UAnimSequence* Other = Main == Walk ? Run : Walk;
            const float OtherWeight = Main == Walk ? RunAlpha : 1.f - RunAlpha;
            FPoseContext Moving(Output); Sample(Main, Phase * Main->GetPlayLength(), true, Moving);
            if (Other && OtherWeight > .001f)
            {
                FPoseContext Second(Output); Sample(Other, Phase * Other->GetPlayLength(), true, Second);
                Mix(Moving, Second, OtherWeight);
            }
            Mix(Output, Moving, MoveWeight);
        }
        if (Action && ActionWeight > .001f)
        {
            FPoseContext Current(Output);
            ResolvedTime = bFullBody && ActionTravel >= 0 ? TimeForTravel(Current, Action, ActionTravel) : ActionTime;
            Sample(Action, ResolvedTime, false, Current);
            if (bFullBody) InPlace(Current, Action);
            const bool bCross = Prev && Switch < 1;
            FPoseContext Old(Output);
            if (bCross) { Sample(Prev, PrevTime, false, Old); if (bPrevFullBody) InPlace(Old, Prev); }
            const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
            const FReferenceSkeleton& PoseSkeleton = Bones.GetReferenceSkeleton();
            for (FCompactPoseBoneIndex Bone : Output.Pose.ForEachBoneIndex())
            {
                bool bUpper = false;
                for (FCompactPoseBoneIndex Parent = Bone; !bUpper && Parent.IsValid(); Parent = Bones.GetParentBoneIndex(Parent))
                    bUpper = PoseSkeleton.GetBoneName(Bones.MakeMeshPoseIndex(Parent).GetInt()).ToString().Contains(TEXT("Spine"), ESearchCase::IgnoreCase);
                const FTransform& Base = Output.Pose[Bone];
                FTransform Act; Act.Blend(Base, Current.Pose[Bone], bFullBody || bUpper ? 1.f : 0.f);
                if (bCross)
                {
                    FTransform Before; Before.Blend(Base, Old.Pose[Bone], bPrevFullBody || bUpper ? 1.f : 0.f);
                    FTransform Crossed; Crossed.Blend(Before, Act, Switch); Act = Crossed;
                }
                FTransform Final; Final.Blend(Base, Act, ActionWeight); Output.Pose[Bone] = Final;
            }
        }
        Output.Pose.NormalizeRotations(); return true;
    }
private:
    static void Sample(UAnimSequence* Clip, float Time, bool bLoop, FPoseContext& Pose)
    {
        Pose.ResetToRefPose(); if (!Clip) return;
        const float Length = FMath::Max(.001f, Clip->GetPlayLength());
        const float Position = bLoop ? FMath::Fmod(FMath::Max(0.f, Time), Length) : FMath::Clamp(Time, 0.f, Length);
        FAnimationPoseData Data(Pose); Clip->GetAnimationPose(Data, FAnimExtractContext(double(Position), false));
    }
    static void Mix(FPoseContext& Into, const FPoseContext& Other, float Alpha)
    {
        for (FCompactPoseBoneIndex Bone : Into.Pose.ForEachBoneIndex())
        {
            FTransform Mixed; Mixed.Blend(Into.Pose[Bone], Other.Pose[Bone], Alpha); Into.Pose[Bone] = Mixed;
        }
    }
    static FCompactPoseBoneIndex Find(const FPoseContext& Pose, const TCHAR* Epic, const TCHAR* Mixamo)
    {
        const FBoneContainer& Bones = Pose.Pose.GetBoneContainer();
        for (FCompactPoseBoneIndex Bone : Pose.Pose.ForEachBoneIndex())
        {
            const FName Name = Bones.GetReferenceSkeleton().GetBoneName(Bones.MakeMeshPoseIndex(Bone).GetInt());
            if (Name == Epic || Name == Mixamo) return Bone;
        }
        return FCompactPoseBoneIndex(INDEX_NONE);
    }
    static FCompactPoseBoneIndex Pelvis(const FPoseContext& Pose) { return Find(Pose, TEXT("pelvis"), TEXT("Hips")); }

    struct FTravel
    {
        FVector2D Start = FVector2D::ZeroVector; float Yaw = 0; bool bMoves = false;
        TArray<float> Path; // 클립 전체를 Steps 칸으로 나눈 골반 누적 이동 거리
    };
    static constexpr int32 Steps = 96;
    // 클립마다 한 번: 골반 시작 위치, 누적 이동 거리, 평균 몸 정면
    FTravel& Measure(const FPoseContext& Like, UAnimSequence* Clip)
    {
        if (FTravel* Found = Travels.Find(Clip)) return *Found;
        FTravel Made;
        const FCompactPoseBoneIndex P = Pelvis(Like);
        const FCompactPoseBoneIndex L = Find(Like, TEXT("upperarm_l"), TEXT("LeftArm")), R = Find(Like, TEXT("upperarm_r"), TEXT("RightArm"));
        FVector2D Facing = FVector2D::ZeroVector, Last = FVector2D::ZeroVector; float Total = 0;
        for (int32 I = 0; P.IsValid() && I <= Steps; ++I)
        {
            FPoseContext At(Like); Sample(Clip, Clip->GetPlayLength() * I / Steps, false, At);
            FCSPose<FCompactPose> Space; Space.InitPose(At.Pose);
            const FVector Hips = Space.GetComponentSpaceTransform(P).GetTranslation();
            const FVector2D Here(Hips.X, Hips.Y);
            if (I == 0) Made.Start = FVector2D(At.Pose[P].GetTranslation().X, At.Pose[P].GetTranslation().Y);
            else Total += FVector2D::Distance(Here, Last);
            Last = Here; Made.Path.Add(Total);
            if (L.IsValid() && R.IsValid())
            {
                const FVector Side = Space.GetComponentSpaceTransform(R).GetTranslation() - Space.GetComponentSpaceTransform(L).GetTranslation();
                const FVector2D Forward(Side.Y, -Side.X); // 어깨선 × 위
                if (!Forward.IsNearlyZero()) Facing += Forward.GetSafeNormal();
            }
        }
        // 골반이 50cm 넘게 가는 클립만 제자리로 만들고 돌린다. 각도는 평균 몸 정면 → +Y(믹사모 구르기는 구르며 몸이 50도쯤 돈다)
        Made.bMoves = Total > 50;
        Made.Yaw = Made.bMoves && !Facing.IsNearlyZero() ? FMath::DegreesToRadians(90.f) - FMath::Atan2(Facing.Y, Facing.X) : 0;
        return Travels.Add(Clip, Made);
    }
    // 게임이 옮긴 거리 비율(0~1)만큼 클립 골반도 가 있는 시각(회피 구간 RollStart~RollEnd 안)
    float TimeForTravel(const FPoseContext& Like, UAnimSequence* Clip, float Fraction)
    {
        const FTravel& T = Measure(Like, Clip);
        const float Length = Clip->GetPlayLength();
        if (!T.bMoves || T.Path.Num() < 2) return RollStart + (RollEnd - RollStart) * Fraction;
        const auto DistAt = [&](float Time)
        {
            const float X = FMath::Clamp(Time / Length, 0.f, 1.f) * Steps; const int32 I = FMath::Min(int32(X), Steps - 1);
            return FMath::Lerp(T.Path[I], T.Path[I + 1], X - I);
        };
        const float Target = FMath::Lerp(DistAt(RollStart), DistAt(RollEnd), FMath::Clamp(Fraction, 0.f, 1.f));
        float Lo = RollStart, Hi = RollEnd;
        for (int32 K = 0; K < 20; ++K) { const float Mid = (Lo + Hi) * .5f; (DistAt(Mid) < Target ? Lo : Hi) = Mid; }
        return (Lo + Hi) * .5f;
    }
    // 몸 전체 동작: 골반 수평 이동을 지우고(이동은 게임이 한다) 몸 정면을 +Y로 돌린다
    void InPlace(FPoseContext& Pose, UAnimSequence* Clip)
    {
        const FCompactPoseBoneIndex P = Pelvis(Pose);
        if (!P.IsValid()) return;
        const FTravel& T = Measure(Pose, Clip);
        if (!T.bMoves) return;
        FTransform& Hips = Pose.Pose[P];
        Hips.SetTranslation(FVector(T.Start.X, T.Start.Y, Hips.GetTranslation().Z));
        FTransform& Root = Pose.Pose[FCompactPoseBoneIndex(0)];
        const FQuat Turn(FVector::UpVector, T.Yaw);
        Root.SetRotation(Turn * Root.GetRotation());
        Root.SetTranslation(Turn.RotateVector(Root.GetTranslation()));
    }
    TMap<UAnimSequence*, FTravel> Travels;
    UAnimSequence* Idle = nullptr; UAnimSequence* Walk = nullptr; UAnimSequence* Run = nullptr; UAnimSequence* Action = nullptr; UAnimSequence* Prev = nullptr;
    float BaseTime = 0, ActionTime = 0, ActionTravel = -1, ResolvedTime = 0, PrevTime = 0, ActionWeight = 0, Switch = 1;
    float Speed = 0, RunAlpha = 0, MoveWeight = 0, Phase = 0;
    bool bFullBody = false, bPrevFullBody = false;
};
UCLHeroMotion::UCLHeroMotion() { bUseMultiThreadedAnimationUpdate = false; }
FAnimInstanceProxy* UCLHeroMotion::CreateAnimInstanceProxy() { return new FCLMotionProxy(this); }
void UCLHeroMotion::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete Proxy; }
