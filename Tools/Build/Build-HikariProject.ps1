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

    # Compile the project code only. Used by the Editor's code check, which builds a copy of the project and
    # loads nothing from it.
    [switch]$SkipSourceModules,

    # Folder of the engine-bundled modules (the Editor's Runtime/Modules). Found next to the SDK when omitted;
    # pass it when the SDK is not inside an Editor distribution (a Shipping build tree).
    [string]$EngineModulesRoot,

    [string]$InstallDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Load the current PowerShell's standard module even when another edition precedes it in PSModulePath.
Import-Module "$PSHOME/Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1" -ErrorAction Stop
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

    # Order the inputs by their relative path, ASCII-lowered and compared
    # ordinally. The Editor computes the same fingerprint before a game
    # export (GameShippingArtifactValidator) and must see the same order;
    # culture sorting orders '_' and '.' differently.
    $lowerAscii = [Text.RegularExpressions.MatchEvaluator] {
        param($match)
        $match.Value.ToLowerInvariant()
    }
    $entries = [Collections.Generic.SortedDictionary[string, object]]::new(
        [StringComparer]::Ordinal)
    foreach ($file in $Files) {
        if ($null -eq $file -or -not $file.Exists) {
            continue
        }
        $relativePath = Get-HikariRelativePath `
            -BasePath $BasePath `
            -Path $file.FullName
        $key = [regex]::Replace($relativePath, '[A-Z]', $lowerAscii)
        if (-not $entries.ContainsKey($key)) {
            $entries[$key] = [pscustomobject]@{
                FullName = $file.FullName
                RelativePath = $relativePath
            }
        }
    }
    if ($entries.Count -eq 0) {
        throw "No fingerprint inputs were found under $BasePath"
    }

    $fingerprintSource =
        [Text.StringBuilder]::new()
    foreach ($file in $entries.Values) {
        $relativePath = $file.RelativePath
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
            $ProjectManifest)) {
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

    # Plugin packages (Plugins/<id>/): their manifests and the public
    # headers project code may include. Replacing a package rebuilds
    # the project module against the new headers.
    $pluginRoot = Join-Path $ProjectRoot 'Plugins'
    if (Test-Path -LiteralPath $pluginRoot -PathType Container) {
        $pluginExtensions = @(
            '.h', '.hh', '.hpp', '.hxx', '.inl', '.hmodule')
        $inputs += Get-ChildItem `
            -LiteralPath $pluginRoot `
            -Recurse `
            -File |
            Where-Object {
                $pluginExtensions -contains
                    $_.Extension.ToLowerInvariant()
            }
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

function Write-HikariIdePresets {
    param(
        [Parameter(Mandatory)]
        [string]$ManifestRoot,

        [Parameter(Mandatory)]
        [string]$ProjectRoot,

        [Parameter(Mandatory)]
        [string]$SdkRoot,

        [Parameter(Mandatory)]
        [string]$PackageDirectory,

        [string[]]$ModuleIncludeDirectories
    )

    # Machine-local IDE preset for this SDK. It uses its own build tree and output so an IDE configure or
    # build never touches the Editor's candidate/last-good module. A user-owned file is left untouched.
    $presetsPath = Join-Path $ManifestRoot 'CMakeUserPresets.json'
    $existingText = $null
    if (Test-Path -LiteralPath $presetsPath -PathType Leaf) {
        $existingText = [IO.File]::ReadAllText($presetsPath)
        $existing = try { $existingText | ConvertFrom-Json } catch { $null }
        if ($null -eq $existing -or
            $null -eq $existing.PSObject.Properties['vendor'] -or
            $null -eq $existing.vendor.PSObject.Properties['hikari']) {
            Write-Host "HIKARI IDE presets skipped: $presetsPath is not generated by HIKARI."
            return
        }
    }

    $toPresetPath = { param($path) [IO.Path]::GetFullPath($path).Replace('\', '/') }
    $cmakeExecutable = & $toPresetPath (Join-Path $SdkRoot 'Tools\CMake\bin\cmake.exe')
    $buildDirectory = & $toPresetPath (Join-Path $ProjectRoot 'Library\Build\CMake\ide')
    $outputDirectory = & $toPresetPath (Join-Path $ProjectRoot 'Library\Build\ide')
    $hikariDirectory = & $toPresetPath $PackageDirectory
    $moduleIncludes = (@($ModuleIncludeDirectories | Where-Object { $_ }) | ForEach-Object { & $toPresetPath $_ }) -join ';'
    $presetsText = @"
{
  "version": 6,
  "vendor": {
    "hikari": {
      "generatedBy": "Build-HikariProject.ps1"
    }
  },
  "configurePresets": [
    {
      "name": "hikari-ide",
      "displayName": "HIKARI IDE",
      "description": "Code navigation and compile checks with this machine's HIKARI SDK. The Editor builds the loaded module separately.",
      "generator": "Visual Studio 17 2022",
      "architecture": "x64",
      "cmakeExecutable": "$cmakeExecutable",
      "binaryDir": "$buildDirectory",
      "cacheVariables": {
        "CMAKE_CONFIGURATION_TYPES": "EditorRelease",
        "HIKARI_DIR": "$hikariDirectory",
        "HIKARI_GAME_PROJECT_OUTPUT_ROOT": "$outputDirectory",
        "HIKARI_PROJECT_MODULE_INCLUDE_DIRECTORIES": "$moduleIncludes"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "hikari-ide",
      "configurePreset": "hikari-ide",
      "configuration": "EditorRelease"
    }
  ]
}

"@
    if ($existingText -cne $presetsText) {
        [IO.File]::WriteAllText($presetsPath, $presetsText, [Text.UTF8Encoding]::new($false))
    }
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
    $editorBundledCMake = Join-Path $SdkRoot '../../Sdk/Tools/CMake/bin/cmake.exe'
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

# Public headers of the project's modules. A module's manifest lists the folders project code may include
# ("publicIncludeDirectories"). The project build adds them to the include path for every enabled module and the
# modules those depend on, wherever the module is: Plugins/<id> (source or built package) or the engine's
# Modules folder. Project code then includes the same header in every case, and no machine path is written
# into the project's CMakeLists.txt.
function Get-HikariModuleIncludeDirectories {
    param(
        [Parameter(Mandatory)]
        [string]$ProjectRoot,

        [Parameter(Mandatory)]
        $Descriptor,

        [string]$EngineModulesRoot
    )

    $directories = [Collections.Generic.List[string]]::new()
    $moduleList = $Descriptor.PSObject.Properties['modules']
    if ($null -eq $moduleList) {
        return , $directories.ToArray()
    }
    $visited = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $visit = $null
    $visit = {
        param([string]$id, [string]$manifestPath)
        if ([string]::IsNullOrWhiteSpace($id) -or -not $visited.Add($id)) {
            return
        }
        # Same order as the engine: the path Project.hikari names, the project's Plugins, the engine's modules.
        $candidates = @($manifestPath, (Join-Path $ProjectRoot "Plugins\$id\$id.hmodule"))
        if (-not [string]::IsNullOrWhiteSpace($EngineModulesRoot)) {
            $candidates += Join-Path $EngineModulesRoot "$id\$id.hmodule"
        }
        $found = @($candidates | Where-Object {
                -not [string]::IsNullOrWhiteSpace($_) -and (Test-Path -LiteralPath $_ -PathType Leaf)
            }) | Select-Object -First 1
        if ($null -eq $found) {
            return
        }
        $moduleRoot = [IO.Path]::GetFullPath((Split-Path -Parent $found))
        $manifest = Get-Content -LiteralPath $found -Encoding UTF8 -Raw | ConvertFrom-Json
        $public = $manifest.PSObject.Properties['publicIncludeDirectories']
        foreach ($entry in @(if ($null -ne $public) { $public.Value })) {
            $path = [IO.Path]::GetFullPath((Join-Path $moduleRoot ([string]$entry)))
            if (-not $path.StartsWith($moduleRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Module ${id}: publicIncludeDirectories must name folders inside the module ($entry)."
            }
            if (Test-Path -LiteralPath $path -PathType Container) {
                $directories.Add($path)
            }
        }
        $dependencies = $manifest.PSObject.Properties['dependencies']
        foreach ($dependency in @(if ($null -ne $dependencies) { $dependencies.Value })) {
            & $visit ([string]$dependency) ''
        }
    }
    foreach ($module in @($moduleList.Value)) {
        $manifestProperty = $module.PSObject.Properties['manifest']
        $manifestPath = ''
        if ($null -ne $manifestProperty -and -not [string]::IsNullOrWhiteSpace([string]$manifestProperty.Value)) {
            $manifestPath = if ([IO.Path]::IsPathRooted([string]$manifestProperty.Value)) {
                [string]$manifestProperty.Value
            } else {
                [IO.Path]::GetFullPath((Join-Path $ProjectRoot ([string]$manifestProperty.Value)))
            }
        }
        & $visit ([string]$module.id) $manifestPath
    }
    return , $directories.ToArray()
}

# Changes when a public header of the project's modules changes, so that project code is rebuilt against it.
# Empty when the project uses no module with public headers.
function Get-HikariModuleHeadersFingerprint {
    param(
        [string[]]$Directories
    )

    $lines = [Collections.Generic.List[string]]::new()
    $index = 0
    foreach ($directory in @($Directories | Where-Object { $_ })) {
        foreach ($file in @(Get-ChildItem -LiteralPath $directory -Recurse -File)) {
            $relative = $file.FullName.Substring($directory.Length).Replace('\', '/')
            $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
            $lines.Add("$index$relative|$hash")
        }
        ++$index
    }
    if ($lines.Count -eq 0) {
        return ''
    }
    $lines.Sort([StringComparer]::Ordinal)
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes(($lines -join "`n"))
        return -join ($sha256.ComputeHash($bytes) | ForEach-Object { $_.ToString('x2') })
    }
    finally {
        $sha256.Dispose()
    }
}

