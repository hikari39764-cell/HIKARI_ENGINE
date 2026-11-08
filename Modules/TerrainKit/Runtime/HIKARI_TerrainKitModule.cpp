#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "HIKARI/Project/GameComponent.h"
#include "HIKARI/Project/GameFlowViews.h"
#include "HIKARI/Project/GameProjectModule.h"
#include "HIKARI/Project/NavigationSource.h"
#include "HIKARI/Project/RenderSurface.h"
#include "HIKARI/Project/RuntimeFeature.h"
#include "HIKARI/Project/RuntimeFeatureRegistration.h"
#include "HIKARI_TerrainKitStore.h"

namespace HIKARI::TERRAINKIT {
namespace {

using TERRAIN::DATA::TileCoord;

constexpr std::string_view kTerrainComponentType = "HIKARI.TerrainKit.Terrain";
constexpr char kSurfaceProgram[] = "HIKARI.EngineShader.Render3D_TerrainKitSurface";
constexpr uint32_t kGridCells = 32u;
// 瓦片の節の段数。0 が一番細かい（辺 = 瓦片 / 8）、3 が瓦片全体。
constexpr uint32_t kLodLevels = 4u;
// 葉の大きさに対する 0 段の範囲。隣り合う節の段の差が 1 以内で、かつ親の格子へ寄せる区間が重ならないよう
// （条件：開始割合 >= 0.5 + 0.705 * 節の辺 / 下の段の範囲）に決める。
constexpr float kFirstRangeInLeaves = 6.0f;
constexpr float kMorphStartFraction = 0.78f;
constexpr uint32_t kUploadTilesPerFrame = 32u;
// 光線追跡の代理：瓦片一枚をこの程度の格子にする（256 m なら 4 m 間隔）。色と法線は区画ごとに一つ。
constexpr uint32_t kRayProxyCells = 64u;
constexpr uint32_t kRayProxyRegionCells = 8u;
constexpr uint32_t kRayProxiesPerFrame = 8u;
// 筆で塗っている間に作り直す数（組み直しは一フレームに集めると引っかかる）。
constexpr uint32_t kRayProxyRebuildsPerFrame = 2u;
// 筆で塗っている間の作り直しの間隔。
constexpr uint64_t kRayProxyRebuildFrames = 30u;

struct TerrainNodeInstance {
    float originX = 0.0f;
    float originZ = 0.0f;
    float nodeSize = 0.0f;
    float morphStart = 0.0f;
    float morphEnd = 0.0f;
    uint32_t tileLayer = 0u;
    float tileOriginX = 0.0f;
    float tileOriginZ = 0.0f;
};
static_assert(sizeof(TerrainNodeInstance) == 32u);

struct TerrainDrawConstants {
    float heightMin = 0.0f;
    float heightRange = 1.0f;
    float tileSize = 256.0f;
    float tileCells = 256.0f;
    uint32_t gridCells = kGridCells;
    uint32_t debugFlags = 0u;
    float reserved[2]{};
};

struct FrustumPlanes {
    std::array<std::array<float, 4>, 6> planes{};

    explicit FrustumPlanes(const float m[16]) {
        // 列優先 m[col][row]：行 r は (m[0+r], m[4+r], m[8+r], m[12+r])。D3D の深度は 0..w。
        const auto row = [&](int r) { return std::array<float, 4>{m[r], m[4 + r], m[8 + r], m[12 + r]}; };
        const auto r0 = row(0);
        const auto r1 = row(1);
        const auto r2 = row(2);
        const auto r3 = row(3);
        for (int i = 0; i < 4; ++i) {
            planes[0][i] = r3[i] + r0[i];
            planes[1][i] = r3[i] - r0[i];
            planes[2][i] = r3[i] + r1[i];
            planes[3][i] = r3[i] - r1[i];
            planes[4][i] = r2[i];
            planes[5][i] = r3[i] - r2[i];
        }
    }

