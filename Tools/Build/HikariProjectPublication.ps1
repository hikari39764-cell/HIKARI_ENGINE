# Project module publication and last-good state. Imported by Build-HikariProject.ps1.

function Assert-HikariBuildOwnedPath([string]$Root, [string]$Path) {
    $owner = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $target = [IO.Path]::GetFullPath($Path)
    if (-not $target.StartsWith($owner + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Build publication path escaped its owner: $target"
    }
    $cursor = $target
    while ($cursor.Length -ge $owner.Length) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Build publication cannot traverse a reparse point: $cursor"
            }
        }
        $cursor = Split-Path -Parent $cursor
    }
    return $target
}

function Remove-HikariPublicationDirectory([string]$Root, [string]$Path) {
    $checked = Assert-HikariBuildOwnedPath $Root $Path
    if (Test-Path -LiteralPath $checked) {
        foreach ($entry in Get-ChildItem -LiteralPath $checked -Force -Recurse) {
            $null = Assert-HikariBuildOwnedPath $Root $entry.FullName
        }
        Remove-Item -LiteralPath $checked -Recurse -Force
    }
}

function Restore-HikariModulePublication([string]$OutputRoot) {
    $transaction = Assert-HikariBuildOwnedPath $OutputRoot (Join-Path $OutputRoot '.module-publication')
    $journal = Join-Path $transaction 'pending.json'
    if (-not (Test-Path -LiteralPath $journal -PathType Leaf)) {
        Remove-HikariPublicationDirectory $OutputRoot $transaction
        return
    }
    $state = Get-Content -LiteralPath $journal -Raw | ConvertFrom-Json
    if ($state.version -ne 1) { throw 'Unknown pending module publication; preserving its recovery data.' }
    foreach ($name in @('bin', 'HIKARI_GameProject.buildstate.json')) {
        $live = Assert-HikariBuildOwnedPath $OutputRoot (Join-Path $OutputRoot $name)
        $backup = Assert-HikariBuildOwnedPath $OutputRoot (Join-Path $transaction "backup/$name")
        $discard = Assert-HikariBuildOwnedPath $OutputRoot (Join-Path $transaction "discard/$name")
        $existed = if ($name -eq 'bin') { [bool]$state.hadBin } else { [bool]$state.hadState }
        if ((Test-Path -LiteralPath $backup) -or -not $existed) {
            if (Test-Path -LiteralPath $live) {
                Move-Item -LiteralPath $live -Destination $discard -Force
            }
            if (Test-Path -LiteralPath $backup) { Move-Item -LiteralPath $backup -Destination $live }
        }
    }
    Remove-Item -LiteralPath $journal -Force
    Remove-HikariPublicationDirectory $OutputRoot $transaction
}

function Publish-HikariProjectModules([string]$OutputRoot, [string]$CandidateRoot, [bool]$WithEditor) {
    $transaction = Assert-HikariBuildOwnedPath $OutputRoot (Join-Path $OutputRoot '.module-publication')
    $pending = Join-Path $transaction 'next'
    $backup = Join-Path $transaction 'backup'
    $liveBin = Assert-HikariBuildOwnedPath $OutputRoot (Join-Path $OutputRoot 'bin')
    $stateName = 'HIKARI_GameProject.buildstate.json'
    $liveState = Assert-HikariBuildOwnedPath $OutputRoot (Join-Path $OutputRoot $stateName)
    $null = New-Item -ItemType Directory -Path (Join-Path $pending 'bin'),$backup,(Join-Path $transaction 'discard') -Force
    # Preserve project dependencies that the module linker does not produce.
    if (Test-Path -LiteralPath $liveBin) {
        foreach ($file in Get-ChildItem -LiteralPath $liveBin -Force -Recurse) {
            $null = Assert-HikariBuildOwnedPath $OutputRoot $file.FullName
        }
        foreach ($file in Get-ChildItem -LiteralPath $liveBin -Force) {
            Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $pending 'bin') -Recurse -Force
        }
    }
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $CandidateRoot 'bin') -File) {
        $null = Assert-HikariBuildOwnedPath $OutputRoot $file.FullName
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $pending 'bin') -Force
    }
    if (-not $WithEditor) {
        foreach ($name in @('HIKARI_GameProjectEditor.dll','HIKARI_GameProjectEditor.pdb')) {
            $file = Join-Path $pending "bin/$name"
            if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force }
        }
    }
    Copy-Item -LiteralPath (Join-Path $CandidateRoot $stateName) -Destination (Join-Path $pending $stateName)
    # Reject occupied products before changing the live set; never stop the user's app.
    $locks = @()
    try {
        foreach ($name in @('HIKARI_GameProject.dll','HIKARI_GameProjectEditor.dll')) {
            $file = Join-Path $liveBin $name
            if (Test-Path -LiteralPath $file) {
                $locks += [IO.File]::Open($file, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
            }
        }
    } catch {
        throw "Project module is in use; candidate built, last-good modules preserved. Close the project's running host before retrying. $($_.Exception.Message)"
    } finally {
        foreach ($handle in $locks) { $handle.Dispose() }
    }
    $journal = Join-Path $transaction 'pending.json'
    @{version=1;hadBin=(Test-Path -LiteralPath $liveBin);hadState=(Test-Path -LiteralPath $liveState)} |
        ConvertTo-Json | Set-Content -LiteralPath $journal -Encoding UTF8
    try {
        if (Test-Path -LiteralPath $liveBin) { Move-Item -LiteralPath $liveBin -Destination (Join-Path $backup 'bin') }
        if (Test-Path -LiteralPath $liveState) { Move-Item -LiteralPath $liveState -Destination (Join-Path $backup $stateName) }
        Move-Item -LiteralPath (Join-Path $pending 'bin') -Destination $liveBin
        Move-Item -LiteralPath (Join-Path $pending $stateName) -Destination $liveState
        Remove-Item -LiteralPath $journal -Force
    } catch {
        $failure = $_
        Restore-HikariModulePublication $OutputRoot
        throw $failure
    }
    try { Remove-HikariPublicationDirectory $OutputRoot $transaction }
    catch { Write-Warning "Modules published; deferred publication cleanup: $($_.Exception.Message)" }
}

