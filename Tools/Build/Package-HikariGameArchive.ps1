[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageRoot,
    [string]$ArchivePath
)

$ErrorActionPreference = 'Stop'

function Get-HikariRelativePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,
        [Parameter(Mandatory = $true)]
        [string]$Target
    )

    $normalizedRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $normalizedTarget = [IO.Path]::GetFullPath($Target)
    $rootPrefix = $normalizedRoot + [IO.Path]::DirectorySeparatorChar
    if (-not $normalizedTarget.StartsWith(
            $rootPrefix,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside the expected root: $normalizedTarget"
    }

    return $normalizedTarget.Substring($rootPrefix.Length).
        Replace('\', '/')
}

$package = (Resolve-Path -LiteralPath $PackageRoot).Path
$productManifest = Join-Path $package 'product_manifest.json'
if (-not (Test-Path -LiteralPath $productManifest -PathType Leaf)) {
    throw "Validated HIKARI product manifest is missing: $productManifest"
}
$manifest = Get-Content -Raw -Encoding UTF8 -LiteralPath $productManifest |
    ConvertFrom-Json
if ([int]$manifest.formatVersion -ne 2) {
    throw 'HIKARI product manifest must use hashed format version 2.'
}
$executableName = ([string]$manifest.executable).Replace('\', '/')
if ([string]::IsNullOrWhiteSpace($executableName)) {
    $executableName = 'HIKARI_Game.exe'
}
if ($executableName.Contains('/') -or
    [IO.Path]::GetExtension($executableName) -ine '.exe') {
    throw "Invalid product executable name: $executableName"
}
$declared = @{}
foreach ($file in @($manifest.files)) {
    $relative = ([string]$file.path).Replace('\', '/')
    $sha256 = ([string]$file.sha256).ToLowerInvariant()
    if ([string]::IsNullOrWhiteSpace($relative) -or
        $relative.StartsWith('/') -or
        $relative.Contains('../') -or
        $sha256 -notmatch '^[0-9a-f]{64}$' -or
        $declared.ContainsKey($relative)) {
        throw "Invalid or duplicate product-manifest entry: $relative"
    }
    $declared[$relative] = $file
}
$actual = @{}
foreach ($file in Get-ChildItem -LiteralPath $package -Recurse -File) {
    $relative = Get-HikariRelativePath `
        -Root $package `
        -Target $file.FullName
    if ($relative -eq 'product_manifest.json') {
        continue
    }
    if (-not $declared.ContainsKey($relative)) {
        throw "Package contains an unmanifested runtime file: $relative"
    }
    if ([int64]$declared[$relative].size -ne $file.Length) {
        throw "Package file size drifted after export: $relative"
    }
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).
        Hash.ToLowerInvariant()
    if ($hash -cne ([string]$declared[$relative].sha256).
            ToLowerInvariant()) {
        throw "Package file hash drifted after export: $relative"
    }
    $actual[$relative] = $true
}
foreach ($relative in $declared.Keys) {
    if (-not $actual.ContainsKey($relative)) {
        throw "Package is missing a manifested runtime file: $relative"
    }
}
if ([string]::IsNullOrWhiteSpace($ArchivePath)) {
    $item = Get-Item -LiteralPath $package
    $ArchivePath = Join-Path $item.Parent.FullName ($item.Name + '.zip')
}
$archive = [IO.Path]::GetFullPath($ArchivePath)
$packagePrefix = $package.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if ($archive.StartsWith($packagePrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Archive output must be outside the package directory.'
}
if ([IO.Path]::GetExtension($archive) -ine '.zip') {
    throw 'Archive output must use the .zip extension.'
}
if (Test-Path -LiteralPath $archive) {
    throw "Archive output already exists: $archive"
}
$archiveParent = Split-Path -Parent $archive
if (-not (Test-Path -LiteralPath $archiveParent -PathType Container)) {
    New-Item -ItemType Directory -Path $archiveParent | Out-Null
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$temporary = $archive + '.tmp-' + [Guid]::NewGuid().ToString('N')
try {
    [IO.Compression.ZipFile]::CreateFromDirectory(
        $package,
        $temporary,
        [IO.Compression.CompressionLevel]::Optimal,
        $false)
    $zip = [IO.Compression.ZipFile]::OpenRead($temporary)
    try {
        $required = @(
            $executableName,
            'HIKARI_GameProject.dll',
            'product_manifest.json',
            'dxcompiler.dll',
            'dxil.dll',
            'HIKARI/runtime_config.json',
            'Content/CookedAssetRegistry.json')
        $entryNames = @($zip.Entries |
            Where-Object { -not [string]::IsNullOrEmpty($_.Name) } |
            ForEach-Object { $_.FullName.Replace('\', '/') })
        $expectedEntries = @($declared.Keys) +
            @('product_manifest.json')
        if ($entryNames.Count -ne $expectedEntries.Count) {
            throw 'Archive file count does not match the product manifest.'
        }
        foreach ($entry in $entryNames) {
            if ($entry -notin $expectedEntries) {
                throw "Archive contains an unexpected file: $entry"
            }
        }
        foreach ($entry in $required) {
            if ($entryNames -notcontains $entry) {
                throw "Archive verification is missing: $entry"
            }
        }
    }
    finally {
        $zip.Dispose()
    }
    Move-Item -LiteralPath $temporary -Destination $archive
}
finally {
    if (Test-Path -LiteralPath $temporary) {
        Remove-Item -LiteralPath $temporary -Force
    }
}

$sourceBytes = (Get-ChildItem -LiteralPath $package -Recurse -File |
    Measure-Object Length -Sum).Sum
$archiveBytes = (Get-Item -LiteralPath $archive).Length
[pscustomobject]@{
    PackageRoot = $package
    ArchivePath = $archive
    SourceBytes = $sourceBytes
    ArchiveBytes = $archiveBytes
    Ratio = if ($sourceBytes -gt 0) {
        [Math]::Round($archiveBytes / $sourceBytes, 4)
    }
    else { 0.0 }
} | Format-List
