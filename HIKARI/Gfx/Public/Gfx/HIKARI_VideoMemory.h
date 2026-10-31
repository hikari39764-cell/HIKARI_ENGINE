#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <d3d12.h>

namespace HIKARI::GFX::VIDEO_MEMORY {

// ローカル VRAM の使用量と予算を問い合わせる唯一の場所。
// 毎フレーム Publish すると、登録表（Diagnostics/HIKARI_MetricRegistry.h）の memory.vram.usage|budget と、
// 用途別の memory.vram.<用途> に出る。呼ぶのは主スレッドだけ。
void Publish(ID3D12Device* device, uint64_t frameIndex);
void Shutdown();

// 用途別の記帳。中央の作成関数（テクスチャ・レンダーターゲット・メッシュ・バッファ）だけが Track を呼び、
// 個々の呼び出し側は触らない。中央を通らず pass が直接作った資源は、どの用途にも入らない（usage との差になる）。
enum class Category : uint8_t { Texture, RenderTarget, Geometry, Buffer, Upload, Count };

// 作った資源を記帳に入れる。資源が壊れると自動で引かれる（資源の private data に持たせた印の破棄で）。
// upload / readback ヒープの資源は用途にかかわらず Upload へ入る（ホストから見えるメモリで、ローカル VRAM ではない）。
// どのスレッドからでも呼べる。
void Track(ID3D12Resource* resource, Category category);

struct AdapterInfo {
    std::string name{};
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    uint64_t dedicatedVideoMemoryBytes = 0;
};

// Publish が最後に見たデバイスのアダプター。まだ無い、または取得できなければ nullopt。
std::optional<AdapterInfo> GetAdapterInfo();

} // namespace HIKARI::GFX::VIDEO_MEMORY
