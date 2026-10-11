$ErrorActionPreference = 'Stop'
$taskPath = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'AscensionPrototype\Secrets\meshy-key.dpapi'
if (-not (Test-Path -LiteralPath $taskPath)) { throw 'Meshy key is not configured. Run ConnectMeshy.ps1.' }
$taskSecure = ConvertTo-SecureString ([IO.File]::ReadAllText($taskPath))
$taskPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($taskSecure)
try {
    $taskKey = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($taskPointer)
    $taskResult = Invoke-RestMethod -Uri 'https://api.meshy.ai/openapi/v1/balance' -Method Get -Headers @{Authorization="Bearer $taskKey"} -TimeoutSec 30
    @{state='connected'; balance=$taskResult.balance; additional_purchase_allowed=$false} | ConvertTo-Json
} catch { throw 'Meshy balance check failed. Check the connection locally; credentials were not printed.' }
finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($taskPointer); $taskKey=$null; $taskSecure.Dispose() }
