[CmdletBinding()]
param(
    [ValidateSet('debug', 'editor-release', 'game-shipping')]
    [string]$Preset = 'editor-release',

    [switch]$ConfigureOnly,

    [switch]$Install,

    [switch]$ReportBudget,

    [string]$Project
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

# Prefer cmake on PATH; fall back to the CMake bundled with the editor build, then the one shipped with
# Visual Studio, so the export's Shipping build also works on machines without cmake on PATH.
# (Comments stay ASCII: Windows PowerShell 5.1 reads this BOM-less script in the system code page.)
function Resolve-HikariBuildCMake {
    $candidates = @()
    $pathCMake = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($null -ne $pathCMake) { $candidates += $pathCMake.Source }
    $candidates += Join-Path $repoRoot 'out\build\editor-release\bin\Runtime\Sdk\Tools\CMake\bin\cmake.exe'
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        foreach ($installation in @(& $vswhere -products * -version '[17.0,)' -property installationPath)) {
            $candidates += Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        }
    }
    foreach ($candidate in $candidates) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
        # A broken candidate (for example one missing its modules) writes to stderr, which is terminating
        # under 'Stop' in Windows PowerShell 5.1; skip it and try the next one.
        try {
            $ErrorActionPreference = 'Continue'
            $output = @(& $candidate --version 2>&1) -join "`n"
            if ($LASTEXITCODE -ne 0) { continue }
        } catch {
            continue
        } finally {
            $ErrorActionPreference = 'Stop'
        }
        $version = [regex]::Match($output, '(?im)^cmake version\s+(\d+)\.(\d+)')
        if ($version.Success -and ([int]$version.Groups[1].Value -gt 3 -or
                ([int]$version.Groups[1].Value -eq 3 -and [int]$version.Groups[2].Value -ge 28))) {
            return $candidate
        }
    }
    throw 'CMake 3.28 or newer was not found (PATH, the editor build''s bundled CMake, or Visual Studio).'
}
$cmake = Resolve-HikariBuildCMake
$buildStopwatch = [System.Diagnostics.Stopwatch]::StartNew()

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

$gameHostExecutableName = 'HIKARI_Game.exe'
$gameHostOutputName = 'HIKARI_Game'

Push-Location $repoRoot
try {
    # Configure is intentionally incremental and always precedes a build.
    # A failed CMake generation can leave a valid-looking CMakeCache.txt while
    # the selected target project is missing.
    $configureArguments = @('--preset', $Preset)
    if ($Preset -eq 'game-shipping') {
        $configureArguments +=
            "-DHIKARI_GAME_HOST_OUTPUT_NAME:STRING=$gameHostOutputName"
    }
    & $cmake @configureArguments
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed for preset '$Preset'."
    }

    if ($null -ne $projectRoot) {
        $projectTarget = if ($Preset -eq 'game-shipping') {
            'HIKARI_GameHost'
        } else {
            'HIKARI_ProjectLauncher'
        }
        & $cmake `
            --build $buildDirectory `
            --config $configuration `
            --target $projectTarget
        if ($LASTEXITCODE -ne 0) {
            throw "HIKARI host build failed for preset '$Preset'."
        }

        $sdkRelative = if ($Preset -eq 'game-shipping') { 'bin\Sdk' } else { 'bin\Runtime\Sdk' }
        $sdkRoot = Join-Path $buildDirectory $sdkRelative
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
            & $cmake --build --preset $Preset
        }
    }
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed for preset '$Preset'."
    }

    if ($Install -and -not $ConfigureOnly) {
        if ($Preset -ne 'game-shipping') {
            & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Preset game-shipping -Install
            if ($LASTEXITCODE -ne 0) { throw 'Could not prepare the paired Shipping runtime kit.' }
        }
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
        & $cmake `
            @installArguments
        if ($LASTEXITCODE -ne 0) {
            throw "CMake install failed for preset '$Preset'."
        }
    }
    $buildStopwatch.Stop()
    $workflowSeconds = [math]::Round(
        $buildStopwatch.Elapsed.TotalSeconds,
        2)
    Write-Host (
        "HIKARI '$Preset' workflow completed in " +
        "$workflowSeconds second(s).")
    if ($ReportBudget) {
        & (Join-Path $PSScriptRoot 'Measure-HikariBuildBudget.ps1') `
            -Preset $Preset `
            -WriteSnapshot `
            -WorkflowSeconds $workflowSeconds
        if ($LASTEXITCODE -ne 0) {
            throw "HIKARI build budget reporting failed for '$Preset'."
        }
    }
} finally {
    if ($buildStopwatch.IsRunning) {
        $buildStopwatch.Stop()
    }
    Pop-Location
}