    bool IntersectsBox(const float minimum[3], const float maximum[3]) const noexcept {
        for (const auto& plane : planes) {
            const float x = plane[0] >= 0.0f ? maximum[0] : minimum[0];
            const float y = plane[1] >= 0.0f ? maximum[1] : minimum[1];
            const float z = plane[2] >= 0.0f ? maximum[2] : minimum[2];
            if (plane[0] * x + plane[1] * y + plane[2] * z + plane[3] < 0.0f) {
                return false;
            }
        }
        return true;
    }
};

float Smoothstep(float edge0, float edge1, float value) noexcept {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// 着色器の TerrainPlaceholderSurface と同じ分け方（材質の段階で置き換える）。
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

// 地形の部品。設定を場面に保存し、自分をその世界のモジュールの面として登録する。
class TerrainComponent final : public IGameComponent, public IRenderSurfaceExtension, public INavigationGeometrySource {
public:
    void OnAttach(GameComponentContext& context) override {
        world_ = context.GetWorld();
        owner_ = context.GetObject().GetHandle();
        (void)world_.AddRenderSurfaceExtension(*this);
        (void)world_.AddNavigationGeometrySource(*this);
    }

    // 物理は Play の世界でだけ動く。瓦片が読めたら当たりを作る。
    void Update(GameComponentContext&, float) override {
        TerrainStore& store = TerrainStore::Get();
        const std::filesystem::path scenePath = CurrentScenePath();
        if (scenePath.empty() || !store.Open(scenePath, settings_) || !heightFields_.empty()) {
            return;
        }
        const auto& grid = store.Settings().grid;
        const uint32_t samples = TERRAIN::DATA::TileSamples(grid);
        std::vector<float> heights{};
        for (const auto& [tile, runtime] : store.Tiles()) {
            if (runtime.heights.empty()) {
                continue;
            }
            heights.resize(runtime.heights.size());
            std::transform(runtime.heights.begin(), runtime.heights.end(), heights.begin(),
                           [&](uint16_t value) { return TERRAIN::DATA::DequantizeHeight(grid, value); });
            const uint64_t body =
                world_.AddStaticHeightField(owner_, static_cast<float>(tile.x) * grid.tileSizeMeters,
                                            static_cast<float>(tile.z) * grid.tileSizeMeters,
                                            static_cast<float>(TERRAIN::DATA::SampleSpacing(grid)), samples, heights);
            if (body == 0u) {
                break; // 物理の無い世界（編集中）。
            }
            heightFields_.push_back(body);
        }
    }
    void OnDetach(GameComponentContext&) override {
        world_.RemoveRenderSurfaceExtension(*this);
        world_.RemoveNavigationGeometrySource(*this);
        for (const uint64_t body : heightFields_) {
            world_.RemoveStaticHeightField(body);
        }
        heightFields_.clear();
    }

    void DescribeProperties(PropertyFields& fields) override {
        fields.Float("tileSizeMeters", "Tile Size", settings_.grid.tileSizeMeters, 16.0f, 4096.0f);
        int tileCells = static_cast<int>(settings_.grid.tileCells);
        if (fields.Int("tileCells", "Tile Cells", tileCells, 64, 512)) {
            settings_.grid.tileCells = static_cast<uint32_t>(tileCells);
        }
        fields.Float("heightMin", "Height Min", settings_.grid.heightMin, -100000.0f, 100000.0f);
        fields.Float("heightRange", "Height Range", settings_.grid.heightRange, 1.0f, 100000.0f);
        fields.Int("firstTileX", "First Tile X", settings_.firstTileX, -32768, 32767);
        fields.Int("firstTileZ", "First Tile Z", settings_.firstTileZ, -32768, 32767);
        fields.Int("tilesX", "Tiles X", settings_.tilesX, 1, 256);
        fields.Int("tilesZ", "Tiles Z", settings_.tilesZ, 1, 256);
    }

    void PrepareFrame(IRenderSurfaceResources& resources) override {
        TerrainStore& store = TerrainStore::Get();
        const std::filesystem::path scenePath = CurrentScenePath();
        if (scenePath.empty()) {
            return;
        }
        std::string error{};
        if (!store.Open(scenePath, settings_, &error)) {
            if (error != lastError_) {
                lastError_ = error;
                // 開けない地形は描かない。理由は describe の操作で読める。
            }
            return;
        }
        EnsureResources(resources, store);
        UploadPendingTiles(resources, store);
        UpdateRayProxies(resources, store);
    }

    void Record(const RenderSurfaceView& view, IRenderSurfaceCommands& commands) override {
        TerrainStore& store = TerrainStore::Get();
        const TerrainRenderResources& render = store.Render();
        if (!store.IsOpen() || render.heightTexture == 0u || render.indexBuffer == 0u) {
            return;
        }
        instances_.clear();
        const FrustumPlanes frustum(view.viewProjection);
        const float tileSize = store.Settings().grid.tileSizeMeters;
        const float leafSize = tileSize / static_cast<float>(kTileLeafDivisions);
        std::array<float, kLodLevels> ranges{};
        ranges[0] = leafSize * kFirstRangeInLeaves;
        for (uint32_t level = 1u; level < kLodLevels; ++level) {
            ranges[level] = ranges[level - 1u] * 2.0f;
        }
        for (const auto& [tile, runtime] : store.Tiles()) {
            if (runtime.heights.empty() || runtime.uploadPending) {
                continue;
            }
            const float tileX = static_cast<float>(tile.x) * tileSize;
            const float tileZ = static_cast<float>(tile.z) * tileSize;
            SelectNode(frustum, view.cameraPosition, ranges, runtime, tileX, tileZ, tileSize, leafSize, kLodLevels - 1u,
                       0u, 0u);
        }
        if (instances_.empty()) {
            return;
        }
        TerrainDrawConstants constants{};
        constants.heightMin = store.Settings().grid.heightMin;
        constants.heightRange = store.Settings().grid.heightRange;
        constants.tileSize = tileSize;
        constants.tileCells = static_cast<float>(store.Settings().grid.tileCells);
        RenderSurfaceDraw draw{};
        draw.programId = kSurfaceProgram;
        draw.textures[0] = render.heightTexture;
        draw.textures[1] = render.normalTexture;
        draw.indexBuffer = render.indexBuffer;
        draw.indexCount = render.indexCount;
        draw.instances = instances_.data();
        draw.instanceStride = sizeof(TerrainNodeInstance);
        draw.instanceCount = static_cast<uint32_t>(instances_.size());
        draw.constants = &constants;
        draw.constantBytes = sizeof(constants);
        (void)commands.Draw(draw);
    }

    void OnRenderResourcesLost() override { TerrainStore::Get().Render() = {}; }

    uint32_t GetNavigationChunkCount() const override {
        return static_cast<uint32_t>(TerrainStore::Get().Tiles().size());
    }

    bool GetNavigationChunk(uint32_t index, int32_t& outX, int32_t& outZ, uint64_t& outRevision) const override {
        const auto& tiles = TerrainStore::Get().Tiles();
        if (index >= tiles.size()) {
            return false;
        }
        const auto entry = std::next(tiles.begin(), index);
        outX = entry->first.x;
        outZ = entry->first.z;
        outRevision = entry->second.revision;
        return true;
    }

    // 道案内には 4 標本おきの粗い格子を渡す（Recast の入力を抑える）。
    void AppendNavigationChunk(int32_t x, int32_t z, INavigationGeometrySink& sink) const override {
        const TerrainStore& store = TerrainStore::Get();
        const auto found = store.Tiles().find(TileCoord{x, z});
        if (found == store.Tiles().end() || found->second.heights.empty()) {
            return;
        }
        const auto& grid = store.Settings().grid;
        const uint32_t samples = TERRAIN::DATA::TileSamples(grid);
        constexpr uint32_t kStep = 4u;
        const uint32_t columns = (samples - 1u) / kStep + 1u;
        const float spacing = static_cast<float>(TERRAIN::DATA::SampleSpacing(grid)) * kStep;
        const float originX = static_cast<float>(x) * grid.tileSizeMeters;
        const float originZ = static_cast<float>(z) * grid.tileSizeMeters;
        std::vector<float> vertices{};
        vertices.reserve(static_cast<size_t>(columns) * columns * 3u);
        for (uint32_t row = 0u; row < columns; ++row) {
            for (uint32_t column = 0u; column < columns; ++column) {
                vertices.push_back(originX + static_cast<float>(column) * spacing);
                vertices.push_back(TERRAIN::DATA::DequantizeHeight(
                    grid, found->second.heights[static_cast<size_t>(row * kStep) * samples + column * kStep]));
                vertices.push_back(originZ + static_cast<float>(row) * spacing);
            }
        }
        std::vector<int32_t> triangles{};
        triangles.reserve(static_cast<size_t>(columns - 1u) * (columns - 1u) * 6u);
        for (uint32_t row = 0u; row + 1u < columns; ++row) {
            for (uint32_t column = 0u; column + 1u < columns; ++column) {
                const int32_t a = static_cast<int32_t>(row * columns + column);
                const int32_t b = a + 1;
                const int32_t c = a + static_cast<int32_t>(columns);
                const int32_t d = c + 1;
                triangles.insert(triangles.end(), {a, c, b, b, c, d});
            }
        }
        sink.Append(vertices, triangles);
    }

private:
    std::filesystem::path CurrentScenePath() const {
        char buffer[1024]{};
        uint32_t length = 0u;
        if (!world_.GetSceneFlow().GetCurrentSceneFilePath(buffer, sizeof(buffer), length) || length == 0u) {
            return {};
        }
        return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(buffer), length));
    }

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
        render.layoutRevision = store.LayoutRevision();
        store.MarkAllForUpload();
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
            runtime.uploadPending = false;
            runtime.rectPending = false;
            if (++uploaded >= kUploadTilesPerFrame) {
                break;
            }
        }
    }

    // 筆刷の一度分：変わった矩形だけを高さと法線の全段へ送る。
    static void UploadPendingRect(IRenderSurfaceResources& resources, const TerrainRenderResources& render,
                                  TileRuntime& runtime, uint32_t samples) {
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

    uint32_t CreateRayProxy(IRenderSurfaceResources& resources, const TerrainStore& store, TileCoord tile,
                            const TileRuntime& runtime) const {
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
                regions.push_back(region);
            }
        }
        return resources.CreateRayTracingProxy({positions, indices, regions});
    }

    void SelectNode(const FrustumPlanes& frustum, const float camera[3], const std::array<float, kLodLevels>& ranges,
                    const TileRuntime& runtime, float tileX, float tileZ, float tileSize, float leafSize,
                    uint32_t level, uint32_t nodeX, uint32_t nodeZ) {
        const uint32_t leavesPerNode = 1u << level;
        const float nodeSize = leafSize * static_cast<float>(leavesPerNode);
        const float originX = tileX + static_cast<float>(nodeX) * nodeSize;
        const float originZ = tileZ + static_cast<float>(nodeZ) * nodeSize;
        float minHeight = std::numeric_limits<float>::max();
        float maxHeight = std::numeric_limits<float>::lowest();
        for (uint32_t z = 0u; z < leavesPerNode; ++z) {
            for (uint32_t x = 0u; x < leavesPerNode; ++x) {
                const size_t index =
                    (static_cast<size_t>(nodeZ * leavesPerNode + z) * kTileLeafDivisions + nodeX * leavesPerNode + x) *
                    2u;
                minHeight = (std::min)(minHeight, runtime.leafMinMax[index]);
                maxHeight = (std::max)(maxHeight, runtime.leafMinMax[index + 1u]);
            }
        }
        const float minimum[3]{originX, minHeight, originZ};
        const float maximum[3]{originX + nodeSize, maxHeight, originZ + nodeSize};
        if (!frustum.IntersectsBox(minimum, maximum)) {
            return;
        }
        if (level > 0u) {
            const float dx = (std::max)({minimum[0] - camera[0], 0.0f, camera[0] - maximum[0]});
            const float dy = (std::max)({minimum[1] - camera[1], 0.0f, camera[1] - maximum[1]});
            const float dz = (std::max)({minimum[2] - camera[2], 0.0f, camera[2] - maximum[2]});
            const float distanceSquared = dx * dx + dy * dy + dz * dz;
            const float splitRange = ranges[level - 1u];
            if (distanceSquared < splitRange * splitRange) {
                for (uint32_t z = 0u; z < 2u; ++z) {
                    for (uint32_t x = 0u; x < 2u; ++x) {
                        SelectNode(frustum, camera, ranges, runtime, tileX, tileZ, tileSize, leafSize, level - 1u,
                                   nodeX * 2u + x, nodeZ * 2u + z);
                    }
                }
                return;
            }
        }
        TerrainNodeInstance instance{};
        instance.originX = originX;
        instance.originZ = originZ;
        instance.nodeSize = nodeSize;
        // 一番粗い段は親がないので寄せない。
        instance.morphStart = level + 1u < kLodLevels ? ranges[level] * kMorphStartFraction : 1.0e9f;
        instance.morphEnd = level + 1u < kLodLevels ? ranges[level] : 2.0e9f;
        instance.tileLayer = runtime.layer;
        instance.tileOriginX = tileX;
        instance.tileOriginZ = tileZ;
        instances_.push_back(instance);
    }

    WorldView world_{};
    GameProjectObjectHandle owner_{};
    std::vector<uint64_t> heightFields_{};
    TerrainSettings settings_{};
    std::vector<TerrainNodeInstance> instances_{};
    std::string lastError_{};
};

