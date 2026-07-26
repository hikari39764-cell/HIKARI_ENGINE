[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Project,

    [ValidateSet('debug', 'editor-release', 'game-shipping')]
    [string]$Preset = 'debug',

    [Parameter(Mandatory)]
    [string]$SdkRoot,

    [switch]$ConfigureOnly,

    [string]$InstallDirectory
)

$ErrorActionPreference = 'Stop'

$cmake = Get-Command cmake.exe -ErrorAction Stop
$projectInput = $Project
if (Test-Path -LiteralPath $projectInput -PathType Leaf) {
    $projectInput = Split-Path -Parent $projectInput
}
$projectRoot = (Resolve-Path -LiteralPath $projectInput).Path
$sdkRootPath = (Resolve-Path -LiteralPath $SdkRoot).Path
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
if ([IO.Path]::GetFileName($projectManifest) -ne 'CMakeLists.txt') {
    throw 'gameProjectManifest must identify a CMakeLists.txt file.'
}

$configuration = switch ($Preset) {
    'debug' { 'Debug' }
    'editor-release' { 'EditorRelease' }
    'game-shipping' { 'Shipping' }
}
$manifestRoot = Split-Path -Parent $projectManifest
$projectBuildDirectory = Join-Path `
    $projectRoot "Library\Build\CMake\sdk-v1\$Preset"
$projectOutputDirectory = Join-Path `
    $projectRoot "Library\Build\$Preset"
$packageDirectory = Join-Path $sdkRootPath 'lib\cmake\HIKARI'
$packageConfig = Join-Path $packageDirectory 'HIKARIConfig.cmake'
if (-not (Test-Path -LiteralPath $packageConfig -PathType Leaf)) {
    throw "HIKARI SDK package is incomplete: $packageConfig"
}

& $cmake.Source `
    -S $manifestRoot `
    -B $projectBuildDirectory `
    -G 'Visual Studio 17 2022' `
    -A x64 `
    "-DCMAKE_CONFIGURATION_TYPES:STRING=$configuration" `
    "-DHIKARI_DIR:PATH=$packageDirectory" `
    "-DHIKARI_GAME_PROJECT_OUTPUT_ROOT:PATH=$projectOutputDirectory"
if ($LASTEXITCODE -ne 0) {
    throw "GameProject configure failed for preset '$Preset'."
}

if (-not $ConfigureOnly) {
    & $cmake.Source `
        --build $projectBuildDirectory `
        --config $configuration `
        --target HIKARI_GameProjectModule
    if ($LASTEXITCODE -ne 0) {
        throw "GameProject build failed for preset '$Preset'."
    }
}

if (-not [string]::IsNullOrWhiteSpace($InstallDirectory) -and
    -not $ConfigureOnly) {
    & $cmake.Source `
        --install $projectBuildDirectory `
        --config $configuration `
        --prefix $InstallDirectory
    if ($LASTEXITCODE -ne 0) {
        throw "GameProject install failed for preset '$Preset'."
    }
}
