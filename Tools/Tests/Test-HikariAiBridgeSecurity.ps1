$ErrorActionPreference = 'Stop'

# ローカル Bridge のセキュリティ前提（loopback 限定、ランダム token、定数時間比較、設定で止められる、
# 接続ファイルは所有者だけが読める）を守る。既定で開く（利用者の決定、2026-10-10）。操作一覧はエディターの
# operations.search が登録済み操作から実行時に返すため、静的な一覧は持たない。
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

function Read-Source([string]$relativePath) {
    return [System.IO.File]::ReadAllText((Join-Path $repoRoot $relativePath), [System.Text.Encoding]::UTF8)
}

$bridge = Read-Source 'HIKARI\Editor\Agent\HIKARI_AgentBridge.cpp'
$connection = Read-Source 'HIKARI\Editor\Agent\HIKARI_AgentConnection.cpp'
$context = Read-Source 'HIKARI\Editor\HIKARI_EditorContext.h'
$settings = Read-Source 'HIKARI\Editor\Settings\HIKARI_EditorSettingsPanel.cpp'
$controller = Read-Source 'HIKARI\Editor\Controllers\DocumentScene\HIKARI_DocumentSceneEditorController.cpp'

$failures = @()
if ($bridge -notmatch 'INADDR_LOOPBACK') { $failures += 'bridge must bind to 127.0.0.1 only' }
if ($bridge -notmatch '"POST"' -or $bridge -notmatch '/v2/operation') { $failures += 'bridge must accept operations only through POST /v2/operation' }
if ($bridge -match 'getenv' -or $connection -match 'getenv') { $failures += 'bridge port and token must not come from the environment' }
if ($bridge -match 'Access-Control-Allow-Origin') { $failures += 'bridge must not send CORS headers' }
if ($bridge -notmatch 'BCryptGenRandom') { $failures += 'bridge token must be a random session token' }
if ($bridge -notmatch 'ConstantTimeEquals') { $failures += 'bridge token must be compared in constant time' }
if ($connection -notmatch 'D:P\(A;;FA;;;OW\)') { $failures += 'connection file must be readable only by its owner' }
if (-not $settings.Contains('aiIntegration.localBridgeEnabled')) { $failures += 'bridge must be user-controlled in settings' }
if (-not $controller.Contains('!integration.localBridgeEnabled') -or -not $controller.Contains('agentBridge_.Start')) {
    $failures += 'bridge must start only when the setting is enabled'
}

if ($failures.Count -gt 0) {
    throw ('AI local bridge security contract failed: ' + ($failures -join '; '))
}
Write-Host 'HIKARI AI local bridge security contract passed.'
