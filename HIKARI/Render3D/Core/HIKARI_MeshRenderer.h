#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <d3d12.h>
#include <DirectXMath.h>
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/HIKARI_Transform3D.h"
#include "Render3D/Core/HIKARI_MeshPassResources.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"

namespace HIKARI::RENDER3D {
    namespace GPUDRIVEN {
        struct GpuDrivenSceneSource;
        struct GpuDepthVisibilityStats;
    }
    namespace SCREENSPACE {
        class ScreenSpaceGeometryAux;
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
    bool HasSubmittedItems();
    bool BeginFrame(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        RenderDebugView debugView = RenderDebugView::None);
    bool BeginFrame(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        uint32_t screenWidth,
        uint32_t screenHeight,
        RenderDebugView debugView = RenderDebugView::None,
        const MeshFrameCameraOverrides* cameraOverrides = nullptr);
    void SetGpuDrivenCullingCameraFreezeEnabled(bool enabled);
    bool IsGpuDrivenCullingCameraFrozen();
    const CameraCB* GetCameraConstants();
    const CameraCB* GetGpuDrivenCullingCameraConstants();
    bool RenderGeometryAuxPass(
        RENDER3D::SCREENSPACE::ScreenSpaceGeometryAux& geometryAux,
        D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv);
    bool HasDepthPrepassWork();
    bool RenderDepthPrepass(D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv);
    bool FinalizeGpuDrivenVisibilityWithoutDepth();
    bool FinalizeGpuDrivenVisibilityFromDepth(
        const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats& depthVisibilityStats);
    bool RenderForwardOpaquePass(
        const MeshPassResources& passResources);
    bool RenderForwardTransparentPass(
        const MeshPassResources& passResources);
    bool HasDepthAwarePassWork();
    bool HasForwardTransparentPassWork();
    bool RenderDepthAwarePass(
        const MeshPassResources& passResources);
    void GatherTemporalVelocityDraws(
        std::vector<TemporalVelocityDraw>& outDraws);
    void SetAmbientOcclusionRuntimeEnabled(bool enabled);
    void EndFrame();
    void RenderAll(
        const RENDER3D::RenderViewContext& view,
        const SceneEnvironment& environment,
        RenderDebugView debugView = RenderDebugView::None);
#if defined(HIKARI_WITH_EDITOR)
    namespace EDITOR_DETAIL {
        struct SceneSourceTag;
    }
    struct EditorInteractiveRenderSettings {
        RenderDebugView debugView = RenderDebugView::None;
        bool neutralLighting = false;
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource*
            sceneSourceOverride = nullptr;
    };

    bool RenderEditorInteractiveOpaque(
        const RENDER3D::RenderViewContext& view,
        const SceneEnvironment& environment,
        uint32_t screenWidth,
        uint32_t screenHeight,
        const EditorInteractiveRenderSettings& settings);
    void ShutdownEditorInteractiveResources();
#endif
    const MeshRendererDebugStats& GetDebugStats();
    const RENDER3D::MATERIAL::GpuMaterialRegistryStats&
        GetGpuMaterialRegistryStats();

} // namespace HIKARI::MESHRENDERER
