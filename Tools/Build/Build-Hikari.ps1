[CmdletBinding()]
param(
    [ValidateSet('debug', 'editor-release', 'game-shipping')]
    [string]$Preset = 'debug',

    [switch]$ConfigureOnly,

    [switch]$Install,

    [switch]$ReportBudget,

    [string]$Project
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$cmake = Get-Command cmake.exe -ErrorAction Stop
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
if ($Preset -eq 'game-shipping' -and $null -ne $projectRoot) {
    $gameExportSettingsPath = Join-Path `
        $projectRoot `
        'ProjectSettings\game_export.json'
    if (Test-Path -LiteralPath $gameExportSettingsPath -PathType Leaf) {
        try {
            $gameExportSettings = Get-Content `
                -LiteralPath $gameExportSettingsPath `
                -Raw | ConvertFrom-Json
        } catch {
            throw (
                "Could not parse project game export settings: " +
                $gameExportSettingsPath)
        }
        $executableProperty =
            $gameExportSettings.PSObject.Properties['executableName']
        if ($null -ne $executableProperty -and
            -not [string]::IsNullOrWhiteSpace(
                [string]$executableProperty.Value)) {
            $gameHostExecutableName = [string]$executableProperty.Value
        }
    }
}
if ([IO.Path]::GetFileName($gameHostExecutableName) -cne
        $gameHostExecutableName -or
    [IO.Path]::GetExtension($gameHostExecutableName) -ine '.exe' -or
    $gameHostExecutableName.EndsWith('.') -or
    $gameHostExecutableName.EndsWith(' ') -or
    $gameHostExecutableName.IndexOfAny(
        [IO.Path]::GetInvalidFileNameChars()) -ge 0) {
    throw (
        "Project executableName must be one safe .exe filename: " +
        $gameHostExecutableName)
}
$gameHostOutputName =
    [IO.Path]::GetFileNameWithoutExtension($gameHostExecutableName)

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
    & $cmake.Source @configureArguments
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed for preset '$Preset'."
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
