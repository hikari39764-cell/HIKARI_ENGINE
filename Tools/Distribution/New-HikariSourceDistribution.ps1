[CmdletBinding()]
param(
    [string]$OutputDirectory,

    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$PackageName,

    [switch]$KeepStaging
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

function Invoke-HikariGit {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $output = @(
        & git -c 'core.excludesFile=' -c 'core.safecrlf=false' `
            -C $repoRoot @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw (
            "git {0} failed with exit code {1}.`n{2}" -f
            ($Arguments -join ' '),
            $LASTEXITCODE,
            ($output -join [Environment]::NewLine))
    }
    return $output
}

function New-DirectoryIfMissing {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        $null = New-Item -ItemType Directory -Path $Path -Force
    }
}

function Copy-SourceFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath,

        [Parameter(Mandatory = $true)]
        [string]$DestinationRoot
    )

    $systemRelativePath =
        $RelativePath.Replace(
            '/',
            [IO.Path]::DirectorySeparatorChar)
    $sourcePath = Join-Path $repoRoot $systemRelativePath
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Source distribution input is missing: $RelativePath"
    }

    $destinationPath =
        Join-Path $DestinationRoot $systemRelativePath
    New-DirectoryIfMissing (
        Split-Path -Parent $destinationPath)
    Copy-Item `
        -LiteralPath $sourcePath `
        -Destination $destinationPath `
        -Force
}

function Copy-DependencyDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath,

        [Parameter(Mandatory = $true)]
        [string]$DestinationRoot
    )

    $systemRelativePath =
        $RelativePath.Replace(
            '/',
            [IO.Path]::DirectorySeparatorChar)
    $sourcePath = Join-Path $repoRoot $systemRelativePath
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) {
        throw "Dependency closure is missing: $RelativePath"
    }

    $destinationPath =
        Join-Path $DestinationRoot $systemRelativePath
    New-DirectoryIfMissing (
        Split-Path -Parent $destinationPath)
    Copy-Item `
        -LiteralPath $sourcePath `
        -Destination $destinationPath `
        -Recurse `
        -Force
}

function Test-IsSourceDistributionPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    $normalizedPath =
        $RelativePath.Replace('\', '/').TrimStart('/')
    $excludedRoots = @(
        '.agents',
        '.codex',
        '.claude',
        '.cursor',
        'docs/ai-team',
        'docs/superpowers',
        'docs/change-contracts')
    foreach ($excludedRoot in $excludedRoots) {
        if (
            $normalizedPath.Equals(
                $excludedRoot,
                [StringComparison]::OrdinalIgnoreCase) -or
            $normalizedPath.StartsWith(
                "$excludedRoot/",
                [StringComparison]::OrdinalIgnoreCase)) {
            return $false
        }
    }

    $excludedFiles = @(
        'AGENTS.md',
        'CLAUDE.md',
        'CODEX.md',
        'Tools/Distribution/New-HikariSourceDistribution.ps1',
        'Tools/Tests/Test-HikariSourceDistributionContracts.ps1')
    foreach ($excludedFile in $excludedFiles) {
        if ($normalizedPath.Equals(
                $excludedFile,
                [StringComparison]::OrdinalIgnoreCase)) {
            return $false
        }
    }
    return $true
}

$sourceRevisionOutput =
    @(Invoke-HikariGit @('rev-parse', 'HEAD'))
$sourceShortRevisionOutput =
    @(Invoke-HikariGit @('rev-parse', '--short=12', 'HEAD'))
$sourceBranchOutput =
    @(Invoke-HikariGit @('branch', '--show-current'))
if (
    $sourceRevisionOutput.Count -ne 1 -or
    $sourceShortRevisionOutput.Count -ne 1 -or
    $sourceBranchOutput.Count -ne 1) {
    throw 'Could not resolve a unique Git source identity.'
}
$sourceRevision =
    ([string]$sourceRevisionOutput[0]).Trim()
$sourceShortRevision =
    ([string]$sourceShortRevisionOutput[0]).Trim()
$sourceBranch =
    ([string]$sourceBranchOutput[0]).Trim()

if ([string]::IsNullOrWhiteSpace($PackageName)) {
    $PackageName =
        "HIKARI_Engine-source-$sourceShortRevision"
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory =
        Join-Path $repoRoot 'out\source-distribution'
}
elseif (-not [IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory =
        Join-Path $repoRoot $OutputDirectory
}
$OutputDirectory =
    [IO.Path]::GetFullPath($OutputDirectory)
New-DirectoryIfMissing $OutputDirectory

$stagingParent =
    Join-Path $OutputDirectory '.staging'
$packageRoot =
    Join-Path $stagingParent $PackageName
$zipPath =
    Join-Path $OutputDirectory "$PackageName.zip"

if (Test-Path -LiteralPath $packageRoot) {
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
if (Test-Path -LiteralPath $zipPath -PathType Leaf) {
    Remove-Item -LiteralPath $zipPath -Force
}
New-DirectoryIfMissing $packageRoot

$dependencyCheck =
    Join-Path $repoRoot `
        'Tools\Dependencies\Test-HikariDependencySnapshot.ps1'
