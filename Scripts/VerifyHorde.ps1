$ErrorActionPreference = 'Stop'
$taskBuild = 'C:\Users\doboh\.codex\builds\AscensionPrototype'
$taskExe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$taskOutput = Join-Path (Split-Path -Parent $PSScriptRoot) 'Verification'
$taskLog = Join-Path $taskOutput 'HordeView.log'
$taskArguments = @("`"$taskBuild\AscensionPrototype.uproject`"", '-game', '-AscensionHordeView', '-windowed', '-ResX=1280', '-ResY=720', '-nosplash', '-unattended', "-abslog=`"$taskLog`"")
$taskProcess = Start-Process -FilePath $taskExe -ArgumentList $taskArguments -WindowStyle Hidden -PassThru
if (-not $taskProcess.WaitForExit(60000)) { Stop-Process -Id $taskProcess.Id; throw 'Horde view timed out.' }
if ($taskProcess.ExitCode -ne 0 -or -not (Select-String -LiteralPath $taskLog -SimpleMatch 'ASCENSION_HORDE_VIEW PASS')) { throw 'Horde view verification failed.' }
foreach ($taskName in @('ExpandedArena','HordeCombat')) {
    Copy-Item -LiteralPath "$taskBuild\Saved\$taskName.png" -Destination (Join-Path $taskOutput "$taskName.png") -Force
}