# Source modules: a folder Plugins/<id> that holds a module's source (CMakeLists.txt and <id>.hmodule) instead of
# a built package. The project build compiles every enabled one, and the source modules they depend on, with
# this SDK into Library/Plugins/<id>, which is where the engine loads them from. A module is rebuilt when its
# files changed, or when the SDK files its library was compiled against changed (the record next to the library,
# checked with Sdk/Tools/HIKARI_SdkInterface.exe). An engine update that does not touch what the module includes
# keeps the existing library.
function Get-HikariSourceModuleState {
    param(
        [Parameter(Mandatory)]
        [string]$ModuleRoot
    )

    # Paths, sizes and write times of the module's files. Its own build output and VCS data are not inputs.
    $excluded = @('Library', 'out', '.git', '.vs')
    $lines = [Collections.Generic.List[string]]::new()
    foreach ($entry in @(Get-ChildItem -LiteralPath $ModuleRoot -Force)) {
        if ($entry.PSIsContainer -and $excluded -contains $entry.Name) {
            continue
        }
        $files = if ($entry.PSIsContainer) {
            @(Get-ChildItem -LiteralPath $entry.FullName -Recurse -File -Force)
        } else {
            @($entry)
        }
        foreach ($file in $files) {
            $lines.Add((
                '{0}|{1}|{2}' -f
                $file.FullName.Substring($ModuleRoot.Length),
                $file.Length,
                $file.LastWriteTimeUtc.Ticks))
        }
    }
    $lines.Sort([StringComparer]::Ordinal)
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes(($lines -join "`n"))
        return -join ($sha256.ComputeHash($bytes) | ForEach-Object { $_.ToString('x2') })
    }
    finally {
        $sha256.Dispose()
    }
}

