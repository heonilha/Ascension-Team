using UnrealBuildTool;
public class AscensionPrototypeEditorTarget : TargetRules
{
    public AscensionPrototypeEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("AscensionPrototype");
    }
}
