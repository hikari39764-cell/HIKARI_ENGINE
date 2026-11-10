#pragma once

#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

#include "Gfx/HIKARI_FencedUploadBuffers.h"
#include "Gfx/RayTracing/HIKARI_DxrAccelerationStructure.h"
#include "Render3D/Cluster/HIKARI_ClusterRayTracingGeometry.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingChanges.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingDeformation.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingInputs.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingMass.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingScene.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"

// RuntimeRayTracingScene の内部状態。全体の組み立て（HIKARI_RuntimeRayTracingScene.cpp）と、配置・材質・変形だけが
// 変わったフレームのその場の更新（HIKARI_RuntimeRayTracingSceneUpdate.cpp）が共有する。
namespace HIKARI::RENDER3D::RAYTRACING {
using AccelerationStructure = GFX::RAYTRACING::DxrAccelerationStructure;
namespace INTERNAL {
struct Asset {
    CLUSTER::ClusterRayTracingGeometry geometry;
    Microsoft::WRL::ComPtr<ID3D12Resource> metadata;
    std::vector<CLUSTER::ClusterRayTracingChunk> chunks;
    std::vector<std::unique_ptr<AccelerationStructure>> bottomLevels;
    const ClusterGeometryResourceRecord* source = nullptr;
    ID3D12Resource* sourceGeometry = nullptr;
    uint64_t sourceRevision = 0;
    uint64_t touched = 0;
    uint32_t geometryDescriptor = 0;
    uint32_t metadataDescriptor = 0;
    uint32_t trianglesPerChunk = 0;
    uint32_t rangesPerChunk = 0;
    uint64_t Bytes() const {
        uint64_t bytes = geometry.indexAllocationBytes;
        for (const auto& bottom : bottomLevels)
            bytes += bottom->GetAllocationBytes();
        return bytes;
    }
    void Reset(const GFX::Context& graphics) {
        for (auto& bottom : bottomLevels)
            bottom->Reset(&graphics);
        bottomLevels.clear();
        geometry.Reset(graphics);
        GFX::RetireD3D12ObjectForFrame(metadata, graphics, "RT.Scene.Metadata");
    }
};
// 変形する面。物体と形状ごとに BLAS を持ち、頂点は compute で world 空間へ変形して refit する。
// 骨格の蒙皮は毎フレーム、地形（高さ場。鍵の 4 番目は地形の記録の番号）は高さ場か配置が変わった時だけ。
using DeformedKey = std::tuple<uint64_t, uint64_t, uint64_t, uint32_t>;
struct Deformed {
    Asset* asset = nullptr;
    std::vector<size_t> records; // 変形の行列と骨格を毎フレーム読み直す表面の番号
    std::vector<std::pair<uint32_t, uint32_t>> surfaces;
    RuntimeRayTracingDeformJob job{};
    std::vector<std::unique_ptr<AccelerationStructure>> bottomLevels;
    const ID3D12Resource* builtIndices = nullptr; // BLAS を組んだ時の添字と pool（変われば組み直す）
    D3D12_GPU_VIRTUAL_ADDRESS builtPool = 0;
    uint64_t touched = 0;
    bool terrain = false;
    bool refresh = true; // このフレーム変形し直す（蒙皮は常に、地形は変化時）
    uint32_t builtOutputBase = 0xffffffffu;
    uint64_t builtFloatVersion = 0;
    std::array<float, 16> builtWorld{};
    uint64_t Bytes() const {
        uint64_t bytes = 0;
        for (const auto& bottom : bottomLevels)
            bytes += bottom->GetAllocationBytes();
        return bytes;
    }
};
inline void WriteTransform(D3D12_RAYTRACING_INSTANCE_DESC& instance, const MATH::Mat4& world) {
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 4; ++col)
            instance.Transform[row][col] = world.m[col][row];
}
inline bool SameMatrix(const MATH::Mat4& a, const MATH::Mat4& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }

// まだ場面へ入れられない面（未解決・組み途中）。残っている間は毎フレーム組み直す。
inline constexpr uint32_t kUnsettledExclusions = RuntimeRayTracingExclusionBit(RuntimeRayTracingExclusion::Missing) |
                                                 RuntimeRayTracingExclusionBit(RuntimeRayTracingExclusion::Pending);
// 表面の前回の入れ方。group は静的な群の番号。変形する面は kDeformedGroup、入れなかった面は kNoGroup。
inline constexpr uint32_t kNoGroup = 0xffffffffu;
inline constexpr uint32_t kDeformedGroup = 0xfffffffeu;
// materialSlot は hit 記録に書いた材質の slot、surfaceIndex は群の中の面の番号（静的な群の面だけ）。
struct RecordUse {
    RayRecordInputs inputs{};
    uint32_t group = kNoGroup;
    uint32_t materialSlot = 0xffffffffu;
    uint32_t surfaceIndex = 0;
};
// 静的な群の前回の行列、lastInstances の中の instance の範囲と lastHits の中の hit 記録の先頭、面ごとの
// （材質 slot, 旗）。同じ物体・形状の群は GroupKey の順で隣り合う。
struct GroupUse {
    uint64_t object = 0, lifetime = 0, assetKey = 0;
    MATH::Mat4 world{};
    uint32_t firstInstance = 0, instanceCount = 0;
    const Asset* asset = nullptr;
    uint32_t firstHit = 0;
    std::vector<std::pair<uint32_t, uint32_t>> surfaces;
};
} // namespace INTERNAL

