#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#include <d3d12.h>

#include "Render3D/Runtime/HIKARI_MassRenderProxy.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {
struct MassGpuSceneSource;
}

namespace HIKARI::RENDER3D::RAYTRACING {
// 大量配置（草や岩の散布）を光追の場面へ並べる。種類ごとに形の BLAS と hit 記録を一つ持ち、個体はそれを共有して
// 個体ごとに instance を置く（形の塊ごとに一つ）。配置だけが動いたフレームは動いた個体の変換だけを書き直す。
class RuntimeRayTracingMass final {
public:
    // 種類ごとに解決した形の塊（BLAS の位置と、その塊の hit 記録の先頭）。入れない種類は空。
    using Shape = std::vector<std::pair<D3D12_GPU_VIRTUAL_ADDRESS, uint32_t>>;

    // 場面の組み直しの始めに、今の大量配置の版と材質 slot を覚え、前の並びを捨てる。
    void Begin(const GPUDRIVEN::MassGpuSceneSource* mass, std::span<const uint32_t> materialSlots);
    // 個体の instance を instances の末尾へ足して並びを覚える。maximumInstances を超えるなら足さずに false。
    bool Append(const GPUDRIVEN::MassGpuSceneSource& mass, std::span<const Shape> shapes, uint32_t maximumInstances,
                std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instances);
    // Append した並びを捨てる（加速構造が組めず大量配置だけを外す時）。版は覚えたまま。
    void Drop() { stores_.clear(); }
    // 前回組んだ時と同じ並び（種類・個体の数・材質）か。違えば場面全体を組み直す。
    bool Matches(const GPUDRIVEN::MassGpuSceneSource* mass, std::span<const uint32_t> materialSlots) const;
    bool Moved(const GPUDRIVEN::MassGpuSceneSource* mass) const;
    // 前回から動いた個体の変換を instances へ書き直す。書いたら true。並びと合わなければ nullopt（全体を組み直す）。
    std::optional<bool> Update(const GPUDRIVEN::MassGpuSceneSource* mass,
                               std::span<D3D12_RAYTRACING_INSTANCE_DESC> instances);
    void Reset();

private:
    // 種類ごとの並び。個体 firstRecord.. の塊 c は TLAS の firstInstance + 個体の番号 × chunks + c。
    struct Store {
        uint32_t firstRecord = 0, recordCount = 0;
        uint32_t firstInstance = 0, chunks = 0;
    };
    const GPUDRIVEN::MassGpuSceneSource* source_ = nullptr;
    bool present_ = false; // Begin の時に個体があったか
    uint64_t worldIdentity_ = 0, layoutVersion_ = 0, sourceVersion_ = 0;
    std::vector<uint32_t> materialSlots_;
    std::vector<Store> stores_;
    std::vector<RUNTIME::MassRenderDirtyRange> dirty_; // Update の作業領域
};
} // namespace HIKARI::RENDER3D::RAYTRACING
