$ErrorActionPreference = 'Stop'
$taskBuild = 'C:\Users\doboh\.codex\builds\AscensionPrototype'
$taskExe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskLog = Join-Path (Split-Path -Parent $PSScriptRoot) 'Verification\Survival.log'
& $taskExe "$taskBuild\AscensionPrototype.uproject" -game -AscensionVerify -AscensionSurvivalVerify -nullrhi -nosound -unattended -nosplash "-abslog=$taskLog"
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $taskLog -SimpleMatch 'ASCENSION_SURVIVAL_VERIFY PASS')) { throw 'Survival verification failed.' }
