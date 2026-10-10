[CmdletBinding()]
param([Parameter(Mandatory)][string]$RequestPath)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$jobDirectory = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($RequestPath))
$workspaceDirectory = Join-Path $jobDirectory 'workspace'
$statusPath = Join-Path $jobDirectory 'result.json'
$result = [ordered]@{
    version = 1; build = 'NotRun'; behavior = 'NotRun'; success = $false; message = 'Preparing project build.'
}

function Write-Result {
    $temporaryPath = Join-Path $jobDirectory 'result.pending.json'
    [IO.File]::WriteAllText($temporaryPath, ($result | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    if (Test-Path -LiteralPath $statusPath) {
        [IO.File]::Replace($temporaryPath, $statusPath, [NullString]::Value)
    } else {
        [IO.File]::Move($temporaryPath, $statusPath)
    }
}

function Test-Cancellation {
    if (Test-Path -LiteralPath (Join-Path $jobDirectory 'cancel')) {
        throw [OperationCanceledException]::new('Project build cancelled; original source and binaries are unchanged.')
    }
}

function Resolve-WorkspaceFile([string]$relativePath) {
    if ([IO.Path]::IsPathRooted($relativePath) -or $relativePath.Contains(':')) {
        throw 'A verification executable must be relative to the candidate project.'
    }
    $resolved = [IO.Path]::GetFullPath((Join-Path $workspaceDirectory $relativePath))
    $testPrefix = [IO.Path]::GetFullPath((Join-Path $workspaceDirectory 'Library/Tests')).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($testPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Verification executables must be in the candidate project Library/Tests directory.'
    }
    for ($segment = $resolved; $segment; $segment = [IO.Path]::GetDirectoryName($segment)) {
        if (Test-Path -LiteralPath $segment) {
            if (((Get-Item -LiteralPath $segment -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'Verification paths cannot traverse reparse points.'
            }
        }
        if ($segment -eq $workspaceDirectory) { break }
    }
    return $resolved
}

try {
    $request = Get-Content -LiteralPath $RequestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($request.version -ne 1 -or -not (Test-Path -LiteralPath (Join-Path $workspaceDirectory 'Project.hikari'))) {
        throw 'Invalid project build request or missing copied project.'
    }
    $sdkDirectory = [IO.Path]::GetFullPath([string]$request.sdkRoot)
    $buildScript = Join-Path $sdkDirectory 'Tools/Build-HikariProject.ps1'
    $cmake = Join-Path $sdkDirectory 'Tools/CMake/bin/cmake.exe'
    if (-not (Test-Path -LiteralPath $buildScript) -or -not (Test-Path -LiteralPath $cmake)) {
        throw 'The distributed Editor SDK build tools are incomplete.'
    }
    Test-Cancellation
    $result.build = 'Running'
    Write-Result
    & $buildScript -Project $workspaceDirectory -SdkRoot $sdkDirectory -Preset editor-release
    if ($LASTEXITCODE -ne 0) { throw "Project compilation failed with exit code $LASTEXITCODE." }
    $result.build = 'Passed'
    $result.message = 'Compilation passed. Behavior has not been verified.'
    Write-Result
    Test-Cancellation

    # This opt-in project manifest describes real, windowless behavior tests.
    # It is trusted project build code, not a security sandbox for native code.
    $verificationPath = Join-Path $workspaceDirectory 'Tests/HikariVerification.json'
    if (Test-Path -LiteralPath $verificationPath) {
        $verification = Get-Content -LiteralPath $verificationPath -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($verification.version -ne 1 -or @($verification.targets).Count -gt 8 -or
            @($verification.executables).Count -eq 0 -or @($verification.executables).Count -gt 8) {
            throw 'Invalid project behavior verification manifest.'
        }
        $arguments = @()
        foreach ($definition in $verification.configureDefinitions.PSObject.Properties) {
            if ($definition.Name -notmatch '^[A-Z][A-Z0-9_]*_BUILD_TESTS$' -or [string]$definition.Value -notin @('ON','OFF')) {
                throw 'Verification configure definitions may only enable project BUILD_TESTS options.'
            }
            $arguments += "-D$($definition.Name)=$($definition.Value)"
        }
        $buildDirectory = Join-Path $workspaceDirectory 'Library/Build/CMake/sdk-v2/editor-release'
        $result.behavior = 'Running'
        Write-Result
        & $cmake -S $workspaceDirectory -B $buildDirectory @arguments
        if ($LASTEXITCODE -ne 0) { throw 'Behavior test configuration failed.' }
        foreach ($target in $verification.targets) {
            if ([string]$target -notmatch '^[A-Za-z][A-Za-z0-9_]{0,95}$') { throw 'Invalid behavior test target name.' }
            Test-Cancellation
            & $cmake --build $buildDirectory --config EditorRelease --target $target --parallel 4
            if ($LASTEXITCODE -ne 0) { throw "Behavior test build failed: $target" }
        }
        foreach ($executable in $verification.executables) {
            Test-Cancellation
            $testPath = Resolve-WorkspaceFile ([string]$executable)
            if (-not (Test-Path -LiteralPath $testPath -PathType Leaf)) { throw "Behavior test is missing: $executable" }
            Push-Location $workspaceDirectory
            try { & $testPath; $testExitCode = $LASTEXITCODE } finally { Pop-Location }
            if ($testExitCode -ne 0) { throw "Behavior test failed: $executable (exit $testExitCode)" }
        }
        $result.behavior = 'Passed'
    }
    Test-Cancellation
    $result.success = $true
    $result.message = 'Build passed on a copy of the current source. The loaded module is unchanged; rebuild and reload to use it.'
    Write-Result
    exit 0
} catch {
    if ($result.build -eq 'Running') { $result.build = 'Failed' }
    if ($result.behavior -eq 'Running') { $result.behavior = 'Failed' }
    $result.message = $_.Exception.Message
    Write-Result
    Write-Error -Message $result.message -ErrorAction Continue
    exit 1
}
