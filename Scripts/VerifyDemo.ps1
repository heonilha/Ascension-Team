param([switch]$View)
$ErrorActionPreference = 'Stop'
$taskRoot = 'C:\Users\doboh\.codex\builds\AscensionPrototype'
$taskEngine = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskLogs = Join-Path (Split-Path -Parent $PSScriptRoot) 'Verification'
New-Item -ItemType Directory -Path $taskLogs -Force | Out-Null
if ($View) {
    & $taskEngine "$taskRoot\AscensionPrototype.uproject" -game -AscensionView -windowed -ResX=1280 -ResY=720 -nosplash -unattended "-abslog=$taskLogs\View.log"
    if ($LASTEXITCODE -ne 0) { throw "View launch failed: $LASTEXITCODE" }
    $taskShot = "$taskRoot\Saved\CombatLab.png"
    if (-not (Test-Path -LiteralPath $taskShot)) { throw 'Expected screenshot missing.' }
    Copy-Item -LiteralPath $taskShot -Destination "$taskLogs\CombatLab.png" -Force
    if (Test-Path -LiteralPath "$taskRoot\Saved\KoreanAwakening.png") {
        Copy-Item -LiteralPath "$taskRoot\Saved\KoreanAwakening.png" -Destination "$taskLogs\KoreanAwakening.png" -Force
    }
} else {
    & $taskEngine "$taskRoot\AscensionPrototype.uproject" -game -AscensionVerify -nullrhi -nosound -unattended -nosplash "-abslog=$taskLogs\Combat.log"
    if ($LASTEXITCODE -ne 0) { throw "Verification failed: $LASTEXITCODE" }
    if (-not (Select-String -LiteralPath "$taskLogs\Combat.log" -SimpleMatch 'ASCENSION_VERIFY PASS')) { throw 'Expected completion marker missing.' }
}
