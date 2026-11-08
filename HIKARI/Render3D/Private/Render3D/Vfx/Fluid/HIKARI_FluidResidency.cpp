#include "Render3D/Vfx/Fluid/HIKARI_FluidResidency.h"

#include <algorithm>
#include <cmath>

#include <d3dx12.h>

#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_VideoMemory.h"
#include "Render3D/Vfx/Fluid/HIKARI_FluidDomainState.h"

namespace HIKARI::RENDER3D::FLUID::INTERNAL {
namespace {
constexpr uint32_t kTileBytes = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
constexpr uint32_t kTilesPerHeap = 256u;
constexpr uint32_t kZeroBytes = 256u * 1024u;
// 要らなくなってから分块を空けるまでの住まいの更新の回（約 0.5 秒）。烟が出入りする境目で貸し借りを繰り返さない。
constexpr uint64_t kKeepUpdates = 30u;

uint32_t TexelBytes(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_R16_FLOAT: return 2u;
    case DXGI_FORMAT_R16G16_FLOAT: return 4u;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return 8u;
    default: return 16u;
    }
}

FluidVolume* FamilyVolumes(FluidDomainResources& resources, size_t family) noexcept {
    return family == 0u ? resources.velocity.data() : family == 1u ? resources.density.data() : resources.fuel.data();
}

// 分块の座標（各辺の分块の番号）。
D3D12_TILED_RESOURCE_COORDINATE TileCoordinate(const FluidResidentFamily& family, uint32_t tile) noexcept {
    D3D12_TILED_RESOURCE_COORDINATE coordinate{};
    coordinate.X = tile % family.tiles[0];
    coordinate.Y = (tile / family.tiles[0]) % family.tiles[1];
    coordinate.Z = tile / (family.tiles[0] * family.tiles[1]);
    coordinate.Subresource = 0u;
    return coordinate;
}

void SetState(ID3D12GraphicsCommandList* commands, FluidVolume& volume, D3D12_RESOURCE_STATES state) {
    if (volume.state == state)
        return;
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(volume.resource.Get(), volume.state, state);
    commands->ResourceBarrier(1, &barrier);
    volume.state = state;
}
// 砖块の要否：占有を margin 砖块ずつ広げ、球（域の局所の中心 [m] と半径 [m]、源と注入の届く範囲）に掛かる砖块を足す。
struct ReachSphere {
    float center[3]{};
    float radius = 0.0f;
};
void MarkFluidNeededBricks(const uint32_t (&cells)[3], float cellSize, std::span<const uint8_t> occupied,
                           uint32_t margin, std::span<const ReachSphere> spheres, std::vector<uint8_t>& needed) {
    const int bricks[3] = {static_cast<int>((cells[0] + 7u) / 8u), static_cast<int>((cells[1] + 7u) / 8u),
                           static_cast<int>((cells[2] + 7u) / 8u)};
    const size_t total = size_t(bricks[0]) * bricks[1] * bricks[2];
    needed.assign(total, 0u);
    if (occupied.size() == total)
        std::copy(occupied.begin(), occupied.end(), needed.begin());
    // 占有を軸ごとに margin 砖块ずつ広げる（分離した 1 次元の最大値を 3 回）。
    std::vector<uint8_t> scratch(total);
    const int strides[3] = {1, bricks[0], bricks[0] * bricks[1]};
    for (int axis = 0; axis < 3 && margin > 0u; ++axis) {
        scratch = needed;
        for (int z = 0; z < bricks[2]; ++z)
            for (int y = 0; y < bricks[1]; ++y)
                for (int x = 0; x < bricks[0]; ++x) {
                    const int position[3] = {x, y, z};
                    const size_t at = size_t(x) + size_t(strides[1]) * y + size_t(strides[2]) * z;
                    if (scratch[at] == 0u)
                        continue;
                    const int low = (std::max)(position[axis] - static_cast<int>(margin), 0);
                    const int high = (std::min)(position[axis] + static_cast<int>(margin), bricks[axis] - 1);
                    for (int step = low; step <= high; ++step)
                        needed[at + size_t(strides[axis]) * (step - position[axis])] = 1u;
                }
    }
    // 源と押すものの届く範囲（域の局所、格子の中心が原点）。
    for (const ReachSphere& sphere : spheres) {
        int low[3]{};
        int high[3]{};
        for (int axis = 0; axis < 3; ++axis) {
            const float grid = sphere.center[axis] / cellSize + 0.5f * static_cast<float>(cells[axis]);
            const float reach = sphere.radius / cellSize;
            low[axis] = (std::max)(static_cast<int>(std::floor((grid - reach) / 8.0f)), 0);
            high[axis] = (std::min)(static_cast<int>(std::floor((grid + reach) / 8.0f)), bricks[axis] - 1);
        }
        for (int z = low[2]; z <= high[2]; ++z)
            for (int y = low[1]; y <= high[1]; ++y)
                for (int x = low[0]; x <= high[0]; ++x)
                    needed[size_t(x) + size_t(strides[1]) * y + size_t(strides[2]) * z] = 1u;
    }
}

// needed（砖块ごとに 0 か 1）の砖块に掛かる分块を住まわせ、続けて要らなかった分块を空ける。
void UpdateFluidResidency(const GFX::Context& context, FluidTilePool& pool, FluidDomainResources& resources,
                          std::span<const uint8_t> needed, ID3D12Resource* zero) {
    FluidResidency& residency = resources.residency;
    if (!residency.sparse || context.queue == nullptr)
        return;
    ++residency.updates;
    const uint32_t* cells = resources.layout.cells;
    const uint32_t bricks[3] = {(cells[0] + 7u) / 8u, (cells[1] + 7u) / 8u, (cells[2] + 7u) / 8u};
    std::vector<uint32_t> mapTiles;
    std::vector<uint32_t> unmapTiles;
    std::vector<D3D12_TILED_RESOURCE_COORDINATE> coordinates;
    std::vector<D3D12_TILE_REGION_SIZE> sizes;
    std::vector<UINT> offsets;
    std::vector<UINT> counts;
    std::vector<D3D12_TILE_RANGE_FLAGS> flags;
    uint32_t resident = 0u;
    for (size_t index = 0u; index < residency.families.size(); ++index) {
        FluidResidentFamily& family = residency.families[index];
        if (family.volumes == 0u)
            continue;
        FluidVolume* volumes = FamilyVolumes(resources, index);
        // 速度の体積は格の下側の面を持つので、砖块の格の範囲の 1 つ上の面まで要る。
        const uint32_t extra = index == 0u ? 1u : 0u;
        for (uint32_t z = 0u; z < bricks[2]; ++z)
            for (uint32_t y = 0u; y < bricks[1]; ++y)
                for (uint32_t x = 0u; x < bricks[0]; ++x) {
                    if (needed[x + bricks[0] * (y + bricks[1] * z)] == 0u)
                        continue;
                    const uint32_t brick[3] = {x, y, z};
                    uint32_t low[3]{};
                    uint32_t high[3]{};
                    for (int axis = 0; axis < 3; ++axis) {
                        low[axis] = brick[axis] * 8u / family.tileTexels[axis];
                        high[axis] = (std::min)((brick[axis] * 8u + 7u + extra) / family.tileTexels[axis],
                                                family.tiles[axis] - 1u);
                    }
                    for (uint32_t tz = low[2]; tz <= high[2]; ++tz)
                        for (uint32_t ty = low[1]; ty <= high[1]; ++ty)
                            for (uint32_t tx = low[0]; tx <= high[0]; ++tx)
                                family.lastNeeded[tx + family.tiles[0] * (ty + family.tiles[1] * tz)] =
                                    residency.updates;
                }
        mapTiles.clear();
        unmapTiles.clear();
        for (uint32_t tile = 0u; tile < family.lastNeeded.size(); ++tile) {
            const bool placed = family.placed[size_t{tile} * family.volumes].Valid();
            if (!placed && family.lastNeeded[tile] == residency.updates)
                mapTiles.push_back(tile);
            else if (placed && family.lastNeeded[tile] + kKeepUpdates < residency.updates)
                unmapTiles.push_back(tile);
        }
        // 空ける分块：枚ごとに 1 回の呼び出しで NULL に戻し、貯めへ返す。
        if (!unmapTiles.empty()) {
            coordinates.resize(unmapTiles.size());
            sizes.assign(unmapTiles.size(), D3D12_TILE_REGION_SIZE{1u, FALSE, 0u, 0u, 0u});
            for (size_t item = 0u; item < unmapTiles.size(); ++item)
                coordinates[item] = TileCoordinate(family, unmapTiles[item]);
            const D3D12_TILE_RANGE_FLAGS nullRange = D3D12_TILE_RANGE_FLAG_NULL;
            const UINT rangeCount = static_cast<UINT>(unmapTiles.size());
            for (uint32_t volume = 0u; volume < family.volumes; ++volume) {
                context.queue->UpdateTileMappings(volumes[volume].resource.Get(), rangeCount, coordinates.data(),
                                                  sizes.data(), nullptr, 1u, &nullRange, nullptr, &rangeCount,
                                                  D3D12_TILE_MAPPING_FLAG_NONE);
                for (const uint32_t tile : unmapTiles) {
                    FluidTile& placed = family.placed[size_t{tile} * family.volumes + volume];
                    pool.Free(placed);
                    placed = {};
                }
            }
        }
        // 住まわせる分块：枚ごとに貯めから借り、ヒープごとにまとめて貸し借りを書き、0 で埋める。借りられなかった分块は
        // そのまま（読むと 0 の分块のまま）。
        if (!mapTiles.empty()) {
            const D3D12_RESOURCE_DESC description = volumes[0].resource->GetDesc();
            const uint32_t texelBytes = TexelBytes(description.Format);
            const uint32_t size[3] = {static_cast<uint32_t>(description.Width), description.Height,
                                      description.DepthOrArraySize};
            for (uint32_t volume = 0u; volume < family.volumes; ++volume) {
                FluidVolume& target = volumes[volume];
                std::vector<std::pair<uint32_t, uint32_t>> borrowed; // (tile, heap)
                for (const uint32_t tile : mapTiles) {
                    // 前の回に一部の枚だけ借りられた分块は、借りた枚をそのまま使う。
                    FluidTile& placed = family.placed[size_t{tile} * family.volumes + volume];
                    if (!placed.Valid() && pool.Allocate(context.device, placed))
                        borrowed.emplace_back(tile, placed.heap);
                }
                std::sort(borrowed.begin(), borrowed.end(),
                          [](const auto& left, const auto& right) { return left.second < right.second; });
                for (size_t begin = 0u; begin < borrowed.size();) {
                    size_t end = begin;
                    coordinates.clear();
                    offsets.clear();
                    while (end < borrowed.size() && borrowed[end].second == borrowed[begin].second) {
                        coordinates.push_back(TileCoordinate(family, borrowed[end].first));
                        offsets.push_back(family.placed[size_t{borrowed[end].first} * family.volumes + volume].index);
                        ++end;
                    }
                    const UINT count = static_cast<UINT>(coordinates.size());
                    sizes.assign(count, D3D12_TILE_REGION_SIZE{1u, FALSE, 0u, 0u, 0u});
                    flags.assign(count, D3D12_TILE_RANGE_FLAG_NONE);
                    counts.assign(count, 1u);
                    context.queue->UpdateTileMappings(target.resource.Get(), count, coordinates.data(), sizes.data(),
                                                      pool.Heap(borrowed[begin].second), count, flags.data(),
                                                      offsets.data(), counts.data(), D3D12_TILE_MAPPING_FLAG_NONE);
                    begin = end;
                }
                if (borrowed.empty() || zero == nullptr)
                    continue;
                const D3D12_RESOURCE_STATES before = target.state;
                SetState(context.cmdList, target, D3D12_RESOURCE_STATE_COPY_DEST);
                for (const auto& [tile, heap] : borrowed) {
                    const D3D12_TILED_RESOURCE_COORDINATE origin = TileCoordinate(family, tile);
                    const uint32_t start[3] = {origin.X * family.tileTexels[0], origin.Y * family.tileTexels[1],
                                               origin.Z * family.tileTexels[2]};
                    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
                    footprint.Footprint.Format = description.Format;
                    footprint.Footprint.Width = (std::min)(family.tileTexels[0], size[0] - start[0]);
                    footprint.Footprint.Height = (std::min)(family.tileTexels[1], size[1] - start[1]);
                    footprint.Footprint.Depth = (std::min)(family.tileTexels[2], size[2] - start[2]);
                    footprint.Footprint.RowPitch =
                        (footprint.Footprint.Width * texelBytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) &
                        ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
                    const CD3DX12_TEXTURE_COPY_LOCATION destination(target.resource.Get(), 0u);
                    const CD3DX12_TEXTURE_COPY_LOCATION source(zero, footprint);
                    context.cmdList->CopyTextureRegion(&destination, start[0], start[1], start[2], &source, nullptr);
                }
                SetState(context.cmdList, target,
                         before == D3D12_RESOURCE_STATE_COPY_DEST ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : before);
            }
        }
        for (size_t tile = 0u; tile < family.lastNeeded.size(); ++tile)
            resident += family.placed[tile * family.volumes].Valid() ? family.volumes : 0u;
    }
    residency.residentTiles = resident;
}
} // namespace

