#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <d3d12.h>

namespace HIKARI::GFX::VIDEO_MEMORY {

// ローカル VRAM の使用量と予算を問い合わせる唯一の場所。
// 毎フレーム Publish すると、登録表（Diagnostics/HIKARI_MetricRegistry.h）の memory.vram.usage|budget に出る。
// 呼ぶのは主スレッドだけ。
void Publish(ID3D12Device* device, uint64_t frameIndex);
void Shutdown();

struct AdapterInfo {
    std::string name{};
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    uint64_t dedicatedVideoMemoryBytes = 0;
};

// Publish が最後に見たデバイスのアダプター。まだ無い、または取得できなければ nullopt。
std::optional<AdapterInfo> GetAdapterInfo();

} // namespace HIKARI::GFX::VIDEO_MEMORY
