#pragma once
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <span>
#include <vector>

#include <DirectXMath.h>
#include <d3d12.h>

#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "Render3D/Cluster/HIKARI_ClusterGpuCullingPass.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenLayer.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_LocalLightGpuData.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Core/Math/HIKARI_Transform3D.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"
#include "Render3D/Meshlet/HIKARI_MeshletRenderBackend.h"
#include "Render3D/Shadow/HIKARI_ShadowLightFrame.h"
#include "Vfx/Common/HIKARI_FxTypes.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

namespace HIKARI {
class Material;
}

namespace HIKARI::MESHRENDERER {

// BeginFrame 成功後から次の source/frame 切り替えまでの借用 publication。
// Material registry と同じ slot/buffer を RT/GI consumer へ渡す。
struct MeshMaterialGpuFrameView {
    std::span<const uint32_t> materialSlots;
    std::span<const RENDER3D::MATERIAL::GpuMaterialData> materialData;
    D3D12_GPU_VIRTUAL_ADDRESS materials = 0;
    D3D12_GPU_VIRTUAL_ADDRESS programParameters = 0;
    D3D12_GPU_VIRTUAL_ADDRESS programResources = 0;
    uint64_t dataVersion = 0;
    uint64_t bindingVersion = 0;
    uint32_t materialCapacity = 0;
    explicit operator bool() const { return materials && programParameters && programResources; }
};

// One resource set per simultaneous secondary scene capture. Keep this
// equal to the public GameProject scene-capture capacity; the document
// integration asserts that contract at the boundary.
inline constexpr uint32_t kSecondarySceneCaptureMaximumCount = 4u;

enum class MeshRenderDebugMode {
    Normal,
    WireOverlay,
    WireOnly,
};

struct MeshRendererFrameResult {
    bool frameRendered = false;
    bool massSubmissionRequired = false;
    bool massSubmissionCommitted = false;

    bool CanAcknowledgeMassRenderPublication() const noexcept {
        return frameRendered && (!massSubmissionRequired || massSubmissionCommitted);
    }
};

struct CameraCB {
    MATH::Mat4 viewProj{};
    MATH::Mat4 invViewProj{};
    MATH::Vec4 cameraPos{};
    MATH::Vec4 timeParams{};
    MATH::Vec4 simulationTimeParams{};
    MATH::Vec4 screenParams{};
};

struct MeshFrameCameraOverrides {
    const MATH::Mat4* renderViewProj = nullptr;
    const MATH::Mat4* renderInvViewProj = nullptr;
    const MATH::Mat4* cullingViewProj = nullptr;
    const MATH::Mat4* cullingInvViewProj = nullptr;

    bool HasRenderMatrices() const { return renderViewProj != nullptr && renderInvViewProj != nullptr; }

