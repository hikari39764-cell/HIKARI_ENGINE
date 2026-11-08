#include "HIKARI_TerrainKitRenderData.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "HIKARI_TerrainKitStore.h"
#include "TerrainKit/Data/HIKARI_TerrainDataControl.h"

namespace HIKARI::TERRAINKIT {
namespace {

using TERRAIN::DATA::TileCoord;

constexpr uint32_t kUploadTilesPerFrame = 32u;
// 光線追跡の代理：瓦片一枚をこの程度の格子にする（256 m なら 4 m 間隔）。色と法線は区画ごとに一つ。
constexpr uint32_t kRayProxyCells = 64u;
constexpr uint32_t kRayProxyRegionCells = 8u;
constexpr uint32_t kRayProxiesPerFrame = 8u;
// 筆で塗っている間に作り直す数（組み直しは一フレームに集めると引っかかる）。
constexpr uint32_t kRayProxyRebuildsPerFrame = 2u;
// 筆で塗っている間の作り直しの間隔。
constexpr uint64_t kRayProxyRebuildFrames = 30u;
// 着色器の材質の表（図 2 行）：0 行 = 繰り返しの長さ、高さ混合の幅、法線の強さ、図があるか。
constexpr uint32_t kSurfaceParamRows = 2u;

float Smoothstep(float edge0, float edge1, float value) noexcept {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// 材質がまだ無い時の色（着色器の TerrainPlaceholderSurface と同じ分け方）。
void PlaceholderAlbedo(float height, float normalY, float outAlbedo[3]) noexcept {
    constexpr float kGrass[3]{0.19f, 0.27f, 0.10f};
    constexpr float kSoil[3]{0.27f, 0.21f, 0.15f};
    constexpr float kRock[3]{0.32f, 0.30f, 0.28f};
    constexpr float kSnow[3]{0.82f, 0.84f, 0.88f};
    const float slope = 1.0f - normalY;
    const float soil = Smoothstep(0.03f, 0.12f, slope);
    const float rock = Smoothstep(0.14f, 0.32f, slope);
    const float snow = Smoothstep(170.0f, 210.0f, height) * (1.0f - Smoothstep(0.2f, 0.45f, slope));
    for (int channel = 0; channel < 3; ++channel) {
        float value = kGrass[channel] + (kSoil[channel] - kGrass[channel]) * soil;
        value += (kRock[channel] - value) * rock;
        value += (kSnow[channel] - value) * snow;
        outAlbedo[channel] = value;
    }
}

void UploadControl(IRenderSurfaceResources& resources, const TerrainRenderResources& render, const TerrainStore& store,
                   TileCoord tile, TileRuntime& runtime, uint32_t samples, uint32_t x0, uint32_t z0, uint32_t x1,
                   uint32_t z1);
void UploadPendingRect(IRenderSurfaceResources& resources, const TerrainRenderResources& render, TileRuntime& runtime,
                       uint32_t samples);
uint32_t CreateRayProxy(IRenderSurfaceResources& resources, const TerrainStore& store, TileCoord tile,
                        const TileRuntime& runtime);

void EnsureResources(IRenderSurfaceResources& resources, TerrainStore& store) {
    TerrainRenderResources& render = store.Render();
    if (render.indexBuffer == 0u) {
        // 頂点番号は (cells + 1) 行の格子。対角線は交互にして、斜面の向きの偏りを減らす。
        std::vector<uint16_t> indices{};
        indices.reserve(static_cast<size_t>(kGridCells) * kGridCells * 6u);
        const uint16_t columns = kGridCells + 1u;
        for (uint16_t z = 0u; z < kGridCells; ++z) {
            for (uint16_t x = 0u; x < kGridCells; ++x) {
                const uint16_t a = static_cast<uint16_t>(z * columns + x);
                const uint16_t b = static_cast<uint16_t>(a + 1u);
                const uint16_t c = static_cast<uint16_t>(a + columns);
                const uint16_t d = static_cast<uint16_t>(c + 1u);
                if (((x ^ z) & 1u) == 0u) {
                    indices.insert(indices.end(), {a, c, b, b, c, d});
                } else {
                    indices.insert(indices.end(), {a, c, d, a, d, b});
                }
            }
        }
        render.indexBuffer = resources.CreateIndexBuffer(indices);
        render.indexCount = static_cast<uint32_t>(indices.size());
    }
    if (render.layoutRevision == store.LayoutRevision() && render.heightTexture != 0u) {
        return;
    }
    if (render.heightTexture != 0u) {
        resources.Release(render.heightTexture);
    }
    if (render.normalTexture != 0u) {
        resources.Release(render.normalTexture);
    }
    if (render.controlTexture != 0u) {
        resources.Release(render.controlTexture);
    }
    if (render.farCache != 0u) {
        resources.Release(render.farCache);
        render.farUploaded = 0u;
    }
    for (const auto& [tile, proxy] : render.rayProxies) {
        resources.Release(proxy.resource);
    }
    render.rayProxies.clear();
    const uint32_t samples = TERRAIN::DATA::TileSamples(store.Settings().grid);
    const uint32_t layers = (std::max)(static_cast<uint32_t>(store.Tiles().size()), 1u);
    render.heightTexture =
        resources.CreateTextureArray({samples, samples, layers, 1u, RenderSurfaceTextureFormat::R16Unorm});
    render.normalTexture =
        resources.CreateTextureArray({samples, samples, layers, 5u, RenderSurfaceTextureFormat::R16G16Snorm});
    render.controlTexture =
        resources.CreateTextureArray({samples, samples, layers, 1u, RenderSurfaceTextureFormat::R32Uint});
    render.farCache = resources.CreateTextureArray(
        {kFarCacheSize, kFarCacheSize, layers, kFarCacheMips, RenderSurfaceTextureFormat::R8G8B8A8Unorm});
    render.layoutRevision = store.LayoutRevision();
    store.MarkAllForUpload();
    store.RuleBaker().MarkAllDirty(store);
}

void UploadPendingTiles(IRenderSurfaceResources& resources, TerrainStore& store) {
    const TerrainRenderResources& render = store.Render();
    if (render.heightTexture == 0u || render.normalTexture == 0u) {
        return;
    }
    const uint32_t samples = TERRAIN::DATA::TileSamples(store.Settings().grid);
    uint32_t uploaded = 0u;
    for (auto& [tile, runtime] : store.MutableTiles()) {
        if (!runtime.uploadPending && runtime.rectPending) {
            UploadPendingRect(resources, render, runtime, samples);
        }
        if (!runtime.uploadPending && runtime.controlRectPending) {
            UploadControl(resources, render, store, tile, runtime, samples, runtime.controlX0, runtime.controlZ0,
                          runtime.controlX1, runtime.controlZ1);
        }
        if (!runtime.uploadPending || runtime.heights.empty()) {
            continue;
        }
        RenderSurfaceTextureUpload height{};
        height.layer = runtime.layer;
        height.width = samples;
        height.height = samples;
        height.rowPitchBytes = samples * sizeof(uint16_t);
        height.pixels = runtime.heights.data();
        (void)resources.UploadTexture(render.heightTexture, height);
        size_t offset = 0u;
        uint32_t size = samples;
        for (uint32_t mip = 0u; mip < 5u; ++mip) {
            RenderSurfaceTextureUpload normal{};
            normal.layer = runtime.layer;
            normal.mip = mip;
            normal.width = size;
            normal.height = size;
            normal.rowPitchBytes = size * 2u * sizeof(int16_t);
            normal.pixels = runtime.normalMips.data() + offset;
            (void)resources.UploadTexture(render.normalTexture, normal);
            offset += static_cast<size_t>(size) * size * 2u;
            size = (std::max)(size >> 1u, 1u);
        }
        UploadControl(resources, render, store, tile, runtime, samples, 0u, 0u, samples - 1u, samples - 1u);
        runtime.uploadPending = false;
        runtime.rectPending = false;
        if (++uploaded >= kUploadTilesPerFrame) {
            break;
        }
    }
}

// 瓦片の制御図の矩形を送る。塗っていない瓦片は 0（材質 0）で埋める。
void UploadControl(IRenderSurfaceResources& resources, const TerrainRenderResources& render, const TerrainStore& store,
                   TileCoord tile, TileRuntime& runtime, uint32_t samples, uint32_t x0, uint32_t z0, uint32_t x1,
                   uint32_t z1) {
    runtime.controlRectPending = false;
    if (render.controlTexture == 0u) {
        return;
    }
    static const std::vector<uint32_t> empty(static_cast<size_t>(513u) * 513u, 0u);
    const auto* raster = store.Data().Control().Find(tile);
    const uint32_t* source = raster != nullptr ? raster->values.data() : empty.data();
    RenderSurfaceTextureUpload upload{};
    upload.layer = runtime.layer;
    upload.x = x0;
    upload.y = z0;
    upload.width = x1 - x0 + 1u;
    upload.height = z1 - z0 + 1u;
    upload.rowPitchBytes = (raster != nullptr ? samples : 513u) * sizeof(uint32_t);
    upload.pixels = raster != nullptr ? source + static_cast<size_t>(z0) * samples + x0 : source;
    (void)resources.UploadTexture(render.controlTexture, upload);
}

// 背景で計算した遠景の色を送る。
void UploadFarColors(IRenderSurfaceResources& resources, TerrainStore& store) {
    TerrainRenderResources& render = store.Render();
    if (render.farCache == 0u) {
        return;
    }
    for (auto& [tile, runtime] : store.MutableTiles()) {
        if (!runtime.farPending || runtime.farColor.empty()) {
            continue;
        }
        size_t offset = 0u;
        uint32_t size = kFarCacheSize;
        for (uint32_t mip = 0u; mip < kFarCacheMips; ++mip) {
            RenderSurfaceTextureUpload upload{};
            upload.layer = runtime.layer;
            upload.mip = mip;
            upload.width = size;
            upload.height = size;
            upload.rowPitchBytes = size * 4u;
            upload.pixels = runtime.farColor.data() + offset;
            (void)resources.UploadTexture(render.farCache, upload);
            offset += static_cast<size_t>(size) * size * 4u;
            size = (std::max)(size >> 1u, 1u);
        }
        if (!runtime.farUploaded) {
            runtime.farUploaded = true;
            ++render.farUploaded;
        }
        runtime.farPending = false;
    }
}

// 材質の図が揃ったら配列へ送る。一フレームに数枠ずつ（1 枠で約 2.7 MB）。
void UpdateSurfaces(IRenderSurfaceResources& resources, TerrainStore& store) {
    TerrainSurfaceLibrary& library = store.Surfaces();
    (void)library.Poll();
    TerrainRenderResources& render = store.Render();
    const auto& surfaces = library.Surfaces();
    if (render.surfaceRevision != library.Revision()) {
        for (uint32_t* texture : {&render.surfaceColor, &render.surfaceDetail, &render.surfaceParams}) {
            if (*texture != 0u) {
                resources.Release(*texture);
                *texture = 0u;
            }
        }
        render.surfacesUploaded = 0u;
        render.surfaceRevision = library.Revision();
        store.RuleBaker().MarkAllDirty(store); // 遠景の色は材質の平均色から作る
        const auto layers = static_cast<uint32_t>(surfaces.size());
        if (layers == 0u) {
            return;
        }
        render.surfaceColor =
            resources.CreateTextureArray({kSurfaceTextureSize, kSurfaceTextureSize, layers, kSurfaceTextureMips,
                                          RenderSurfaceTextureFormat::Bc7UnormSrgb});
        render.surfaceDetail =
            resources.CreateTextureArray({kSurfaceTextureSize, kSurfaceTextureSize, layers, kSurfaceTextureMips,
                                          RenderSurfaceTextureFormat::Bc7Unorm});
        render.surfaceParams = resources.CreateTextureArray(
            {layers, kSurfaceParamRows, 1u, 1u, RenderSurfaceTextureFormat::R32G32B32A32Float});
        std::vector<float> params(static_cast<size_t>(layers) * kSurfaceParamRows * 4u, 0.0f);
        for (uint32_t index = 0u; index < layers; ++index) {
            const TerrainSurfaceAsset& asset = surfaces[index].asset;
            float* row = params.data() + static_cast<size_t>(index) * 4u;
            row[0] = asset.tilingMeters;
            row[1] = asset.heightContrast;
            row[2] = asset.normalStrength;
            row[3] = surfaces[index].colorHeight.empty() ? 0.0f : 1.0f;
        }
        RenderSurfaceTextureUpload upload{};
        upload.width = layers;
        upload.height = kSurfaceParamRows;
        upload.rowPitchBytes = layers * 4u * sizeof(float);
        upload.pixels = params.data();
        (void)resources.UploadTexture(render.surfaceParams, upload);
    }
    constexpr uint32_t kSurfacesPerFrame = 2u;
    for (uint32_t count = 0u; count < kSurfacesPerFrame && render.surfacesUploaded < surfaces.size(); ++count) {
        const CookedSurface& surface = surfaces[render.surfacesUploaded];
        const auto uploadMips = [&](uint32_t texture, const std::vector<uint8_t>& bytes) {
            if (bytes.empty()) {
                return; // 焼けなかった材質は黒ではなく表の値で描く（着色器が見る）
            }
            size_t offset = 0u;
            for (uint32_t mip = 0u; mip < kSurfaceTextureMips; ++mip) {
                const uint32_t size = (std::max)(kSurfaceTextureSize >> mip, 1u);
                const uint32_t blocks = (size + 3u) / 4u;
                RenderSurfaceTextureUpload upload{};
                upload.layer = render.surfacesUploaded;
                upload.mip = mip;
                upload.width = size;
                upload.height = size;
                upload.rowPitchBytes = blocks * 16u;
                upload.pixels = bytes.data() + offset;
                (void)resources.UploadTexture(texture, upload);
                offset += static_cast<size_t>(blocks) * blocks * 16u;
            }
        };
        uploadMips(render.surfaceColor, surface.colorHeight);
        uploadMips(render.surfaceDetail, surface.detail);
        ++render.surfacesUploaded;
    }
}

// 筆刷の一度分：変わった矩形だけを高さと法線の全段へ送る。
void UploadPendingRect(IRenderSurfaceResources& resources, const TerrainRenderResources& render, TileRuntime& runtime,
                       uint32_t samples) {
    RenderSurfaceTextureUpload height{};
    height.layer = runtime.layer;
    height.x = runtime.pendingX0;
    height.y = runtime.pendingZ0;
    height.width = runtime.pendingX1 - runtime.pendingX0 + 1u;
    height.height = runtime.pendingZ1 - runtime.pendingZ0 + 1u;
    height.rowPitchBytes = samples * sizeof(uint16_t);
    height.pixels = runtime.heights.data() + static_cast<size_t>(height.y) * samples + height.x;
    (void)resources.UploadTexture(render.heightTexture, height);
    size_t offset = 0u;
    uint32_t size = samples;
    uint32_t x0 = runtime.pendingX0;
    uint32_t z0 = runtime.pendingZ0;
    uint32_t x1 = runtime.pendingX1;
    uint32_t z1 = runtime.pendingZ1;
    for (uint32_t mip = 0u; mip < 5u; ++mip) {
        RenderSurfaceTextureUpload normal{};
        normal.layer = runtime.layer;
        normal.mip = mip;
        normal.x = x0;
        normal.y = z0;
        normal.width = x1 - x0 + 1u;
        normal.height = z1 - z0 + 1u;
        normal.rowPitchBytes = size * 2u * sizeof(int16_t);
        normal.pixels = runtime.normalMips.data() + offset + (static_cast<size_t>(z0) * size + x0) * 2u;
        (void)resources.UploadTexture(render.normalTexture, normal);
        offset += static_cast<size_t>(size) * size * 2u;
        size = (std::max)(size >> 1u, 1u);
        x0 >>= 1u;
        z0 >>= 1u;
        x1 = (std::min)(x1 >> 1u, size - 1u);
        z1 = (std::min)(z1 >> 1u, size - 1u);
    }
    runtime.rectPending = false;
}

void UpdateRayProxies(IRenderSurfaceResources& resources, TerrainStore& store) {
    TerrainRenderResources& render = store.Render();
    ++render.frame;
    uint32_t created = 0u;
    for (const auto& [tile, runtime] : store.Tiles()) {
        if (runtime.heights.empty()) {
            continue;
        }
        TerrainRayProxy& proxy = render.rayProxies[tile];
        if (proxy.resource != 0u &&
            (proxy.revision == runtime.revision || render.frame - proxy.builtFrame < kRayProxyRebuildFrames)) {
            continue;
        }
        if (created >= (proxy.resource != 0u ? kRayProxyRebuildsPerFrame : kRayProxiesPerFrame)) {
            continue;
        }
        const uint32_t resource = CreateRayProxy(resources, store, tile, runtime);
        if (resource == 0u) {
            continue;
        }
        if (proxy.resource != 0u) {
            resources.Release(proxy.resource);
        }
        proxy = {resource, runtime.revision, render.frame};
        ++created;
    }
}

// 材質の図が揃っていれば、区画の色を制御図の材質の平均色で置き換える（数点の平均）。
void AddSurfaceColor(const TerrainStore& store, TileCoord tile, uint32_t x, uint32_t z, uint32_t size,
                     float albedo[3]) {
    const auto& surfaces = store.Surfaces().Surfaces();
    if (surfaces.empty()) {
        return;
    }
    const auto* raster = store.Data().Control().Find(tile);
    const uint32_t samples = TERRAIN::DATA::TileSamples(store.Settings().grid);
    float sum[3]{};
    constexpr uint32_t kTaps = 4u;
    for (uint32_t tapZ = 0u; tapZ < kTaps; ++tapZ) {
        for (uint32_t tapX = 0u; tapX < kTaps; ++tapX) {
            const uint32_t sx = (std::min)(x + size * tapX / kTaps + size / (2u * kTaps), samples - 1u);
            const uint32_t sz = (std::min)(z + size * tapZ / kTaps + size / (2u * kTaps), samples - 1u);
            const uint32_t value = raster != nullptr ? raster->values[static_cast<size_t>(sz) * samples + sx] : 0u;
            const auto texel = TERRAIN::DATA::UnpackControlTexel(value);
            const float blend = static_cast<float>(texel.blend) / 255.0f;
            const auto base = (std::min)(static_cast<size_t>(texel.baseMaterial), surfaces.size() - 1u);
            const auto overlay = (std::min)(static_cast<size_t>(texel.overlayMaterial), surfaces.size() - 1u);
            for (int channel = 0; channel < 3; ++channel) {
                sum[channel] += surfaces[base].averageColor[channel] * (1.0f - blend) +
                                surfaces[overlay].averageColor[channel] * blend;
            }
        }
    }
    for (int channel = 0; channel < 3; ++channel) {
        albedo[channel] = sum[channel] / static_cast<float>(kTaps * kTaps);
    }
}

uint32_t CreateRayProxy(IRenderSurfaceResources& resources, const TerrainStore& store, TileCoord tile,
                        const TileRuntime& runtime) {
    const auto& grid = store.Settings().grid;
    const uint32_t samples = TERRAIN::DATA::TileSamples(grid);
    uint32_t step = (std::max)((samples - 1u) / kRayProxyCells, 1u);
    while ((samples - 1u) % step != 0u) {
        ++step;
    }
    const uint32_t cells = (samples - 1u) / step;
    const uint32_t columns = cells + 1u;
    const float spacing = static_cast<float>(TERRAIN::DATA::SampleSpacing(grid)) * static_cast<float>(step);
    const float originX = static_cast<float>(tile.x) * grid.tileSizeMeters;
    const float originZ = static_cast<float>(tile.z) * grid.tileSizeMeters;
    const auto heightAt = [&](uint32_t column, uint32_t row) {
        return TERRAIN::DATA::DequantizeHeight(
            grid, runtime.heights[static_cast<size_t>(row * step) * samples + column * step]);
    };
    std::vector<float> positions{};
    positions.reserve(static_cast<size_t>(columns) * columns * 3u);
    for (uint32_t row = 0u; row < columns; ++row) {
        for (uint32_t column = 0u; column < columns; ++column) {
            positions.push_back(originX + static_cast<float>(column) * spacing);
            positions.push_back(heightAt(column, row));
            positions.push_back(originZ + static_cast<float>(row) * spacing);
        }
    }
    std::vector<uint16_t> indices{};
    indices.reserve(static_cast<size_t>(cells) * cells * 6u);
    std::vector<RenderSurfaceRayProxyRegion> regions{};
    for (uint32_t regionZ = 0u; regionZ < cells; regionZ += kRayProxyRegionCells) {
        for (uint32_t regionX = 0u; regionX < cells; regionX += kRayProxyRegionCells) {
            RenderSurfaceRayProxyRegion region{};
            region.firstIndex = static_cast<uint32_t>(indices.size());
            float slopeX = 0.0f;
            float slopeZ = 0.0f;
            float height = 0.0f;
            uint32_t count = 0u;
            for (uint32_t z = regionZ; z < (std::min)(regionZ + kRayProxyRegionCells, cells); ++z) {
                for (uint32_t x = regionX; x < (std::min)(regionX + kRayProxyRegionCells, cells); ++x) {
                    const uint16_t a = static_cast<uint16_t>(z * columns + x);
                    const uint16_t b = static_cast<uint16_t>(a + 1u);
                    const uint16_t c = static_cast<uint16_t>(a + columns);
                    const uint16_t d = static_cast<uint16_t>(c + 1u);
                    if (((x ^ z) & 1u) == 0u) {
                        indices.insert(indices.end(), {a, c, b, b, c, d});
                    } else {
                        indices.insert(indices.end(), {a, c, d, a, d, b});
                    }
                    const float base = heightAt(x, z);
                    slopeX += heightAt(x + 1u, z) - base;
                    slopeZ += heightAt(x, z + 1u) - base;
                    height += base;
                    ++count;
                }
            }
            region.indexCount = static_cast<uint32_t>(indices.size()) - region.firstIndex;
            const float scale = 1.0f / (static_cast<float>(count) * spacing);
            const float normalX = -slopeX * scale;
            const float normalZ = -slopeZ * scale;
            const float inverseLength = 1.0f / std::sqrt(normalX * normalX + normalZ * normalZ + 1.0f);
            region.normal[0] = normalX * inverseLength;
            region.normal[1] = inverseLength;
            region.normal[2] = normalZ * inverseLength;
            PlaceholderAlbedo(height / static_cast<float>(count), region.normal[1], region.albedo);
            AddSurfaceColor(store, tile, regionX * step, regionZ * step, kRayProxyRegionCells * step, region.albedo);
            regions.push_back(region);
        }
    }
    return resources.CreateRayTracingProxy({positions, indices, regions});
}

} // namespace

void PrepareTerrainRenderData(IRenderSurfaceResources& resources, TerrainStore& store) {
    EnsureResources(resources, store);
    UploadPendingTiles(resources, store);
    UpdateSurfaces(resources, store);
    store.RuleBaker().Update(store);
    UploadFarColors(resources, store);
    UpdateRayProxies(resources, store);
    store.Erosion().Update(resources);
}

} // namespace HIKARI::TERRAINKIT
