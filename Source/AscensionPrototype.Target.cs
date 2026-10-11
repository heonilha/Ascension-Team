using UnrealBuildTool;
public class AscensionPrototypeTarget : TargetRules
{
    public AscensionPrototypeTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("AscensionPrototype");
    }
}
