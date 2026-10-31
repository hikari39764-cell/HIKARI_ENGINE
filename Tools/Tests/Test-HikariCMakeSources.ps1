$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$manifestPath = Join-Path $repoRoot 'cmake\HikariSources.cmake'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'The authoritative CMake source manifest is missing.'
}

$manifest = [System.IO.File]::ReadAllText(
    $manifestPath,
    [System.Text.Encoding]::UTF8)
$listedSources = @(
    [regex]::Matches(
        $manifest,
        '\$\{HIKARI_SOURCE_ROOT\}/([^"]+\.cpp)') |
        ForEach-Object {
            $_.Groups[1].Value.Replace('\', '/')
        }
)
if ($listedSources.Count -eq 0) {
    throw 'No compile sources were found in cmake/HikariSources.cmake.'
}

$duplicateSources = @(
    $listedSources |
        Group-Object { $_.ToLowerInvariant() } |
        Where-Object { $_.Count -gt 1 } |
        ForEach-Object { $_.Group[0] }
)
if ($duplicateSources.Count -ne 0) {
    throw "Duplicate CMake compile sources: $($duplicateSources -join ', ')"
}

$missingFiles = @(
    $listedSources |
        Where-Object {
            -not (Test-Path -LiteralPath (Join-Path $repoRoot $_) -PathType Leaf)
        }
)
if ($missingFiles.Count -ne 0) {
    throw "CMake source files are missing: $($missingFiles -join ', ')"
}

$ownedSourceRoots = @(
    'HIKARI',
    'Tests\Fixtures',
    'ThirdParty\spine',
    'ThirdParty\meshoptimizer',
    'ThirdParty\xatlas',
    'ThirdParty\imgui',
    'ThirdParty\ImGuizmo'
)
# Fixture game projects (directories with Project.hikari) build their own sources.
$projectRoots = @(
    Get-ChildItem -LiteralPath (Join-Path $repoRoot 'Tests\Fixtures') -Recurse -File -Filter 'Project.hikari' |
        ForEach-Object { $_.DirectoryName.Substring($repoRoot.Length + 1).Replace('\', '/') + '/' }
)
$expectedSources = @()
foreach ($sourceRoot in $ownedSourceRoots) {
    $absoluteRoot = Join-Path $repoRoot $sourceRoot
    $expectedSources += @(
        Get-ChildItem -LiteralPath $absoluteRoot -Recurse -File -Filter '*.cpp' |
            ForEach-Object {
                $_.FullName.Substring($repoRoot.Length + 1).Replace('\', '/')
            } |
            Where-Object {
                $path = $_
                -not @($projectRoots | Where-Object { $path.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) })
            }
    )
}

$expectedSources = @($expectedSources | Sort-Object -Unique)

$sourceDifference = @(
    Compare-Object $expectedSources $listedSources
)
if ($sourceDifference.Count -ne 0) {
    $missingFromManifest = @(
        $sourceDifference |
            Where-Object { $_.SideIndicator -eq '<=' } |
            Select-Object -ExpandProperty InputObject
    )
    $unexpectedInManifest = @(
        $sourceDifference |
            Where-Object { $_.SideIndicator -eq '=>' } |
            Select-Object -ExpandProperty InputObject
    )
    throw (
        "CMake source manifest does not match the owned source closure. " +
        "Missing: [$($missingFromManifest -join ', ')]. " +
        "Unexpected: [$($unexpectedInManifest -join ', ')].")
}

Write-Host (
    "CMake source manifest checks passed: " +
    "$($listedSources.Count) compile source(s).")
exit 0
