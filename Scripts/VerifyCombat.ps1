param([switch]$View)
$ErrorActionPreference = 'Stop'
$taskBuild = 'C:\Users\doboh\.codex\builds\AscensionPrototype'
$taskExe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskLog = Join-Path (Split-Path -Parent $PSScriptRoot) 'Verification\CombatFeel.log'
if ($View) {
    $taskViewLog = Join-Path (Split-Path -Parent $PSScriptRoot) 'Verification\CombatFeelView.log'
    $taskViewExe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
    $taskArguments = @("`"$taskBuild\AscensionPrototype.uproject`"", '-game', '-AscensionCombatFeelView', '-windowed', '-ResX=1280', '-ResY=720', '-nosplash', '-unattended', "-abslog=`"$taskViewLog`"")
    $taskViewProcess = Start-Process -FilePath $taskViewExe -ArgumentList $taskArguments -WindowStyle Hidden -PassThru
    if (-not $taskViewProcess.WaitForExit(60000)) { Stop-Process -Id $taskViewProcess.Id; throw 'Combat view timed out.' }
    if ($taskViewProcess.ExitCode -ne 0 -or -not (Select-String -LiteralPath $taskViewLog -SimpleMatch 'ASCENSION_COMBAT_FEEL_VIEW PASS')) { throw 'Combat view verification failed.' }
    foreach ($taskName in @('MovingSlashEarly','MovingSlash','ChargingWave','ChargingWaveMid','ChargingWaveFull','ChargedRelease','WaveRoll')) {
        Copy-Item -LiteralPath "$taskBuild\Saved\$taskName.png" -Destination (Join-Path (Split-Path -Parent $taskLog) "$taskName.png") -Force
    }
    return
}
& $taskExe "$taskBuild\AscensionPrototype.uproject" -game -AscensionVerify -AscensionCombatVerify -nullrhi -nosound -unattended -nosplash "-abslog=$taskLog"
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $taskLog -SimpleMatch 'ASCENSION_COMBAT_VERIFY PASS')) { throw 'Combat feel verification failed.' }