function Test-HikariLastGoodModule {
    param(
        [Parameter(Mandatory)]
        [string]$BuildStatePath,

        [Parameter(Mandatory)]
        [string]$ModulePath,

        [Parameter(Mandatory)]
        [string]$EditorModulePath,

        [Parameter(Mandatory)]
        [string]$Preset,

        [Parameter(Mandatory)]
        [string]$Configuration,

        [Parameter(Mandatory)]
        [string]$ProjectFingerprint,

        [Parameter(Mandatory)]
        [string]$SdkFingerprint
    )

    if (-not (Test-Path -LiteralPath $BuildStatePath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $ModulePath -PathType Leaf)) {
        return $false
    }
    try {
        $state = Get-Content `
            -LiteralPath $BuildStatePath `
            -Raw |
            ConvertFrom-Json
        if ([int]$state.schemaVersion -ne 2 -or
            [string]$state.preset -cne $Preset -or
            [string]$state.configuration -cne $Configuration -or
            [string]$state.projectFingerprint -cne
                $ProjectFingerprint -or
            [string]$state.sdkFingerprint -cne
                $SdkFingerprint) {
            return $false
        }
        $moduleHash =
            (Get-FileHash `
                -LiteralPath $ModulePath `
                -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($moduleHash -cne
            ([string]$state.moduleSha256).ToLowerInvariant()) {
            return $false
        }

        $editorModulePresent = [bool]$state.editorModulePresent
        if ($Configuration -ceq 'Shipping' -and
            $editorModulePresent) {
            return $false
        }
        if (-not $editorModulePresent) {
            return -not (Test-Path `
                -LiteralPath $EditorModulePath `
                -PathType Leaf)
        }
        if (-not (Test-Path `
                -LiteralPath $EditorModulePath `
                -PathType Leaf)) {
            return $false
        }
        $editorModuleHash =
            (Get-FileHash `
                -LiteralPath $EditorModulePath `
                -Algorithm SHA256).Hash.ToLowerInvariant()
        return $editorModuleHash -ceq
            ([string]$state.editorModuleSha256).ToLowerInvariant()
    }
    catch {
        return $false
    }
}

function Write-HikariBuildState {
    param(
        [Parameter(Mandatory)]
        [string]$BuildStatePath,

        [Parameter(Mandatory)]
        [string]$ModulePath,

        [Parameter(Mandatory)]
        [string]$EditorModulePath,

        [Parameter(Mandatory)]
        [string]$Preset,

        [Parameter(Mandatory)]
        [string]$Configuration,

        [Parameter(Mandatory)]
        [string]$ProjectFingerprint,

        [Parameter(Mandatory)]
        [string]$SdkFingerprint
    )

    $moduleHash =
        (Get-FileHash `
            -LiteralPath $ModulePath `
            -Algorithm SHA256).Hash.ToLowerInvariant()
    $editorModulePresent = Test-Path `
        -LiteralPath $EditorModulePath `
        -PathType Leaf
    $editorModuleHash = if ($editorModulePresent) {
        (Get-FileHash `
            -LiteralPath $EditorModulePath `
            -Algorithm SHA256).Hash.ToLowerInvariant()
    } else {
        ''
    }
    $state = [ordered]@{
        schemaVersion = 2
        preset = $Preset
        configuration = $Configuration
        projectFingerprint = $ProjectFingerprint
        sdkFingerprint = $SdkFingerprint
        moduleSha256 = $moduleHash
        editorModulePresent = $editorModulePresent
        editorModuleSha256 = $editorModuleHash
        generatedAtUtc = [DateTime]::UtcNow.ToString('o')
    }
    $stateDirectory = Split-Path -Parent $BuildStatePath
    $null = New-Item `
        -ItemType Directory `
        -Path $stateDirectory `
        -Force
    $temporaryPath =
        "$BuildStatePath.tmp-$PID"
    try {
        $state |
            ConvertTo-Json -Depth 4 |
            Set-Content `
                -LiteralPath $temporaryPath `
                -Encoding UTF8
        Move-Item `
            -LiteralPath $temporaryPath `
            -Destination $BuildStatePath `
            -Force
    }
    finally {
        if (Test-Path -LiteralPath $temporaryPath -PathType Leaf) {
            Remove-Item -LiteralPath $temporaryPath -Force
        }
    }
}
