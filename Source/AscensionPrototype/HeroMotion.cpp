#include "HeroMotion.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimNodeBase.h"

class FCLMotionProxy : public FAnimInstanceProxy
{
public:
    explicit FCLMotionProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
    virtual void PreUpdate(UAnimInstance* Instance,float Delta) override
    {
        FAnimInstanceProxy::PreUpdate(Instance,Delta);
        const UCLHeroMotion* Motion=CastChecked<UCLHeroMotion>(Instance);
        Idle=Motion->IdleClip; Run=Motion->RunClip; Action=Motion->ActionClip;
        BaseTime=Motion->BaseTime; ActionTime=Motion->ActionTime;
        RunWeight=FMath::FInterpTo(RunWeight,Motion->MovingWeight,Delta,12.f);
        ActionWeight=FMath::FInterpTo(ActionWeight,Motion->ActionWeight,Delta,20.f);
        bFullBody=Motion->bFullBodyAction;
    }
    virtual bool Evaluate(FPoseContext& Output) override
    {
        const auto Sample=[](UAnimSequence* Clip,float Time,bool Loop,FPoseContext& Pose)
        {
            Pose.ResetToRefPose(); if(!Clip) return;
            const float Length=FMath::Max(.001f,Clip->GetPlayLength());
            const float Position=Loop?FMath::Fmod(FMath::Max(0.f,Time),Length):FMath::Clamp(Time,0.f,Length);
            FAnimationPoseData Data(Pose); Clip->GetAnimationPose(Data,FAnimExtractContext(double(Position),false));
        };
        Sample(Idle,BaseTime,true,Output);
        FPoseContext Running(Output); Sample(Run,BaseTime,true,Running);
        for(FCompactPoseBoneIndex Bone:Output.Pose.ForEachBoneIndex())
        {
            FTransform Mixed; Mixed.Blend(Output.Pose[Bone],Running.Pose[Bone],RunWeight); Output.Pose[Bone]=Mixed;
        }
        if(Action && ActionWeight>.001f)
        {
            FPoseContext Upper(Output); Sample(Action,ActionTime,false,Upper);
            const FBoneContainer& Bones=Output.Pose.GetBoneContainer();
            const FReferenceSkeleton& PoseSkeleton=Bones.GetReferenceSkeleton();
            for(FCompactPoseBoneIndex Bone:Output.Pose.ForEachBoneIndex())
            {
                bool AboveSpine=bFullBody;
                for(FCompactPoseBoneIndex Parent=Bone;!AboveSpine && Parent.IsValid();Parent=Bones.GetParentBoneIndex(Parent))
                {
                    const FName Name=PoseSkeleton.GetBoneName(Bones.MakeMeshPoseIndex(Parent).GetInt());
                    AboveSpine=Name.ToString().Contains(TEXT("Spine"),ESearchCase::IgnoreCase);
                }
                if(AboveSpine) { FTransform Mixed; Mixed.Blend(Output.Pose[Bone],Upper.Pose[Bone],ActionWeight); Output.Pose[Bone]=Mixed; }
            }
        }
        Output.Pose.NormalizeRotations(); return true;
    }
private:
    UAnimSequence* Idle=nullptr; UAnimSequence* Run=nullptr; UAnimSequence* Action=nullptr;
    float BaseTime=0,ActionTime=0,RunWeight=0,ActionWeight=0;
    bool bFullBody=false;
};
UCLHeroMotion::UCLHeroMotion() { bUseMultiThreadedAnimationUpdate=false; }
FAnimInstanceProxy* UCLHeroMotion::CreateAnimInstanceProxy() { return new FCLMotionProxy(this); }
void UCLHeroMotion::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete Proxy; }
