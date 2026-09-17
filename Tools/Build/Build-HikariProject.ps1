[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Project,

    [ValidateSet('debug', 'editor-release', 'game-shipping')]
    [string]$Preset = 'editor-release',

    [Parameter(Mandatory)]
    [string]$SdkRoot,

    [switch]$ConfigureOnly,

    [switch]$ForceBuild,

    [switch]$PrepareGameExport,

    [string]$InstallDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'HikariProjectPublication.ps1')

function Get-HikariRelativePath {
    param(
        [Parameter(Mandatory)]
        [string]$BasePath,

        [Parameter(Mandatory)]
        [string]$Path
    )

    $resolvedBase = [IO.Path]::GetFullPath($BasePath).
        TrimEnd(
            [IO.Path]::DirectorySeparatorChar,
            [IO.Path]::AltDirectorySeparatorChar)
    $resolvedPath = [IO.Path]::GetFullPath($Path)
    $basePrefix =
        $resolvedBase + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedPath.StartsWith(
            $basePrefix,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Fingerprint input escaped its owner root: $resolvedPath"
    }
    return $resolvedPath.Substring($basePrefix.Length).
        Replace('\', '/')
}

function Get-HikariFileSetFingerprint {
    param(
        [Parameter(Mandatory)]
        [string]$BasePath,

        [Parameter(Mandatory)]
        [IO.FileInfo[]]$Files
    )

    $resolvedFiles = @(
        $Files |
            Where-Object {
                $null -ne $_ -and
                $_.Exists
            } |
            Sort-Object -Property FullName -Unique)
    if ($resolvedFiles.Count -eq 0) {
        throw "No fingerprint inputs were found under $BasePath"
    }

    $fingerprintSource =
        [Text.StringBuilder]::new()
    foreach ($file in $resolvedFiles) {
        $relativePath = Get-HikariRelativePath `
            -BasePath $BasePath `
            -Path $file.FullName
        $fileHash =
            (Get-FileHash `
                -LiteralPath $file.FullName `
                -Algorithm SHA256).Hash
        $null = $fingerprintSource.Append($relativePath)
        $null = $fingerprintSource.Append('|')
        $null = $fingerprintSource.Append($fileHash)
        $null = $fingerprintSource.Append("`n")
    }

    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes(
            $fingerprintSource.ToString())
        return -join (
            $sha256.ComputeHash($bytes) |
                ForEach-Object { $_.ToString('x2') })
    }
    finally {
        $sha256.Dispose()
    }
}

function Get-HikariProjectInputFiles {
    param(
        [Parameter(Mandatory)]
        [string]$ProjectRoot,

        [Parameter(Mandatory)]
        [string]$ManifestRoot,

        [Parameter(Mandatory)]
        [string]$ProjectManifest
    )

    $inputs = @()
    foreach ($path in @(
            (Join-Path $ProjectRoot 'Project.hikari'),
            $ProjectManifest,
            (Join-Path $ProjectRoot 'CMakePresets.json'),
            (Join-Path $ManifestRoot 'CMakePresets.json'))) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $inputs += Get-Item -LiteralPath $path
        }
    }

    $sourceRoot = Join-Path $ManifestRoot 'Source'
    if (Test-Path -LiteralPath $sourceRoot -PathType Container) {
        $sourceExtensions = @(
            '.c', '.cc', '.cpp', '.cxx',
            '.h', '.hh', '.hpp', '.hxx', '.inl')
        $inputs += Get-ChildItem `
            -LiteralPath $sourceRoot `
            -Recurse `
            -File |
            Where-Object {
                $sourceExtensions -contains
                    $_.Extension.ToLowerInvariant()
            }
    }

    $cmakeRoot = Join-Path $ManifestRoot 'cmake'
    if (Test-Path -LiteralPath $cmakeRoot -PathType Container) {
        $inputs += Get-ChildItem `
            -LiteralPath $cmakeRoot `
            -Recurse `
            -File
    }
    return @(
        $inputs |
            Sort-Object -Property FullName -Unique)
}