function Get-HikariSourceModuleOrder {
    param(
        [Parameter(Mandatory)]
        [string]$PluginRoot,

        [string[]]$Ids
    )

    # Dependencies first. Ids that are not source modules (built packages, engine modules) are skipped.
    $ordered = [Collections.Generic.List[string]]::new()
    $visited = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $visit = $null
    $visit = {
        param([string]$id)
        if (-not $visited.Add($id)) {
            return
        }
        $moduleRoot = Join-Path $PluginRoot $id
        $manifestPath = Join-Path $moduleRoot "$id.hmodule"
        if (-not (Test-Path -LiteralPath (Join-Path $moduleRoot 'CMakeLists.txt') -PathType Leaf) -or
            -not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
            return
        }
        $manifest = Get-Content -LiteralPath $manifestPath -Encoding UTF8 -Raw | ConvertFrom-Json
        $dependencies = $manifest.PSObject.Properties['dependencies']
        foreach ($dependency in @(if ($null -ne $dependencies) { $dependencies.Value })) {
            & $visit ([string]$dependency)
        }
        $ordered.Add($id)
    }
    foreach ($id in @($Ids)) {
        & $visit $id
    }
    return , $ordered.ToArray()
}

function Build-HikariSourceModules {
    param(
        [Parameter(Mandatory)]
        [string]$ProjectRoot,

        [Parameter(Mandatory)]
        $Descriptor,

        [Parameter(Mandatory)]
        [string]$Preset,

        [Parameter(Mandatory)]
        [string]$Configuration,

        [Parameter(Mandatory)]
        [string]$SdkRoot,

        [switch]$Force
    )

    $pluginRoot = Join-Path $ProjectRoot 'Plugins'
    $moduleList = $Descriptor.PSObject.Properties['modules']
    if ($null -eq $moduleList -or -not (Test-Path -LiteralPath $pluginRoot -PathType Container)) {
        return
    }
    $enabled = @(
        foreach ($module in @($moduleList.Value)) {
            $manifestProperty = $module.PSObject.Properties['manifest']
            if ($null -eq $manifestProperty -or [string]::IsNullOrWhiteSpace([string]$manifestProperty.Value)) {
                [string]$module.id
            }
        })
    $ids = Get-HikariSourceModuleOrder -PluginRoot $pluginRoot -Ids $enabled
    if ($ids.Count -eq 0) {
        return
    }
    if ($Preset -eq 'debug') {
        Write-Host 'HIKARI source modules are not built for the debug preset: module packages carry EditorRelease and Shipping libraries.'
        return
    }

    $tool = Join-Path $SdkRoot 'Tools\HIKARI_SdkInterface.exe'
    $table = Join-Path $SdkRoot 'HIKARI_SdkInterface.json'
    foreach ($required in @($tool, $table)) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "This HIKARI SDK cannot build source modules: $required is missing."
        }
    }
    $packageRoot = Join-Path $ProjectRoot 'Library\Plugins'
    $libraryKey = if ($Preset -eq 'game-shipping') { 'shippingRuntimeLibrary' } else { 'runtimeLibrary' }
    $cmake = $null
    foreach ($id in $ids) {
        $moduleRoot = (Resolve-Path -LiteralPath (Join-Path $pluginRoot $id)).Path
        $package = Join-Path $packageRoot $id
        $buildTree = Join-Path $ProjectRoot "Library\Build\CMake\modules\$id\$Preset"
        $statePath = Join-Path $buildTree 'HIKARI_ModuleBuild.json'
        $fingerprint = Get-HikariSourceModuleState -ModuleRoot $moduleRoot

        # The library this preset produces (named by the packaged manifest) and its interface record.
        $findRecord = {
            $manifestPath = Join-Path $package "$id.hmodule"
            if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
                return $null
            }
            $manifest = Get-Content -LiteralPath $manifestPath -Encoding UTF8 -Raw | ConvertFrom-Json
            $property = $manifest.PSObject.Properties[$libraryKey]
            if ($null -eq $property -or [string]::IsNullOrWhiteSpace([string]$property.Value)) {
                return $null
            }
            $library = Join-Path $package ([string]$property.Value)
            $record = [IO.Path]::ChangeExtension($library, '.interface.json')
            if (-not (Test-Path -LiteralPath $library -PathType Leaf) -or
                -not (Test-Path -LiteralPath $record -PathType Leaf)) {
                return $null
            }
            return $record
        }

        if (-not $Force -and (Test-Path -LiteralPath $statePath -PathType Leaf)) {
            $state = try {
                Get-Content -LiteralPath $statePath -Encoding UTF8 -Raw | ConvertFrom-Json
            } catch {
                $null
            }
            $built = if ($null -ne $state) { $state.PSObject.Properties['fingerprint'] } else { $null }
            $record = & $findRecord
            if ($null -ne $built -and [string]$built.Value -ceq $fingerprint -and $null -ne $record) {
                $null = & $tool check $record $table
                if ($LASTEXITCODE -eq 0) {
                    Write-Host "HIKARI module $id is current."
                    continue
                }
                Write-Host "HIKARI module ${id}: the engine files it was built against changed."
            }
        }

        Write-Host 'HIKARI_STAGE building'
        Write-Host "HIKARI module ${id}: building from its source in Plugins\$id."
        if ($null -eq $cmake) {
            $cmake = Resolve-HikariCMake -SdkRoot $SdkRoot
        }
        # No precompiled headers: CMake writes the header's absolute path into cmake_pch.hxx in the system code
        # page, and a project folder may contain characters that code page lacks (the build then cannot open it).
        & $cmake `
            -S $moduleRoot `
            -B $buildTree `
            -G 'Visual Studio 17 2022' `
            -A x64 `
            "-DCMAKE_CONFIGURATION_TYPES:STRING=$Configuration" `
            "-DHIKARI_DIR:PATH=$(Join-Path $SdkRoot 'lib\cmake\HIKARI')" `
            "-DHIKARI_MODULE_PACKAGE_ROOT:PATH=$packageRoot" `
            '-DCMAKE_DISABLE_PRECOMPILE_HEADERS:BOOL=ON'
        if ($LASTEXITCODE -ne 0) {
            throw "Module $id could not be configured from $moduleRoot."
        }
        & $cmake --build $buildTree --config $Configuration
        if ($LASTEXITCODE -ne 0) {
            throw "Module $id failed to build from $moduleRoot."
        }
        $record = & $findRecord
        if ($null -eq $record) {
            throw "Module $id was built, but its $libraryKey or the interface record next to it is missing in $package."
        }
        & $tool check $record $table
        if ($LASTEXITCODE -ne 0) {
            throw "Module $id was built, but its interface record does not match this SDK."
        }
        [IO.File]::WriteAllText(
            $statePath,
            ([ordered]@{ version = 1; fingerprint = $fingerprint } | ConvertTo-Json),
            [Text.UTF8Encoding]::new($false))
    }
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

