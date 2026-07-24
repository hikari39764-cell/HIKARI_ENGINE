param(
    [string]$DxcPath = "",
    [switch]$VerboseErrors
)

$ErrorActionPreference = "Stop"

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

$dxc = Find-Dxc $DxcPath

$entries = @(
    @{ Shader = "HIKARI/Shaders/Render3D_StaticVS.hlsl"; Entry = "main"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_SkinnedVS.hlsl"; Entry = "main"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_StaticPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_StaticFxPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_GeometryAuxPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_FxWaterVS.hlsl"; Entry = "main"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_FxWaterPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_SkyVS.hlsl"; Entry = "main"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_SkyPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_DebugLineVS.hlsl"; Entry = "main"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_DebugLinePS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_ShadowStaticVS.hlsl"; Entry = "main"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_ShadowSkinnedVS.hlsl"; Entry = "main"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_ShadowAlphaPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/DepthPyramid_BuildCS.hlsl"; Entry = "main"; Target = "cs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_ClusterWorklistCS.hlsl"; Entry = "ExpandPageTasksCS"; Target = "cs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_ClusterDispatchFinalizeCS.hlsl"; Entry = "FinalizePageTaskDispatchCS"; Target = "cs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_ClusterMeshletDispatchFinalizeCS.hlsl"; Entry = "FinalizeMeshletDispatchCS"; Target = "cs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_ClusterFineVisibilityCS.hlsl"; Entry = "CullPageTasksCS"; Target = "cs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_GpuTraditionalCommandCompactCS.hlsl"; Entry = "CompactGpuTraditionalCommandStreamCS"; Target = "cs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_MeshletAS.hlsl"; Entry = "main"; Target = "as_6_5" },
    @{ Shader = "HIKARI/Shaders/Render3D_MeshletMS.hlsl"; Entry = "main"; Target = "ms_6_5" },
    @{ Shader = "HIKARI/Shaders/Render3D_MeshletDepthMS.hlsl"; Entry = "main"; Target = "ms_6_5" },
    @{ Shader = "HIKARI/Shaders/Render3D_MeshletDepthPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_MeshletGeometryAuxMS.hlsl"; Entry = "main"; Target = "ms_6_5" },
    @{ Shader = "HIKARI/Shaders/Render3D_MeshletGeometryAuxPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_MeshletShadowPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_GpuDrivenOpaqueFullPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_GpuDrivenOpaqueAlbedoOnlyPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_GpuDrivenOpaqueNoNormalMapPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_GpuDrivenOpaqueNoShadowPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_GpuDrivenOpaqueNoSsaoPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_GpuDrivenOpaqueNoMaterialExtrasPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_ToneMappingPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_FXAA.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_BloomExtractPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_BloomBlurHPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_BloomBlurVPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_AniPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_CinematicGradePS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_TransitionLinearWipePS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_SSAOPS.hlsl"; Entry = "VSMain"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_SSAOPS.hlsl"; Entry = "PSMain"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_SSAOPS.hlsl"; Entry = "PSMainOptimizedHigh"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_SSAOPS.hlsl"; Entry = "PSMainDepthOnly"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_SSAOBlurPS.hlsl"; Entry = "VSMain"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_SSAOBlurPS.hlsl"; Entry = "PSMain"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_SSAOBlurPS.hlsl"; Entry = "PSMainUpsample"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_SSAOBlurPS.hlsl"; Entry = "PSMainDepthOnly"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_SSAOBlurPS.hlsl"; Entry = "PSMainUpsampleDepthOnly"; Target = "ps_6_0" }
)

$failures = New-Object System.Collections.Generic.List[object]
foreach ($entry in $entries) {
    $output = & $dxc -E $entry.Entry -T $entry.Target -HV 2021 -I HIKARI/Shaders -Fo NUL $entry.Shader 2>&1
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
