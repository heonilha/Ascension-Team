# Online P0: connect to the Ascension server and move together with players that use the same team code.
# Example:
#   .\Scripts\OpenOnline.ps1 -BuildRoot D:\Builds\AscensionPrototype -Name player1 -Team 1
#   Run it again with another -Name to open a second client on the same PC.
param(
    [string]$Server = '127.0.0.1:7777',
    [string]$Name = $env:USERNAME,
    [int]$Team = 1,
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$BuildRoot = 'C:\Users\doboh\.codex\builds\AscensionPrototype'
)
$ErrorActionPreference = 'Stop'
$taskEngine = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not (Test-Path -LiteralPath "$BuildRoot\Binaries\Win64\UnrealEditor-AscensionPrototype.dll")) {
    throw "Build first: .\Scripts\BuildDemo.ps1 -BuildRoot $BuildRoot"
}
$taskArgs = @(
    "`"$BuildRoot\AscensionPrototype.uproject`"", '-game', '-windowed', '-ResX=1280', '-ResY=720', '-nosplash',
    "-AscensionServer=$Server", "-AscensionName=`"$Name`"", "-AscensionTeam=$Team"
)
Start-Process -FilePath $taskEngine -ArgumentList $taskArgs -WindowStyle Normal