# Modules referenced by path with their own Project.hikari are built first. Engine-bundled modules ship with the host.
if (-not $ConfigureOnly) {
    $moduleList = $descriptor.PSObject.Properties['modules']
    foreach ($module in @(if ($null -ne $moduleList) { $moduleList.Value })) {
        $manifestProperty = $module.PSObject.Properties['manifest']
        if ($null -eq $manifestProperty -or [string]::IsNullOrWhiteSpace([string]$manifestProperty.Value)) {
            continue
        }
        $moduleManifest = if ([IO.Path]::IsPathRooted([string]$module.manifest)) {
            [string]$module.manifest
        } else {
            [IO.Path]::GetFullPath((Join-Path $projectRoot ([string]$module.manifest)))
        }
        $moduleRoot = Split-Path -Parent $moduleManifest
        if (-not (Test-Path -LiteralPath (Join-Path $moduleRoot 'Project.hikari') -PathType Leaf)) {
            continue
        }
        $moduleArguments = @{
            Project = $moduleRoot
            Preset = $Preset
            SdkRoot = $SdkRoot
            PrepareGameExport = $PrepareGameExport
            ForceBuild = $ForceBuild
        }
        if (-not [string]::IsNullOrWhiteSpace($InstallDirectory)) {
            $moduleArguments.InstallDirectory = Join-Path $moduleRoot "out/install/$Preset"
        }
        & $PSCommandPath @moduleArguments
    }
}

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
# Engine-bundled modules sit next to the Editor host (Runtime/Modules); the Shipping kit is one level below it.
$engineModulesPath = ''
foreach ($candidate in @(
        $EngineModulesRoot,
        (Join-Path $sdkRootPath '..\Modules'),
        (Join-Path $sdkRootPath '..\..\Modules'))) {
    if (-not [string]::IsNullOrWhiteSpace($candidate) -and
        (Test-Path -LiteralPath $candidate -PathType Container)) {
        $engineModulesPath = (Resolve-Path -LiteralPath $candidate).Path
        break
    }
}
$moduleIncludeDirectories = @(Get-HikariModuleIncludeDirectories `
        -ProjectRoot $projectRoot `
        -Descriptor $descriptor `
        -EngineModulesRoot $engineModulesPath)
if ($Preset -ne 'game-shipping') {
    Write-HikariIdePresets `
        -ManifestRoot $manifestRoot `
        -ProjectRoot $projectRoot `
        -SdkRoot $sdkRootPath `
        -PackageDirectory $packageDirectory `
        -ModuleIncludeDirectories $moduleIncludeDirectories
}