bool QueryFluidSparseSupport(ID3D12Device* device) noexcept {
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    return device != nullptr &&
           SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))) &&
           options.TiledResourcesTier >= D3D12_TILED_RESOURCES_TIER_3;
}

bool FluidTilePool::Allocate(ID3D12Device* device, FluidTile& tile) {
    for (uint32_t index = 0u; index < chunks_.size(); ++index) {
        Chunk& chunk = chunks_[index];
        if (chunk.heap && !chunk.free.empty()) {
            tile = {index, chunk.free.back()};
            chunk.free.pop_back();
            return true;
        }
    }
    uint32_t slot = 0u;
    while (slot < chunks_.size() && chunks_[slot].heap)
        ++slot;
    if (slot == chunks_.size())
        chunks_.emplace_back();
    Chunk& chunk = chunks_[slot];
    D3D12_HEAP_DESC description{};
    description.SizeInBytes = uint64_t{kTileBytes} * kTilesPerHeap;
    description.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    description.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    description.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    if (device == nullptr || FAILED(GFX::VIDEO_MEMORY::CreateHeap(device, &description, GFX::VIDEO_MEMORY::Owner::Vfx,
                                                                  IID_PPV_ARGS(chunk.heap.ReleaseAndGetAddressOf()))))
        return false;
    chunk.free.resize(kTilesPerHeap);
    for (uint32_t index = 0u; index < kTilesPerHeap; ++index)
        chunk.free[index] = kTilesPerHeap - 1u - index;
    tile = {slot, chunk.free.back()};
    chunk.free.pop_back();
    return true;
}