    bool HasCullingMatrices() const { return cullingViewProj != nullptr && cullingInvViewProj != nullptr; }
};

struct ObjectCB {
    MATH::Mat4 world{};
    MATH::Mat4 normalMatrix{};
    MATH::Vec4 baseColor{};
    uint32_t hasBaseColorTexture = 0;
    uint32_t fxFlags = 0;
    uint32_t materialFlags = 0;
    float alphaCutoff = 0.5f;
    MATH::Vec4 emissiveFactor{};
    uint32_t hasNormalTexture = 0;
    float normalScale = 1.0f;
    float normalPadding[2]{};
    uint32_t receiveShadow = 1;
    float shadowObjectPadding[3]{};
    uint32_t hasEmissiveTexture = 0;
    float emissivePadding[3]{};
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    uint32_t hasMetallicRoughnessTexture = 0;
    uint32_t hasOcclusionTexture = 0;
    float occlusionStrength = 1.0f;
    float pbrPadding[3]{};
    MATH::Vec4 fxUser[VFX::kMaterialFxUserCount]{};
    MATH::Vec4 fxUser0{};
    MATH::Vec4 fxUser1{};
    MATH::Vec4 fxUser2{};
    MATH::Vec4 fxUser3{};
};

// StructuredBuffer layout. Keep this compact and independent from ObjectCB padding.
struct ObjectGpuData {
    MATH::Mat4 world{};
    MATH::Mat4 normalMatrix{};
    MATH::Vec4 baseColor{};
    uint32_t hasBaseColorTexture = 0;
    uint32_t fxFlags = 0;
    uint32_t materialFlags = 0;
    float alphaCutoff = 0.5f;
    MATH::Vec4 emissiveFactor{};
    uint32_t hasNormalTexture = 0;
    float normalScale = 1.0f;
    float normalPadding[2]{};
    uint32_t receiveShadow = 1;
    float shadowObjectPadding[3]{};
    uint32_t hasEmissiveTexture = 0;
    float emissivePadding[3]{};
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    uint32_t hasMetallicRoughnessTexture = 0;
    uint32_t hasOcclusionTexture = 0;
    float occlusionStrength = 1.0f;
    uint32_t materialDataIndex = 0xffffffffu;
    float pbrPadding[2]{};
    MATH::Vec4 fxUser[VFX::kMaterialFxUserCount]{};
};

static_assert(sizeof(ObjectGpuData) == 384u);

constexpr uint32_t kInvalidMaterialDataIndex = RENDER3D::MATERIAL::kInvalidGpuMaterialIndex;
constexpr uint32_t kInvalidTextureDescriptorIndex = RENDER3D::MATERIAL::kInvalidTextureDescriptorIndex;
using MaterialGpuData = RENDER3D::MATERIAL::GpuMaterialData;

struct LightCB {
    // xyz: 光線方向、w: 太陽角半径の半角の sin^2（正規化 GGX 円盤近似）。
    MATH::Vec4 directionalDir{};
    MATH::Vec4 directionalColor{};
    MATH::Vec4 ambientColor{};
    // ビューの露出 {P, 1/P, P/E, P/P_prev}（COLOR::MakeViewExposureConstants）。
    MATH::Vec4 viewExposure{1.0f, 1.0f, 1.0f, 1.0f};
    float directionalIntensity = 1.0f;
    float ambientIntensity = 0.25f;
    uint32_t localLightCount = 0;
    uint32_t localLightClusterCountX = 0u;
    uint32_t localLightClusterCountY = 0u;
    uint32_t localLightClusterCountZ = 0u;
    uint32_t localLightTileSizePixels = 64u;
    uint32_t localLightClusterIndexCount = 0u;
    float localLightNearDistance = 0.1f;
    float localLightFarDistance = 100.0f;
    float localLightDepthScale = 1.0f;
    float localLightDepthBias = 0.0f;
    MATH::Vec4 fogColorDensity{};
    MATH::Vec4 fogParams{};
    uint32_t debugView = 0;
    float forwardCostMode = 0.0f;
    float debugPadding[2]{};
    // View 単位の SSR 設定。Water も opaque provider と同じ authoring 値を使い、
    // 変形後の surface origin だけを差し替えて trace する。
    MATH::Vec4 screenSpaceReflectionParams0{};
    MATH::Vec4 screenSpaceReflectionParams1{};
    // 描画の様式の標準材質の光の受け方。0: {明るい側の段数（0 で物理）, 影の境, 境のぼかし, 鏡面反射の強さ}、
    // 1: {影の側の環境光の色, 縁の光}。
    MATH::Vec4 styleShading0{};
    MATH::Vec4 styleShading1{1.0f, 1.0f, 1.0f, 0.0f};
};

static_assert(offsetof(LightCB, localLightDepthScale) == 104u);
static_assert(offsetof(LightCB, fogColorDensity) == 112u);
static_assert(offsetof(LightCB, screenSpaceReflectionParams0) == 160u);
static_assert(offsetof(LightCB, styleShading0) == 192u);
static_assert(sizeof(LightCB) == 224u);

struct ReflectionProbeGpuData {
    MATH::Vec4 positionRadius{};
    MATH::Vec4 params{};
    MATH::Vec4 intensity{};
    MATH::Vec4 influenceBoxMin{};
    MATH::Vec4 influenceBoxMax{};
    MATH::Vec4 projectionBoxMin{};
    MATH::Vec4 projectionBoxMax{};
    MATH::Vec4 shapeParams{};
};

using IrradianceVolumeGpuData = RENDER3D::LIGHTPROBE::IrradianceVolumeGpuData;

struct SkyEnvironmentCB {
    // x 空の画素値を輝度へ写す倍率、y 反射の倍率、z 天空光の倍率。未設定の空はすべて 0（空の光なし）。
    MATH::Vec4 skyScales{0, 0, 0, 0};
    // x SkyMode、y 立方体あり、w 空の回転。
    MATH::Vec4 skyParams{0, 0, 0, 0};
    // rgb tint, w Texture2D user-SRV descriptor index plus one.
    MATH::Vec4 skyTintLighting{0, 0, 0, 0};
    MATH::Vec4 iblParams{0, 0, 0, 0};
    ReflectionProbeGpuData reflectionProbes[2]{};
    MATH::Vec4 ambientSamplingParams{};
    IrradianceVolumeGpuData irradianceVolumes[2]{};
    // Shared atmosphere source for Forward, water, and analytic IBL.
    MATH::Vec4 atmospherePlanetParams{};
    MATH::Vec4 atmosphereRayleigh{};
    MATH::Vec4 atmosphereMie{};
    MATH::Vec4 atmosphereOzone{};
    MATH::Vec4 atmosphereOzoneParams{};
    MATH::Vec4 atmosphereSunDirection{0, 0, 0, 0};
    MATH::Vec4 atmosphereSunColorRadiance{};
    RENDER3D::LIGHTING::CloudSolarVisibilityGpuData cloudSolarVisibility{};
    // 動的 GI（手置きの区域、近景、遠景の優先順）。焼き込みの irradianceVolumes より先に重ねる。
    IrradianceVolumeGpuData dynamicGiVolumes[RENDER3D::LIGHTPROBE::kDynamicGiVolumeCount]{};
};

static_assert(offsetof(SkyEnvironmentCB, atmospherePlanetParams) == 496u);
static_assert(offsetof(SkyEnvironmentCB, cloudSolarVisibility) == 608u);
static_assert(offsetof(SkyEnvironmentCB, dynamicGiVolumes) == 672u);
static_assert(sizeof(SkyEnvironmentCB) == 912u);

struct ShadowCB {
    MATH::Mat4 lightViewProj[SHADOW::kMaxDirectionalShadowCascades]{};
    MATH::Vec4 cascadeSplitDistances{};
    MATH::Vec4 cascadeParams{};
    MATH::Vec4 cascadeDepthBiasScales{1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t enabled = 0;
    float depthBias = 0.001f;
    float normalBias = 0.02f;
    float strength = 0.75f;
    uint32_t pcfEnabled = 1;
    float pcfRadius = 1.0f;
    float texelSizeX = 1.0f / 2048.0f;
    float texelSizeY = 1.0f / 2048.0f;
    float edgeFade = 0.04f;
    float frameIndex = 0.0f;    // 画素ごとの tap 回転と接触影の始点のずらし
    float lightTangent = 0.0f;  // PCSS：tan(太陽の角半径) × 半影の倍率。0 で固定 PCF
    float contactLength = 0.0f; // 接触影の長さ [m]。0 で無効
    float contactStrength = 1.0f;
    float contactThickness = 0.25f; // 遮る物の厚みの想定 [m]
    float contactPadding[2]{};
    RENDER3D::LIGHTING::LocalShadowGpuData localShadow{};
};

constexpr size_t kMaxJointPaletteMatrices = 128u;
static_assert(sizeof(ShadowCB) == 2432u);
constexpr UINT kMaxObjectCount = 2048u;
constexpr UINT kMaxMaterialDataCount = RENDER3D::MATERIAL::kDefaultGpuMaterialCapacity;

struct JointPaletteCB {
    MATH::Mat4 jointMatrices[kMaxJointPaletteMatrices]{};
};

struct MaterialTextureDescriptorIndices {
    uint32_t baseColor = kInvalidTextureDescriptorIndex;
    uint32_t normal = kInvalidTextureDescriptorIndex;
    uint32_t emissive = kInvalidTextureDescriptorIndex;
    uint32_t metallicRoughness = kInvalidTextureDescriptorIndex;
    uint32_t occlusion = kInvalidTextureDescriptorIndex;
    uint32_t specular = kInvalidTextureDescriptorIndex;
    uint32_t specularColor = kInvalidTextureDescriptorIndex;
};

struct DrawItem {
    const ModelAsset* asset = nullptr;
    const Material* materialOverride = nullptr;
    Transform3D transform{};
    std::vector<MATH::Mat4> jointPalette{};
    std::string materialFxProfileId{};
    uint32_t postGroupMask = 0;
    VFX::VariantKey variant{};
    std::array<MATH::Vec4, VFX::kMaterialFxUserCount> fxValues{};
    uint32_t fxFlags = 0;
    std::array<DirectX::XMFLOAT4, VFX::kMaterialFxUserCount> materialFxParamValues{};
    bool materialFxValuesInitialized = false;
    bool hasResolvedMaterialFxProfile = false;
    MaterialFxProfile resolvedMaterialFxProfile{};
    bool usePrimitiveFilter = false;
    uint32_t meshIndexFilter = 0;
    uint32_t primitiveIndexFilter = 0;
    bool receiveShadow = true;
    MeshRenderDebugMode renderDebugMode = MeshRenderDebugMode::Normal;
};

// 描画の準備で積み上げる回数。論理フレームの最初のビューが 0 に戻し、そのフレームの全ビュー分が溜まる。
// MeshRenderer.cpp が render.* 指標として登録表へ出す。回数を足す時は、そこの表にも 1 行足す。
// MeshRendererDebugStats の残りは毎フレーム上書きされる値なので、戻さない（ここへ置かない）。
struct MeshRendererFrameCounters {
    size_t gpuDrivenSkinnedCommandCount = 0;
    size_t meshCacheMissCount = 0;
    size_t materialTextureLoadCount = 0;
    size_t materialTextureLoadDeferredCount = 0;
    size_t psoCacheMissCount = 0;
};

struct MeshRendererDebugStats : MeshRendererFrameCounters {
    size_t materialFxProfileCacheHitCount = 0;
    size_t materialFxProfileCacheMissCount = 0;
    size_t materialFxProfileCacheFailCount = 0;
    RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamStats traditionalCommandStream{};
    RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadStats gpuSceneUpload{};
    size_t gpuDrivenCommandStreamGpuCommandCount = 0;
    size_t gpuDrivenCommandStreamTraditionalCommandCount = 0;
    RENDER3D::CLUSTER::ClusterGpuCullingPassStats clusterGpuCull{};
    size_t depthPrepassOccluderRecordCount = 0;
    size_t depthPrepassRejectedSmallRecordCount = 0;
    size_t depthPrepassRejectedUnsafeMaterialRecordCount = 0;
    RENDER3D::MESHLET::MeshletRenderBackendStats meshletBackend{};
    bool meshletDispatchArgumentBufferReady = false;
    bool meshletDispatchCommandSignatureReady = false;
    bool gpuDrivenCullingCameraFrozen = false;
    bool directionalEnabled = false;
    float directionalIntensity = 0.0f;
    float ambientIntensity = 0.0f;
    size_t localLightSourceCount = 0;
    size_t localLightUploadableCount = 0;
    size_t localLightUploadedCount = 0;
    size_t localLightClampedCount = 0;
    size_t pointLightUploadedCount = 0;
    size_t spotLightUploadedCount = 0;
    size_t localLightClusterCount = 0;
    size_t localLightClusterIndexCount = 0;
    size_t localLightClusterOverflowCount = 0;
};

} // namespace HIKARI::MESHRENDERER
