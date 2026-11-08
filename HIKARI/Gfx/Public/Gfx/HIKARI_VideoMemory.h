#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <d3d12.h>

namespace HIKARI::GFX::VIDEO_MEMORY {

// ローカル VRAM の使用量と予算を問い合わせる唯一の場所。
// 毎フレーム Publish すると、登録表（Diagnostics/HIKARI_MetricRegistry.h）の memory.vram.usage|budget と、
// 所有者別の memory.vram.<所有者>、記帳できなかった分の memory.vram.unattributed に出る。呼ぶのは主スレッドだけ。
void Publish(ID3D12Device* device, uint64_t frameIndex);
void Shutdown();

// 所有者（サブシステム）別の記帳。資源は CreateCommittedResource を通して作る。作成と同時に所有者へ記帳され、
// 資源が壊れると自動で引かれる（資源の private data に持たせた印の破棄で）。呼び出し側が Track を書く必要はない。
// 所有者の表は HIKARI_VideoMemory.cpp の kOwners に 1 つだけ。「誰が作り、誰が解放するか」で決める。
// upload / readback ヒープの資源は所有者にかかわらず Upload へ入る（ホストから見えるメモリで、ローカル VRAM ではない）。
// どのスレッドからでも呼べる。
enum class Owner : uint8_t {
    Textures,
    SceneTargets,
    Post,
    Shadow,
    Lighting,
    ScreenSpace,
    Geometry,
    GpuDriven,
    Vfx,
    Ui,
    Editor,
    Diagnostics,
    Upload,
    Count
};

// ID3D12Device::CreateCommittedResource と同じ引数に所有者を足したもの。成功した資源だけを記帳する。
HRESULT CreateCommittedResource(ID3D12Device* device, const D3D12_HEAP_PROPERTIES* heapProperties,
                                D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC* desc,
                                D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, Owner owner,
                                REFIID riid, void** resource);

// ID3D12Device::CreateHeap と同じ引数に所有者を足したもの（予約資源へ分块を貸すヒープ）。成功したヒープの大きさを記帳する
// （ヒープが壊れると自動で引かれる）。
HRESULT CreateHeap(ID3D12Device* device, const D3D12_HEAP_DESC* desc, Owner owner, REFIID riid, void** heap);

// 外部ライブラリ（DirectXTex の CreateTexture など）が作った資源だけをここで記帳に入れる。自前で作る資源は
// CreateCommittedResource を使う。
void Track(ID3D12Resource* resource, Owner owner);

struct AdapterInfo {
    std::string name{};
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    uint64_t dedicatedVideoMemoryBytes = 0;
};

// Publish が最後に見たデバイスのアダプター。まだ無い、または取得できなければ nullopt。
std::optional<AdapterInfo> GetAdapterInfo();

} // namespace HIKARI::GFX::VIDEO_MEMORY
