#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Vfx/HIKARI_FluidTypes.h"

// 流体の体積の疎な記憶（D3D12 の Volume Tiled Resources。NVIDIA Flow 1 と同じ考え方）：速度・密度・燃料の体積は予約資源で
// 作り、烟のある辺りの 64 KiB の分块にだけ物理の記憶を貸す。読むと 0、書くと捨てられる分块は烟の無い所なので、解算と描画の
// 着色器はそのまま。大きな場地の域（浮岛の烟壳）の記憶が烟の範囲に比例する。圧力と光照体積は密なまま。
namespace HIKARI::RENDER3D::FLUID::INTERNAL {

struct FluidDomainResources;
struct FluidDomainState;
struct SourceMotion;

// 予約資源の 3D の分块を使えるか（Tiled Resources Tier 3）。使えない装置は密な体積のまま。
bool QueryFluidSparseSupport(ID3D12Device* device) noexcept;

// 64 KiB の分块 1 つの置き場（貸し出しのヒープの番号と、その中の分块の番号）。
struct FluidTile {
    uint32_t heap = UINT32_MAX;
    uint32_t index = 0u;
    bool Valid() const noexcept { return heap != UINT32_MAX; }
};

// 流体の体積へ貸す分块の貯め：16 MiB のヒープを足していき、前から順に貸す（後ろのヒープほど空きやすい）。全部空いた
// ヒープは退役 fence を待って返す（最初の 1 つは残す）。新しいヒープは記帳の Vfx に入る。新しく貸した分块を 0 で埋める
// 写し元の緩衝（256 KiB：どの形式の 64 KiB の分块 1 つの写しより大きい。R16 の 32³ で行 256 byte）も持つ。
class FluidTilePool final {
public:
    bool Allocate(ID3D12Device* device, FluidTile& tile);
    void Free(const FluidTile& tile) noexcept;
    void Trim(const GFX::Context& context);
    void Reset(const GFX::Context& context);
    ID3D12Heap* Heap(uint32_t index) const noexcept;
    ID3D12Resource* Zero(ID3D12Device* device);

private:
    struct Chunk {
        Microsoft::WRL::ComPtr<ID3D12Heap> heap{};
        std::vector<uint32_t> free{};
    };
    std::vector<Chunk> chunks_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> zero_{};
};

// 同じ形の予約資源の組（速度 3 枚、密度 3 枚、燃料 2 枚）の分块の住まい。分块は組の全部の枚に同時に貸す。
struct FluidResidentFamily {
    uint32_t tileTexels[3]{};
    uint32_t tiles[3]{};
    uint32_t volumes = 0u;
    // 分块ごとに最後に要った住まいの更新の回と、枚ごとの置き場（分块 × 枚）。
    std::vector<uint64_t> lastNeeded{};
    std::vector<FluidTile> placed{};
};

struct FluidResidency {
    bool sparse = false;
    std::array<FluidResidentFamily, 3> families{};
    uint64_t updates = 0u;
    uint32_t residentTiles = 0u;
};

// 予約資源の組の分块の形を読み、住まいを空にする（作った直後）。
void InitializeFluidResidency(ID3D12Device* device, FluidDomainResources& resources);

// 疎な域の住まいの更新（子步を記録する前）。要る砖块は、回読の活動（烟とその 26 近傍、源・注入・粒子の注入が動かした
// 砖块）を 1 砖块広げたもの（回読の数フレームの遅れの間に烟が広がる分）と、このフレームに烟を入れる源の形と注入の届く範囲
// （子步の間に掃く線分ごと）。形状引导の帯と力場は烟を作らないので数えない。域全体の形の源は GPU の活動と同じく今ある烟の
// 所にだけ効くので数えない。everything（預熱）と粒子から注入を受ける域（注入の位置は回読まで分からない）は全部。
// 要る砖块に掛かる分块を住まわせて 0 で埋め（貸した記憶は前の持ち主の値のまま）、30 回（約 0.5 秒）続けて要らなかった
// 分块を空ける。分块の貸し借りは命令の列より前に走る（queue の UpdateTileMappings）。resources.bytes を住んでいる量にする。
void UpdateFluidDomainResidency(const GFX::Context& context, FluidTilePool& pool, FluidDomainState& domain,
                                std::span<const FluidSourceSubmission> sources, std::span<const SourceMotion> motion,
                                bool everything);

// 域を捨てる時：貸した分块を貯めへ返す（資源と一緒に貸し借りも消える）。
void ReleaseFluidResidency(FluidTilePool& pool, FluidDomainResources& resources) noexcept;

} // namespace HIKARI::RENDER3D::FLUID::INTERNAL
