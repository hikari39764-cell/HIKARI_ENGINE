[CmdletBinding()]
param(
    [ValidateSet('source', 'assets', 'editor', 'ui', 'camera', 'terrain', 'vfx', 'runtime', 'render', 'gpu', 'sdk')]
    [string]$Group = 'source'
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$buildRoot = Join-Path $repoRoot 'out/build/editor-release'
Push-Location $repoRoot
try {
    if ($Group -eq 'source') {
        foreach ($check in 'HikariCMakeSources', 'HikariLocalizationTables', 'HikariAiBridgeSecurity', 'HikariRuntimeModuleBoundaries') {
            & (Join-Path $PSScriptRoot "Test-$check.ps1")
        }
        return
    }
    & cmake --preset editor-release
    if ($LASTEXITCODE -ne 0) { throw 'EditorRelease configuration failed.' }
    if ($Group -eq 'sdk') {
        & cmake --build $buildRoot --config EditorRelease --target HIKARI_GameProjectActionSdkContracts -- /m:2 /nologo /v:minimal
        if ($LASTEXITCODE -ne 0) { throw 'Public SDK compile contract failed.' }
        & (Join-Path $PSScriptRoot 'Test-GameProjectPublicHeaders.ps1')
        return
    }
    # CTest owns suite membership; do not maintain another source/target list.
    $listing = & ctest --test-dir $buildRoot -C EditorRelease --show-only=json-v1 -L "^$Group$"
    if ($LASTEXITCODE -ne 0) { throw 'Unable to list behavior tests.' }
    $targets = @((($listing -join "`n") | ConvertFrom-Json).tests | ForEach-Object { $_.name })
    if ($targets.Count -eq 0) { throw "No configured tests for group: $Group" }
    & cmake --build $buildRoot --config EditorRelease --target @targets -- /m:2 /nologo /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "Test build failed: $Group" }
    & ctest --test-dir $buildRoot -C EditorRelease -L "^$Group$" --output-on-failure --no-tests=error
    if ($LASTEXITCODE -ne 0) { throw "Behavior tests failed: $Group" }
} finally {
    Pop-Location
}
