#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshPipelineStore.h"

#include <array>
#include <cstddef>

#include <d3dx12.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshInputLayouts.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshRootParameters.h"

namespace HIKARI::MESHRENDERER {

    using Microsoft::WRL::ComPtr;

    bool InitializeMeshPipelines(ID3D12Device* device, MeshPipelineStore& store) {
        if (!GFX::SupportsShaderModel6(device)) {
            DEBUGLOG::PushRenderError("[MeshRenderer][ERROR] Shader Model 6.0 is not supported by this device.");
            return false;
        }

        if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_StaticVS.hlsl", "main", GFX::ShaderStage::Vertex, store.vsBlob.GetAddressOf())) {
            return false;
        }
        if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_SkinnedVS.hlsl", "main", GFX::ShaderStage::Vertex, store.skinnedVsBlob.GetAddressOf())) {
            return false;
        }
        if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_StaticPS.hlsl", "main", GFX::ShaderStage::Pixel, store.psBlob.GetAddressOf())) {
            return false;
        }
        const bool waterDepthShaderReady =
            GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_WaterVisibilityPS.hlsl",
                "main",
                GFX::ShaderStage::Pixel,
                store.waterDepthPsBlob.GetAddressOf());
        if (!waterDepthShaderReady) {
            DEBUGLOG::PushRenderError(
                "[MeshRenderer][WARN] Water visibility depth shader is "
                "unavailable; volumetric lighting will use opaque SceneDepth.");
        }
        if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_GeometryAuxPS.hlsl", "main", GFX::ShaderStage::Pixel, store.geometryPsBlob.GetAddressOf())) {
            return false;
        }
        const bool geometryFeatureMaskShaderReady =
            GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_GeometryAuxFeaturePS.hlsl",
                "main",
                GFX::ShaderStage::Pixel,
                store.geometryFeatureMaskPsBlob.GetAddressOf());
        const bool geometryOutlineShadersReady =
            GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_GeometryOutlineVS.hlsl",
                "main",
                GFX::ShaderStage::Vertex,
                store.geometryOutlineVsBlob.GetAddressOf()) &&
            GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_SkinnedGeometryOutlineVS.hlsl",
                "main",
                GFX::ShaderStage::Vertex,
                store.geometryOutlineSkinnedVsBlob.GetAddressOf()) &&
            GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_GeometryOutlinePS.hlsl",
                "main",
                GFX::ShaderStage::Pixel,
                store.geometryOutlinePsBlob.GetAddressOf());
        const bool visibilityOverlayShaderReady =
            GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_VisibilityOverlayPS.hlsl",
                "main",
                GFX::ShaderStage::Pixel,
                store.visibilityOverlayPsBlob.GetAddressOf());
        if (!geometryFeatureMaskShaderReady ||
            !geometryOutlineShadersReady ||
            !visibilityOverlayShaderReady) {
            DEBUGLOG::PushRenderError(
                "[MeshRenderer][WARN] One or more optional stylized feature "
                "shaders failed to compile. Core Forward rendering remains "
                "available and the affected feature will be skipped.");
        }

        D3D12_DESCRIPTOR_RANGE shadowTextureRange{};
        shadowTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        shadowTextureRange.NumDescriptors = 1;
        shadowTextureRange.BaseShaderRegister = 2;
        shadowTextureRange.RegisterSpace = 0;
        shadowTextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE skyCubeTextureRange{};
        skyCubeTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        skyCubeTextureRange.NumDescriptors = 1;
        skyCubeTextureRange.BaseShaderRegister = 6;
        skyCubeTextureRange.RegisterSpace = 0;
        skyCubeTextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE sceneDepthTextureRange{};
        sceneDepthTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        sceneDepthTextureRange.NumDescriptors = 1;
        sceneDepthTextureRange.BaseShaderRegister = 7;
        sceneDepthTextureRange.RegisterSpace = 0;
        sceneDepthTextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE sceneColorTextureRange{};
        sceneColorTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        sceneColorTextureRange.NumDescriptors = 1;
        sceneColorTextureRange.BaseShaderRegister = 8;
        sceneColorTextureRange.RegisterSpace = 0;
        sceneColorTextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE iblIrradianceRange{};
        iblIrradianceRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        iblIrradianceRange.NumDescriptors = 1;
        iblIrradianceRange.BaseShaderRegister = 9;
        iblIrradianceRange.RegisterSpace = 0;
        iblIrradianceRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE iblPrefilteredRange{};
        iblPrefilteredRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        iblPrefilteredRange.NumDescriptors = 1;
        iblPrefilteredRange.BaseShaderRegister = 10;
        iblPrefilteredRange.RegisterSpace = 0;
        iblPrefilteredRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE iblBrdfLutRange{};
        iblBrdfLutRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        iblBrdfLutRange.NumDescriptors = 1;
        iblBrdfLutRange.BaseShaderRegister = 11;
        iblBrdfLutRange.RegisterSpace = 0;
        iblBrdfLutRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE reflectionProbePrefilteredRange{};
        reflectionProbePrefilteredRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        reflectionProbePrefilteredRange.NumDescriptors = 1;
        reflectionProbePrefilteredRange.BaseShaderRegister = 12;
        reflectionProbePrefilteredRange.RegisterSpace = 0;
        reflectionProbePrefilteredRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE
            reflectionProbePrefilteredSecondaryRange{};
        reflectionProbePrefilteredSecondaryRange.RangeType =
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        reflectionProbePrefilteredSecondaryRange.NumDescriptors = 1;
        reflectionProbePrefilteredSecondaryRange.BaseShaderRegister = 14;
        reflectionProbePrefilteredSecondaryRange.RegisterSpace = 0;
        reflectionProbePrefilteredSecondaryRange.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE ssaoRange{};
        ssaoRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ssaoRange.NumDescriptors = 1;
        ssaoRange.BaseShaderRegister = 13;
        ssaoRange.RegisterSpace = 0;
        ssaoRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        // Light probe volume: SH 係数 Texture3D ×9 (t0-t8, space2)。
        // heap 側は SystemSrv::LightProbeShVolume0..8 の連続スロットに対応する。
        D3D12_DESCRIPTOR_RANGE lightProbeShRange{};
        lightProbeShRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        lightProbeShRange.NumDescriptors = GFX::DESCRIPTOR::kLightProbeShVolumeTextureCount;
        lightProbeShRange.BaseShaderRegister = 0;
        lightProbeShRange.RegisterSpace = 2;
        lightProbeShRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE lightProbeShSecondaryRange{};
        lightProbeShSecondaryRange.RangeType =
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        lightProbeShSecondaryRange.NumDescriptors =
            GFX::DESCRIPTOR::kLightProbeShVolumeTextureCount;
        lightProbeShSecondaryRange.BaseShaderRegister = 0;
        lightProbeShSecondaryRange.RegisterSpace = 4;
        lightProbeShSecondaryRange.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE objectDataRange{};
        objectDataRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        objectDataRange.NumDescriptors = 1;
        objectDataRange.BaseShaderRegister = 15;
        objectDataRange.RegisterSpace = 0;
        objectDataRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE materialDataRange{};
        materialDataRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        materialDataRange.NumDescriptors = 1;
        materialDataRange.BaseShaderRegister = 16;
        materialDataRange.RegisterSpace = 0;
        materialDataRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE surfaceGpuSceneRange{};
        surfaceGpuSceneRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        surfaceGpuSceneRange.NumDescriptors = 1;
        surfaceGpuSceneRange.BaseShaderRegister = 17;
        surfaceGpuSceneRange.RegisterSpace = 0;
        surfaceGpuSceneRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE materialTexturePoolRange{};
        materialTexturePoolRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        materialTexturePoolRange.NumDescriptors = GFX::DESCRIPTOR::kUserSrvCount;
        materialTexturePoolRange.BaseShaderRegister = 20;
        materialTexturePoolRange.RegisterSpace = 0;
        materialTexturePoolRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE clusterGeometryPoolRange{};
        clusterGeometryPoolRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        clusterGeometryPoolRange.NumDescriptors = GFX::DESCRIPTOR::kSystemSrvDynamicCount;
        clusterGeometryPoolRange.BaseShaderRegister = 0;
        clusterGeometryPoolRange.RegisterSpace = 1;
        clusterGeometryPoolRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE screenSpaceDepthPyramidRange{};
        screenSpaceDepthPyramidRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        screenSpaceDepthPyramidRange.NumDescriptors = 1;
        screenSpaceDepthPyramidRange.BaseShaderRegister = 2;
        screenSpaceDepthPyramidRange.RegisterSpace = 7;
        screenSpaceDepthPyramidRange.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE screenSpacePackedSurfaceRange{};
        screenSpacePackedSurfaceRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        screenSpacePackedSurfaceRange.NumDescriptors = 1;
        screenSpacePackedSurfaceRange.BaseShaderRegister = 3;
        screenSpacePackedSurfaceRange.RegisterSpace = 7;
        screenSpacePackedSurfaceRange.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE oceanSpectrumRange{};
        oceanSpectrumRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        oceanSpectrumRange.NumDescriptors = 6u;
        oceanSpectrumRange.BaseShaderRegister = 5u;
        oceanSpectrumRange.RegisterSpace = 7u;
        oceanSpectrumRange.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        // 既存 slot を動かさず、GPU-driven 用のリソースプールを末尾へ追加する。
        D3D12_ROOT_PARAMETER params[ROOT_PARAM::Count]{};
        params[ROOT_PARAM::Camera].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[ROOT_PARAM::Camera].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::Camera].Descriptor.ShaderRegister = 0;
        params[ROOT_PARAM::Camera].Descriptor.RegisterSpace = 0;

        params[ROOT_PARAM::CullingCamera].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[ROOT_PARAM::CullingCamera].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::CullingCamera].Descriptor.ShaderRegister = 9;
        params[ROOT_PARAM::CullingCamera].Descriptor.RegisterSpace = 0;

        params[ROOT_PARAM::Object].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[ROOT_PARAM::Object].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::Object].Descriptor.ShaderRegister = 1;
        params[ROOT_PARAM::Object].Descriptor.RegisterSpace = 0;

        params[ROOT_PARAM::Light].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[ROOT_PARAM::Light].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::Light].Descriptor.ShaderRegister = 2;
        params[ROOT_PARAM::Light].Descriptor.RegisterSpace = 0;

        params[ROOT_PARAM::ShadowMap].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::ShadowMap].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::ShadowMap].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::ShadowMap].DescriptorTable.pDescriptorRanges = &shadowTextureRange;

        params[ROOT_PARAM::ShadowCB].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[ROOT_PARAM::ShadowCB].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::ShadowCB].Descriptor.ShaderRegister = 4;
        params[ROOT_PARAM::ShadowCB].Descriptor.RegisterSpace = 0;

        params[ROOT_PARAM::SkyEnvironment].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[ROOT_PARAM::SkyEnvironment].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::SkyEnvironment].Descriptor.ShaderRegister = 5;
        params[ROOT_PARAM::SkyEnvironment].Descriptor.RegisterSpace = 0;

        params[ROOT_PARAM::SkyCube].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::SkyCube].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::SkyCube].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::SkyCube].DescriptorTable.pDescriptorRanges = &skyCubeTextureRange;

        params[ROOT_PARAM::SceneDepth].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::SceneDepth].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::SceneDepth].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::SceneDepth].DescriptorTable.pDescriptorRanges = &sceneDepthTextureRange;

        params[ROOT_PARAM::SceneColor].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::SceneColor].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::SceneColor].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::SceneColor].DescriptorTable.pDescriptorRanges = &sceneColorTextureRange;

        params[ROOT_PARAM::IblIrradiance].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::IblIrradiance].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::IblIrradiance].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::IblIrradiance].DescriptorTable.pDescriptorRanges = &iblIrradianceRange;

        params[ROOT_PARAM::IblPrefiltered].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::IblPrefiltered].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::IblPrefiltered].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::IblPrefiltered].DescriptorTable.pDescriptorRanges = &iblPrefilteredRange;

        params[ROOT_PARAM::IblBrdfLut].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::IblBrdfLut].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::IblBrdfLut].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::IblBrdfLut].DescriptorTable.pDescriptorRanges = &iblBrdfLutRange;

        params[ROOT_PARAM::ReflectionProbePrefiltered].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::ReflectionProbePrefiltered].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::ReflectionProbePrefiltered].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::ReflectionProbePrefiltered].DescriptorTable.pDescriptorRanges = &reflectionProbePrefilteredRange;

        params[ROOT_PARAM::Ssao].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::Ssao].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::Ssao].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::Ssao].DescriptorTable.pDescriptorRanges = &ssaoRange;

        params[ROOT_PARAM::LightProbeSh].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::LightProbeSh].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::LightProbeSh].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::LightProbeSh].DescriptorTable.pDescriptorRanges = &lightProbeShRange;

        params[
            ROOT_PARAM::ReflectionProbePrefilteredSecondary].
            ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[
            ROOT_PARAM::ReflectionProbePrefilteredSecondary].
            ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[
            ROOT_PARAM::ReflectionProbePrefilteredSecondary].
            DescriptorTable.NumDescriptorRanges = 1;
        params[
            ROOT_PARAM::ReflectionProbePrefilteredSecondary].
            DescriptorTable.pDescriptorRanges =
                &reflectionProbePrefilteredSecondaryRange;

        params[ROOT_PARAM::LightProbeShSecondary].
            ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::LightProbeShSecondary].
            ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::LightProbeShSecondary].
            DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::LightProbeShSecondary].
            DescriptorTable.pDescriptorRanges =
                &lightProbeShSecondaryRange;

        params[ROOT_PARAM::ObjectData].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::ObjectData].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::ObjectData].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::ObjectData].DescriptorTable.pDescriptorRanges = &objectDataRange;

        params[ROOT_PARAM::ObjectIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[ROOT_PARAM::ObjectIndex].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::ObjectIndex].Constants.ShaderRegister = 6;
        params[ROOT_PARAM::ObjectIndex].Constants.RegisterSpace = 0;
        params[ROOT_PARAM::ObjectIndex].Constants.Num32BitValues = 1;

        params[ROOT_PARAM::MaterialData].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::MaterialData].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::MaterialData].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::MaterialData].DescriptorTable.pDescriptorRanges = &materialDataRange;

        params[ROOT_PARAM::MaterialIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[ROOT_PARAM::MaterialIndex].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::MaterialIndex].Constants.ShaderRegister = 7;
        params[ROOT_PARAM::MaterialIndex].Constants.RegisterSpace = 0;
        params[ROOT_PARAM::MaterialIndex].Constants.Num32BitValues = 1;

        params[ROOT_PARAM::TexturePool].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::TexturePool].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::TexturePool].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::TexturePool].DescriptorTable.pDescriptorRanges = &materialTexturePoolRange;

        params[ROOT_PARAM::JointPalette].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[ROOT_PARAM::JointPalette].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::JointPalette].Descriptor.ShaderRegister = 0;
        params[ROOT_PARAM::JointPalette].Descriptor.RegisterSpace = 3;

        params[ROOT_PARAM::SurfaceGpuScene].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::SurfaceGpuScene].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::SurfaceGpuScene].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::SurfaceGpuScene].DescriptorTable.pDescriptorRanges = &surfaceGpuSceneRange;

        params[ROOT_PARAM::SurfaceGpuSceneControl].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[ROOT_PARAM::SurfaceGpuSceneControl].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::SurfaceGpuSceneControl].Constants.ShaderRegister = 8;
        params[ROOT_PARAM::SurfaceGpuSceneControl].Constants.RegisterSpace = 0;
        params[ROOT_PARAM::SurfaceGpuSceneControl].Constants.Num32BitValues = 4;

        params[ROOT_PARAM::ClusterGeometryPool].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::ClusterGeometryPool].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::ClusterGeometryPool].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::ClusterGeometryPool].DescriptorTable.pDescriptorRanges = &clusterGeometryPoolRange;

        params[ROOT_PARAM::MeshletVisibleRanges].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[ROOT_PARAM::MeshletVisibleRanges].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::MeshletVisibleRanges].Descriptor.ShaderRegister = 18;
        params[ROOT_PARAM::MeshletVisibleRanges].Descriptor.RegisterSpace = 0;

        params[ROOT_PARAM::MeshletVisibleClusterList].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[ROOT_PARAM::MeshletVisibleClusterList].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::MeshletVisibleClusterList].Descriptor.ShaderRegister = 19;
        params[ROOT_PARAM::MeshletVisibleClusterList].Descriptor.RegisterSpace = 0;

        params[ROOT_PARAM::LocalLights].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[ROOT_PARAM::LocalLights].ShaderVisibility =
            D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::LocalLights].Descriptor.ShaderRegister = 0;
        params[ROOT_PARAM::LocalLights].Descriptor.RegisterSpace = 5;

        params[ROOT_PARAM::LocalLightClusterHeaders].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[ROOT_PARAM::LocalLightClusterHeaders].ShaderVisibility =
            D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::LocalLightClusterHeaders].
            Descriptor.ShaderRegister = 1;
        params[ROOT_PARAM::LocalLightClusterHeaders].
            Descriptor.RegisterSpace = 5;

        params[ROOT_PARAM::LocalLightClusterIndices].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[ROOT_PARAM::LocalLightClusterIndices].ShaderVisibility =
            D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::LocalLightClusterIndices].
            Descriptor.ShaderRegister = 2;
        params[ROOT_PARAM::LocalLightClusterIndices].
            Descriptor.RegisterSpace = 5;

        params[ROOT_PARAM::MaterialProgramParameters].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[ROOT_PARAM::MaterialProgramParameters].ShaderVisibility =
            D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::MaterialProgramParameters].Descriptor.ShaderRegister = 0;
        params[ROOT_PARAM::MaterialProgramParameters].Descriptor.RegisterSpace = 6;

        params[ROOT_PARAM::MaterialProgramResources].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[ROOT_PARAM::MaterialProgramResources].ShaderVisibility =
            D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::MaterialProgramResources].Descriptor.ShaderRegister = 1;
        params[ROOT_PARAM::MaterialProgramResources].Descriptor.RegisterSpace = 6;

        params[ROOT_PARAM::SurfaceFeatureData].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[ROOT_PARAM::SurfaceFeatureData].ShaderVisibility =
            D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::SurfaceFeatureData].Descriptor.ShaderRegister = 0;
        params[ROOT_PARAM::SurfaceFeatureData].Descriptor.RegisterSpace = 7;

        params[ROOT_PARAM::SandSurfaceData].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[ROOT_PARAM::SandSurfaceData].ShaderVisibility =
            D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::SandSurfaceData].Descriptor.ShaderRegister = 4;
        params[ROOT_PARAM::SandSurfaceData].Descriptor.RegisterSpace = 7;

        params[ROOT_PARAM::SurfaceFeatureHeights].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[ROOT_PARAM::SurfaceFeatureHeights].ShaderVisibility =
            D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::SurfaceFeatureHeights].Descriptor.ShaderRegister = 1;
        params[ROOT_PARAM::SurfaceFeatureHeights].Descriptor.RegisterSpace = 7;

        params[ROOT_PARAM::ScreenSpaceDepthPyramid].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::ScreenSpaceDepthPyramid].ShaderVisibility =
            D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::ScreenSpaceDepthPyramid].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::ScreenSpaceDepthPyramid].DescriptorTable.pDescriptorRanges =
            &screenSpaceDepthPyramidRange;

        params[ROOT_PARAM::ScreenSpacePackedSurface].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::ScreenSpacePackedSurface].ShaderVisibility =
            D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::ScreenSpacePackedSurface].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::ScreenSpacePackedSurface].DescriptorTable.pDescriptorRanges =
            &screenSpacePackedSurfaceRange;

        params[ROOT_PARAM::ScreenSpaceTraceControl].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[ROOT_PARAM::ScreenSpaceTraceControl].ShaderVisibility =
            D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::ScreenSpaceTraceControl].Constants.ShaderRegister = 10;
        params[ROOT_PARAM::ScreenSpaceTraceControl].Constants.RegisterSpace = 0;
        params[ROOT_PARAM::ScreenSpaceTraceControl].Constants.Num32BitValues = 2;

        params[ROOT_PARAM::OceanSpectrumData].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::OceanSpectrumData].ShaderVisibility =
            D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::OceanSpectrumData].DescriptorTable.
            NumDescriptorRanges = 1u;
        params[ROOT_PARAM::OceanSpectrumData].DescriptorTable.
            pDescriptorRanges = &oceanSpectrumRange;

        params[ROOT_PARAM::OceanSpectrumControl].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[ROOT_PARAM::OceanSpectrumControl].ShaderVisibility =
            D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::OceanSpectrumControl].Constants.ShaderRegister = 11u;
        params[ROOT_PARAM::OceanSpectrumControl].Constants.RegisterSpace = 0u;
        params[ROOT_PARAM::OceanSpectrumControl].Constants.Num32BitValues = 2u;

        D3D12_STATIC_SAMPLER_DESC linearWrapSampler{};
        linearWrapSampler.Filter = D3D12_FILTER_ANISOTROPIC;
        linearWrapSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        linearWrapSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        linearWrapSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        linearWrapSampler.ShaderRegister = 0;
        linearWrapSampler.RegisterSpace = 0;
        linearWrapSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        linearWrapSampler.MaxAnisotropy = 8;
        linearWrapSampler.MaxLOD = D3D12_FLOAT32_MAX;

        D3D12_STATIC_SAMPLER_DESC shadowSampler{};
        shadowSampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        shadowSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        shadowSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        shadowSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        shadowSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        shadowSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        shadowSampler.ShaderRegister = 1;
        shadowSampler.RegisterSpace = 0;
        shadowSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        shadowSampler.MaxLOD = D3D12_FLOAT32_MAX;

        D3D12_STATIC_SAMPLER_DESC linearClampSampler{};
        linearClampSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        linearClampSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        linearClampSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        linearClampSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        linearClampSampler.ShaderRegister = 2;
        linearClampSampler.RegisterSpace = 0;
        linearClampSampler.ShaderVisibility =
            D3D12_SHADER_VISIBILITY_PIXEL;
        linearClampSampler.MaxLOD = D3D12_FLOAT32_MAX;

        D3D12_STATIC_SAMPLER_DESC pointClampSampler{};
        pointClampSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        pointClampSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        pointClampSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        pointClampSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        pointClampSampler.ShaderRegister = 3;
        pointClampSampler.RegisterSpace = 0;
        pointClampSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        pointClampSampler.MaxLOD = D3D12_FLOAT32_MAX;

        const D3D12_STATIC_SAMPLER_DESC staticSamplers[] = {
            linearWrapSampler,
            shadowSampler,
            linearClampSampler,
            pointClampSampler
        };

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters = static_cast<UINT>(std::size(params));
        rsDesc.pParameters = params;
        rsDesc.NumStaticSamplers = static_cast<UINT>(std::size(staticSamplers));
        rsDesc.pStaticSamplers = staticSamplers;
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> sigBlob;
        ComPtr<ID3DBlob> errBlob;
        if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, sigBlob.GetAddressOf(), errBlob.GetAddressOf()))) {
            if (errBlob) OutputDebugStringA(static_cast<const char*>(errBlob->GetBufferPointer()));
            return false;
        }

        if (FAILED(device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(store.rootSig.GetAddressOf())))) {
            return false;
        }

        D3D12_ROOT_PARAMETER skinnedParams[ROOT_PARAM::Count]{};
        for (size_t i = 0; i < std::size(params); ++i) {
            skinnedParams[i] = params[i];
        }
        skinnedParams[ROOT_PARAM::JointPalette].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        skinnedParams[ROOT_PARAM::JointPalette].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        skinnedParams[ROOT_PARAM::JointPalette].Descriptor.ShaderRegister = 3;
        skinnedParams[ROOT_PARAM::JointPalette].Descriptor.RegisterSpace = 0;

        D3D12_ROOT_SIGNATURE_DESC skinnedRsDesc = rsDesc;
        skinnedRsDesc.NumParameters = static_cast<UINT>(std::size(skinnedParams));
        skinnedRsDesc.pParameters = skinnedParams;

        sigBlob.Reset();
        errBlob.Reset();
        if (FAILED(D3D12SerializeRootSignature(&skinnedRsDesc, D3D_ROOT_SIGNATURE_VERSION_1, sigBlob.GetAddressOf(), errBlob.GetAddressOf()))) {
            if (errBlob) OutputDebugStringA(static_cast<const char*>(errBlob->GetBufferPointer()));
            return false;
        }
        if (FAILED(device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(store.skinnedRootSig.GetAddressOf())))) {
            return false;
        }

        const auto inputElements = GetStaticMeshInputLayout();

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = store.rootSig.Get();
        psoDesc.VS = { store.vsBlob->GetBufferPointer(), store.vsBlob->GetBufferSize() };
        psoDesc.PS = { store.psBlob->GetBufferPointer(), store.psBlob->GetBufferSize() };
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        psoDesc.InputLayout = { inputElements.data(), static_cast<UINT>(inputElements.size()) };
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc.Count = 1;

        if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(store.pso.GetAddressOf())))) {
            return false;
        }

        const auto skinnedInputElements = GetSkinnedMeshInputLayout();

        D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedPsoDesc = psoDesc;
        skinnedPsoDesc.pRootSignature = store.skinnedRootSig.Get();
        skinnedPsoDesc.VS = { store.skinnedVsBlob->GetBufferPointer(), store.skinnedVsBlob->GetBufferSize() };
        skinnedPsoDesc.InputLayout = { skinnedInputElements.data(), static_cast<UINT>(skinnedInputElements.size()) };
        if (FAILED(device->CreateGraphicsPipelineState(&skinnedPsoDesc, IID_PPV_ARGS(store.skinnedPso.GetAddressOf())))) {
            return false;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC depthPsoDesc = psoDesc;
        depthPsoDesc.PS = {};
        depthPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        depthPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        depthPsoDesc.DepthStencilState.DepthEnable = TRUE;
        depthPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        depthPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        depthPsoDesc.NumRenderTargets = 0;
        for (DXGI_FORMAT& format : depthPsoDesc.RTVFormats) {
            format = DXGI_FORMAT_UNKNOWN;
        }
        depthPsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        if (FAILED(device->CreateGraphicsPipelineState(&depthPsoDesc, IID_PPV_ARGS(store.depthPso.GetAddressOf())))) {
            return false;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC depthSkinnedPsoDesc = depthPsoDesc;
        depthSkinnedPsoDesc.pRootSignature = store.skinnedRootSig.Get();
        depthSkinnedPsoDesc.VS = { store.skinnedVsBlob->GetBufferPointer(), store.skinnedVsBlob->GetBufferSize() };
        depthSkinnedPsoDesc.InputLayout = { skinnedInputElements.data(), static_cast<UINT>(skinnedInputElements.size()) };
        if (FAILED(device->CreateGraphicsPipelineState(&depthSkinnedPsoDesc, IID_PPV_ARGS(store.depthSkinnedPso.GetAddressOf())))) {
            return false;
        }

        if (waterDepthShaderReady) {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC waterDepthPsoDesc =
                depthPsoDesc;
            waterDepthPsoDesc.PS = {
                store.waterDepthPsBlob->GetBufferPointer(),
                store.waterDepthPsBlob->GetBufferSize()
            };
            if (FAILED(device->CreateGraphicsPipelineState(
                    &waterDepthPsoDesc,
                    IID_PPV_ARGS(store.waterDepthPso.GetAddressOf())))) {
                store.waterDepthPso.Reset();
            }

            D3D12_GRAPHICS_PIPELINE_STATE_DESC waterDepthSkinnedPsoDesc =
                waterDepthPsoDesc;
            waterDepthSkinnedPsoDesc.pRootSignature = store.skinnedRootSig.Get();
            waterDepthSkinnedPsoDesc.VS = {
                store.skinnedVsBlob->GetBufferPointer(),
                store.skinnedVsBlob->GetBufferSize()
            };
            waterDepthSkinnedPsoDesc.InputLayout = {
                skinnedInputElements.data(),
                static_cast<UINT>(skinnedInputElements.size())
            };
            if (FAILED(device->CreateGraphicsPipelineState(
                    &waterDepthSkinnedPsoDesc,
                    IID_PPV_ARGS(store.waterDepthSkinnedPso.GetAddressOf())))) {
                store.waterDepthSkinnedPso.Reset();
            }
            if (store.waterDepthPso == nullptr ||
                store.waterDepthSkinnedPso == nullptr) {
                DEBUGLOG::PushRenderError(
                    "[MeshRenderer][WARN] Water visibility depth PSO is "
                    "unavailable; volumetric lighting will use opaque SceneDepth.");
            }
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC geometryPsoDesc = psoDesc;
        geometryPsoDesc.PS = { store.geometryPsBlob->GetBufferPointer(), store.geometryPsBlob->GetBufferSize() };
        geometryPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        geometryPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        geometryPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        geometryPsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        if (FAILED(device->CreateGraphicsPipelineState(&geometryPsoDesc, IID_PPV_ARGS(store.geometryPso.GetAddressOf())))) {
            return false;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC geometrySkinnedPsoDesc = geometryPsoDesc;
        geometrySkinnedPsoDesc.pRootSignature = store.skinnedRootSig.Get();
        geometrySkinnedPsoDesc.VS = { store.skinnedVsBlob->GetBufferPointer(), store.skinnedVsBlob->GetBufferSize() };
        geometrySkinnedPsoDesc.InputLayout = { skinnedInputElements.data(), static_cast<UINT>(skinnedInputElements.size()) };
        if (FAILED(device->CreateGraphicsPipelineState(&geometrySkinnedPsoDesc, IID_PPV_ARGS(store.geometrySkinnedPso.GetAddressOf())))) {
            return false;
        }

        if (geometryFeatureMaskShaderReady) {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC geometryFeatureMaskPsoDesc =
                geometryPsoDesc;
            geometryFeatureMaskPsoDesc.PS = {
                store.geometryFeatureMaskPsBlob->GetBufferPointer(),
                store.geometryFeatureMaskPsBlob->GetBufferSize()
            };
            geometryFeatureMaskPsoDesc.NumRenderTargets = 2u;
            geometryFeatureMaskPsoDesc.RTVFormats[1] = DXGI_FORMAT_R32G32_UINT;
            const bool staticPipelineReady = SUCCEEDED(
                device->CreateGraphicsPipelineState(
                    &geometryFeatureMaskPsoDesc,
                    IID_PPV_ARGS(store.geometryFeatureMaskPso.GetAddressOf())));

            bool skinnedPipelineReady = false;
            if (staticPipelineReady) {
                D3D12_GRAPHICS_PIPELINE_STATE_DESC
                    geometryFeatureMaskSkinnedPsoDesc =
                        geometryFeatureMaskPsoDesc;
                geometryFeatureMaskSkinnedPsoDesc.pRootSignature =
                    store.skinnedRootSig.Get();
                geometryFeatureMaskSkinnedPsoDesc.VS = {
                    store.skinnedVsBlob->GetBufferPointer(),
                    store.skinnedVsBlob->GetBufferSize()
                };
                geometryFeatureMaskSkinnedPsoDesc.InputLayout = {
                    skinnedInputElements.data(),
                    static_cast<UINT>(skinnedInputElements.size())
                };
                skinnedPipelineReady = SUCCEEDED(
                    device->CreateGraphicsPipelineState(
                        &geometryFeatureMaskSkinnedPsoDesc,
                        IID_PPV_ARGS(
                            store.geometryFeatureMaskSkinnedPso.GetAddressOf())));
            }
            if (!staticPipelineReady || !skinnedPipelineReady) {
                store.geometryFeatureMaskPso.Reset();
                store.geometryFeatureMaskSkinnedPso.Reset();
                DEBUGLOG::PushRenderError(
                    "[MeshRenderer][WARN] Optional stylized feature-mask "
                    "pipeline is unavailable. Core Forward rendering remains "
                    "available.");
            }
        }

        if (geometryOutlineShadersReady) {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC outlinePsoDesc = psoDesc;
            outlinePsoDesc.VS = {
                store.geometryOutlineVsBlob->GetBufferPointer(),
                store.geometryOutlineVsBlob->GetBufferSize()
            };
            outlinePsoDesc.PS = {
                store.geometryOutlinePsBlob->GetBufferPointer(),
                store.geometryOutlinePsBlob->GetBufferSize()
            };
            outlinePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
            outlinePsoDesc.DepthStencilState.DepthWriteMask =
                D3D12_DEPTH_WRITE_MASK_ZERO;
            GFX::ClearD3D12InfoQueue(device);
            const HRESULT staticPipelineResult =
                device->CreateGraphicsPipelineState(
                    &outlinePsoDesc,
                    IID_PPV_ARGS(store.geometryOutlinePso.GetAddressOf()));
            const bool staticPipelineReady = SUCCEEDED(staticPipelineResult);
            if (!staticPipelineReady) {
                HIKARI_DX_CHECK(
                    staticPipelineResult,
                    "MeshRenderer::CreateTraditionalGeometryOutlineStaticPSO");
                GFX::DumpD3D12InfoQueue(
                    device,
                    "MeshRenderer::CreateTraditionalGeometryOutlineStaticPSO");
            }

            bool skinnedPipelineReady = false;
            if (staticPipelineReady) {
                D3D12_GRAPHICS_PIPELINE_STATE_DESC outlineSkinnedPsoDesc =
                    outlinePsoDesc;
                outlineSkinnedPsoDesc.pRootSignature = store.skinnedRootSig.Get();
                outlineSkinnedPsoDesc.VS = {
                    store.geometryOutlineSkinnedVsBlob->GetBufferPointer(),
                    store.geometryOutlineSkinnedVsBlob->GetBufferSize()
                };
                outlineSkinnedPsoDesc.InputLayout = {
                    skinnedInputElements.data(),
                    static_cast<UINT>(skinnedInputElements.size())
                };
                GFX::ClearD3D12InfoQueue(device);
                const HRESULT skinnedPipelineResult =
                    device->CreateGraphicsPipelineState(
                        &outlineSkinnedPsoDesc,
                        IID_PPV_ARGS(
                            store.geometryOutlineSkinnedPso.GetAddressOf()));
                skinnedPipelineReady = SUCCEEDED(skinnedPipelineResult);
                if (!skinnedPipelineReady) {
                    HIKARI_DX_CHECK(
                        skinnedPipelineResult,
                        "MeshRenderer::CreateTraditionalGeometryOutlineSkinnedPSO");
                    GFX::DumpD3D12InfoQueue(
                        device,
                        "MeshRenderer::CreateTraditionalGeometryOutlineSkinnedPSO");
                }
            }
            if (!staticPipelineReady || !skinnedPipelineReady) {
                store.geometryOutlinePso.Reset();
                store.geometryOutlineSkinnedPso.Reset();
                DEBUGLOG::PushRenderError(
                    "[MeshRenderer][WARN] Optional traditional VS/PS "
                    "geometry-outline pipeline is unavailable. Mesh Shader "
                    "outline readiness is evaluated independently.");
            }
        }

        if (visibilityOverlayShaderReady) {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC visibilityPsoDesc = psoDesc;
            visibilityPsoDesc.PS = {
                store.visibilityOverlayPsBlob->GetBufferPointer(),
                store.visibilityOverlayPsBlob->GetBufferSize()
            };
            ApplyCompositeBlendState(
                visibilityPsoDesc.BlendState,
                VFX::CompositeMode::Alpha);
            visibilityPsoDesc.DepthStencilState.DepthWriteMask =
                D3D12_DEPTH_WRITE_MASK_ZERO;
            visibilityPsoDesc.DepthStencilState.DepthFunc =
                D3D12_COMPARISON_FUNC_GREATER;
            const bool staticPipelineReady = SUCCEEDED(
                device->CreateGraphicsPipelineState(
                    &visibilityPsoDesc,
                    IID_PPV_ARGS(store.visibilityOverlayPso.GetAddressOf())));

            bool skinnedPipelineReady = false;
            if (staticPipelineReady) {
                D3D12_GRAPHICS_PIPELINE_STATE_DESC visibilitySkinnedPsoDesc =
                    visibilityPsoDesc;
                visibilitySkinnedPsoDesc.pRootSignature =
                    store.skinnedRootSig.Get();
                visibilitySkinnedPsoDesc.VS = {
                    store.skinnedVsBlob->GetBufferPointer(),
                    store.skinnedVsBlob->GetBufferSize()
                };
                visibilitySkinnedPsoDesc.InputLayout = {
                    skinnedInputElements.data(),
                    static_cast<UINT>(skinnedInputElements.size())
                };
                skinnedPipelineReady = SUCCEEDED(
                    device->CreateGraphicsPipelineState(
                        &visibilitySkinnedPsoDesc,
                        IID_PPV_ARGS(
                            store.visibilityOverlaySkinnedPso.GetAddressOf())));
            }
            if (!staticPipelineReady || !skinnedPipelineReady) {
                store.visibilityOverlayPso.Reset();
                store.visibilityOverlaySkinnedPso.Reset();
                DEBUGLOG::PushRenderError(
                    "[MeshRenderer][WARN] Optional visibility-overlay pipeline "
                    "is unavailable. Core Forward rendering remains available.");
            }
        }

        store.psBlobCache["StaticLit"] = store.psBlob;
        return true;
    }

} // namespace HIKARI::MESHRENDERER
