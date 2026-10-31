$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$runtimePath = Join-Path $repoRoot 'HIKARI\Core\Text\HIKARI_Localization.cpp'
$tablePaths = @{
    English = Join-Path $repoRoot 'HIKARI\Core\Text\Localization\HIKARI_LocalizationEnglish.inl'
    Chinese = Join-Path $repoRoot 'HIKARI\Core\Text\Localization\HIKARI_LocalizationChinese.inl'
    Japanese = Join-Path $repoRoot 'HIKARI\Core\Text\Localization\HIKARI_LocalizationJapanese.inl'
}

$runtimeSource = [System.IO.File]::ReadAllText(
    $runtimePath,
    [System.Text.Encoding]::UTF8)

function Get-TableContract([string]$language) {
    $path = $tablePaths[$language]
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Localization table is missing: $language ($path)"
    }

    $source = [System.IO.File]::ReadAllText(
        $path,
        [System.Text.Encoding]::UTF8)
    $matches = [regex]::Matches(
        $source,
        '\{\s*"((?:\\.|[^"\\])*)"\s*,')
    $keys = [System.Collections.Generic.Dictionary[string, bool]]::new([System.StringComparer]::Ordinal)
    foreach ($entry in $matches) {
        $keys[$entry.Groups[1].Value] = $true
    }

    if ($matches.Count -ne $keys.Count) {
        throw "$language table contains duplicate keys: entries=$($matches.Count), unique=$($keys.Count)"
    }

    return @{
        Keys = $keys
        Source = $source
    }
}

$tables = @{}
foreach ($language in 'English', 'Chinese', 'Japanese') {
    $tables[$language] = Get-TableContract $language
}

$english = $tables.English.Keys
$chinese = $tables.Chinese.Keys
$japanese = $tables.Japanese.Keys
if ($english.Count -ne $chinese.Count -or
    $english.Count -ne $japanese.Count) {
    throw "Localization key counts differ: EN=$($english.Count) ZH=$($chinese.Count) JA=$($japanese.Count)"
}

foreach ($language in 'Chinese', 'Japanese') {
    $missing = @(
        $english.Keys |
            Where-Object { -not $tables[$language].Keys.ContainsKey($_) })
    $extra = @(
        $tables[$language].Keys.Keys |
            Where-Object { -not $english.ContainsKey($_) })
    if ($missing.Count -gt 0 -or $extra.Count -gt 0) {
        throw "$language key set differs from English. Missing=$($missing -join ', '); Extra=$($extra -join ', ')"
    }

    # English lookup keys may contain punctuation. Check actual replacement characters, not '?'.
    $replacementCharacters = [regex]::Matches(
        $tables[$language].Source,
        '\uFFFD').Count
    if ($replacementCharacters -gt 0) {
        throw "$language table contains $replacementCharacters Unicode replacement characters."
    }
}

# Literal keys passed to TEXT::Localize / LocalizeWindow must exist; a missing key is shown to users verbatim.
$keyCall = [regex]'\bLocalize(?:Window)?\(\s*"([A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)+)"'
$missingReferences = [System.Collections.Generic.List[string]]::new()
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $repoRoot 'HIKARI') -Recurse -File -Include '*.cpp', '*.h') {
    $source = [System.IO.File]::ReadAllText($file.FullName, [System.Text.Encoding]::UTF8)
    foreach ($reference in $keyCall.Matches($source)) {
        if (-not $english.ContainsKey($reference.Groups[1].Value)) {
            $missingReferences.Add("$($reference.Groups[1].Value) ($($file.Name))")
        }
    }
}
if ($missingReferences.Count -gt 0) {
    throw "Localization keys referenced by code are missing: $($missingReferences -join ', ')"
}

$runtimeLineCount = ([regex]::Matches($runtimeSource, "`n")).Count + 1
if ($runtimeLineCount -ge 200) {
    throw "Localization runtime/data ownership regressed: runtime source has $runtimeLineCount lines."
}

Write-Host "HIKARI localization tables passed ($($english.Count) keys x EN/ZH/JA; runtime=$runtimeLineCount lines)."
