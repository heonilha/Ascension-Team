# Online: connect to the Ascension server (Ascension-Server, Rust) and play a battle royale match.
# Players who use the same -Match number play the same match; -Team (1-4) picks your team (up to 3 per team).
# The editor's Play button (PIE) always runs the offline demo; online mode needs this launcher (or -AscensionServer=...).
# Example:
#   .\Scripts\OpenOnline.ps1 -Name player1 -Team 1      # runs this checkout (build it once: open in the editor, or Build.bat)
#   .\Scripts\OpenOnline.ps1 -Name player2 -Team 2      # second window on the same PC: another team, so the match starts
#   .\Scripts\OpenOnline.ps1 -BuildRoot D:\Builds\AscensionPrototype -Name player1   # runs a BuildDemo.ps1 copy instead
#   -Mode practice joins the developer practice room (no teams, no rounds) instead of a match.
#   -Server 192.168.0.10:7777 connects to another PC's server.
param(
    [string]$Server = '127.0.0.1:7777',
    [string]$Name = $env:USERNAME,
    [ValidateRange(1, 4)][int]$Team = 1,
    [int]$Match = 1,
    [ValidateSet('match', 'practice')][string]$Mode = 'match',
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$BuildRoot = ''
)
$ErrorActionPreference = 'Stop'
if (-not $BuildRoot) { $BuildRoot = Split-Path -Parent $PSScriptRoot }
$taskEngine = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not (Test-Path -LiteralPath "$BuildRoot\Binaries\Win64\UnrealEditor-AscensionPrototype.dll")) {
    throw "Not built: $BuildRoot (open the project in the editor once, or run .\Scripts\BuildDemo.ps1 -BuildRoot <folder>)"
}
$taskArgs = @(
    "`"$BuildRoot\AscensionPrototype.uproject`"", '-game', '-windowed', '-ResX=1280', '-ResY=720', '-nosplash',
    "-AscensionServer=$Server", "-AscensionName=`"$Name`"", "-AscensionMatch=$Match", "-AscensionTeam=$Team", "-AscensionMode=$Mode"
)
Start-Process -FilePath $taskEngine -ArgumentList $taskArgs -WindowStyle Normal