function Get-HikariSdkInputFiles {
    param(
        [Parameter(Mandatory)]
        [string]$SdkRoot
    )

    $inputs = @()
    foreach ($directory in @(
            (Join-Path $SdkRoot 'include'),
            (Join-Path $SdkRoot 'third_party'),
            (Join-Path $SdkRoot 'lib\cmake\HIKARI'),
            (Join-Path $SdkRoot 'src'))) {
        if (Test-Path -LiteralPath $directory -PathType Container) {
            $inputs += Get-ChildItem `
                -LiteralPath $directory `
                -Recurse `
                -File
        }
    }
    foreach ($file in @(
            (Join-Path $SdkRoot 'lib\HIKARI_Engine.lib'),
            (Join-Path $SdkRoot 'Tools\Build-HikariProject.ps1'))) {
        if (Test-Path -LiteralPath $file -PathType Leaf) {
            $inputs += Get-Item -LiteralPath $file
        }
    }
    return @(
        $inputs |
            Sort-Object -Property FullName -Unique)
}

function Resolve-HikariCMake {
    param(
        [Parameter(Mandatory)]
        [string]$SdkRoot
    )

    $candidates = @()
    $bundledCMake =
        Join-Path $SdkRoot 'Tools\CMake\bin\cmake.exe'
    if (Test-Path -LiteralPath $bundledCMake -PathType Leaf) {
        $candidates += [IO.Path]::GetFullPath($bundledCMake)
    }
    $pathCMake =
        Get-Command cmake.exe -ErrorAction SilentlyContinue
    $editorBundledCMake = Join-Path $SdkRoot '../../../Sdk/Tools/CMake/bin/cmake.exe'
    if (Test-Path -LiteralPath $editorBundledCMake -PathType Leaf) {
        $candidates += [IO.Path]::GetFullPath($editorBundledCMake)
    }
    if ($null -ne $pathCMake) {
        $candidates += $pathCMake.Source
    }

    $failures = @()
    foreach ($candidate in @($candidates | Select-Object -Unique)) {
        $versionOutput = @(& $candidate --version 2>&1)
        if ($LASTEXITCODE -ne 0) {
            $failures += "$candidate could not report its version."
            continue
        }
        $versionMatch = [regex]::Match(
            ($versionOutput -join "`n"),
            '(?im)^cmake version\s+(\d+)\.(\d+)(?:\.(\d+))?')
        if (-not $versionMatch.Success) {
            $failures += "$candidate reported an unknown version."
            continue
        }
        $major = [int]$versionMatch.Groups[1].Value
        $minor = [int]$versionMatch.Groups[2].Value
        if ($major -lt 3 -or
            ($major -eq 3 -and $minor -lt 28)) {
            $failures += "$candidate is older than CMake 3.28."
            continue
        }
        return $candidate
    }

    $failureText = if ($failures.Count -gt 0) {
        ' ' + ($failures -join ' ')
    } else {
        ''
    }
    throw (
        'CMake 3.28 or newer is unavailable. The HIKARI Editor SDK ' +
        "bundle and PATH were checked.$failureText")
}

