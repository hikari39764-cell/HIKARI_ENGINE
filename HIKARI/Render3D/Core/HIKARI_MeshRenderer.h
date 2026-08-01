#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <d3d12.h>
#include <DirectXMath.h>
#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Lighting/HIKARI_LocalLightList.h"
#include "Render3D/HIKARI_Transform3D.h"
#include "Render3D/Core/HIKARI_MeshPassResources.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"

namespace HIKARI::RENDER3D {
    namespace GPUDRIVEN {
        struct GpuDrivenSceneSource;
        struct GpuDepthVisibilityStats;
    }
    namespace SURFACE {
        class SceneSurfaceBuffer;
    }
    namespace MATERIAL {
        struct GpuMaterialRegistryStats;
    }
}

namespace HIKARI::MESHRENDERER {

    struct TemporalVelocityDraw {
        uint64_t objectId = 0;
        MATH::Mat4 world{};
        D3D12_VERTEX_BUFFER_VIEW vertexBuffer{};
        D3D12_INDEX_BUFFER_VIEW indexBuffer{};
        uint32_t indexCount = 0;
        uint32_t startIndex = 0;
        int32_t baseVertex = 0;
        bool skinned = false;
        bool doubleSided = false;
        bool alphaMasked = false;
        const std::vector<MATH::Mat4>* jointPalette = nullptr;
    };

    void Reset();
    void InvalidateMaterialFxPipelineCache();
    void SetGpuDrivenSceneSource(
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* source);
    const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource&
        GetActiveGpuDrivenSceneSource();
    bool HasSubmittedItems();
    bool BeginFrame(
        const GFX::Context& graphics,
        const Camera3D& camera,
        const SceneEnvironment& environment,
        const RENDER3D::LIGHTING::SceneLightingSet& lighting,
        uint32_t screenWidth,
        uint32_t screenHeight,
        RenderDebugView debugView = RenderDebugView::None,
        const MeshFrameCameraOverrides* cameraOverrides = nullptr);
    void SetGpuDrivenCullingCameraFreezeEnabled(bool enabled);
    bool IsGpuDrivenCullingCameraFrozen();
    const CameraCB* GetCameraConstants();
    const CameraCB* GetGpuDrivenCullingCameraConstants();
    RENDER3D::LIGHTING::LocalLightListGpuBindings
        GetLocalLightListGpuBindings();
    bool RenderGeometryAuxPass(
        const GFX::Context& graphics,
        RENDER3D::SURFACE::SceneSurfaceBuffer& surfaceBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv,
        bool writeFeatureMask = false);
    bool HasGeometryOutlineFeatureBackend();
    bool HasVisibilityOverlayFeatureBackend();
    bool RenderGeometryOutlineFeaturePass(
        const GFX::Context& graphics,
        const MeshPassResources& passResources);
    bool RenderVisibilityOverlayFeaturePass(
        const GFX::Context& graphics,
        const MeshPassResources& passResources);
    bool HasDepthPrepassWork();
    bool RenderDepthPrepass(
        const GFX::Context& graphics,
        D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv);
    bool FinalizeGpuDrivenVisibilityWithoutDepth(
        const GFX::Context& graphics);
    bool FinalizeGpuDrivenVisibilityFromDepth(
        const GFX::Context& graphics,
        const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats& depthVisibilityStats);
    bool RenderForwardOpaquePass(
        const GFX::Context& graphics,
        const MeshPassResources& passResources);
    bool RenderForwardTransparentPass(
        const GFX::Context& graphics,
        const MeshPassResources& passResources);
    bool HasDepthAwarePassWork();
    bool HasForwardTransparentPassWork();
    bool RenderDepthAwarePass(
        const GFX::Context& graphics,
        const MeshPassResources& passResources);
    void GatherTemporalVelocityDraws(
        const GFX::Context& graphics,
        std::vector<TemporalVelocityDraw>& outDraws);
    void SetAmbientOcclusionRuntimeEnabled(bool enabled);
    void EndFrame();
    void RenderAll(
        const GFX::Context& graphics,
        const RENDER3D::RenderViewContext& view,
        const SceneEnvironment& environment,
        const RENDER3D::LIGHTING::SceneLightingSet& lighting,
        RenderDebugView debugView = RenderDebugView::None);
#if defined(HIKARI_WITH_EDITOR)
    namespace EDITOR_DETAIL {
        struct SceneSourceTag;
    }
    struct EditorInteractiveRenderSettings {
        using RebindTargetCallback = bool (*)(void* userData);

        RenderDebugView debugView = RenderDebugView::None;
        bool neutralLighting = false;
        bool neutralClayMaterial = false;
        bool writeStylizedFeatureMask = false;
        bool renderGeometryOutline = false;
        bool renderVisibilityOverlay = false;
        RENDER3D::SURFACE::SceneSurfaceBuffer* stylizedSurfaceBuffer = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv{};
        void* renderTargetUserData = nullptr;
        RebindTargetCallback rebindTarget = nullptr;
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource*
            sceneSourceOverride = nullptr;
    };

    bool RenderEditorInteractiveOpaque(
        const RENDER3D::RenderViewContext& view,
        const SceneEnvironment& environment,
        const RENDER3D::LIGHTING::SceneLightingSet& lighting,
        uint32_t screenWidth,
        uint32_t screenHeight,
        const EditorInteractiveRenderSettings& settings);
    void ReleaseEditorInteractiveSceneSource(
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* source);
    void ShutdownEditorInteractiveResources();
#endif
    const MeshRendererDebugStats& GetDebugStats();
    const RENDER3D::MATERIAL::GpuMaterialRegistryStats&
        GetGpuMaterialRegistryStats();

} // namespace HIKARI::MESHRENDERER