void FluidTilePool::Free(const FluidTile& tile) noexcept {
    if (tile.Valid() && tile.heap < chunks_.size())
        chunks_[tile.heap].free.push_back(tile.index);
}

void FluidTilePool::Trim(const GFX::Context& context) {
    for (size_t index = 1u; index < chunks_.size(); ++index) {
        Chunk& chunk = chunks_[index];
        if (chunk.heap && chunk.free.size() == kTilesPerHeap) {
            GFX::RetireD3D12ObjectForFrame(chunk.heap, context, "FluidTilePool.Heap");
            chunk.free.clear();
        }
    }
}

void FluidTilePool::Reset(const GFX::Context& context) {
    for (Chunk& chunk : chunks_)
        if (chunk.heap)
            GFX::RetireD3D12ObjectForFrame(chunk.heap, context, "FluidTilePool.Heap");
    chunks_.clear();
    if (zero_)
        GFX::RetireD3D12ObjectForFrame(zero_, context, "FluidTilePool.Zero");
}

ID3D12Heap* FluidTilePool::Heap(uint32_t index) const noexcept {
    return index < chunks_.size() ? chunks_[index].heap.Get() : nullptr;
}

ID3D12Resource* FluidTilePool::Zero(ID3D12Device* device) {
    if (!zero_ && !CreateFluidBuffer(device, D3D12_HEAP_TYPE_DEFAULT, kZeroBytes, D3D12_RESOURCE_STATE_COMMON, zero_))
        zero_.Reset();
    return zero_.Get();
}

