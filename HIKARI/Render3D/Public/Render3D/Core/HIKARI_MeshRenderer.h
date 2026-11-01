#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <d3d12.h>
#include <DirectXMath.h>
#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "Render3D/Debug/HIKARI_RenderDebugView.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Lighting/HIKARI_LocalLightList.h"
#include "Core/Math/HIKARI_Transform3D.h"
#include "Render3D/Core/HIKARI_MeshPassResources.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"

namespace HIKARI::IBL {
struct IblEnvironmentSource;
}

namespace HIKARI::RENDER3D {
namespace GPUDRIVEN {
struct GpuDrivenSceneSource;
struct GpuDepthVisibilityStats;
struct MassGpuSceneFrameBufferStats;
} // namespace GPUDRIVEN
namespace SURFACE {
class SceneSurfaceBuffer;
}
namespace MATERIAL {
struct GpuMaterialRegistryStats;
}
namespace PARTICLES {
struct ParticleGpuDrivenPassStats;
struct ParticleRenderFrameSource;
} // namespace PARTICLES
namespace WORLD_SPRITES {
struct WorldSpriteFrameSource;
struct WorldSpritePassStats;
class WorldSpritePass;
} // namespace WORLD_SPRITES
namespace WORLD_TEXT {
struct WorldTextFrameSource;
struct WorldTextPassStats;
} // namespace WORLD_TEXT
} // namespace HIKARI::RENDER3D

namespace HIKARI::MESHRENDERER {

struct TemporalVelocityDraw {
    uint64_t objectId = 0;
    uint64_t objectLifetimeGeneration = 0u;
    uint64_t objectVersion = 0u;
    uint32_t sourceSurfaceInstanceIndex = UINT32_MAX;
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
    MATH::Vec4 shellSettings{};
    MATH::Vec4 boundsCenterRadius{};
};

void Reset();
// 初回描画時のパイプライン初期化をユーザー操作から切り離して呼ぶ。
void EnsureReadyForFirstFrame(const GFX::Context& graphics);
void InvalidateMaterialFxPipelineCache();
void SetGpuDrivenSceneSource(const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* source);
void SetParticleFrameSource(const RENDER3D::PARTICLES::ParticleRenderFrameSource* source);
void SetWorldSpriteFrameSource(const RENDER3D::WORLD_SPRITES::WorldSpriteFrameSource* source);
void SetWorldTextFrameSource(const RENDER3D::WORLD_TEXT::WorldTextFrameSource* source);
void ReleaseParticleNamespace(uint64_t playbackNamespace);
void ShutdownParticleGpuDrivenPass();
void ShutdownWorldSpritePass();
void ShutdownWorldTextPass();
void ShutdownOceanSpectrumRuntime();
const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& GetActiveGpuDrivenSceneSource();
bool HasSubmittedItems();
bool BeginFrame(const GFX::Context& graphics, const Camera3D& camera, const SceneEnvironment& environment,
                const RENDER3D::LIGHTING::SceneLightingSet& lighting, uint32_t screenWidth, uint32_t screenHeight,
                RenderDebugView debugView = RenderDebugView::None,
                const MeshFrameCameraOverrides* cameraOverrides = nullptr);
// BeginFrame 後、Forward 提出前に一度確定。準備に失敗した体積霧は通常霧を消さない。
bool FinalizeVolumetricFog(const GFX::Context& graphics, bool volumetricFogReady);
// 主ビューの前向きの解析霧の定数（体積霧で消した時は無効の印）。画面の貼り直しが材質と同じ霧を掛けるのに使う。
void GetMainViewFogConstants(MATH::Vec4& fogParams, MATH::Vec4& fogColorDensity);
void SetGpuDrivenCullingCameraFreezeEnabled(bool enabled);
bool IsGpuDrivenCullingCameraFrozen();
const CameraCB* GetCameraConstants();
const CameraCB* GetGpuDrivenCullingCameraConstants();
MeshMaterialGpuFrameView GetMaterialGpuFrameView(const GFX::Context& graphics);
bool RefreshLightingProviders(const GFX::Context& graphics,
                              const RENDER3D::LIGHTING::CloudSolarVisibilityGpuData& clouds);
RENDER3D::LIGHTING::LocalLightListGpuBindings GetLocalLightListGpuBindings();
// 今フレームの地形の記録と高さ場の float pool（描画と同じ番地）。光線追跡の場面が地形を変形して入れるのに使う。
struct TerrainSurfaceGpuBindings {
    D3D12_GPU_VIRTUAL_ADDRESS records = 0;
    D3D12_GPU_VIRTUAL_ADDRESS floats = 0;
    uint64_t floatVersion = 0;
};
TerrainSurfaceGpuBindings GetTerrainSurfaceGpuBindings();
bool RenderGeometryAuxPass(const GFX::Context& graphics, RENDER3D::SURFACE::SceneSurfaceBuffer& surfaceBuffer,
                           D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv, bool writeFeatureMask = false);
bool HasStylizedFeatureMaskBackend();
bool HasGeometryOutlineFeatureBackend();
bool HasVisibilityOverlayFeatureBackend();
bool RenderGeometryOutlineFeaturePass(const GFX::Context& graphics, const MeshPassResources& passResources);
bool RenderVisibilityOverlayFeaturePass(const GFX::Context& graphics, const MeshPassResources& passResources);
bool HasDepthPrepassWork();
bool RenderDepthPrepass(const GFX::Context& graphics, D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv,
                        bool useCullingCamera = false);
bool RenderWaterVisibilityDepth(const GFX::Context& graphics, D3D12_CPU_DESCRIPTOR_HANDLE waterDsv,
                                const MeshPassResources& passResources);
bool FinalizeGpuDrivenVisibilityWithoutDepth(const GFX::Context& graphics);
bool FinalizeGpuDrivenVisibilityFromDepth(const GFX::Context& graphics,
                                          const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats& depthVisibilityStats);
bool PrepareForwardReflectionCapture(const GFX::Context& graphics, bool captureSubsurface = false);

bool RenderForwardOpaquePass(const GFX::Context& graphics, const MeshPassResources& passResources);
bool RenderForwardTransparentPass(const GFX::Context& graphics, const MeshPassResources& passResources);
bool HasDepthAwarePassWork();
bool HasForwardTransparentPassWork();
bool HasPreservedVfxWork();
bool RenderPreservedVfxPass(const GFX::Context& graphics, const MeshPassResources& passResources);
// 歪み層の粒子を戦闘の画面歪み遮罩へ描き、その貼図を返す（合成は PostSystem が行う）
bool HasVfxDistortionWork();
bool RenderVfxDistortionMaskPass(const GFX::Context& graphics, const MeshPassResources& passResources,
                                 RENDER3D::RenderTextureView& outMask, D3D12_CPU_DESCRIPTOR_HANDLE& outMaskSrvCpu);
bool RenderDepthAwarePass(const GFX::Context& graphics, const MeshPassResources& passResources);
// cullViewProj があれば、その視錐の外の面は集めない。
bool GatherTemporalVelocityDraws(const GFX::Context& graphics, std::vector<TemporalVelocityDraw>& outDraws,
                                 uint32_t maximumDrawCount, uint32_t maximumSkinnedDrawCount,
                                 const MATH::Mat4* cullViewProj = nullptr);
bool SetAmbientOcclusionRuntimeEnabled(bool enabled);
void EndFrame();
MeshRendererFrameResult RenderAll(const GFX::Context& graphics, const RENDER3D::RenderViewContext& view,
                                  const SceneEnvironment& environment,
                                  const RENDER3D::LIGHTING::SceneLightingSet& lighting,
                                  RenderDebugView debugView = RenderDebugView::None);
#if defined(HIKARI_WITH_EDITOR)
namespace EDITOR_DETAIL {
struct SceneSourceTag;
}
struct EditorInteractiveRenderSettings {
    using CaptureSceneColorCallback = RENDER3D::RenderTextureView (*)(void* userData);
    using DepthAwareOverlayCallback = void (*)(void* userData);

