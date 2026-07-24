[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$lockPath = Join-Path $PSScriptRoot 'HIKARI_ThirdParty.lock.json'

if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
    throw 'The HIKARI third-party lock file is missing.'
}

$lock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 |
    ConvertFrom-Json
if ($lock.schemaVersion -ne 1) {
    throw "Unsupported dependency lock schema: $($lock.schemaVersion)"
}

foreach ($installation in $lock.vcpkg.installations) {
    $manifestPath = Join-Path $repoRoot $installation.manifest
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Missing vcpkg manifest: $($installation.manifest)"
    }

    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 |
        ConvertFrom-Json
    if ($manifest.'builtin-baseline' -ne $lock.vcpkg.builtinBaseline) {
        throw (
            "vcpkg baseline mismatch in {0}: expected {1}, found {2}" -f
            $installation.manifest,
            $lock.vcpkg.builtinBaseline,
            $manifest.'builtin-baseline')
    }

    $actualPackages = @($manifest.dependencies | Sort-Object)
    $expectedPackages = @($installation.packages | Sort-Object)
    if (
        ($actualPackages -join "`n") -cne
        ($expectedPackages -join "`n")) {
        throw (
            "vcpkg package set mismatch in {0}: expected [{1}], found [{2}]" -f
            $installation.manifest,
            ($expectedPackages -join ', '),
            ($actualPackages -join ', '))
    }
}

$checkedArtifacts = 0
$skippedArtifacts = 0
foreach ($dependency in $lock.dependencies) {
    if ($null -eq $dependency.artifacts) {
        continue
    }

    foreach ($artifact in $dependency.artifacts) {
        $artifactPath = Join-Path $repoRoot $artifact.path
        if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
            if ($artifact.required) {
                throw (
                    "Missing required {0} artifact: {1}" -f
                    $dependency.name,
                    $artifact.path)
            }

            $skippedArtifacts += 1
            continue
        }

        $actualHash = (
            Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256
        ).Hash
        if ($actualHash -cne $artifact.sha256) {
            throw (
                "SHA-256 mismatch for {0}: expected {1}, found {2}" -f
                $artifact.path,
                $artifact.sha256,
                $actualHash)
        }
        $checkedArtifacts += 1
    }
}

$resultMessage = (
    'Dependency snapshot checks passed: {0} artifact(s) verified, ' +
    '{1} optional artifact(s) absent.') -f
    $checkedArtifacts,
    $skippedArtifacts
Write-Host $resultMessage
