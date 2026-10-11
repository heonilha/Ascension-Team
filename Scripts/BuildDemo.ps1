param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$BuildRoot = 'C:\Users\doboh\.codex\builds\AscensionPrototype'
)
$ErrorActionPreference = 'Stop'
$taskProjectRoot = Split-Path -Parent $PSScriptRoot
if ($BuildRoot -match '[^\x00-\x7F]') { throw 'BuildRoot must use an ASCII path.' }
if ([IO.Path]::GetFullPath($BuildRoot) -eq [IO.Path]::GetFullPath($taskProjectRoot)) { throw 'Use a separate build copy.' }
New-Item -ItemType Directory -Path $BuildRoot -Force | Out-Null
foreach ($taskDirectory in @('Source','Config','Scripts','Content')) {
    $taskInput = Join-Path $taskProjectRoot $taskDirectory
    if (Test-Path -LiteralPath $taskInput) {
        $taskOutput = Join-Path $BuildRoot $taskDirectory
        New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
        Get-ChildItem -LiteralPath $taskInput -Force | Copy-Item -Destination $taskOutput -Recurse -Force
    }
}
Copy-Item -LiteralPath (Join-Path $taskProjectRoot 'AscensionPrototype.uproject') -Destination $BuildRoot -Force
& (Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat') AscensionPrototypeEditor Win64 Development "-Project=$BuildRoot\AscensionPrototype.uproject" -WaitMutex -NoHotReloadFromIDE -NoUBA
if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath "$BuildRoot\Content\Maps\CombatLab.umap")) {
    & (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') "$BuildRoot\AscensionPrototype.uproject" -run=pythonscript "-script=$BuildRoot\Scripts\CreateMap.py" -unattended -nullrhi -nosplash "-abslog=$BuildRoot\MapCreation.log"
    if ($LASTEXITCODE -ne 0) { throw "Map creation failed: $LASTEXITCODE" }
    if (-not (Test-Path -LiteralPath "$BuildRoot\Content\Maps\CombatLab.umap")) { throw 'Map missing after creation.' }
    New-Item -ItemType Directory -Path "$taskProjectRoot\Content\Maps" -Force | Out-Null
    Copy-Item -LiteralPath "$BuildRoot\Content\Maps\CombatLab.umap" -Destination "$taskProjectRoot\Content\Maps\CombatLab.umap" -Force
}
Write-Output "ASCENSION_BUILD_READY $BuildRoot"
