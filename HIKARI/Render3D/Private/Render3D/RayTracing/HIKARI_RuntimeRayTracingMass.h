#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_FencedUploadBuffers.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Runtime/HIKARI_MassRenderProxy.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {
struct MassGpuSceneSource;
}

namespace HIKARI::RENDER3D::RAYTRACING {
// 大量配置（草や岩の散布）を光追の場面へ並べる。種類ごとに形の BLAS と hit 記録を一つ持ち、個体はそれを共有して
// 個体ごとに instance を置く（形の塊ごとに一つ）。組み立てでは CPU が記述を書き、配置だけが動いたフレームは
// compute が描画の常駐記録から記述を GPU 上で書く（CPU は upload しない）。compute が使えない時だけ CPU で書き直す。
class RuntimeRayTracingMass final {
public:
    // 種類ごとに解決した形の塊（BLAS の位置と、その塊の hit 記録の先頭）。入れない種類は空。
    using Shape = std::vector<std::pair<D3D12_GPU_VIRTUAL_ADDRESS, uint32_t>>;

    ~RuntimeRayTracingMass();
    // 場面の組み直しの始めに、今の大量配置の版と材質 slot を覚え、前の並びを捨てる。
    void Begin(const GPUDRIVEN::MassGpuSceneSource* mass, std::span<const uint32_t> materialSlots);
    // 個体の instance を instances の末尾へ足して並びを覚える。maximumInstances を超えるなら足さずに false。
    bool Append(const GPUDRIVEN::MassGpuSceneSource& mass, std::span<const Shape> shapes, uint32_t maximumInstances,
                std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instances);
    // Append した並びを捨てる（加速構造が組めず大量配置だけを外す時）。版は覚えたまま。
    void Drop();
    bool Included() const noexcept { return !stores_.empty(); }
    // 最上位の instance 記述の中で大量配置が始まる位置（その前は CPU が書く他の面）。
    uint32_t FirstInstance() const noexcept { return firstInstance_; }
    // 前回組んだ時と同じ並び（種類・個体の数・材質）か。違えば場面全体を組み直す。
    bool Matches(const GPUDRIVEN::MassGpuSceneSource* mass, std::span<const uint32_t> materialSlots) const;
    bool Moved(const GPUDRIVEN::MassGpuSceneSource* mass) const;
    // CPU で書く時：前回から動いた個体の変換を instances へ書き直す。書いたら true。並びと合わなければ nullopt。
    std::optional<bool> Update(const GPUDRIVEN::MassGpuSceneSource* mass,
                               std::span<D3D12_RAYTRACING_INSTANCE_DESC> instances);
    // GPU で書けるか（compute の shader が使える）。
    bool CanDispatch(const GFX::Context& graphics);
    // GPU で書く時：records（描画の常駐記録、recordCount 個）から全個体の記述を instances（UAV 状態の最上位の記述）へ
    // 書き、版を進める。並びと合わなければ false。
    bool Dispatch(const GFX::Context& graphics, const GPUDRIVEN::MassGpuSceneSource& mass,
                  D3D12_GPU_VIRTUAL_ADDRESS records, D3D12_GPU_VIRTUAL_ADDRESS instances);
    void Reset(const GFX::Context& graphics);

private:
    // 種類ごとの並び（shader の MassInstanceStore と同じ 32 byte）。個体 firstRecord.. の塊 c は最上位の
    // firstInstance + 個体の番号 × chunks + c。
    struct Store {
        uint32_t firstRecord = 0, recordCount = 0;
        uint32_t firstInstance = 0, chunks = 0;
        uint32_t firstChunk = 0, padding[3]{};
    };
    // 形の塊（shader の MassInstanceChunk と同じ 16 byte）。
    struct Chunk {
        D3D12_GPU_VIRTUAL_ADDRESS bottomLevel = 0;
        uint32_t hitBase = 0, padding = 0;
    };
    static_assert(sizeof(Store) == 32 && sizeof(Chunk) == 16);
    bool EnsurePipeline(const GFX::Context& graphics);

    const GPUDRIVEN::MassGpuSceneSource* source_ = nullptr;
    bool present_ = false; // Begin の時に個体があったか
    uint64_t worldIdentity_ = 0, layoutVersion_ = 0, sourceVersion_ = 0;
    std::vector<uint32_t> materialSlots_;
    std::vector<Store> stores_;
    std::vector<Chunk> chunks_;
    uint32_t firstInstance_ = 0;
    std::vector<RUNTIME::MassRenderDirtyRange> dirty_; // Update の作業領域
    // GPU で書く時の並びの表（stores_ の後ろに chunks_）。並びを組み直した時だけ書く。
    GFX::FencedUploadBuffers table_{GFX::UPLOAD_TRAFFIC::Owner::RayTracingInstances};
    uint64_t chunkOffset_ = 0;
    bool tableDirty_ = true;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    uint64_t deviceGeneration_ = 0;
    bool pipelineFailed_ = false;
};
} // namespace HIKARI::RENDER3D::RAYTRACING
