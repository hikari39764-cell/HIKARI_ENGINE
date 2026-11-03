#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>

#include "Core/Text/HIKARI_EnumNames.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Gfx/HIKARI_GfxContext.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {
struct GpuDrivenSceneSource;
}
namespace HIKARI::RENDER3D::MATERIAL {
struct GpuMaterialData;
}

namespace HIKARI::RENDER3D::RAYTRACING {
enum class RuntimeRayTracingStatus : uint8_t {
    Disabled,
    UnsupportedDevice,
    InvalidInput,
    Building,
    BudgetExceeded,
    BuildFailed,
    Ready
};

// 加速構造へ入れなかった面の種類。抜けを許せる程度は機能ごとに違う（CanTraceDiffuse / CanTraceShadows）。
enum class RuntimeRayTracingExclusion : uint8_t {
    Water,
    Skinned,
    FoliageWind,
    Terrain,
    CustomVertex,
    MaterialEffect,
    ScreenMaterial,
    // 形が解決できない、または予算に収まらない大量配置。カスケードの影も大量配置を描かないので、外しても光追の影は
    // カスケードへ戻らない（影を落とす面として数えない）。
    MassInstances,
    // 完成した場面へ後から足した少数の面で、形状の BLAS をまだ組んでいる途中（数フレームで入る）。
    Pending,
    Missing,
    Count
};
inline constexpr size_t kRuntimeRayTracingExclusionCount = static_cast<size_t>(RuntimeRayTracingExclusion::Count);
inline constexpr TEXT::EnumName<RuntimeRayTracingExclusion> kRuntimeRayTracingExclusionNames[] = {
    {RuntimeRayTracingExclusion::Water, "Water"},
    {RuntimeRayTracingExclusion::Skinned, "Skinned"},
    {RuntimeRayTracingExclusion::FoliageWind, "FoliageWind"},
    {RuntimeRayTracingExclusion::Terrain, "Terrain"},
    {RuntimeRayTracingExclusion::CustomVertex, "CustomVertex"},
    {RuntimeRayTracingExclusion::MaterialEffect, "MaterialEffect"},
    {RuntimeRayTracingExclusion::ScreenMaterial, "ScreenMaterial"},
    {RuntimeRayTracingExclusion::MassInstances, "MassInstances"},
    {RuntimeRayTracingExclusion::Pending, "Pending"},
    {RuntimeRayTracingExclusion::Missing, "Missing"},
};

constexpr uint32_t RuntimeRayTracingExclusionBit(RuntimeRayTracingExclusion exclusion) noexcept {
    return 1u << static_cast<uint32_t>(exclusion);
}

// 種類ごとの入れなかった面の数と、その種類のビット（影を落とす面だけのビットも）。
// 静止した形で入れた面（風に揺れる植生など）は approximated に数える（入ってはいるが動きは追わない）。
struct RuntimeRayTracingExclusions {
    std::array<uint32_t, kRuntimeRayTracingExclusionCount> surfaces{};
    std::array<uint32_t, kRuntimeRayTracingExclusionCount> approximated{};
    uint32_t mask = 0;
    uint32_t shadowCasterMask = 0;
    uint32_t approximatedMask = 0;

