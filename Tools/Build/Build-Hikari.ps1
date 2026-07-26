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
if (-not [string]::IsNullOrWhiteSpace($Project)) {
    $projectInput = $Project
    if (Test-Path -LiteralPath $projectInput -PathType Leaf) {
        $projectInput = Split-Path -Parent $projectInput
    }
    $projectRoot = (Resolve-Path -LiteralPath $projectInput).Path
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
            'HIKARI_ProjectLauncher'
        }
        & $cmake.Source `
            --build $buildDirectory `
            --config $configuration `
            --target $projectTarget
        if ($LASTEXITCODE -ne 0) {
            throw "HIKARI host build failed for preset '$Preset'."
        }

        $sdkRoot = Join-Path $buildDirectory 'bin\Sdk'
        $projectBuildScript = Join-Path `
            $sdkRoot 'Tools\Build-HikariProject.ps1'
        if (-not (Test-Path -LiteralPath $projectBuildScript -PathType Leaf)) {
            throw "The staged HIKARI SDK is incomplete: $projectBuildScript"
        }

        $projectArguments = @(
            '-NoProfile',
            '-ExecutionPolicy',
            'Bypass',
            '-File',
            $projectBuildScript,
            '-Project',
            $projectRoot,
            '-Preset',
            $Preset,
            '-SdkRoot',
            $sdkRoot)
        if ($ConfigureOnly) {
            $projectArguments += '-ConfigureOnly'
        }
        if ($Install -and -not $ConfigureOnly) {
            $projectArguments += @(
                '-InstallDirectory',
                (Join-Path $projectRoot "out\install\$Preset"))
        }
        & powershell.exe @projectArguments
        if ($LASTEXITCODE -ne 0) {
            throw "GameProject SDK build failed for preset '$Preset'."
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
    }
} finally {
    Pop-Location
}
