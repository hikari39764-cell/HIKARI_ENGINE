[CmdletBinding()]
param(
    [ValidateSet('debug', 'editor-release', 'game-shipping')]
    [string]$Preset = 'debug',

    [switch]$ConfigureOnly,

    [switch]$Install,

    [string]$Project
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
$projectRoot = $null
$projectManifest = $null
if (-not [string]::IsNullOrWhiteSpace($Project)) {
    $projectInput = $Project
    if (Test-Path -LiteralPath $projectInput -PathType Leaf) {
        $projectInput = Split-Path -Parent $projectInput
    }
    $projectRoot = (Resolve-Path -LiteralPath $projectInput).Path
    $descriptorPath = Join-Path $projectRoot 'Project.hikari'
    if (-not (Test-Path -LiteralPath $descriptorPath -PathType Leaf)) {
        throw "Project.hikari was not found: $descriptorPath"
    }
    $descriptor = Get-Content -LiteralPath $descriptorPath -Raw |
        ConvertFrom-Json
    $manifestRelative = if (
        $null -ne $descriptor.gameProjectManifest -and
        -not [string]::IsNullOrWhiteSpace(
            [string]$descriptor.gameProjectManifest)) {
        [string]$descriptor.gameProjectManifest
    } else {
        'CMakeLists.txt'
    }
    if ([IO.Path]::IsPathRooted($manifestRelative)) {
        throw 'gameProjectManifest must be project-relative.'
    }
    $projectManifest = [IO.Path]::GetFullPath(
        (Join-Path $projectRoot $manifestRelative))
    $projectRootPrefix = [IO.Path]::GetFullPath(
        $projectRoot + [IO.Path]::DirectorySeparatorChar)
    if (-not $projectManifest.StartsWith(
            $projectRootPrefix,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'gameProjectManifest must stay inside the project root.'
    }
    if (-not (Test-Path -LiteralPath $projectManifest -PathType Leaf)) {
        throw "GameProject CMake manifest was not found: $projectManifest"
    }
}

Push-Location $repoRoot
try {
    $configureArguments = @('--preset', $Preset)
    if ($null -ne $projectManifest) {
        $configureArguments += (
            "-DHIKARI_GAME_PROJECT_MANIFEST:FILEPATH=$projectManifest")
    }
    & $cmake.Source @configureArguments
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed for preset '$Preset'."
    }
    if ($ConfigureOnly) {
        exit 0
    }

    if ($null -ne $projectRoot) {
        $projectTarget = if ($Preset -eq 'game-shipping') {
            'HIKARI_GameHost'
        } else {
            'HIKARI_EditorHost'
        }
        & $cmake.Source `
            --build $buildDirectory `
            --config $configuration `
            --target $projectTarget
    } else {
        & $cmake.Source --build --preset $Preset
    }
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
