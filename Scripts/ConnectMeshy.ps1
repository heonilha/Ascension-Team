$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()
$taskSecretRoot = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'AscensionPrototype\Secrets'
New-Item -ItemType Directory -Path $taskSecretRoot -Force | Out-Null
$taskStatusPath = Join-Path $taskSecretRoot 'connection-status.json'
$taskForm = New-Object System.Windows.Forms.Form
$taskForm.Text = 'Meshy 연결 · 추가 결제 없음'
$taskForm.Size = New-Object System.Drawing.Size(560,250)
$taskForm.StartPosition = 'CenterScreen'
$taskForm.TopMost = $true
$taskForm.FormBorderStyle = 'FixedDialog'
$taskForm.MaximizeBox = $false
$taskLabel = New-Object System.Windows.Forms.Label
$taskLabel.Location = New-Object System.Drawing.Point(20,20)
$taskLabel.Size = New-Object System.Drawing.Size(510,55)
$taskLabel.Text = "발급받은 Meshy API 키를 아래에 붙여넣으세요.`r`n잔액만 조회한 뒤 Windows 계정으로 암호화해 저장합니다."
$taskBox = New-Object System.Windows.Forms.TextBox
$taskBox.Location = New-Object System.Drawing.Point(20,85)
$taskBox.Size = New-Object System.Drawing.Size(510,28)
$taskBox.UseSystemPasswordChar = $true
$taskButton = New-Object System.Windows.Forms.Button
$taskButton.Location = New-Object System.Drawing.Point(365,135)
$taskButton.Size = New-Object System.Drawing.Size(165,35)
$taskButton.Text = '연결 확인 및 저장'
$taskHint = New-Object System.Windows.Forms.Label
$taskHint.Location = New-Object System.Drawing.Point(20,140)
$taskHint.Size = New-Object System.Drawing.Size(325,55)
$taskHint.Text = '키는 채팅·게임 폴더·로그에 저장하지 않습니다.'
$taskButton.Add_Click({
    $taskKey = $taskBox.Text.Trim()
    if ([string]::IsNullOrWhiteSpace($taskKey)) { $taskHint.Text = 'API 키를 입력해주세요.'; return }
    $taskButton.Enabled = $false
    $taskHint.Text = '잔액을 조회하고 있습니다…'
    $taskForm.Refresh()
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $taskResult = Invoke-RestMethod -Uri 'https://api.meshy.ai/openapi/v1/balance' -Method Get -Headers @{Authorization = "Bearer $taskKey"} -TimeoutSec 30
        if ($null -eq $taskResult.balance) { throw 'Unexpected balance response' }
        $taskSecureKey = ConvertTo-SecureString -String $taskKey -AsPlainText -Force
        $taskEncryptedKey = ConvertFrom-SecureString -SecureString $taskSecureKey
        [IO.File]::WriteAllText((Join-Path $taskSecretRoot 'meshy-key.dpapi'), $taskEncryptedKey)
        @{state='connected'; balance=$taskResult.balance; checked_at=[DateTimeOffset]::Now.ToString('o'); additional_purchase_allowed=$false} | ConvertTo-Json | Set-Content -LiteralPath $taskStatusPath -Encoding UTF8
        $taskBox.Clear(); $taskKey = $null
        [System.Windows.Forms.MessageBox]::Show("연결 확인 완료! 잔여 크레딧: $($taskResult.balance)`r`n추가 결제 없이 기존 크레딧만 사용합니다.",'Meshy 연결') | Out-Null
        $taskForm.Close()
    } catch {
        $taskCode = 'network_or_auth_error'
        if ($_.Exception.Response) { $taskCode = [int]$_.Exception.Response.StatusCode }
        @{state='failed'; reason=$taskCode; checked_at=[DateTimeOffset]::Now.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $taskStatusPath -Encoding UTF8
        $taskHint.Text = '연결 실패. 키와 네트워크를 확인해주세요.'
        $taskButton.Enabled = $true
    } finally { $taskKey = $null }
})
$taskForm.Controls.AddRange(@($taskLabel,$taskBox,$taskButton,$taskHint))
$taskForm.AcceptButton = $taskButton
$taskForm.Add_Shown({$taskBox.Focus()})
$taskForm.ShowDialog() | Out-Null
$taskForm.Dispose()
