using UnrealBuildTool;
public class AscensionPrototype : ModuleRules
{
    public AscensionPrototype(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] {"Core", "CoreUObject", "Engine", "InputCore"});
    }
}