& powershell.exe `
    -NoProfile `
    -ExecutionPolicy Bypass `
    -File $dependencyCheck
if ($LASTEXITCODE -ne 0) {
    throw 'The dependency snapshot is not valid for source distribution.'
}

$trackedSourceFiles = @(
    Invoke-HikariGit @(
        'ls-files',
        '--cached'))
$supplementalSourceFiles = @(
    'README.md')
$sourceFileSet =
    [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
foreach ($relativePathValue in @(
        $trackedSourceFiles +
        $supplementalSourceFiles)) {
    $relativePath = [string]$relativePathValue
    if (
        [string]::IsNullOrWhiteSpace($relativePath) -or
        -not (Test-IsSourceDistributionPath $relativePath)) {
        continue
    }
    $null = $sourceFileSet.Add(
        $relativePath.Replace('\', '/'))
}
$sourceFiles = @($sourceFileSet | Sort-Object)
if ($sourceFiles.Count -eq 0) {
    throw 'No source files were selected for distribution.'
}

$trackedChangePaths = @(
    @(
        Invoke-HikariGit @(
            'diff',
            '--name-only',
            'HEAD',
            '--')) +
    @(
        Invoke-HikariGit @(
            'diff',
            '--cached',
            '--name-only',
            'HEAD',
            '--')))
$selectedTrackedChanges = @(
    $trackedChangePaths |
        Where-Object {
            -not [string]::IsNullOrWhiteSpace($_) -and
            $sourceFileSet.Contains(
                ([string]$_).Replace('\', '/'))
        } |
        Sort-Object -Unique)
if ($selectedTrackedChanges.Count -gt 0) {
    throw (
        "Tracked project files differ from commit $sourceRevision. " +
        "Commit or revert them before creating a clean source distribution:`n" +
        ($selectedTrackedChanges -join [Environment]::NewLine))
}

foreach ($relativePathValue in $sourceFiles) {
    $relativePath = [string]$relativePathValue
    if ([string]::IsNullOrWhiteSpace($relativePath)) {
        continue
    }
    Copy-SourceFile `
        -RelativePath $relativePath `
        -DestinationRoot $packageRoot
}

$bundledDependencyRoots = @(
    'vcpkg_installed/x64-windows',
    'vcpkg_installed_static/x64-windows-static',
    'ThirdParty/WinPixEventRuntime/bin/x64'
)
foreach ($dependencyRoot in $bundledDependencyRoots) {
    Copy-DependencyDirectory `
        -RelativePath $dependencyRoot `
        -DestinationRoot $packageRoot
}

$manifest = [ordered]@{
    schemaVersion = 1
    packageName = $PackageName
    generatedAtUtc = [DateTime]::UtcNow.ToString('o')
    source = [ordered]@{
        branch = $sourceBranch
        commit = $sourceRevision
        trackedProjectFilesMatchCommit = $true
    }
    build = [ordered]@{
        operatingSystem = 'Windows x64'
        cmakeMinimum = '3.28'
        generator = 'Visual Studio 17 2022'
        toolchain = 'MSVC v143'
        presets = @(
            'debug',
            'editor-release',
            'game-shipping')
    }
    bundledDependencyRoots = $bundledDependencyRoots
    dependencyLock =
        'Tools/Dependencies/HIKARI_ThirdParty.lock.json'
    sourceSelection = [ordered]@{
        policy =
            'Git-tracked project files plus the recipient README'
        supplementalFiles = $supplementalSourceFiles
        internalDevelopmentMetadataOmitted = $true
    }
    omittedOptionalDependencies = @(
        [ordered]@{
            name = 'NVIDIA Streamline SDK'
            expectedPath = 'ThirdParty/Streamline/sdk'
            reason =
                'Optional local SDK; not redistributed as a standalone source-package dependency.'
        })
}
$manifestPath =
    Join-Path $packageRoot 'SOURCE_PACKAGE_MANIFEST.json'
$manifest |
    ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $manifestPath -Encoding UTF8

Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory(
    $packageRoot,
    $zipPath,
    [IO.Compression.CompressionLevel]::Optimal,
    $true)

$zipFile = Get-Item -LiteralPath $zipPath
$zipHash =
    (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash

if (-not $KeepStaging) {
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}

Write-Host 'HIKARI source distribution created:'
Write-Host "  ZIP:    $zipPath"
Write-Host (
    '  Size:   {0} MiB' -f
    [math]::Round($zipFile.Length / 1MB, 2))
Write-Host "  SHA256: $zipHash"

[pscustomobject]@{
    PackageName = $PackageName
    ZipPath = $zipPath
    SizeBytes = $zipFile.Length
    Sha256 = $zipHash
    SourceCommit = $sourceRevision
    SourceBranch = $sourceBranch
}
