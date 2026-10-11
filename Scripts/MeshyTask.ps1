param(
    [Parameter(Mandatory=$true)][ValidateSet('Preview','Refine','Rig','Animation','Library','Status')][string]$Action,
    [string]$Name = 'SharedKnight',
    [string]$TaskId,
    [ValidateSet('text-to-3d','rigging','animations')][string]$Kind = 'text-to-3d',
    [int[]]$AnimationIds,
    [string]$Prompt,
    [string]$TexturePrompt
)
$ErrorActionPreference = 'Stop'
if ($Name -notmatch '^[A-Za-z0-9_-]+$') { throw 'Use a simple asset name.' }
$taskAssetRoot = Join-Path (Split-Path -Parent $PSScriptRoot) "Assets\Meshy\$Name"
New-Item -ItemType Directory -Path $taskAssetRoot -Force | Out-Null
$taskSecretPath = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'AscensionPrototype\Secrets\meshy-key.dpapi'
$taskSecure = ConvertTo-SecureString ([IO.File]::ReadAllText($taskSecretPath))
$taskPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($taskSecure)
try {
    $taskKey = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($taskPointer)
    $taskHeaders = @{Authorization="Bearer $taskKey"}
    $taskBase = 'https://api.meshy.ai'
    if ($Action -eq 'Library') {
        $taskResult=Invoke-RestMethod -Uri "$taskBase/openapi/v1/animations/library" -Headers $taskHeaders -TimeoutSec 30
        $taskResult | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath "$taskAssetRoot\AnimationLibrary.json" -Encoding UTF8
        $taskResult | Where-Object { $_.name -match 'idle|sword|slash|dodge|roll|standing' } | Select-Object action_id,name,key,category | ConvertTo-Json -Depth 5
    } elseif ($Action -eq 'Status') {
        if ($TaskId -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid task ID.' }
        $taskVersion = if ($Kind -in @('rigging','animations')) {'v1'} else {'v2'}
        $taskResult = Invoke-RestMethod -Uri "$taskBase/openapi/$taskVersion/$Kind/$TaskId" -Headers $taskHeaders -TimeoutSec 30
        $taskResult | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath "$taskAssetRoot\$TaskId.json" -Encoding UTF8
        @{id=$taskResult.id; status=$taskResult.status; progress=$taskResult.progress; consumed_credits=$taskResult.consumed_credits} | ConvertTo-Json
    } else {
        $taskReceiptPath = Join-Path $taskAssetRoot "$Action-receipt.json"
        if (Test-Path -LiteralPath $taskReceiptPath) { throw 'This step already has a receipt. Retrieve its status instead of spending twice.' }
        if (Test-Path -LiteralPath "$taskAssetRoot\$Action-request.json") { throw 'An existing request needs review before any retry; avoid duplicate spending.' }
        $taskBudget = Get-Content -LiteralPath (Join-Path (Split-Path -Parent $PSScriptRoot) 'Assets\Meshy\budget.json') -Raw | ConvertFrom-Json
        $taskBalance = (Invoke-RestMethod -Uri "$taskBase/openapi/v1/balance" -Headers $taskHeaders -TimeoutSec 30).balance
        $taskEstimate = if ($Action -eq 'Refine') {10} elseif ($Action -eq 'Animation') {3*$AnimationIds.Count} else {5}
        if ($taskBalance - $taskEstimate -lt $taskBudget.minimum_remaining) { throw 'Blocked: must preserve at least 1000 credits.' }
        if ($taskBudget.initial_balance - $taskBalance + $taskEstimate -gt $taskBudget.maximum_total_spend) { throw 'Blocked: total spending would exceed 700 credits.' }
        if ($Action -eq 'Preview') {
            $taskEndpoint = '/openapi/v2/text-to-3d'
            $taskBody = @{
                mode='preview'; ai_model='meshy-6-lite'; should_remesh=$true; target_polycount=12000; pose_mode='t-pose'; target_formats=@('fbx','glb');
                prompt='A single stylized dark fantasy humanoid dungeon adventurer for a third person action game. Clear athletic human proportions, simple fitted leather tunic with a steel breastplate, small shoulder plates, trousers, boots and gloves. Uncovered face, short hair. Full body neutral T pose, arms straight out, feet apart, fingers and limbs separated. No weapon, no shield, no cape, no robe, no skirt, no ground base, no accessories, no scenery. Clean readable game silhouette, medium low polygon detail, suitable for automatic skeleton rigging.'
            }
            if ($Prompt) { $taskBody.prompt = $Prompt }
        } elseif ($Action -eq 'Refine') {
            if ($TaskId -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid preview ID.' }
            $taskEndpoint = '/openapi/v2/text-to-3d'
            $taskBody = @{mode='refine';preview_task_id=$TaskId;ai_model='meshy-6-lite';enable_pbr=$true;texture_resolution='2k';target_formats=@('fbx','glb');texture_prompt='Stylized dark fantasy adventurer. Muted dark brown fitted leather, worn steel armor, charcoal trousers, practical boots. Clear readable surfaces, subtle cyan fabric accent, no painted lighting. No logos or text.'}
            if ($TexturePrompt) { $taskBody.texture_prompt = $TexturePrompt }
        } elseif ($Action -eq 'Animation') {
            if ($TaskId -notmatch '^[A-Za-z0-9_-]+$' -or $AnimationIds.Count -lt 1 -or $AnimationIds.Count -gt 10) { throw 'Invalid animation request.' }
            $taskEndpoint='/openapi/v1/animations'
            $taskBody=@{rig_task_id=$TaskId;action_ids=@($AnimationIds)}
        } else {
            if ($TaskId -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid textured task ID.' }
            $taskEndpoint = '/openapi/v1/rigging'
            $taskBody = @{input_task_id=$TaskId;height_meters=1.8}
        }
        # Save intent before sending. An ambiguous network failure must not trigger an automatic retry.
        @{action=$Action;estimated_cost=$taskEstimate;balance_before=$taskBalance;body=$taskBody;state='prepared'} | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath "$taskAssetRoot\$Action-request.json" -Encoding UTF8
        $taskResult = Invoke-RestMethod -Uri "$taskBase$taskEndpoint" -Method Post -Headers $taskHeaders -ContentType 'application/json' -Body ([Text.Encoding]::UTF8.GetBytes(($taskBody | ConvertTo-Json -Depth 10))) -TimeoutSec 60
        @{action=$Action;task_id=$taskResult.result;estimated_cost=$taskEstimate;balance_before=$taskBalance;created_at=[DateTimeOffset]::Now.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $taskReceiptPath -Encoding UTF8
        @{action=$Action;task_id=$taskResult.result;estimated_cost=$taskEstimate;minimum_remaining=$taskBudget.minimum_remaining} | ConvertTo-Json
    }
} catch {
    $taskErrorCode='local_or_network_error'
    if ($_.Exception.Response) { $taskErrorCode=[int]$_.Exception.Response.StatusCode }
    Write-Error "Meshy task did not complete: $taskErrorCode. No automatic retry or purchase."
} finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($taskPointer); $taskKey=$null; $taskHeaders=$null; $taskSecure.Dispose()
}