    void Add(RuntimeRayTracingExclusion exclusion, bool castsShadow) noexcept {
        ++surfaces[static_cast<size_t>(exclusion)];
        mask |= RuntimeRayTracingExclusionBit(exclusion);
        if (castsShadow) {
            shadowCasterMask |= RuntimeRayTracingExclusionBit(exclusion);
        }
    }
    void Approximate(RuntimeRayTracingExclusion exclusion) noexcept {
        ++approximated[static_cast<size_t>(exclusion)];
        approximatedMask |= RuntimeRayTracingExclusionBit(exclusion);
    }
};

// GI は未解決の面（読み込み中の形状など）が抜けると光線が面を抜けて空を拾い、場面へ光が漏れるので、揃うまで待つ。
// 組み途中（Pending）は完成した場面に足した少数の面だけなので、数フレーム遅れて入るのを許して履歴を捨てない。
inline constexpr uint32_t kRuntimeRayTracingDiffuseBlockingExclusions =
    RuntimeRayTracingExclusionBit(RuntimeRayTracingExclusion::Missing);
constexpr bool CanTraceDiffuse(uint32_t excluded) noexcept {
    return (excluded & kRuntimeRayTracingDiffuseBlockingExclusions) == 0u;
}
// 光追の影は不透明面のカスケード影と接触影を置き換えるので、影を落とす面が抜けると影が消える。
// 水面は抜けても影の形が崩れない。組み途中の面は数フレーム遅れて影が入る（場面全体をカスケードへ戻すより目立たない）。
inline constexpr uint32_t kRuntimeRayTracingShadowToleratedExclusions =
    RuntimeRayTracingExclusionBit(RuntimeRayTracingExclusion::Water) |
    RuntimeRayTracingExclusionBit(RuntimeRayTracingExclusion::Pending);
constexpr bool CanTraceShadows(uint32_t excludedShadowCasters) noexcept {
    return (excludedShadowCasters & ~kRuntimeRayTracingShadowToleratedExclusions) == 0u;
}

// 地形の高さ場（HIKARI_TerrainSurfaceParams.hlsli の記録と float pool）。地形の面を世界空間へ変形して入れるのに使う。
// 無ければ地形は入れない。floatVersion が変わった時だけ地形を変形し直す。
struct RuntimeRayTracingSurfaceBindings {
    D3D12_GPU_VIRTUAL_ADDRESS terrainRecords = 0;
    D3D12_GPU_VIRTUAL_ADDRESS surfaceFloats = 0;
    uint64_t floatVersion = 0;
    // 大量配置の描画の常駐記録（このフレームの MassGpuSceneSource と同じ中身。無ければ 0）。動いた個体の instance 記述を
    // GPU で書くのに使う。
    D3D12_GPU_VIRTUAL_ADDRESS massRecords = 0;
};

struct RuntimeRayTracingBudget {
    uint64_t maximumBytes = 128ull * 1024 * 1024;
    uint32_t trianglesPerChunk = 131072;
    uint32_t rangesPerChunk = 128;
    uint32_t chunksPerAsset = 1024;
    uint32_t buildsPerFrame = 2;
    uint32_t geometryUploadsPerFrame = 1;
    uint32_t maximumInstances = 262144; // 大量配置（最大 131072 個体）を含む
    uint32_t maximumHitRecords = 262144;
};

// InstanceID + GeometryIndex で参照する。BLAS は材質/オブジェクト間で共有する。
// firstPrimitive は分割 BLAS 内の PrimitiveIndex を HCMESH 全体へ戻す基点。
// 変形した面（骨格の蒙皮）は頂点を world 空間へ変形した pool から読む（instance の変換は単位）。
inline constexpr uint32_t kRuntimeRayTracingHitDeformed = 4u;
// 透過する面（半透明）。光線を止めず、影の光線だけ透過率を掛けて先へ進む。
inline constexpr uint32_t kRuntimeRayTracingHitTransmissive = 8u;

struct RuntimeRayTracingHitRecord {
    uint32_t geometryDescriptor = 0;
    uint32_t metadataDescriptor = 0;
    uint32_t materialIndex = 0xffffffffu;
    uint32_t flags = 0; // bit0: 両面, bit1: 影を落とす, bit2: 変形, bit3: 透過
    uint32_t firstPrimitive = 0;
    uint32_t firstCluster = 0;
    uint32_t clusterCount = 0;
    uint32_t primitiveCount = 0;
    // 変形した面（bit2）: 変形した頂点の pool 内の byte 位置と頂点数（[位置 × N][法線 × N][接線 × N]）。
    // 変形しない面で光照贴图がある時は、その UV の縮尺 (x, y) と偏り (x, y) を unorm16 二つずつ（光照贴图は静的な面だけ
    // なので重ならない。記録を 48 byte に保つのは動的 GI の更新 shader が資源の臨界にあるため）。
    uint32_t deformedVertexBase = 0;
    uint32_t deformedVertexCount = 0;
    // 面の光照贴图（描画と同じ LIGHTMAP の登録）。光線が当たった画面外の面を焼き込みの間接光で照らすのに使う。
    uint32_t lightmapDescriptor = 0xffffffffu;
    uint32_t reserved = 0;
};
static_assert(sizeof(RuntimeRayTracingHitRecord) == 48);

struct RuntimeRayTracingStats {
    RuntimeRayTracingStatus status = RuntimeRayTracingStatus::Disabled;
    uint32_t sourceSurfaces = 0;
    // 毎フレーム変形して refit する面（骨格の蒙皮）の物体の数。
    uint32_t deformedObjects = 0;
    RuntimeRayTracingExclusions excluded{};
    uint32_t sharedAssets = 0;
    uint32_t readyChunks = 0;
    uint32_t pendingChunks = 0;
    uint32_t builtChunks = 0;
    uint32_t geometryUploads = 0;
    uint32_t instances = 0;
    uint32_t hitRecords = 0;
    uint64_t residentBytes = 0;
    uint64_t retiringBytes = 0;
    bool reusedTopLevel = false;
};

struct RuntimeRayTracingMaterialVersion {
    uint64_t data = 0;
    uint64_t bindings = 0;
    friend bool operator==(const RuntimeRayTracingMaterialVersion&, const RuntimeRayTracingMaterialVersion&) = default;
};

struct RuntimeRayTracingFrame {
    D3D12_GPU_VIRTUAL_ADDRESS accelerationStructure = 0;
    D3D12_GPU_VIRTUAL_ADDRESS hitRecords = 0;
    uint32_t hitRecordCount = 0;
    uint64_t version = 0;
    uint64_t retireFenceValue = 0;
    bool ready = false;
    bool empty = false;
    // 借用 resource の有効範囲と scene/material の由来。consumer は使用前に照合する。
    ID3D12Fence* frameFence = nullptr;
    uint64_t deviceGeneration = 0;
    const void* sceneIdentity = nullptr;
    uint64_t sceneLayoutVersion = 0;
    RuntimeRayTracingMaterialVersion materialVersion{};
    // 入れなかった面の種類（RuntimeRayTracingExclusionBit）。機能はこれで自分が使えるかを決める。
    uint32_t excluded = 0;
    uint32_t excludedShadowCasters = 0;
    // 変形した頂点の pool（無ければ 0）。命中の解決が読む。
    D3D12_GPU_VIRTUAL_ADDRESS deformedVertices = 0;
    MATH::Vec4 changedMinimum{0, 0, 0, 0}, changedMaximum{0, 0, 0, 0};
};

bool IsRuntimeRayTracingFrameCurrent(const RuntimeRayTracingFrame& frame, const GFX::Context& graphics,
                                     RuntimeRayTracingMaterialVersion materials) noexcept;

// Presentation thread owner。View culling と独立し、可視 scene 全体を記録する。
// 材質は既存 registry の slot を借用し、ロード/評価/照明をここへ重複実装しない。
// 返却値は同じ command stream/fence 内の consumer 用。入れられない面は外して種類を記録し、場面全体は止めない。
class RuntimeRayTracingScene final {
public:
    RuntimeRayTracingScene();
    ~RuntimeRayTracingScene();
    RuntimeRayTracingScene(const RuntimeRayTracingScene&) = delete;
    RuntimeRayTracingScene& operator=(const RuntimeRayTracingScene&) = delete;
    // massMaterialSlots は大量配置の種類ごとの材質 slot（MassGpuSceneSource::materialSources の並び）。
    RuntimeRayTracingFrame Prepare(const GFX::Context& graphics, const GPUDRIVEN::GpuDrivenSceneSource& source,
                                   std::span<const uint32_t> materialSlots, std::span<const uint32_t> massMaterialSlots,
                                   std::span<const MATERIAL::GpuMaterialData> materials,
                                   const RuntimeRayTracingBudget& budget,
                                   RuntimeRayTracingMaterialVersion materialVersion = {},
                                   const RuntimeRayTracingSurfaceBindings& surfaces = {});
    void Reset(const GFX::Context& graphics);
    const RuntimeRayTracingStats& GetStats() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace HIKARI::RENDER3D::RAYTRACING
