param(
    [string]$DxcPath = "",
    [string]$Group = "",
    [string]$OutputRoot = "",
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
$resolvedOutputRoot = $null
if (-not [string]::IsNullOrWhiteSpace($OutputRoot)) {
    $resolvedOutputRoot = [IO.Path]::GetFullPath($OutputRoot)
    [IO.Directory]::CreateDirectory($resolvedOutputRoot) | Out-Null
}

$entries = @(
    @{ Shader = "HIKARI/Shaders/Render3D_StaticVS.hlsl"; Entry = "main"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_SkinnedVS.hlsl"; Entry = "main"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_StaticPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_WaterVisibilityPS.hlsl"; Entry = "main"; Target = "ps_6_0"; Group = "WaterVisibility"; WarningsAsErrors = $true },
    @{ Shader = "HIKARI/Shaders/WaterSpectrum/HIKARI_WaterSpectrumEvolutionCS.hlsl"; Entry = "main"; Target = "cs_6_0"; Group = "WaterSpectrum"; WarningsAsErrors = $true },
    @{ Shader = "HIKARI/Shaders/WaterSpectrum/HIKARI_WaterSpectrumStockhamCS.hlsl"; Entry = "main"; Target = "cs_6_0"; Group = "WaterSpectrum"; WarningsAsErrors = $true },
    @{ Shader = "HIKARI/Shaders/WaterSpectrum/HIKARI_WaterSpectrumResolveCS.hlsl"; Entry = "main"; Target = "cs_6_0"; Group = "WaterSpectrum"; WarningsAsErrors = $true },
    @{ Shader = "HIKARI/Shaders/Render3D_StaticFxPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_GeometryAuxPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_GeometryAuxFeaturePS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_GeometryOutlineVS.hlsl"; Entry = "main"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_SkinnedGeometryOutlineVS.hlsl"; Entry = "main"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_GeometryOutlinePS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_VisibilityOverlayPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_SkyVS.hlsl"; Entry = "main"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_SkyPS.hlsl"; Entry = "main"; Target = "ps_6_0"; Group = "Atmosphere"; WarningsAsErrors = $true },
    @{ Shader = "HIKARI/Shaders/Render3D_PanoramaSkyLayerPS.hlsl"; Entry = "main"; Target = "ps_6_0"; Group = "Atmosphere"; WarningsAsErrors = $true },
    # StylizedClouds is a strict, independently runnable shader group.  Noise
    # generation is compute; trace, temporal reprojection and composite remain
    # graphics entries so the MRT/depth contract cannot be hidden by a compute
    # substitute.
    @{ Shader = "HIKARI/Shaders/Render3D_StylizedCloudNoise.hlsl"; Entry = "GenerateStylizedCloudShapeCS"; Target = "cs_6_0"; Group = "StylizedClouds"; WarningsAsErrors = $true },
    @{ Shader = "HIKARI/Shaders/Render3D_StylizedCloudNoise.hlsl"; Entry = "GenerateStylizedCloudDetailCS"; Target = "cs_6_0"; Group = "StylizedClouds"; WarningsAsErrors = $true },
    @{ Shader = "HIKARI/Shaders/Render3D_StylizedCloudNoise.hlsl"; Entry = "GenerateStylizedCloudWeatherCS"; Target = "cs_6_0"; Group = "StylizedClouds"; WarningsAsErrors = $true },
    @{ Shader = "HIKARI/Shaders/Render3D_StylizedCloudPass.hlsl"; Entry = "FullscreenVS"; Target = "vs_6_0"; Group = "StylizedClouds"; WarningsAsErrors = $true },
    @{ Shader = "HIKARI/Shaders/Render3D_StylizedCloudPass.hlsl"; Entry = "RenderCloudPS"; Target = "ps_6_0"; Group = "StylizedClouds"; WarningsAsErrors = $true },
    @{ Shader = "HIKARI/Shaders/Render3D_StylizedCloudPass.hlsl"; Entry = "ReprojectCloudPS"; Target = "ps_6_0"; Group = "StylizedClouds"; WarningsAsErrors = $true },
    @{ Shader = "HIKARI/Shaders/Render3D_StylizedCloudPass.hlsl"; Entry = "CompositeCloudPS"; Target = "ps_6_0"; Group = "StylizedClouds"; WarningsAsErrors = $true },
    @{ Shader = "HIKARI/Shaders/Render3D_VolumetricLighting.hlsl"; Entry = "InjectAndIntegrateCS"; Target = "cs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_VolumetricLighting.hlsl"; Entry = "CompositeCS"; Target = "cs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_ParticleSimulation.hlsl"; Entry = "ResetCS"; Target = "cs_6_0"; Group = "NativeVfx" },
    @{ Shader = "HIKARI/Shaders/Render3D_ParticleSimulation.hlsl"; Entry = "UpdateCS"; Target = "cs_6_0"; Group = "NativeVfx" },
    @{ Shader = "HIKARI/Shaders/Render3D_ParticleSimulation.hlsl"; Entry = "SpawnCS"; Target = "cs_6_0"; Group = "NativeVfx" },
    @{ Shader = "HIKARI/Shaders/Render3D_ParticleGpuDrivenCompute.hlsl"; Entry = "ResetDrawCS"; Target = "cs_6_0"; Group = "NativeVfx" },
    @{ Shader = "HIKARI/Shaders/Render3D_ParticleGpuDrivenCompute.hlsl"; Entry = "CompactDrawCS"; Target = "cs_6_0"; Group = "NativeVfx" },
    @{ Shader = "HIKARI/Shaders/Render3D_ParticleGpuDrivenCompute.hlsl"; Entry = "BuildDrawArgsCS"; Target = "cs_6_0"; Group = "NativeVfx" },
    @{ Shader = "HIKARI/Shaders/Render3D_ParticleGpuDrivenDraw.hlsl"; Entry = "ParticleSpriteMS"; Target = "ms_6_5"; Group = "NativeVfx" },
    @{ Shader = "HIKARI/Shaders/Render3D_ParticleGpuDrivenDraw.hlsl"; Entry = "ParticleStripMS"; Target = "ms_6_5"; Group = "NativeVfx" },
    @{ Shader = "HIKARI/Shaders/Render3D_ParticleGpuDrivenDraw.hlsl"; Entry = "ParticlePS"; Target = "ps_6_0"; Group = "NativeVfx" },
    @{ Shader = "HIKARI/Shaders/Render3D_DebugStrokeVS.hlsl"; Entry = "main"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_DebugStrokeGS.hlsl"; Entry = "main"; Target = "gs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_DebugStrokePS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_DebugSolidVS.hlsl"; Entry = "main"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_DebugSolidPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_ShadowStaticVS.hlsl"; Entry = "main"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_ShadowSkinnedVS.hlsl"; Entry = "main"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_ShadowAlphaPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/DepthPyramid_BuildCS.hlsl"; Entry = "main"; Target = "cs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_ClusterWorklistCS.hlsl"; Entry = "ExpandPageTasksCS"; Target = "cs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_ClusterDispatchFinalizeCS.hlsl"; Entry = "FinalizePageTaskDispatchCS"; Target = "cs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_ClusterMeshletDispatchFinalizeCS.hlsl"; Entry = "FinalizeMeshletDispatchCS"; Target = "cs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_ClusterFineVisibilityCS.hlsl"; Entry = "CullPageTasksCS"; Target = "cs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_ClusterTransparentSortCS.hlsl"; Entry = "SortTransparentRangesCS"; Target = "cs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_GpuTraditionalCommandCompactCS.hlsl"; Entry = "CompactGpuTraditionalCommandStreamCS"; Target = "cs_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_MeshletAS.hlsl"; Entry = "main"; Target = "as_6_5" },
    @{ Shader = "HIKARI/Shaders/Render3D_MeshletMS.hlsl"; Entry = "main"; Target = "ms_6_5" },
    @{ Shader = "HIKARI/Shaders/Render3D_MeshletDepthMS.hlsl"; Entry = "main"; Target = "ms_6_5" },
    @{ Shader = "HIKARI/Shaders/Render3D_MeshletWaterVisibilityMS.hlsl"; Entry = "main"; Target = "ms_6_5"; Group = "WaterVisibility"; WarningsAsErrors = $true },
    @{ Shader = "HIKARI/Shaders/Render3D_MeshletWaterVisibilityPS.hlsl"; Entry = "main"; Target = "ps_6_0"; Group = "WaterVisibility"; WarningsAsErrors = $true },
    @{ Shader = "HIKARI/Shaders/Render3D_MeshletShadowMS.hlsl"; Entry = "main"; Target = "ms_6_5" },
    @{ Shader = "HIKARI/Shaders/Render3D_MeshletDepthPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_MeshletGeometryAuxMS.hlsl"; Entry = "main"; Target = "ms_6_5" },
    @{ Shader = "HIKARI/Shaders/Render3D_MeshletGeometryAuxPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_MeshletGeometryAuxFeaturePS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_MeshletGeometryOutlineMS.hlsl"; Entry = "main"; Target = "ms_6_5" },
    @{ Shader = "HIKARI/Shaders/Render3D_MeshletShadowPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_GpuDrivenOpaqueFullPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_GpuDrivenOpaqueAlbedoOnlyPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_GpuDrivenOpaqueNoNormalMapPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_GpuDrivenOpaqueNoShadowPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_GpuDrivenOpaqueNoSsaoPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Render3D_GpuDrivenOpaqueNoMaterialExtrasPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_ToneMappingPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Color_AutomaticExposureCS.hlsl"; Entry = "main"; Target = "cs_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_FXAA.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_BloomExtractPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_BloomBlurHPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_BloomBlurVPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_SunGlarePS.hlsl"; Entry = "main"; Target = "ps_6_0"; Group = "Atmosphere"; WarningsAsErrors = $true },
    @{ Shader = "HIKARI/Shaders/Render3D_AtmosphereTransmittanceCS.hlsl"; Entry = "main"; Target = "cs_6_0"; Group = "Atmosphere"; WarningsAsErrors = $true },
    @{ Shader = "HIKARI/Shaders/Render3D_AtmosphereMultiScatteringCS.hlsl"; Entry = "main"; Target = "cs_6_0"; Group = "Atmosphere"; WarningsAsErrors = $true },
    @{ Shader = "HIKARI/Shaders/Render3D_AtmosphereSkyViewCS.hlsl"; Entry = "main"; Target = "cs_6_0"; Group = "Atmosphere"; WarningsAsErrors = $true },
    @{ Shader = "HIKARI/Shaders/Post_AniPS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_CinematicGradePS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_TransitionLinearWipePS.hlsl"; Entry = "main"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_TransitionBurnDissolvePS.hlsl"; Entry = "main"; Target = "ps_6_0"; Group = "Transitions"; WarningsAsErrors = $true },
    @{ Shader = "HIKARI/Shaders/Post_SSAOPS.hlsl"; Entry = "VSMain"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_SSAOPS.hlsl"; Entry = "PSMain"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_SSAOPS.hlsl"; Entry = "PSMainOptimizedHigh"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_SSAOPS.hlsl"; Entry = "PSMainDepthOnly"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_SSAOBlurPS.hlsl"; Entry = "VSMain"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_SSAOBlurPS.hlsl"; Entry = "PSMain"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_SSAOBlurPS.hlsl"; Entry = "PSMainUpsample"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_SSAOBlurPS.hlsl"; Entry = "PSMainDepthOnly"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_SSAOBlurPS.hlsl"; Entry = "PSMainUpsampleDepthOnly"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_ScreenSpaceReflections.hlsl"; Entry = "VSMain"; Target = "vs_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_ScreenSpaceReflections.hlsl"; Entry = "PSTrace"; Target = "ps_6_0" },
    @{ Shader = "HIKARI/Shaders/Post_ScreenSpaceReflections.hlsl"; Entry = "PSResolve"; Target = "ps_6_0" }
    @{ Shader = "HIKARI/Shaders/Post_StylizedScreenFeatures.hlsl"; Entry = "VSMain"; Target = "vs_6_0" }
    @{ Shader = "HIKARI/Shaders/Post_StylizedScreenFeatures.hlsl"; Entry = "PSMain"; Target = "ps_6_0" }
    @{ Shader = "HIKARI/Shaders/Render3D_WeaponTrail.hlsl"; Entry = "VSMain"; Target = "vs_6_0"; Group = "CombatVfx"; WarningsAsErrors = $true }
    @{ Shader = "HIKARI/Shaders/Render3D_WeaponTrail.hlsl"; Entry = "PSMain"; Target = "ps_6_0"; Group = "CombatVfx"; WarningsAsErrors = $true }
    @{ Shader = "HIKARI/Shaders/Render3D_CombatAfterimage.hlsl"; Entry = "VSMain"; Target = "vs_6_0"; Group = "CombatVfx"; WarningsAsErrors = $true }
    @{ Shader = "HIKARI/Shaders/Render3D_CombatAfterimage.hlsl"; Entry = "VSStaticMain"; Target = "vs_6_0"; Group = "CombatVfx"; WarningsAsErrors = $true }
    @{ Shader = "HIKARI/Shaders/Render3D_CombatAfterimage.hlsl"; Entry = "PSMain"; Target = "ps_6_0"; Group = "CombatVfx"; WarningsAsErrors = $true }
    @{ Shader = "HIKARI/Shaders/Render3D_GroundTelegraph.hlsl"; Entry = "VSMain"; Target = "vs_6_0"; Group = "CombatVfx"; WarningsAsErrors = $true }
    @{ Shader = "HIKARI/Shaders/Render3D_GroundTelegraph.hlsl"; Entry = "PSMain"; Target = "ps_6_0"; Group = "CombatVfx"; WarningsAsErrors = $true }
    @{ Shader = "HIKARI/Shaders/Post_ActionScreenImpulse.hlsl"; Entry = "main"; Target = "ps_6_0"; Group = "CombatVfx"; WarningsAsErrors = $true }
    @{ Shader = "HIKARI/Shaders/Post_CombatScreenDistortionComposite.hlsl"; Entry = "main"; Target = "ps_6_0"; Group = "CombatVfx"; WarningsAsErrors = $true }
    @{ Shader = "HIKARI/Shaders/Render2D_GameUiEffect.hlsl"; Entry = "VSMain"; Target = "vs_6_0"; Group = "GameUi"; WarningsAsErrors = $true }
    @{ Shader = "HIKARI/Shaders/Render2D_GameUiEffect.hlsl"; Entry = "PSMain"; Target = "ps_6_0"; Group = "GameUi"; WarningsAsErrors = $true }
    @{ Shader = "HIKARI/Shaders/Post_GameUiBackdrop.hlsl"; Entry = "main"; Target = "ps_6_0"; Group = "GameUi"; WarningsAsErrors = $true }
)

if (-not [string]::IsNullOrWhiteSpace($Group)) {
    $entries = @($entries | Where-Object {
        $_.ContainsKey('Group') -and $_.Group -eq $Group
    })
    if ($entries.Count -eq 0) {
        throw "No shader compile entries are registered for group '$Group'."
    }
}

$failures = New-Object System.Collections.Generic.List[object]
foreach ($entry in $entries) {
    $arguments = @(
        '-E', $entry.Entry,
        '-T', $entry.Target,
        '-HV', '2021',
        '-O3',
        '-I', 'HIKARI/Shaders')
    if ($entry.ContainsKey('WarningsAsErrors') -and $entry.WarningsAsErrors) {
        $arguments += '-WX'
    }
    $outputPath = 'NUL'
    if ($resolvedOutputRoot) {
        $normalizedShader = $entry.Shader.Replace('\', '/')
        if (-not $normalizedShader.StartsWith(
                'HIKARI/Shaders/',
                [StringComparison]::Ordinal)) {
            throw "Precompiled engine shader is outside HIKARI/Shaders: $normalizedShader"
        }
        $engineRelative = $normalizedShader.Substring('HIKARI/'.Length)
        $bytecodeDirectory = Join-Path `
            $resolvedOutputRoot `
            $engineRelative.Replace('/', '\')
        [IO.Directory]::CreateDirectory($bytecodeDirectory) | Out-Null
        $safeEntry = $entry.Entry -replace '[^A-Za-z0-9_.-]', '_'
        $safeTarget = $entry.Target -replace '[^A-Za-z0-9_.-]', '_'
        $outputPath = Join-Path `
            $bytecodeDirectory `
            ("{0}_{1}.dxil" -f $safeEntry, $safeTarget)
    }
    $arguments += @('-Fo', $outputPath, $entry.Shader)
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

if ($resolvedOutputRoot) {
    Write-Host ("Shader bytecode bake passed: {0} entries -> {1}" -f `
        $entries.Count, $resolvedOutputRoot)
} else {
    Write-Host ("Shader compile check passed: {0} entries with {1}" -f $entries.Count, $dxc)
}