class TerrainFeature final : public IRuntimeFeature {
public:
    std::string_view GetFeatureId() const noexcept override { return "HIKARI.TerrainKit.Core"; }
    std::string_view GetDisplayName() const noexcept override { return "Terrain Kit"; }
    std::string_view GetDescription() const noexcept override {
        return "Tiled open-world terrain: layer stack, sculpting, GPU level of detail and collision.";
    }
    bool Register(RuntimeFeatureContext& context) override {
        return context.RegisterComponent<TerrainComponent>({.typeName = kTerrainComponentType,
                                                            .displayName = "Terrain",
                                                            .category = "Terrain",
                                                            .description = "An open-world terrain made of tiles."});
    }
};

class TerrainKitModule final : public RUNTIME::IGameProjectModule {
public:
    std::string_view GetModuleId() const noexcept override { return "HIKARI.TerrainKit"; }
    bool RegisterRuntimeFeatures(RuntimeFeatureCatalog& catalog) override {
        return RegisterRuntimeFeature<TerrainFeature>(catalog);
    }
};

} // namespace
} // namespace HIKARI::TERRAINKIT

namespace HIKARI::RUNTIME {
std::unique_ptr<IGameProjectModule> CreateGameProjectModule() {
    return std::make_unique<::HIKARI::TERRAINKIT::TerrainKitModule>();
}
} // namespace HIKARI::RUNTIME
