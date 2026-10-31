[CmdletBinding()]
param([string]$SdkRoot = '')

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (-not $SdkRoot) {
    $SdkRoot = Join-Path $repoRoot 'out/build/editor-release/bin/Runtime/Sdk'
}
$SdkRoot = (Resolve-Path -LiteralPath $SdkRoot).Path
$output = Join-Path $repoRoot 'out/build/editor-release/tests/sdk-public-headers'
[IO.Directory]::CreateDirectory($output) | Out-Null
$headers = @(Get-ChildItem -LiteralPath (Join-Path $SdkRoot 'include/HIKARI/Project') -Filter '*.h')
$sources = @()
foreach ($header in $headers) {
    $source = $header.BaseName + '.cpp'
    [IO.File]::WriteAllText((Join-Path $output $source),
        '#include "HIKARI/Project/' + $header.Name + '"' + "`n", [Text.UTF8Encoding]::new($false))
    $sources += '    "' + $source + '"'
}
# installed SDK の usage requirements だけを検査する。エンジン側の compile flags を混ぜない。
$cmake = @'
cmake_minimum_required(VERSION 3.28)
project(HikariPublicHeaders LANGUAGES CXX)
find_package(HIKARI CONFIG REQUIRED)
add_library(HIKARI_PublicHeaders OBJECT
@SOURCES@
)
target_link_libraries(HIKARI_PublicHeaders PRIVATE HIKARI::GameProjectSdk)
'@
[IO.File]::WriteAllText((Join-Path $output 'CMakeLists.txt'),
    $cmake.Replace('@SOURCES@', ($sources -join "`n")), [Text.UTF8Encoding]::new($false))
$cmakeExe = Join-Path $SdkRoot 'Tools/CMake/bin/cmake.exe'
if (-not (Test-Path -LiteralPath $cmakeExe)) { $cmakeExe = 'cmake' }
$build = Join-Path $output 'build'
& $cmakeExe -S $output -B $build -G 'Visual Studio 17 2022' -A x64 `
    "-DHIKARI_DIR=$SdkRoot/lib/cmake/HIKARI" -DCMAKE_CONFIGURATION_TYPES=EditorRelease `
    *> (Join-Path $output 'configure.log')
if ($LASTEXITCODE -ne 0) { throw "Public SDK configure failed: $output/configure.log" }
& $cmakeExe --build $build --config EditorRelease --target HIKARI_PublicHeaders -- /m:2 /nologo /v:minimal `
    *> (Join-Path $output 'build.log')
if ($LASTEXITCODE -ne 0) { throw "Public SDK header compile failed: $output/build.log" }
Write-Host "Public SDK headers compiled independently: $($headers.Count)."
