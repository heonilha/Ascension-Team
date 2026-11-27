# 스킬 공통 틀(무기 교체·슬롯·쿨타임·마나·피해 공식) 자동 시험.
# 먼저 .\Scripts\BuildDemo.ps1 -BuildRoot <같은 경로> 로 빌드한다.
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$BuildRoot = 'C:\Users\doboh\.codex\builds\AscensionPrototype'
)
$ErrorActionPreference = 'Stop'
$taskLog = Join-Path (Split-Path -Parent $PSScriptRoot) 'Verification\Skills.log'
& (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') "$BuildRoot\AscensionPrototype.uproject" -game -AscensionSkillVerify -nullrhi -nosound -unattended -nosplash "-abslog=$taskLog"
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $taskLog -SimpleMatch 'ASCENSION_SKILL_VERIFY PASS')) { throw 'Skill verification failed.' }
Select-String -LiteralPath $taskLog -Pattern 'ASCENSION_CHECK|ASCENSION_SKILL_VERIFY' | ForEach-Object { $_.Line -replace '^.*?(ASCENSION_)', '$1' }
