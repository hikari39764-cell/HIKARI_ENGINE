[CmdletBinding()]
param(
    [string]$VcpkgRoot
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
        $VcpkgRoot = $env:VCPKG_ROOT
    } else {
        $vcpkgCommand = Get-Command vcpkg.exe -ErrorAction SilentlyContinue
        if ($vcpkgCommand) {
            $VcpkgRoot = Split-Path -Parent $vcpkgCommand.Source
        }
    }
}
if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    throw (
        'vcpkg was not found. Pass -VcpkgRoot or set VCPKG_ROOT. ' +
        'The repository does not download package managers implicitly.')
}

$vcpkg = Join-Path $VcpkgRoot 'vcpkg.exe'
if (-not (Test-Path -LiteralPath $vcpkg -PathType Leaf)) {
    throw "vcpkg.exe was not found under: $VcpkgRoot"
}

$dynamicInstallRoot = Join-Path $repoRoot 'vcpkg_installed'
$staticInstallRoot = Join-Path $repoRoot 'vcpkg_installed_static'
$staticManifestRoot = Join-Path $repoRoot 'Tools\Dependencies\vcpkg-static'

& $vcpkg install `
    --x-manifest-root $repoRoot `
    --x-install-root $dynamicInstallRoot `
    --triplet x64-windows
if ($LASTEXITCODE -ne 0) {
    throw 'Restoring the x64-windows dependency tree failed.'
}

& $vcpkg install `
    --x-manifest-root $staticManifestRoot `
    --x-install-root $staticInstallRoot `
    --triplet x64-windows-static
if ($LASTEXITCODE -ne 0) {
    throw 'Restoring the x64-windows-static runtime dependencies failed.'
}

Write-Host 'Pinned HIKARI vcpkg dependencies restored.'
