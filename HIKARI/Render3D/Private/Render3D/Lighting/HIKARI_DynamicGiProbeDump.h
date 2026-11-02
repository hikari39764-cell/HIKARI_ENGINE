#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"

namespace HIKARI::RENDER3D::DYNAMICGI {

// 動的 GI の probe を一度だけ読み戻して JSON に書く（調査用。編集器の操作 lighting.gi.probes.dump）。
// 更新した図集を読み戻し用の buffer へ写し、GPU がそのフレームを終えた後で解いて書く。各 probe の世界位置
// （格子点 + 再配置）、活性、更新回数、裏面率、隣 26 probe との連通、6 軸方向の E/pi と空の見える割合を出す。
class ProbeDump final {
public:
    void Request(std::filesystem::path path);
    bool Pending() const noexcept { return !requested_.empty() && captures_.empty(); }
    // 更新を終えた図集（シェーダー読み取りの状態）を写す。
    void Record(const GFX::Context& graphics, ID3D12GraphicsCommandList* commands, size_t field, ID3D12Resource* atlas,
                const LIGHTPROBE::LightProbeVolumeRuntimeData& data);
    // GPU が写し終えていれば解いて書き、読み戻しの資源を捨てる。
    void Poll(const GFX::Context& graphics);
    void Reset(const GFX::Context& graphics);

private:
    struct Capture {
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        size_t field = 0;
        LIGHTPROBE::LightProbeVolumeRuntimeData data{};
    };
    std::filesystem::path requested_;
    std::vector<Capture> captures_;
    uint64_t fence_ = 0;
};

} // namespace HIKARI::RENDER3D::DYNAMICGI
