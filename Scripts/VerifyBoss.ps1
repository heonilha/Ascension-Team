param([switch]$View)
$ErrorActionPreference = 'Stop'
$bossBuild = 'C:\Users\doboh\.codex\builds\AscensionPrototype'
$bossEngine = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64'
$bossOutput = Join-Path (Split-Path -Parent $PSScriptRoot) 'Verification'
if ($View) {
    $bossLog = Join-Path $bossOutput 'BossView.log'
    $bossArgs = @("`"$bossBuild\AscensionPrototype.uproject`"", '-game', '-AscensionBossView', '-windowed', '-ResX=1280', '-ResY=720', '-nosplash', '-unattended', "-abslog=`"$bossLog`"")
    $bossProcess = Start-Process -FilePath "$bossEngine\UnrealEditor.exe" -ArgumentList $bossArgs -WindowStyle Hidden -PassThru
    if (-not $bossProcess.WaitForExit(60000)) { Stop-Process -Id $bossProcess.Id; throw 'Boss view timed out.' }
    if ($bossProcess.ExitCode -ne 0 -or -not (Select-String -LiteralPath $bossLog -SimpleMatch 'ASCENSION_BOSS_VIEW PASS')) { throw 'Boss view verification failed.' }
    foreach ($bossName in @('BossCleave','BossCharge','BossSlamEnraged')) {
        Copy-Item -LiteralPath "$bossBuild\Saved\$bossName.png" -Destination (Join-Path $bossOutput "$bossName.png") -Force
    }
    return
}
$bossLog = Join-Path $bossOutput 'BossCombat.log'
& "$bossEngine\UnrealEditor-Cmd.exe" "$bossBuild\AscensionPrototype.uproject" -game -AscensionVerify -AscensionBossVerify -nullrhi -nosound -unattended -nosplash "-abslog=$bossLog"
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $bossLog -SimpleMatch 'ASCENSION_BOSS_VERIFY PASS')) { throw 'Boss combat verification failed.' }