    RenderDebugView debugView = RenderDebugView::None;
    bool neutralLighting = false;
    bool neutralClayMaterial = false;
    bool writeStylizedFeatureMask = false;
    bool renderGeometryOutline = false;
    bool renderVisibilityOverlay = false;
    RENDER3D::SURFACE::SceneSurfaceBuffer* stylizedSurfaceBuffer = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv{};
    MeshPassResources passResources{};
    RENDER3D::PIPELINE::RenderTargetAccess targetAccess{};
    CaptureSceneColorCallback captureSceneColor = nullptr;
    void* depthAwareOverlayUserData = nullptr;
    DepthAwareOverlayCallback renderDepthAwareOverlay = nullptr;
    const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* sceneSourceOverride = nullptr;
    const RENDER3D::PARTICLES::ParticleRenderFrameSource* particleFrameSource = nullptr;
    const IBL::IblEnvironmentSource* iblEnvironmentOverride = nullptr;
    bool suppressSpatialLightingProviders = false;
};

enum class EditorInteractiveMeshRenderResult : uint8_t {
    Ready,
    RendererUnavailable,
    MaterialFrameUnavailable,
    FrameResourcesUnavailable,
    SceneResidencyUnavailable,
    StylizedSurfacePassFailed,
    ForwardPassFailed,
    StylizedGeometryPassFailed,
};

EditorInteractiveMeshRenderResult RenderEditorInteractiveOpaque(const RENDER3D::RenderViewContext& view,
                                                                const SceneEnvironment& environment,
                                                                const RENDER3D::LIGHTING::SceneLightingSet& lighting,
                                                                uint32_t screenWidth, uint32_t screenHeight,
                                                                const EditorInteractiveRenderSettings& settings);
void ReleaseEditorInteractiveSceneSource(const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* source);
void ShutdownEditorInteractiveResources();
#endif
bool RenderSecondarySceneCapture(const GFX::Context& graphics, const RENDER3D::RenderViewContext& view,
                                 const SceneEnvironment& environment,
                                 const RENDER3D::LIGHTING::SceneLightingSet& lighting, uint32_t captureSlot,
                                 uint32_t screenWidth, uint32_t screenHeight, bool renderTransparent,
                                 const MeshPassResources* captureResources = nullptr, bool prepareFrame = true);
void ResetSecondarySceneCaptureResources(uint32_t captureSlot) noexcept;
void ResetSecondarySceneCaptureResources() noexcept;
const MeshRendererDebugStats& GetDebugStats();
// GeometryAux に参加しない opaque depth writer の有無。frame pass owner が判定する。
RENDER3D::WORLD_SPRITES::WorldSpritePass* PrepareWorldSpritesForView(const GFX::Context& graphics,
                                                                     const RENDER3D::RenderViewContext& view);
const RENDER3D::MATERIAL::GpuMaterialRegistryStats& GetGpuMaterialRegistryStats();
const RENDER3D::GPUDRIVEN::MassGpuSceneFrameBufferStats& GetMassGpuSceneFrameBufferStats();
const RENDER3D::PARTICLES::ParticleGpuDrivenPassStats& GetParticleGpuDrivenPassStats();
const RENDER3D::WORLD_SPRITES::WorldSpritePassStats& GetWorldSpritePassStats();

} // namespace HIKARI::MESHRENDERER
