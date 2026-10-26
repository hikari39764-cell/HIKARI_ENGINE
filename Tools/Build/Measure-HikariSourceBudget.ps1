[CmdletBinding()]
param(
    [switch]$Check,

    [switch]$WriteSnapshot
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$budgetPath = Join-Path $PSScriptRoot 'HIKARI_EngineeringBudgets.json'
if (-not (Test-Path -LiteralPath $budgetPath -PathType Leaf)) {
    throw 'The HIKARI engineering budget configuration is missing.'
}
$budget = ConvertFrom-Json (
    [System.IO.File]::ReadAllText(
        $budgetPath,
        [System.Text.Encoding]::UTF8))

$sourceExtensions = @(
    '.c',
    '.cc',
    '.cpp',
    '.cxx',
    '.h',
    '.hh',
    '.hpp',
    '.hxx',
    '.inl',
    '.hlsl',
    '.hlsli')
$sourceRoots = @(
    'HIKARI',
    'include',
    'Tools',
    'cmake\SDK')

function Get-RelativeRepositoryPath([string]$fullPath) {
    $prefixLength = $repoRoot.Length + 1
    if ($fullPath.Length -lt $prefixLength) {
        throw "Path escapes the HIKARI repository: $fullPath"
    }
    return $fullPath.Substring($prefixLength).Replace('\', '/')
}

$sourceFiles = @()
foreach ($sourceRoot in $sourceRoots) {
    $absoluteRoot = Join-Path $repoRoot $sourceRoot
    if (-not (Test-Path -LiteralPath $absoluteRoot -PathType Container)) {
        continue
    }
    $sourceFiles += @(
        Get-ChildItem -LiteralPath $absoluteRoot -Recurse -File |
            Where-Object {
                $_.Extension.ToLowerInvariant() -in $sourceExtensions
            })
}
$sourceFiles += @(
    Get-ChildItem -LiteralPath $repoRoot -File |
        Where-Object {
            $_.Extension.ToLowerInvariant() -in $sourceExtensions
        })
$sourceFiles = @(
    $sourceFiles |
        Sort-Object FullName -Unique)

$records = @(
    foreach ($file in $sourceFiles) {
        $relativePath = Get-RelativeRepositoryPath $file.FullName
        $pathParts = $relativePath.Split('/')
        $module = if (
            $pathParts.Count -gt 1 -and
            $pathParts[0] -eq 'HIKARI') {
            $pathParts[1]
        } elseif ($pathParts.Count -gt 1) {
            $pathParts[0]
        } else {
            '[root]'
        }
        [pscustomobject]@{
            Path = $relativePath
            Module = $module
            Lines = (
                [System.IO.File]::ReadAllLines(
                    $file.FullName,
                    [System.Text.Encoding]::UTF8)).Length
            Bytes = $file.Length
        }
    })

$totalLines = [int64](
    $records |
        Measure-Object -Property Lines -Sum).Sum
$totalBytes = [int64](
    $records |
        Measure-Object -Property Bytes -Sum).Sum
$softThreshold = [int]$budget.source.softLargeFileLineThreshold
$hardThreshold = [int]$budget.source.hardLargeFileLineThreshold
$softLargeFiles = @(
    $records |
        Where-Object { $_.Lines -ge $softThreshold })
$hardLargeFiles = @(
    $records |
        Where-Object { $_.Lines -ge $hardThreshold })
$moduleRecords = @(
    $records |
        Where-Object { $_.Path.StartsWith('HIKARI/') } |
        Group-Object Module |
        ForEach-Object {
            [pscustomobject]@{
                Module = $_.Name
                Files = $_.Count
                Lines = [int64](
                    $_.Group |
                        Measure-Object -Property Lines -Sum).Sum
            }
        } |
        Sort-Object Lines -Descending)

Write-Host 'HIKARI source budget'
Write-Host (
    "  Authored source: $($records.Count) file(s), " +
    "$totalLines physical line(s), " +
    "$([math]::Round($totalBytes / 1MB, 2)) MiB")
Write-Host (
    "  Files >= $softThreshold lines: " +
    "$($softLargeFiles.Count)")
Write-Host (
    "  Files >= $hardThreshold lines: " +
    "$($hardLargeFiles.Count)")
Write-Host ''
Write-Host (
    $moduleRecords |
        Select-Object -First 12 |
        Format-Table -AutoSize |
        Out-String)

$failures = @()
if (
    $totalLines -gt
    [int64]$budget.source.authoredPhysicalLineCeiling) {
    $failures += (
        "Authored source grew beyond the frozen baseline: " +
        "$totalLines > " +
        "$($budget.source.authoredPhysicalLineCeiling).")
}
if (
    $softLargeFiles.Count -gt
    [int]$budget.source.softLargeFileCountCeiling) {
    $failures += (
        "The >= $softThreshold line debt grew: " +
        "$($softLargeFiles.Count) > " +
        "$($budget.source.softLargeFileCountCeiling).")
}
if (
    $hardLargeFiles.Count -gt
    [int]$budget.source.hardLargeFileCountCeiling) {
    $failures += (
        "The >= $hardThreshold line debt grew: " +
        "$($hardLargeFiles.Count) > " +
        "$($budget.source.hardLargeFileCountCeiling).")
}
foreach ($property in $budget.source.modulePhysicalLineCeilings.PSObject.Properties) {
    $moduleRecord = @(
        $moduleRecords |
            Where-Object { $_.Module -eq $property.Name })
    $actualLines = if ($moduleRecord.Count -eq 1) {
        [int64]$moduleRecord[0].Lines
    } else {
        0
    }
    if ($actualLines -gt [int64]$property.Value) {
        $failures += (
            "Module '$($property.Name)' grew beyond its baseline: " +
            "$actualLines > $($property.Value).")
    }
}

$snapshot = [ordered]@{
    schemaVersion = 1
    capturedAtUtc = [DateTime]::UtcNow.ToString('o')
    authoredFiles = $records.Count
    authoredPhysicalLines = $totalLines
    authoredMiB = [math]::Round($totalBytes / 1MB, 3)
    softLargeFileCount = $softLargeFiles.Count
    hardLargeFileCount = $hardLargeFiles.Count
    modules = @($moduleRecords)
}
if ($WriteSnapshot) {
    $metricsRoot = Join-Path $repoRoot 'out\metrics'
    [System.IO.Directory]::CreateDirectory($metricsRoot) | Out-Null
    $snapshotPath = Join-Path $metricsRoot 'source-budget.json'
    $snapshot |
        ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $snapshotPath -Encoding UTF8
    Write-Host "  Snapshot: $snapshotPath"
}

if ($Check -and $failures.Count -ne 0) {
    throw (
        "HIKARI source budget check failed:`n - " +
        ($failures -join "`n - "))
}
if ($Check) {
    Write-Host 'HIKARI source budget check passed.'
}