Write-Host "HIKARI_STAGE checking"
if (-not $ConfigureOnly -and -not $SkipSourceModules) {
    Build-HikariSourceModules `
        -ProjectRoot $projectRoot `
        -Descriptor $descriptor `
        -Preset $Preset `
        -Configuration $configuration `
        -SdkRoot $sdkRootPath `
        -Force:$ForceBuild
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
$moduleHeadersFingerprint = Get-HikariModuleHeadersFingerprint -Directories $moduleIncludeDirectories

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
        -SdkFingerprint $sdkFingerprint `
        -ModuleHeadersFingerprint $moduleHeadersFingerprint)) {
    Write-Host (
        'HIKARI GameProject last-good module is current: ' +
        $projectModule)
    exit 0
}

Write-Host "HIKARI_STAGE building"
$cmake = Resolve-HikariCMake -SdkRoot $sdkRootPath

& $cmake `
    -S $manifestRoot `
    -B $projectBuildDirectory `
    -G 'Visual Studio 17 2022' `
    -A x64 `
    "-DCMAKE_CONFIGURATION_TYPES:STRING=$configuration" `
    "-DHIKARI_DIR:PATH=$packageDirectory" `
    "-DHIKARI_GAME_PROJECT_OUTPUT_ROOT:PATH=$candidateDirectory" `
    "-DHIKARI_PROJECT_MODULE_INCLUDE_DIRECTORIES:STRING=$(($moduleIncludeDirectories | ForEach-Object { $_.Replace('\', '/') }) -join ';')"
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
    $finalModuleHeadersFingerprint = Get-HikariModuleHeadersFingerprint -Directories $moduleIncludeDirectories
    if ($finalProjectFingerprint -cne $projectFingerprint -or $finalSdkFingerprint -cne $sdkFingerprint -or
        $finalModuleHeadersFingerprint -cne $moduleHeadersFingerprint) {
        throw 'Project source, a module header or the SDK changed during the build. Rebuild after saving; all previous modules and build state were preserved.'
    }
    Write-HikariBuildState `
        -BuildStatePath $candidateState `
        -ModulePath $candidateModule `
        -EditorModulePath $candidateEditorModule `
        -Preset $Preset `
        -Configuration $configuration `
        -ProjectFingerprint $projectFingerprint `
        -SdkFingerprint $sdkFingerprint `
        -ModuleHeadersFingerprint $moduleHeadersFingerprint
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
