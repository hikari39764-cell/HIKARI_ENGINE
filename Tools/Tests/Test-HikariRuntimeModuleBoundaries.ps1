[CmdletBinding()]
param(
    # Removes baseline entries whose includes no longer exist. It never adds entries.
    [switch]$PruneBaseline
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
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
$forbidden = '#include\s+["<](Editor/|imgui\.h|imgui_impl_|ImGuizmo\.h)'
foreach ($line in [IO.File]::ReadAllLines($report)) {
    $owner, $path = $line -split '\|', 2
    $counts[$owner]++
    $source = [IO.File]::ReadAllText($path, [Text.Encoding]::UTF8)
    if ($source -match $forbidden) { throw "Runtime source directly includes Editor UI: $path" }
}
$rootCMake = [IO.File]::ReadAllText((Join-Path $repoRoot 'CMakeLists.txt'))
$closure = [regex]::Match($rootCMake, '(?s)set\(\s*HIKARI_RUNTIME_PUBLIC_LINK_LIBRARIES(?<body>.*?)\)')
if (-not $closure.Success) { throw 'Runtime public dependency closure is missing.' }
if ($closure.Groups['body'].Value -match 'HIKARI::(Assimp|RecastNavigation|ImGui|ImGuizmo)\b') {
    throw 'Editor/importer or private navigation dependencies leaked into public Runtime linkage.'
}
$budget = Get-Content -LiteralPath (Join-Path $repoRoot 'Tools/Build/HIKARI_EngineeringBudgets.json') -Raw | ConvertFrom-Json
$limit = [int]$budget.source.runtimeModuleCompileUnitCeiling
$violations = @($counts.GetEnumerator() | Where-Object { $_.Value -gt $limit } |
    ForEach-Object { "$($_.Key): $($_.Value) > $limit" })
Write-Host "Runtime ownership and UI dependency boundaries passed ($($counts.Count) modules)."
$failures = [Collections.Generic.List[string]]::new()
if ($violations.Count) { $failures.Add('Compile-unit budgets exceeded: ' + ($violations -join '; ')) }

# Engine module layers, bottom to top. A source may include its own module or a lower layer.
# Domain systems that run on the World (Animation, Physics, Gameplay) sit above Scene; Runtime
# composes them; Render3D consumes the runtime contracts of Assets. A header under an Internal
# directory belongs to that directory's parent and is not included from outside it.
# Existing exceptions are listed in the baseline: new ones fail, and fixed ones must be pruned
# with -PruneBaseline, so the list only shrinks.
$layers = @('Core', 'Platform', 'Diagnostics', 'Project', 'Authoring', 'Gfx', 'Input', 'Audio', 'Assets',
    'Render2D', 'Render3D', 'Vfx', 'Scene', 'Animation', 'Physics', 'Gameplay', 'Runtime', 'Tools', 'Editor')
$rank = @{}
for ($index = 0; $index -lt $layers.Count; ++$index) { $rank[$layers[$index]] = $index }
$engineRoot = Join-Path $repoRoot 'HIKARI'
$includePattern = [regex]'(?m)^\s*#\s*include\s+"(?<path>[A-Za-z0-9_]+/[^"]+)"'
$found = [Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)
foreach ($file in Get-ChildItem -LiteralPath $engineRoot -Recurse -File -Include '*.cpp', '*.h') {
    $relative = $file.FullName.Substring($engineRoot.Length + 1).Replace('\', '/')
    $module = $relative.Split('/')[0]
    if (-not $rank.ContainsKey($module)) { continue }
    foreach ($match in $includePattern.Matches([IO.File]::ReadAllText($file.FullName, [Text.Encoding]::UTF8))) {
        $target = $match.Groups['path'].Value
        $targetModule = $target.Split('/')[0]
        if (-not $rank.ContainsKey($targetModule)) { continue }
        $internal = $target.IndexOf('/Internal/', [StringComparison]::Ordinal)
        $foreignInternal = $internal -ge 0 -and
            -not $relative.StartsWith($target.Substring(0, $internal + 1), [StringComparison]::Ordinal)
        if ($rank[$targetModule] -gt $rank[$module] -or $foreignInternal) { [void]$found.Add("$relative -> $target") }
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
