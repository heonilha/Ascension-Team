$ErrorActionPreference = 'Stop'
$taskOutput = Join-Path (Split-Path -Parent $PSScriptRoot) 'Assets\Audio'
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskRate = 22050
$taskSamples = [int]($taskRate * 0.18)
$taskWriter = [IO.BinaryWriter]::new([IO.File]::Create((Join-Path $taskOutput 'SwordImpact.wav')))
$taskRandom = [Random]::new(371)
try {
    $taskWriter.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $taskWriter.Write([int](36 + $taskSamples * 2))
    $taskWriter.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt ')); $taskWriter.Write([int]16)
    $taskWriter.Write([int16]1); $taskWriter.Write([int16]1); $taskWriter.Write([int]$taskRate)
    $taskWriter.Write([int]($taskRate * 2)); $taskWriter.Write([int16]2); $taskWriter.Write([int16]16)
    $taskWriter.Write([Text.Encoding]::ASCII.GetBytes('data')); $taskWriter.Write([int]($taskSamples * 2))
    for ($taskIndex=0; $taskIndex -lt $taskSamples; $taskIndex++) {
        $taskTime = $taskIndex / $taskRate
        $taskEnvelope = [Math]::Exp(-$taskTime*27) * [Math]::Min(1,$taskTime*1800)
        $taskNoise = ($taskRandom.NextDouble()*2-1) * [Math]::Exp(-$taskTime*80)
        $taskTone = [Math]::Sin(2*[Math]::PI*170*$taskTime)*0.4 + [Math]::Sin(2*[Math]::PI*1830*$taskTime)*0.2 + [Math]::Sin(2*[Math]::PI*2917*$taskTime)*0.12
        $taskTail = [Math]::Min(1,(0.18-$taskTime)*120)
        $taskWriter.Write([int16]([Math]::Clamp(($taskTone*$taskEnvelope+$taskNoise*0.28)*$taskTail,-1,1)*24000))
    }
} finally { $taskWriter.Dispose() }