$projectInput = $Project
if (Test-Path -LiteralPath $projectInput -PathType Leaf) {
    $projectInput = Split-Path -Parent $projectInput
}
$projectRoot = (Resolve-Path -LiteralPath $projectInput).Path
$sdkRootPath = (Resolve-Path -LiteralPath $SdkRoot).Path
if ($PrepareGameExport) {
    if ($Preset -ne 'game-shipping' -or $ConfigureOnly) {
        throw 'PrepareGameExport requires a complete game-shipping build.'
    }
    $shippingRuntimeRoot = Split-Path -Parent $sdkRootPath
    $shippingConfig = Get-Content -LiteralPath (Join-Path $sdkRootPath 'lib/cmake/HIKARI/HIKARIConfig.cmake') -Raw
    if ($shippingConfig -notmatch 'HIKARI_GAME_PROJECT_EDITOR_AVAILABLE\s+(OFF|FALSE|0)') {
        throw 'PrepareGameExport requires the Shipping SDK paired with its runtime, not the Editor SDK.'
    }
    $hostManifestPath = Join-Path $sdkRootPath 'lib/cmake/HIKARI/HIKARI_GameHost.json'
    $hostManifest = Get-Content -LiteralPath $hostManifestPath -Encoding UTF8 -Raw | ConvertFrom-Json
    if ($hostManifest.schemaVersion -ne 1 -or $hostManifest.sdkBinding -cne 'currentProcess') {
        throw 'Update the Shipping SDK: current-process host binding is required for game export.'
    }
    $exportExecutableName = [string]$hostManifest.executable
    if ([IO.Path]::GetFileName($exportExecutableName) -cne $exportExecutableName -or
        [IO.Path]::GetExtension($exportExecutableName) -ine '.exe') {
        throw 'Invalid Shipping host manifest executable.'
    }
    foreach ($required in @($exportExecutableName, 'EngineContent/Shaders/Cooked', 'Sdk/lib/HIKARI_Engine.lib')) {
        if (-not (Test-Path -LiteralPath (Join-Path $shippingRuntimeRoot $required))) {
            throw "The paired Shipping runtime kit is incomplete: $required. Build the matching host; do not rename an Editor host."
        }
    }
    $InstallDirectory = Join-Path $projectRoot 'out/install/game-shipping'
    if ([IO.Path]::GetFullPath($shippingRuntimeRoot).TrimEnd('\') -ine
        [IO.Path]::GetFullPath($InstallDirectory).TrimEnd('\')) {
        New-Item -ItemType Directory -Force -Path $InstallDirectory | Out-Null
        foreach ($entry in Get-ChildItem -LiteralPath $shippingRuntimeRoot -Force) {
            Copy-Item -LiteralPath $entry.FullName -Destination $InstallDirectory -Recurse -Force
        }
    }
}
$descriptorPath = Join-Path $projectRoot 'Project.hikari'
if (-not (Test-Path -LiteralPath $descriptorPath -PathType Leaf)) {
    throw "Project.hikari was not found: $descriptorPath"
}

$descriptor = Get-Content -LiteralPath $descriptorPath -Encoding UTF8 -Raw |
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
    $projectRoot "Library\Build\CMake\sdk-v2\$Preset"
$projectOutputDirectory = Join-Path `
    $projectRoot "Library\Build\$Preset"
$null = Assert-HikariBuildOwnedPath $projectRoot $projectOutputDirectory
$null = New-Item -ItemType Directory -Path $projectOutputDirectory -Force
$buildLockPath = Assert-HikariBuildOwnedPath $projectOutputDirectory (Join-Path $projectOutputDirectory '.module-build.lock')
try {
    $buildLock = [IO.File]::Open($buildLockPath, [IO.FileMode]::OpenOrCreate,
        [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
} catch {
    throw "Another project build owns '$Preset'. Wait for that build to finish."
}
try {
Restore-HikariModulePublication $projectOutputDirectory
$candidateDirectory = Assert-HikariBuildOwnedPath $projectOutputDirectory (Join-Path $projectOutputDirectory '.candidate')
$candidateModule = Join-Path $candidateDirectory 'bin\HIKARI_GameProject.dll'
$candidateEditorModule = Join-Path $candidateDirectory 'bin\HIKARI_GameProjectEditor.dll'
$candidateState = Join-Path $candidateDirectory 'HIKARI_GameProject.buildstate.json'
$projectModule = Join-Path `
    $projectOutputDirectory 'bin\HIKARI_GameProject.dll'
$projectEditorModule = Join-Path `
    $projectOutputDirectory 'bin\HIKARI_GameProjectEditor.dll'
$buildStatePath = Join-Path `
    $projectOutputDirectory 'HIKARI_GameProject.buildstate.json'
$packageDirectory = Join-Path $sdkRootPath 'lib\cmake\HIKARI'
$packageConfig = Join-Path $packageDirectory 'HIKARIConfig.cmake'
if (-not (Test-Path -LiteralPath $packageConfig -PathType Leaf)) {
    throw "HIKARI SDK package is incomplete: $packageConfig"
}

$projectInputs = Get-HikariProjectInputFiles `
    -ProjectRoot $projectRoot `
    -ManifestRoot $manifestRoot `
    -ProjectManifest $projectManifest
$sdkInputs = Get-HikariSdkInputFiles `
    -SdkRoot $sdkRootPath
$projectFingerprint = Get-HikariFileSetFingerprint `
    -BasePath $projectRoot `
    -Files $projectInputs
$sdkFingerprint = Get-HikariFileSetFingerprint `
    -BasePath $sdkRootPath `
    -Files $sdkInputs

if (-not $ForceBuild -and
    -not $ConfigureOnly -and
    [string]::IsNullOrWhiteSpace($InstallDirectory) -and
    (Test-HikariLastGoodModule `
        -BuildStatePath $buildStatePath `
        -ModulePath $projectModule `
        -EditorModulePath $projectEditorModule `
        -Preset $Preset `
        -Configuration $configuration `
        -ProjectFingerprint $projectFingerprint `
        -SdkFingerprint $sdkFingerprint)) {
    Write-Host (
        'HIKARI GameProject last-good module is current: ' +
        $projectModule)
    exit 0
}

$cmake = Resolve-HikariCMake -SdkRoot $sdkRootPath

& $cmake `
    -S $manifestRoot `
    -B $projectBuildDirectory `
    -G 'Visual Studio 17 2022' `
    -A x64 `
    "-DCMAKE_CONFIGURATION_TYPES:STRING=$configuration" `
    "-DHIKARI_DIR:PATH=$packageDirectory" `
    "-DHIKARI_GAME_PROJECT_OUTPUT_ROOT:PATH=$candidateDirectory"
if ($LASTEXITCODE -ne 0) {
    throw "GameProject configure failed for preset '$Preset'."
}

if (-not $ConfigureOnly) {
    $editorCompanionMarker = Join-Path `
        $projectBuildDirectory `
        'HIKARI_GameProjectEditorCompanion.txt'
    if (-not (Test-Path `
            -LiteralPath $editorCompanionMarker `
            -PathType Leaf)) {
        throw (
            'The HIKARI SDK did not publish the Editor companion ' +
            'configuration marker.')
    }
    $editorCompanionEnabled =
        (Get-Content `
            -LiteralPath $editorCompanionMarker `
            -Raw).Trim() -ceq 'TRUE'
    if ($configuration -ceq 'Shipping' -and
        $editorCompanionEnabled) {
        throw 'A GameShipping build cannot enable an Editor companion.'
    }

    $buildTargets = @('HIKARI_GameProjectModule')
    if ($editorCompanionEnabled) {
        $buildTargets += 'HIKARI_GameProjectEditorModule'
    }
    $buildArguments = @(
        '--build',
        $projectBuildDirectory,
        '--config',
        $configuration,
        '--target') + $buildTargets
    & $cmake @buildArguments
    if ($LASTEXITCODE -ne 0) {
        throw "GameProject build failed for preset '$Preset'."
    }
    if (-not (Test-Path -LiteralPath $candidateModule -PathType Leaf)) {
        throw "GameProject module was not produced: $candidateModule"
    }
    if ($editorCompanionEnabled -and
        -not (Test-Path `
            -LiteralPath $candidateEditorModule `
            -PathType Leaf)) {
        throw (
            'GameProject Editor companion was not produced: ' +
            $candidateEditorModule)
    }
    if (-not $editorCompanionEnabled -and
        (Test-Path `
            -LiteralPath $candidateEditorModule `
            -PathType Leaf)) {
        Remove-Item -LiteralPath $candidateEditorModule -Force
    }

    $finalProjectFingerprint = Get-HikariFileSetFingerprint `
        -BasePath $projectRoot `
        -Files (Get-HikariProjectInputFiles `
            -ProjectRoot $projectRoot `
            -ManifestRoot $manifestRoot `
            -ProjectManifest $projectManifest)
    $finalSdkFingerprint = Get-HikariFileSetFingerprint `
        -BasePath $sdkRootPath `
        -Files (Get-HikariSdkInputFiles `
            -SdkRoot $sdkRootPath)
    if ($finalProjectFingerprint -cne $projectFingerprint -or $finalSdkFingerprint -cne $sdkFingerprint) {
        throw 'Project source or SDK changed during the build. Rebuild after saving; all previous modules and build state were preserved.'
    }
    Write-HikariBuildState `
        -BuildStatePath $candidateState `
        -ModulePath $candidateModule `
        -EditorModulePath $candidateEditorModule `
        -Preset $Preset `
        -Configuration $configuration `
        -ProjectFingerprint $projectFingerprint `
        -SdkFingerprint $sdkFingerprint
    Publish-HikariProjectModules $projectOutputDirectory $candidateDirectory $editorCompanionEnabled
}

if (-not [string]::IsNullOrWhiteSpace($InstallDirectory) -and
    -not $ConfigureOnly) {
    & $cmake `
        --install $projectBuildDirectory `
        --config $configuration `
        --prefix $InstallDirectory
    if ($LASTEXITCODE -ne 0) {
        throw "GameProject install failed for preset '$Preset'."
    }
}
} finally {
    $buildLock.Dispose()
}
