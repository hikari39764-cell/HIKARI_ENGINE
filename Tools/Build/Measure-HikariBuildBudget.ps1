[CmdletBinding()]
param(
    [ValidateSet('debug', 'editor-release', 'game-shipping')]
    [string[]]$Preset = @(
        'debug',
        'editor-release',
        'game-shipping'),

    [switch]$Check,

    [switch]$WriteSnapshot,

    [double]$WorkflowSeconds = -1
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

function Measure-Directory([string]$path) {
    if (-not (Test-Path -LiteralPath $path -PathType Container)) {
        return [pscustomobject]@{
            Files = 0
            Bytes = [int64]0
            MiB = 0.0
        }
    }

    $bytes = [int64]0
    $fileCount = 0
    foreach ($filePath in [System.IO.Directory]::EnumerateFiles(
            $path,
            '*',
            [System.IO.SearchOption]::AllDirectories)) {
        try {
            $file = Get-Item -LiteralPath $filePath -ErrorAction Stop
            $bytes += $file.Length
            $fileCount++
        } catch {
            Write-Warning "Skipped inaccessible build artifact: $filePath"
        }
    }
    return [pscustomobject]@{
        Files = $fileCount
        Bytes = $bytes
        MiB = [math]::Round($bytes / 1MB, 1)
    }
}

$presetRecords = @(
    foreach ($presetName in $Preset) {
        $directory = Join-Path $repoRoot "out\build\$presetName"
        $measurement = Measure-Directory $directory
        $hardMiB = [double](
            $budget.build.presetHardMiB.PSObject.Properties[
                $presetName].Value)
        [pscustomobject]@{
            Preset = $presetName
            Files = $measurement.Files
            MiB = $measurement.MiB
            HardMiB = $hardMiB
            Status = if ($measurement.MiB -gt $hardMiB) {
                'OVER'
            } else {
                'OK'
            }
        }
    })
$residentMiB = [math]::Round(
    ($presetRecords | Measure-Object -Property MiB -Sum).Sum,
    1)
$shippingInstall = Measure-Directory (
    Join-Path $repoRoot 'out\install\game-shipping')
$visualStudioCache = Measure-Directory (
    Join-Path $repoRoot '.vs')

Write-Host 'HIKARI build budget'
Write-Host (
    $presetRecords |
        Format-Table -AutoSize |
        Out-String)
Write-Host "  Resident build trees: $residentMiB MiB"
Write-Host (
    "  Daily target / current debt ceiling: " +
    "$($budget.build.residentBuildTargetMiB) / " +
    "$($budget.build.residentBuildDebtCeilingMiB) MiB")
Write-Host (
    "  Shipping install: $($shippingInstall.MiB) MiB / " +
    "$($budget.build.shippingInstallHardMiB) MiB")
Write-Host (
    "  Visual Studio cache: $($visualStudioCache.MiB) MiB / " +
    "$($budget.build.visualStudioCacheHardMiB) MiB")

$failures = @()
if (
    $residentMiB -gt
    [double]$budget.build.residentBuildDebtCeilingMiB) {
    $failures += (
        "Resident build trees exceed the retained-output debt ceiling: " +
        "$residentMiB MiB > " +
        "$($budget.build.residentBuildDebtCeilingMiB) MiB.")
}
foreach ($record in $presetRecords) {
    if ($record.MiB -gt $record.HardMiB) {
        $failures += (
            "Preset '$($record.Preset)' exceeds its debt ceiling: " +
            "$($record.MiB) MiB > $($record.HardMiB) MiB.")
    }
}
if (
    $shippingInstall.MiB -gt
    [double]$budget.build.shippingInstallHardMiB) {
    $failures += (
        "Shipping install exceeds its product ceiling: " +
        "$($shippingInstall.MiB) MiB > " +
        "$($budget.build.shippingInstallHardMiB) MiB.")
}
if (
    $visualStudioCache.MiB -gt
    [double]$budget.build.visualStudioCacheHardMiB) {
    $failures += (
        "Visual Studio cache exceeds its local ceiling: " +
        "$($visualStudioCache.MiB) MiB > " +
        "$($budget.build.visualStudioCacheHardMiB) MiB.")
}

$snapshot = [ordered]@{
    schemaVersion = 1
    capturedAtUtc = [DateTime]::UtcNow.ToString('o')
    workflowSeconds = if ($WorkflowSeconds -ge 0) {
        [math]::Round($WorkflowSeconds, 2)
    } else {
        $null
    }
    presetBuilds = @($presetRecords)
    residentBuildMiB = $residentMiB
    shippingInstallMiB = $shippingInstall.MiB
    visualStudioCacheMiB = $visualStudioCache.MiB
}
if ($WriteSnapshot) {
    $metricsRoot = Join-Path $repoRoot 'out\metrics'
    [System.IO.Directory]::CreateDirectory($metricsRoot) | Out-Null
    $snapshotPath = Join-Path $metricsRoot 'build-budget.json'
    $snapshot |
        ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $snapshotPath -Encoding UTF8
    Write-Host "  Snapshot: $snapshotPath"
}

if ($Check -and $failures.Count -ne 0) {
    throw (
        "HIKARI build budget check failed:`n - " +
        ($failures -join "`n - "))
}
if ($Check) {
    Write-Host 'HIKARI build budget check passed.'
}
