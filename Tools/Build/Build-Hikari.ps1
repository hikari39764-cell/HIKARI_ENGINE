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
$projectBuildDirectory = $null
$projectOutputDirectory = $null
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
    $projectBuildDirectory = Join-Path `
        $projectRoot "Library\Build\CMake\$Preset"
    $projectOutputDirectory = Join-Path `
        $projectRoot "Library\Build\$Preset"
}

Push-Location $repoRoot
try {
    $engineCache = Join-Path $buildDirectory 'CMakeCache.txt'
    if ($ConfigureOnly -or
        -not (Test-Path -LiteralPath $engineCache -PathType Leaf)) {
        & $cmake.Source --preset $Preset
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configure failed for preset '$Preset'."
        }
    }

    if ($null -ne $projectRoot) {
        $projectTarget = if ($Preset -eq 'game-shipping') {
            'HIKARI_GameHost'
        } else {
            'HIKARI_EditorHost'
        }
        if (-not $ConfigureOnly) {
            & $cmake.Source `
                --build $buildDirectory `
                --config $configuration `
                --target $projectTarget
            if ($LASTEXITCODE -ne 0) {
                throw "HIKARI host build failed for preset '$Preset'."
            }
        }

        $projectCache = Join-Path `
            $projectBuildDirectory 'CMakeCache.txt'
        if ($ConfigureOnly -or
            -not (Test-Path -LiteralPath $projectCache -PathType Leaf)) {
            & $cmake.Source `
                -S (Join-Path $repoRoot 'cmake\GameProjectModule') `
                -B $projectBuildDirectory `
                -G 'Visual Studio 17 2022' `
                -A x64 `
                "-DCMAKE_CONFIGURATION_TYPES:STRING=$configuration" `
                "-DHIKARI_PRODUCT:STRING=$configuration" `
                "-DHIKARI_ENGINE_ROOT:PATH=$repoRoot" `
                "-DHIKARI_ENGINE_BUILD_ROOT:PATH=$buildDirectory" `
                "-DHIKARI_GAME_PROJECT_MANIFEST:FILEPATH=$projectManifest" `
                "-DHIKARI_GAME_PROJECT_OUTPUT_ROOT:PATH=$projectOutputDirectory"
            if ($LASTEXITCODE -ne 0) {
                throw (
                    "GameProject module configure failed for " +
                    "preset '$Preset'.")
            }
        }
        if (-not $ConfigureOnly) {
            & $cmake.Source `
                --build $projectBuildDirectory `
                --config $configuration `
                --target HIKARI_GameProjectModule
            if ($LASTEXITCODE -ne 0) {
                throw (
                    "GameProject module build failed for " +
                    "preset '$Preset'.")
            }
        }
    } else {
        if (-not $ConfigureOnly) {
            & $cmake.Source --build --preset $Preset
        }
    }
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed for preset '$Preset'."
    }

    if ($Install -and -not $ConfigureOnly) {
        $installArguments = @(
            '--install',
            $buildDirectory,
            '--config',
            $configuration)
        if ($null -ne $projectRoot) {
            $installDirectory = Join-Path `
                $projectRoot "out\install\$Preset"
            $installArguments += @(
                '--prefix',
                $installDirectory)
        }
        & $cmake.Source `
            @installArguments
        if ($LASTEXITCODE -ne 0) {
            throw "CMake install failed for preset '$Preset'."
        }
        if ($null -ne $projectRoot) {
            & $cmake.Source `
                --install $projectBuildDirectory `
                --config $configuration `
                --prefix $installDirectory
            if ($LASTEXITCODE -ne 0) {
                throw "GameProject module install failed for preset '$Preset'."
            }
        }
    }
} finally {
    Pop-Location
}
