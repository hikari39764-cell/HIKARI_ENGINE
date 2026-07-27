[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('debug', 'editor-release', 'game-shipping')]
    [string]$Preset,

    [switch]$IncludeInstall,

    [switch]$Apply
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

function Resolve-SafePresetPath(
    [string]$category,
    [string]$presetName) {
    $categoryRoot = [System.IO.Path]::GetFullPath(
        (Join-Path $repoRoot "out\$category"))
    $targetPath = [System.IO.Path]::GetFullPath(
        (Join-Path $categoryRoot $presetName))
    $targetParent = [System.IO.Directory]::GetParent($targetPath)
    if (
        $null -eq $targetParent -or
        -not [string]::Equals(
            $targetParent.FullName,
            $categoryRoot,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe HIKARI preset output path: $targetPath"
    }
    return $targetPath
}

function Measure-DirectoryMiB([string]$path) {
    if (-not (Test-Path -LiteralPath $path -PathType Container)) {
        return 0.0
    }
    $bytes = [int64]0
    foreach ($filePath in [System.IO.Directory]::EnumerateFiles(
            $path,
            '*',
            [System.IO.SearchOption]::AllDirectories)) {
        try {
            $bytes += (
                Get-Item -LiteralPath $filePath -ErrorAction Stop).Length
        } catch {
            Write-Warning "Skipped inaccessible build artifact: $filePath"
        }
    }
    return [math]::Round($bytes / 1MB, 1)
}

$targets = @(
    Resolve-SafePresetPath 'build' $Preset)
if ($IncludeInstall) {
    $targets += Resolve-SafePresetPath 'install' $Preset
}

foreach ($target in $targets) {
    $sizeMiB = Measure-DirectoryMiB $target
    if (-not (Test-Path -LiteralPath $target -PathType Container)) {
        Write-Host "Not present: $target"
        continue
    }

    if (-not $Apply) {
        Write-Host "Preview: $target ($sizeMiB MiB)"
        continue
    }

    if ($PSCmdlet.ShouldProcess(
            $target,
            "Remove generated HIKARI preset output ($sizeMiB MiB)")) {
        Remove-Item -LiteralPath $target -Recurse -Force
        Write-Host "Removed: $target"
    }
}

if (-not $Apply) {
    Write-Host 'No files were removed. Add -Apply to perform this cleanup.'
}
