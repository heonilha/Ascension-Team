# Online: connect to the Ascension server and play together with players that use the same team code.
# The editor's Play button (PIE) always runs the offline demo; online mode needs this launcher (or -AscensionServer=...).
# Example:
#   .\Scripts\OpenOnline.ps1 -Name player1 -Team 1      # runs this checkout (build it once: open in the editor, or Build.bat)
#   .\Scripts\OpenOnline.ps1 -Name player2 -Team 1      # second window on the same PC joins the same room
#   .\Scripts\OpenOnline.ps1 -BuildRoot D:\Builds\AscensionPrototype -Name player1   # runs a BuildDemo.ps1 copy instead
#   -Mode pvp joins the PvP stage room (server-judged attacks and dodges, 0.30 s dodge invulnerability).
#   -Server 192.168.0.10:7777 connects to another PC's server.
param(
    [string]$Server = '127.0.0.1:7777',
    [string]$Name = $env:USERNAME,
    [int]$Team = 1,
    [ValidateSet('pve', 'pvp')][string]$Mode = 'pve',
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
    "-AscensionServer=$Server", "-AscensionName=`"$Name`"", "-AscensionTeam=$Team", "-AscensionMode=$Mode"
)
Start-Process -FilePath $taskEngine -ArgumentList $taskArgs -WindowStyle Normal