void InitializeFluidResidency(ID3D12Device* device, FluidDomainResources& resources) {
    FluidResidency& residency = resources.residency;
    const size_t counts[3] = {resources.velocity.size(), resources.density.size(),
                              resources.layout.combustion ? resources.fuel.size() : 0u};
    for (size_t index = 0u; index < residency.families.size(); ++index) {
        FluidResidentFamily& family = residency.families[index];
        family = {};
        FluidVolume* volumes = FamilyVolumes(resources, index);
        if (!residency.sparse || counts[index] == 0u || !volumes[0].resource)
            continue;
        UINT tileCount = 0u;
        D3D12_PACKED_MIP_INFO packed{};
        D3D12_TILE_SHAPE shape{};
        UINT subresources = 1u;
        D3D12_SUBRESOURCE_TILING tiling{};
        device->GetResourceTiling(volumes[0].resource.Get(), &tileCount, &packed, &shape, &subresources, 0u, &tiling);
        family.tileTexels[0] = shape.WidthInTexels;
        family.tileTexels[1] = shape.HeightInTexels;
        family.tileTexels[2] = shape.DepthInTexels;
        family.tiles[0] = tiling.WidthInTiles;
        family.tiles[1] = tiling.HeightInTiles;
        family.tiles[2] = tiling.DepthInTiles;
        family.volumes = static_cast<uint32_t>(counts[index]);
        const size_t tiles = size_t{family.tiles[0]} * family.tiles[1] * family.tiles[2];
        family.lastNeeded.assign(tiles, 0u);
        family.placed.assign(tiles * family.volumes, FluidTile{});
    }
    residency.updates = 0u;
    residency.residentTiles = 0u;
}

