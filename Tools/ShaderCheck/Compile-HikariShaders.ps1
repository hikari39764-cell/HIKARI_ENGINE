param(
    [string]$DxcPath = "",
    [string]$Group = "",
    [switch]$VerboseErrors
)

$ErrorActionPreference = "Stop"

# Transitional source diagnostic only. This script never publishes runtime
# artifacts; formal built-in HSHADERC production must use the engine-owned
# GFX::CompileShaderProgramForCook toolchain and the same catalog.

function Find-Dxc {
    param([string]$RequestedPath)

    if ($RequestedPath -and (Test-Path -LiteralPath $RequestedPath)) {
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }

    $fromPath = Get-Command dxc.exe -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    $kitsRoot = "C:\Program Files (x86)\Windows Kits\10\bin"
    if (Test-Path -LiteralPath $kitsRoot) {
        $candidate = Get-ChildItem -LiteralPath $kitsRoot -Recurse -Filter dxc.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "\\x64\\dxc.exe$" } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) {
            return $candidate.FullName
        }
    }

    throw "dxc.exe was not found. Pass -DxcPath or install Windows SDK."
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$shaderRoot = Join-Path $repoRoot 'HIKARI\Shaders'
$catalogPath = Join-Path $repoRoot 'HIKARI\Shaders\BuiltinShaderCatalog.json'
if (-not (Test-Path -LiteralPath $catalogPath -PathType Leaf)) {
    throw "Built-in shader catalog is missing: $catalogPath"
}
$catalog = [System.IO.File]::ReadAllText(
    $catalogPath,
    [System.Text.Encoding]::UTF8) | ConvertFrom-Json
if (
    $catalog.schema -ne 'HIKARI.BuiltinShaderCatalog' -or
    $catalog.version -ne 1 -or
    @($catalog.programs).Count -eq 0) {
    throw "Built-in shader catalog schema is invalid: $catalogPath"
}

$entries = @()
foreach ($program in $catalog.programs) {
    foreach ($catalogEntry in $program.entries) {
        $entries += @{
            Shader = Join-Path $shaderRoot $program.source
            Entry = $catalogEntry.entry
            Target = $catalogEntry.profile
            Group = $program.group
            IncludeRoots = @($program.includeRoots)
            Defines = @($program.defines)
        }
    }
}

if (-not [string]::IsNullOrWhiteSpace($Group)) {
    $entries = @($entries | Where-Object {
        $_.ContainsKey('Group') -and $_.Group -eq $Group
    })
    if ($entries.Count -eq 0) {
        throw "No shader compile entries are registered for group '$Group'."
    }
}

$dxc = Find-Dxc $DxcPath
$failures = New-Object System.Collections.Generic.List[object]
foreach ($entry in $entries) {
    $arguments = [System.Collections.Generic.List[string]]::new()
    $arguments.AddRange([string[]]@(
        '-E', $entry.Entry,
        '-T', $entry.Target,
        '-HV', '2021',
        '-O3',
        '-WX'))
    foreach ($includeRoot in $entry.IncludeRoots) {
        if ($includeRoot -ne 'EngineSdk') {
            throw "Unsupported built-in Shader include root: $includeRoot"
        }
        $arguments.Add('-I')
        $arguments.Add($shaderRoot)
    }
    foreach ($define in $entry.Defines) {
        $arguments.Add(
            '-D' + [string]$define.name + '=' + [string]$define.value)
    }
    $arguments.AddRange([string[]]@('-Fo', 'NUL', $entry.Shader))
    $output = & $dxc @arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        $failures.Add([pscustomobject]@{
            Shader = $entry.Shader
            Entry = $entry.Entry
            Target = $entry.Target
            Message = ($output -join "`n")
        })
    } elseif ($VerboseErrors -and $output) {
        Write-Host ($output -join "`n")
    }
}

if ($failures.Count -gt 0) {
    $failures | Format-List
    Write-Error ("Shader compile check failed: {0}/{1}" -f $failures.Count, $entries.Count)
    exit 1
}

Write-Host ("Shader compile check passed: {0} entries with {1}" -f $entries.Count, $dxc)
