[CmdletBinding()]
param(
    # Removes baseline entries whose includes no longer exist. It never adds entries.
    [switch]$PruneBaseline
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$engineRoot = Join-Path $repoRoot 'HIKARI'

# Engine module layers, bottom to top (docs/alpha/design.html, section 1). A source may include its own module,
# a module of the same group, or a lower group. Scene core sits below the domain systems that run on the World
# (Physics, Animation, Gameplay), which in turn sit below Runtime. Authoring (inspector interfaces) is placed
# beside Assets. RenderContracts (shader contracts and artifact readers, cluster geometry and material program
# formats, lightmap contracts, model data) sits between Gfx and Render3D/Render2D. A header under an Internal
# directory belongs to that directory's parent and is not included from outside it.
$layers = @(
    @('Core', 'Platform', 'Diagnostics'),
    @('Gfx', 'Input', 'Audio', 'Project'),
    @('RenderContracts'),
    @('Render3D', 'Render2D'),
    @('Assets', 'Authoring'),
    @('Scene'),
    @('Physics', 'Animation', 'Gameplay'),
    @('Runtime'),
    @('Editor', 'Tools'))
$rank = @{}
for ($index = 0; $index -lt $layers.Count; ++$index) {
    foreach ($name in $layers[$index]) { $rank[$name] = $index }
}
# Vfx (with its Post) is folded into Render3D; drop this entry when the directory moves.
$moduleAlias = @{ Vfx = 'Render3D' }

# A moved module keeps its include paths under HIKARI/<Module>/Public|Private/, so 'Core/Private/Core/Math/X.h'
# is the include path 'Core/Math/X.h'. Baseline entries use the include path and stay valid when files move.
function Get-IncludePath([string]$relative) {
    return [regex]::Replace($relative, '^[A-Za-z0-9_]+/(?:Public|Private)/', '')
}
function Get-EngineModule([string]$includePath) {
    $name = $includePath.Split('/')[0]
    if ($moduleAlias.ContainsKey($name)) { return $moduleAlias[$name] }
    return $name
}

$output = Join-Path $repoRoot 'out/build/editor-release/tests/source-ownership'
[IO.Directory]::CreateDirectory($output) | Out-Null
$probe = Join-Path $output 'inspect.cmake'
$report = Join-Path $output 'ownership.tsv'
# Execute production ownership rules; don't maintain a second classification map.
$cmake = @'
cmake_minimum_required(VERSION 3.28)
include("${HIKARI_SOURCE_ROOT}/cmake/HikariSources.cmake")
include("${HIKARI_SOURCE_ROOT}/cmake/HikariRuntimeModules.cmake")
file(WRITE "${OUTPUT_PATH}" "")
foreach(owner IN LISTS HIKARI_RUNTIME_MODULE_SOURCE_VARIABLES)
    foreach(source IN LISTS ${owner})
        file(APPEND "${OUTPUT_PATH}" "${owner}|${source}\n")
    endforeach()
endforeach()
'@
[IO.File]::WriteAllText($probe, $cmake, [Text.UTF8Encoding]::new($false))
& cmake "-DHIKARI_SOURCE_ROOT=$($repoRoot.Replace('\','/'))" "-DOUTPUT_PATH=$($report.Replace('\','/'))" -P $probe
if ($LASTEXITCODE -ne 0) { throw 'Runtime ownership rules rejected the source manifest.' }
$counts = @{}
$moduleUnits = @{}
$engineRootPrefix = $engineRoot.Replace('\', '/') + '/'
$forbidden = '#include\s+["<](Editor/|imgui\.h|imgui_impl_|ImGuizmo\.h)'
foreach ($line in [IO.File]::ReadAllLines($report)) {
    $owner, $path = $line -split '\|', 2
    $counts[$owner]++
    $moduleUnits[(Get-EngineModule (Get-IncludePath $path.Substring($engineRootPrefix.Length)))]++
    $source = [IO.File]::ReadAllText($path, [Text.Encoding]::UTF8)
    if ($source -match $forbidden) { throw "Runtime source directly includes Editor UI: $path" }
}
$rootCMake = [IO.File]::ReadAllText((Join-Path $repoRoot 'CMakeLists.txt'))
$closure = [regex]::Match($rootCMake, '(?s)set\(\s*HIKARI_RUNTIME_PUBLIC_LINK_LIBRARIES(?<body>.*?)\)')
if (-not $closure.Success) { throw 'Runtime public dependency closure is missing.' }
if ($closure.Groups['body'].Value -match 'HIKARI::(Assimp|RecastNavigation|ImGui|ImGuizmo)\b') {
    throw 'Editor/importer or private navigation dependencies leaked into public Runtime linkage.'
}
# Compile units are counted per module, because the module is one library once it is moved. Vfx counts as Render3D
# (Render3D 235 = 216 + Vfx 19). Render3D, Scene and Assets are bigger than the default ceiling and are frozen at
# their current size in moduleCompileUnitCeilings; the numbers only go down.
$budget = Get-Content -LiteralPath (Join-Path $repoRoot 'Tools/Build/HIKARI_EngineeringBudgets.json') -Raw | ConvertFrom-Json
$defaultLimit = [int]$budget.source.runtimeModuleCompileUnitCeiling
$violations = foreach ($entry in $moduleUnits.GetEnumerator()) {
    $override = $budget.source.moduleCompileUnitCeilings.PSObject.Properties[$entry.Key]
    $limit = if ($override) { [int]$override.Value } else { $defaultLimit }
    if ($entry.Value -gt $limit) { "$($entry.Key): $($entry.Value) > $limit" }
}
Write-Host "Runtime ownership and UI dependency boundaries passed ($($counts.Count) modules)."
$failures = [Collections.Generic.List[string]]::new()
if ($violations) { $failures.Add('Compile-unit budgets exceeded: ' + (@($violations) -join '; ')) }

# Existing exceptions are listed in the baseline: new ones fail, and fixed ones must be pruned
# with -PruneBaseline, so the list only shrinks.
$includePattern = [regex]'(?m)^\s*#\s*include\s+"(?<path>[A-Za-z0-9_]+/[^"]+)"'
$found = [Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)
$upward = @{}
foreach ($file in Get-ChildItem -LiteralPath $engineRoot -Recurse -File -Include '*.cpp', '*.h') {
    $relative = Get-IncludePath $file.FullName.Substring($engineRoot.Length + 1).Replace('\', '/')
    $module = Get-EngineModule $relative
    if (-not $rank.ContainsKey($module)) { continue }
    foreach ($match in $includePattern.Matches([IO.File]::ReadAllText($file.FullName, [Text.Encoding]::UTF8))) {
        $target = $match.Groups['path'].Value
        $targetModule = Get-EngineModule $target
        if (-not $rank.ContainsKey($targetModule)) { continue }
        $internal = $target.IndexOf('/Internal/', [StringComparison]::Ordinal)
        $foreignInternal = $internal -ge 0 -and
            -not $relative.StartsWith($target.Substring(0, $internal + 1), [StringComparison]::Ordinal)
        $isUpward = $rank[$targetModule] -gt $rank[$module]
        if (($isUpward -or $foreignInternal) -and $found.Add("$relative -> $target")) {
            $label = if ($isUpward) { $targetModule } else { "$targetModule/Internal" }
            if (-not $upward.ContainsKey($module)) { $upward[$module] = @{} }
            $upward[$module][$label]++
        }
    }
}
Write-Host 'Upward and foreign Internal includes per module (0 = no layer violation):'
foreach ($group in $layers) {
    foreach ($name in $group) {
        if (-not (Test-Path -LiteralPath (Join-Path $engineRoot $name))) { continue }
        $entries = @()
        if ($upward.ContainsKey($name)) { $entries = @($upward[$name].GetEnumerator() | Sort-Object Value -Descending) }
        $total = [int]($entries | Measure-Object -Property Value -Sum).Sum
        Write-Host ('  {0,-12}{1,4}  {2}' -f $name, $total, (($entries | ForEach-Object { "$($_.Key):$($_.Value)" }) -join ', '))
    }
}
$baselinePath = Join-Path $PSScriptRoot 'HIKARI_ModuleBoundaryBaseline.txt'
$encoding = [Text.UTF8Encoding]::new($false)
if (-not (Test-Path -LiteralPath $baselinePath)) {
    [IO.File]::WriteAllLines($baselinePath, [string[]]@($found), $encoding)
    Write-Host "Module boundary baseline recorded ($($found.Count) entries)."
}
$baseline = [Collections.Generic.SortedSet[string]]::new(
    [string[]]@([IO.File]::ReadAllLines($baselinePath) | Where-Object { $_ }), [StringComparer]::Ordinal)
$added = @($found | Where-Object { -not $baseline.Contains($_) })
$fixed = @($baseline | Where-Object { -not $found.Contains($_) })
if ($PruneBaseline -and $fixed.Count) {
    $baseline.ExceptWith([string[]]$fixed)
    [IO.File]::WriteAllLines($baselinePath, [string[]]@($baseline), $encoding)
    Write-Host "Pruned $($fixed.Count) fixed module boundary entries."
    $fixed = @()
}
if ($added.Count) {
    $failures.Add("New upward or foreign Internal includes (move the code to its owner instead):`n  " +
        ($added -join "`n  "))
}
if ($fixed.Count) {
    $failures.Add("Fixed module boundary entries are still in the baseline; rerun with -PruneBaseline:`n  " +
        ($fixed -join "`n  "))
}
if ($failures.Count) { throw ($failures -join "`n") }
Write-Host "Module layer boundaries passed ($($baseline.Count) recorded exceptions)."