void UpdateFluidDomainResidency(const GFX::Context& context, FluidTilePool& pool, FluidDomainState& domain,
                                std::span<const FluidSourceSubmission> sources, std::span<const SourceMotion> motion,
                                bool everything) {
    FluidDomainResources& resources = domain.resources;
    if (!resources.residency.sparse || context.queue == nullptr)
        return;
    ID3D12Resource* zero = pool.Zero(context.device);
    if (zero == nullptr)
        return;
    const FluidGridLayout& layout = resources.layout;
    std::vector<ReachSphere> spheres;
    const auto add = [&](const MATH::Vec3& center, float radius) {
        spheres.push_back({{center.x, center.y, center.z}, radius});
    };
    everything = everything || resources.injection != nullptr;
    for (size_t index = 0u; index < sources.size() && !everything; ++index) {
        const FluidSourceSubmission& source = sources[index];
        const SourceTrack* track =
            motion[index].track >= 0 ? &domain.tracks[static_cast<size_t>(motion[index].track)] : nullptr;
        const float burst = track != nullptr ? track->pendingBurst : source.burstDensity;
        const FluidSourceSettings& feed = source.settings;
        if (source.ratePerSecond <= 0.0f && burst <= 0.0f && feed.outflow == 0.0f && feed.radialSpeed == 0.0f &&
            MATH::Length(feed.velocity) <= 0.0f)
            continue;
        const float extent = FluidSourceEmitReach(source, layout.cellSize);
        if (!std::isfinite(extent))
            continue;
        const MATH::Vec3 begin = track != nullptr ? track->position : motion[index].position;
        add((begin + motion[index].position) * 0.5f, extent + 0.5f * MATH::Length(motion[index].position - begin));
    }
    for (const CarriedInteraction& event : domain.carried)
        if (event.interaction.kind == FluidInteractionKind::Emit)
            add(RotateToFluidLocal(domain.frameRotation, event.interaction.worldPosition - domain.framePosition),
                event.interaction.extents.x + MATH::Length(event.interaction.displacement) + 2.0f * layout.cellSize);
    std::vector<uint8_t> needed;
    if (everything || domain.activity.size() != resources.BrickCount())
        needed.assign(resources.BrickCount(), 1u);
    else
        MarkFluidNeededBricks(layout.cells, layout.cellSize, domain.activity, 1u, spheres, needed);
    UpdateFluidResidency(context, pool, resources, needed, zero);
    resources.bytes = resources.denseBytes + uint64_t{resources.residency.residentTiles} * kTileBytes;
}

void ReleaseFluidResidency(FluidTilePool& pool, FluidDomainResources& resources) noexcept {
    for (FluidResidentFamily& family : resources.residency.families) {
        for (FluidTile& tile : family.placed) {
            pool.Free(tile);
            tile = {};
        }
    }
    resources.residency.residentTiles = 0u;
}

} // namespace HIKARI::RENDER3D::FLUID::INTERNAL
