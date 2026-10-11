param([switch]$Editor)
$taskEngine = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$taskRoot = 'C:\Users\doboh\.codex\builds\AscensionPrototype'
if (-not (Test-Path -LiteralPath "$taskRoot\Binaries\Win64\UnrealEditor-AscensionPrototype.dll")) { throw 'Run BuildDemo.ps1 first.' }
$taskArgs = @("`"$taskRoot\AscensionPrototype.uproject`"")
if (-not $Editor) { $taskArgs += @('-game','-windowed','-ResX=1280','-ResY=720','-nosplash') }
Start-Process -FilePath $taskEngine -ArgumentList $taskArgs -WindowStyle Hidden
