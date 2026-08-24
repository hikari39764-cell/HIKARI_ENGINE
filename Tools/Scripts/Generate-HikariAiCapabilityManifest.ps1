param(
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $OutputPath) {
    $OutputPath = Join-Path $repoRoot 'docs\ai\hikari-ai-capabilities.json'
}

# Registered editor operations are the single source of truth for executable
# AI capabilities. The manifest generator extracts them from the registration
# sites so adding an operation never requires a second manual edit.
$registryFiles = @(
    'HIKARI\Editor\Authoring\HIKARI_ComponentAuthoringOperations.cpp',
    'HIKARI\Editor\Controllers\DocumentScene\HIKARI_DocumentSceneEditorOperations.cpp',
    'HIKARI\Editor\Workspaces\ModelCollision\HIKARI_ModelCollisionOperations.cpp',
    'HIKARI\Editor\Workspaces\AnimationStateMachine\HIKARI_AnimationStateMachineOperations.cpp',
    'HIKARI\Editor\Workspaces\Cinematics\HIKARI_CinematicsOperations.cpp',
    'HIKARI\Editor\Workspaces\Dialogue\HIKARI_DialogueOperations.cpp',
    'HIKARI\Editor\Workspaces\ModelMaterial\HIKARI_ModelMaterialAssetOperations.cpp',
    'HIKARI\Editor\Workspaces\ModelMaterial\HIKARI_ModelMaterialOperations.cpp',
    'HIKARI\Editor\Workspaces\RuntimeBinding\HIKARI_RuntimeBindingOperations.cpp',
    'HIKARI\Editor\Workspaces\Vfx\HIKARI_VfxBindingOperations.cpp',
    'HIKARI\Editor\Workspaces\Vfx\HIKARI_VfxCurveOperations.cpp',
    'HIKARI\Editor\Workspaces\Vfx\HIKARI_VfxOperations.cpp',
    'HIKARI\Editor\Workspaces\Vfx\HIKARI_VfxPreviewOperations.cpp'
)

$operations = @{}
$registrationPatterns = @(
    'Register\(\{\s*"([a-z][a-z0-9_.]+)"\s*,\s*"([^"]+)"',
    'register[A-Z][A-Za-z0-9]*Operation\(\s*"([a-z][a-z0-9_.]+)"\s*,\s*"([^"]+)"'
)
foreach ($relative in $registryFiles) {
    $path = Join-Path $repoRoot $relative
    $source = [System.IO.File]::ReadAllText(
        $path,
        [System.Text.Encoding]::UTF8)
    foreach ($pattern in $registrationPatterns) {
        foreach ($match in [regex]::Matches($source, $pattern)) {
            $id = $match.Groups[1].Value
            $summary = $match.Groups[2].Value
            if ($operations.ContainsKey($id) -and
                $operations[$id] -ne $summary) {
                throw "Operation registered with different summaries: $id"
            }
            $operations[$id] = $summary
        }
    }
}

# Panel-local capabilities live in the BuiltInCapabilities table and do not
# map to registered operations (chat/vision/help/queries/documentation
# boundaries plus explicit unsupported entries).
$panelPath = Join-Path $repoRoot 'HIKARI\Editor\AI\HIKARI_AI_AssistantPanel.cpp'
$panelSource = [System.IO.File]::ReadAllText(
    $panelPath,
    [System.Text.Encoding]::UTF8)
$builtinMatch = [regex]::Match(
    $panelSource,
    'BuiltInCapabilities\(\) \{\s*static const std::vector<AiCapability> capabilities\{([\s\S]*?)\n    \};')
if (-not $builtinMatch.Success) {
    throw 'BuiltInCapabilities table not found in AI panel source.'
}

$capabilities = @{}
foreach ($entry in [regex]::Matches(
    $builtinMatch.Groups[1].Value,
    '\{\s*"([a-z][a-z0-9._]+)"\s*,\s*"([^"]+)"\s*,\s*"(stable|planned|unsupported)"\s*\}')) {
    $capabilities[$entry.Groups[1].Value] = @{
        id = $entry.Groups[1].Value
        status = $entry.Groups[3].Value
        summary = $entry.Groups[2].Value
    }
}

foreach ($id in $operations.Keys) {
    if (-not $capabilities.ContainsKey($id)) {
        $capabilities[$id] = @{
            id = $id
            status = 'stable'
            summary = $operations[$id]
        }
    }
}

$capabilityList = @(
    $capabilities.Values |
        Sort-Object { [string]$_['id'] })

$manifest = [ordered]@{
    manifestVersion = 1
    updated = (Get-Date -Format 'yyyy-MM-dd')
    callingMethod = 'Send {"action":"..."} JSON through the Editor AI Assistant window, or POST it to the loopback local bridge at http://127.0.0.1:18725/action.'
    capabilities = $capabilityList
    boundaries = @(
        'No AI action may mutate engine internals without going through an editor command entry.',
        'Unsupported actions return ok=false with an explicit boundary message.',
        'Long-running operations (import, bake) require an async status contract before they are exposed.',
        'SceneColor/SceneDepth sampling, vertex displacement, refraction and custom HLSL are not material capabilities.'
    )
    failureSemantics = 'Unknown or unsupported actions are rejected explicitly. Partial multi-step work must be reported per step with the failing reason. AI never claims success without a verified result (compile diagnostic, query result or preview output).'
}

$json = $manifest | ConvertTo-Json -Depth 5
[System.IO.File]::WriteAllText(
    $OutputPath,
    $json + [Environment]::NewLine,
    [System.Text.Encoding]::UTF8)

Write-Host "Generated AI capability manifest: $OutputPath ($($capabilityList.Count) capabilities)."
