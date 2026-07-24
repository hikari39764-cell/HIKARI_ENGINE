[CmdletBinding()]
param(
    [ValidateSet('debug', 'editor-release', 'game-shipping')]
    [string]$Preset = 'debug',

    [switch]$ConfigureOnly,

    [switch]$Install
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$cmake = Get-Command cmake.exe -ErrorAction Stop

$configuration = switch ($Preset) {
    'debug' { 'Debug' }
    'editor-release' { 'EditorRelease' }
    'game-shipping' { 'Shipping' }
}
$buildDirectory = Join-Path $repoRoot "out\build\$Preset"

Push-Location $repoRoot
try {
    & $cmake.Source --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed for preset '$Preset'."
    }
    if ($ConfigureOnly) {
        exit 0
    }

    & $cmake.Source --build --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed for preset '$Preset'."
    }

    if ($Install) {
        & $cmake.Source `
            --install $buildDirectory `
            --config $configuration
        if ($LASTEXITCODE -ne 0) {
            throw "CMake install failed for preset '$Preset'."
        }
    }
} finally {
    Pop-Location
}