struct RuntimeRayTracingScene::State {
    struct RetiringBytes {
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
        uint64_t value = 0;
        uint64_t bytes = 0;
    };
    ID3D12Device* device = nullptr;
    uint64_t deviceGeneration = 0;
    bool supportsQueries = false;
    uint64_t serial = 0;
    uint64_t version = 0;
    uint64_t budgetBytes = 0;
    uint64_t workFence = 0;
    uint32_t frameBuilds = 0;
    uint32_t frameUploads = 0;
    const void* sourceIdentity = nullptr;
    uint64_t sourceVersion = 0, layoutVersion = 0, proxyVersion = 0;
    uint32_t sourceSurfaceCount = 0;
    size_t sourceRecordCount = 0, materialSlotCount = 0, materialCount = 0;
    RuntimeRayTracingMaterialVersion materialVersion{};
    std::map<uint64_t, std::unique_ptr<INTERNAL::Asset>> assets;
    CLUSTER::ClusterRayTracingGeometryBuilder converter;
    std::unique_ptr<AccelerationStructure> topLevel;
    // hit 記録。材質の slot だけが替わった時は GPU が読み終えた予備へ書き直す。
    GFX::FencedUploadBuffers hitUploads{GFX::UPLOAD_TRAFFIC::Owner::RayTracingHitRecords};
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> lastInstances;
    std::vector<RuntimeRayTracingHitRecord> lastHits;
    std::vector<RetiringBytes> retiring;
    RuntimeRayTracingStats stats;
    // 最後に組んだ時の入れなかった面。加速構造を使い回すフレームも同じ値を出す。
    RuntimeRayTracingExclusions lastExcluded{};
    uint64_t lightmapVersion = 0; // hit 記録に書いた光照贴图の登録の版
    // この場面が一度欠けなく揃ったか（失敗と場面の入れ替えで戻る）。揃った後に足した面だけを Pending として遅らせる
    // （初めの読み込み中は揃うまで待つ。欠けた場面で GI を始めると光線が抜けて空の光を拾う）。
    bool complete = false;
    std::map<INTERNAL::DeformedKey, INTERNAL::Deformed> deformed;
    bool deformedRefreshed = false; // 直前の DeformAndRefit が頂点を書き直した（TLAS の refit が要る）
    RuntimeRayTracingDeformation deformation;
    RuntimeRayTracingChanges changes;
    RuntimeRayTracingMass mass;
    // 最上位の instance 記述の GPU 側の写し。大量配置が入った場面の配置替えは、他の面の記述（先頭）を CPU から写し、
    // 大量配置の記述を compute が書いて、ここから TLAS を組み直す（大量配置の記述は upload しない）。
    Microsoft::WRL::ComPtr<ID3D12Resource> gpuInstances;
    uint64_t gpuInstanceBytes = 0;
    D3D12_RESOURCE_STATES gpuInstanceState = D3D12_RESOURCE_STATE_COMMON;
    bool gpuPrefixValid = false; // 先頭（大量配置の前）が lastInstances と同じか
    // 大量配置の記述を GPU で書いた（lastInstances の大量配置の分は古い。CPU で書き直すなら全体を組み直す）。
    bool massDescsOnGpu = false;
    GFX::FencedUploadBuffers gpuPrefixUploads{GFX::UPLOAD_TRAFFIC::Owner::RayTracingInstances};
    // 変形の compute が使えない（shader の失敗など）時は蒙皮を外す面に戻す。
    bool deformationBroken = false;
    // 前回の全体の組み立てで決めた表面と群の入れ方。配置だけが動いたフレームはこれで instance の変換だけを直す。
    std::vector<INTERNAL::RecordUse> recordUses;
    std::vector<INTERNAL::GroupUse> groupUses;
    bool terrainBound = false;
    // モジュールの代理の並び：lastInstances と lastHits の中の先頭と、代理ごとの面の数。
    uint32_t proxyFirstInstance = 0, proxyFirstHit = 0;
    std::vector<uint32_t> proxySurfaceCounts;
    // UpdateInPlace の作業領域（毎フレーム確保し直さない）。groupStates は 0: 未確認, 1: 同じ, 2: 動いた。
    // groupRetints は面の材質 slot が替わった群（hit 記録を書き直す）。
    std::vector<MATH::Mat4> movedWorlds;
    std::vector<uint8_t> groupStates;
    std::vector<uint8_t> groupRetints;

