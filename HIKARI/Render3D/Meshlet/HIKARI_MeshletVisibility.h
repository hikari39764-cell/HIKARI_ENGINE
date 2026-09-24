#pragma once

#include <array>
#include <cstdint>
#include <wrl/client.h>
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Render3D/Depth/HIKARI_DepthPyramidLayer.h"

namespace HIKARI::RENDER3D::MESHLET {
struct MeshletOcclusionGpuData {
    MATH::Mat4 viewProj{};
    uint32_t control = 0;
    uint32_t padding[3]{};
};
static_assert(sizeof(MeshletOcclusionGpuData) == 80);

bool IsMeshletOcclusionViewUsable(const DEPTH::DepthPyramidView& depth, const MATH::Mat4& renderViewProj,
                                  const MATH::Mat4& cullingViewProj, bool frozen);

struct MeshletVisibilityCounters {
    uint32_t candidates = 0, frustumCulled = 0, coneTested = 0, coneCulled = 0;
    uint32_t hzbTested = 0, hzbCulled = 0, unbounded = 0, submitted = 0, invalid = 0;
    uint32_t padding[7]{};
};
static_assert(sizeof(MeshletVisibilityCounters) == 64);

struct MeshletVisibilitySample {
    MeshletVisibilityCounters counters{};
    uint64_t retireFence = 0;
    bool valid = false;
};
struct MeshletVisibilityStats {
    // 0: GeometryAux、1: Forward。CS 候補数とは別の AS 実測値。
    std::array<MeshletVisibilitySample, 2> samples{};
    bool forwardHzbBound = false;
    uint32_t width = 0, height = 0, mipCount = 0;
};
struct MeshletVisibilityBinding {
    D3D12_GPU_DESCRIPTOR_HANDLE table{};
    uint32_t control = 0; // low 8 bits: mip count; bit 31: sampled counters
};

// AS 入力 descriptor と診断の GPU lifetime を所有。可視性判定は shader が所有する。
// DEFAULT counter + fence 完了後の READBACK。UPLOAD はゼロクリアと view ごとの小さな query 定数のみ。
class MeshletVisibility final {
public:
    MeshletVisibilityBinding Begin(const GFX::Context& graphics, const DEPTH::DepthPyramidView* depth, uint32_t phase);
    void End(const GFX::Context& graphics);
    void ResetFrame() {
        stats_.forwardHzbBound = false;
        stats_.width = stats_.height = stats_.mipCount = 0;
    }
    void Reset();
    const MeshletVisibilityStats& GetStats() const { return stats_; }

private:
    static constexpr uint32_t kMaxViewsPerFrame = 8;
    struct Entry {
        std::array<RenderResourceView, 3> views{};
        uint32_t phase = 2;
        bool sampled = false;
    };
    struct Frame {
        DX::DynamicUploadBuffer queries;
        Microsoft::WRL::ComPtr<ID3D12Resource> counters, readback;
        std::array<Entry, kMaxViewsPerFrame> entries{};
        uint64_t fence = 0;
        uint32_t count = 0;
        bool initialized = false, resolved = false;
    };
    bool EnsureFrame(const GFX::Context& graphics, Frame& frame);
    void Collect(Frame& frame);
    std::array<Frame, GFX::kFrameResourceCount> frames_{};
    std::array<RenderResourceView, 3> nullViews_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> zeroUpload_;
    MeshletVisibilityStats stats_{};
    Frame* active_ = nullptr;
    uint32_t activeIndex_ = 0;
};
} // namespace HIKARI::RENDER3D::MESHLET