    void RetireDeformed(const GFX::Context& graphics, INTERNAL::Deformed& entry);
    // 表面から変形の行列と骨格（地形は高さ場の番号）を読み直し、頂点を変形して BLAS を作るか refit する。
    bool DeformAndRefit(const GFX::Context& graphics, const GPUDRIVEN::GpuDrivenSceneSource& source,
                        const RuntimeRayTracingBudget& budget, const RuntimeRayTracingSurfaceBindings& surfaces);
    void TrackRetirement(const GFX::Context& graphics, uint64_t bytes);
    void RetireTop(const GFX::Context& graphics, bool keepHits = false);
    uint64_t Available(const RuntimeRayTracingBudget& budget);
    RuntimeRayTracingFrame Fail(const GFX::Context& graphics, const RuntimeRayTracingBudget& budget,
                                RuntimeRayTracingStatus status);
    INTERNAL::Asset* ResolveAsset(const GFX::Context& graphics, const RuntimeRayTracingBudget& budget,
                                  ClusterGeometryResourceHandle handle);
    // 加速構造の形（表面の並び・形状の資源・予算）が前回のままか。配置（sourceVersion）と材質の値（版）が
    // 変わったかは見ない。それは UpdateInPlace が表面ごとに確かめる。
    bool SameStructure(const GPUDRIVEN::GpuDrivenSceneSource& source, std::span<const uint32_t> massMaterialSlots,
                       const RuntimeRayTracingBudget& budget, RuntimeRayTracingMaterialVersion materials);
    // 配置か材質だけが変わったフレーム。表面の入れ方が前回と同じなら群をまとめ直さず、動いた群の instance の変換を
    // 書き換えて TLAS をその場で組み直し、材質の slot が替わった群は hit 記録の材質番号だけを書き直す（資源は作り
    // 直さない。材質の値は版だけ進める）。群が割れる・重なる、行列が不正になるなど全体の組み直しと結果が変わる時は
    // nullopt を返し、呼び出し側が全体を組み直す。
    std::optional<RuntimeRayTracingFrame> UpdateInPlace(const GFX::Context& graphics,
                                                        const GPUDRIVEN::GpuDrivenSceneSource& source,
                                                        std::span<const uint32_t> materialSlots,
                                                        std::span<const MATERIAL::GpuMaterialData> materials,
                                                        RuntimeRayTracingMaterialVersion currentMaterials,
                                                        const RuntimeRayTracingBudget& budget,
                                                        const RuntimeRayTracingSurfaceBindings& surfaces);
    // モジュールの代理（地形の瓦片など）が、前回と同じ数・同じ面の数のまま BLAS か面の値だけ変わったか。そうなら
    // UpdateInPlace が RetargetProxies で記述を直して TLAS をその場で組み直す（筆で彫る間は毎回ここを通る）。
    bool CanRetargetProxies(std::span<const RuntimeRayTracingProxy> proxies) const;
    // 代理の instance 記述の BLAS と hit 記録の面の値を書き直す。hit 記録が変わったら true。
    bool RetargetProxies(std::span<const RuntimeRayTracingProxy> proxies);
    // 大量配置が入った場面の配置替え：先頭を（変わっていれば）写し、大量配置の記述を compute で書いて TLAS を組み直す。
    bool RebuildFromGpuInstances(const GFX::Context& graphics, const GPUDRIVEN::GpuDrivenSceneSource& source,
                                 D3D12_GPU_VIRTUAL_ADDRESS massRecords, bool prefixChanged);
    RuntimeRayTracingFrame WithCoverage(RuntimeRayTracingFrame frame) const;
    RuntimeRayTracingFrame Publish(const GFX::Context& graphics, const RuntimeRayTracingBudget& budget);
};
} // namespace HIKARI::RENDER3D::RAYTRACING
