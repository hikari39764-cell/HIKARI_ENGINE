#include "HIKARI_TerrainKitGrass.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "HIKARI_TerrainKitFrustum.h"
#include "HIKARI_TerrainKitStore.h"

namespace HIKARI::TERRAINKIT {
namespace {

constexpr char kGrassProgram[] = "HIKARI.EngineShader.Render3D_TerrainKitGrass";
// 着色器の GRASS_* と揃える。小区画は辺 64 個の格子点で、一組 16 枚なら 256 組。
constexpr float kBaseSpacing = 0.125f;
constexpr uint32_t kPatchCells = 64u;
constexpr uint32_t kGroupsPerPatch = 256u;
constexpr uint32_t kTopLevel = 3u;
constexpr float kBladeMargin = 2.0f; // 葉の高さと揺れの分だけ箱を広げる

struct GrassPatchInstance {
    float originX = 0.0f;
    float originZ = 0.0f;
    float tileOriginX = 0.0f;
    float tileOriginZ = 0.0f;
    uint32_t level = 0u;
    uint32_t tileLayer = 0u;
    uint32_t flips = 0u; // bit 0, 1：x, z を逆順に描く（カメラに近い葉から描いて、隠れる画素の着色を減らす）
    uint32_t reserved = 0u;
};
static_assert(sizeof(GrassPatchInstance) == 32u);

struct GrassDrawConstants {
    float heightMin = 0.0f;
    float heightRange = 1.0f;
    float tileSize = 256.0f;
    float tileCells = 256.0f;
    float firstRange = 20.0f;
    float density = 1.0f;
    uint32_t surfaceCount = 0u;
    uint32_t colorMapReady = 0u;
    float windDirectionX = 0.82f;
    float windDirectionZ = 0.57f;
    float windStrength = 1.0f;
    float windSpeed = 3.0f;
    float waterLevel = kNoSeaLevel; // これより低い地面には生やさない
    float reserved[3]{};
};

class PatchSelector {
public:
    PatchSelector(const float cameraPosition[3], const FrustumPlanes& frustum, float firstRange,
                  std::vector<GrassPatchInstance>& out)
        : camera_{cameraPosition[0], cameraPosition[1], cameraPosition[2]}, frustum_(frustum), firstRange_(firstRange),
          out_(out) {}

    // 瓦片の範囲に重なる最上段の区画から順に分ける。
    void SelectTile(float tileX, float tileZ, float tileSize, uint32_t layer, float minimumHeight,
                    float maximumHeight) {
        tileX_ = tileX;
        tileZ_ = tileZ;
        tileSize_ = tileSize;
        layer_ = layer;
        minimumHeight_ = minimumHeight - kBladeMargin;
        maximumHeight_ = maximumHeight + kBladeMargin;
        const float top = PatchSize(kTopLevel);
        const float reach = firstRange_ * static_cast<float>(1u << kTopLevel);
        const int32_t x0 = static_cast<int32_t>(std::floor((std::max)(tileX, camera_[0] - reach) / top));
        const int32_t z0 = static_cast<int32_t>(std::floor((std::max)(tileZ, camera_[2] - reach) / top));
        const int32_t x1 = static_cast<int32_t>(std::ceil((std::min)(tileX + tileSize, camera_[0] + reach) / top));
        const int32_t z1 = static_cast<int32_t>(std::ceil((std::min)(tileZ + tileSize, camera_[2] + reach) / top));
        for (int32_t z = z0; z < z1; ++z) {
            for (int32_t x = x0; x < x1; ++x) {
                Select(static_cast<float>(x) * top, static_cast<float>(z) * top, kTopLevel);
            }
        }
    }

private:
    static float PatchSize(uint32_t level) noexcept {
        return kBaseSpacing * static_cast<float>(kPatchCells) * static_cast<float>(1u << level);
    }

    void Select(float originX, float originZ, uint32_t level) {
        const float size = PatchSize(level);
        const float x0 = (std::max)(originX, tileX_);
        const float z0 = (std::max)(originZ, tileZ_);
        const float x1 = (std::min)(originX + size, tileX_ + tileSize_);
        const float z1 = (std::min)(originZ + size, tileZ_ + tileSize_);
        if (x0 >= x1 || z0 >= z1) {
            return;
        }
        const float minimum[3]{x0, minimumHeight_, z0};
        const float maximum[3]{x1, maximumHeight_, z1};
        if (!frustum_.IntersectsBox(minimum, maximum)) {
            return;
        }
        const float dx = (std::max)({x0 - camera_[0], 0.0f, camera_[0] - x1});
        const float dz = (std::max)({z0 - camera_[2], 0.0f, camera_[2] - z1});
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (distance >= firstRange_ * static_cast<float>(1u << kTopLevel)) {
            return;
        }
        // 一つ下の段の葉が消えきる距離より内側なら分ける（葉の見え方は距離だけで決まり、区画の段には依らない）。
        if (level != 0u && distance < firstRange_ * static_cast<float>(1u << (level - 1u))) {
            const float half = size * 0.5f;
            Select(originX, originZ, level - 1u);
            Select(originX + half, originZ, level - 1u);
            Select(originX, originZ + half, level - 1u);
            Select(originX + half, originZ + half, level - 1u);
            return;
        }
        GrassPatchInstance patch{};
        patch.originX = originX;
        patch.originZ = originZ;
        patch.tileOriginX = tileX_;
        patch.tileOriginZ = tileZ_;
        patch.level = level;
        patch.tileLayer = layer_;
        patch.flips = (camera_[0] > originX + size * 0.5f ? 1u : 0u) | (camera_[2] > originZ + size * 0.5f ? 2u : 0u);
        out_.push_back(patch);
    }

    float camera_[3];
    const FrustumPlanes& frustum_;
    float firstRange_;
    std::vector<GrassPatchInstance>& out_;
    float tileX_ = 0.0f;
    float tileZ_ = 0.0f;
    float tileSize_ = 0.0f;
    uint32_t layer_ = 0u;
    float minimumHeight_ = 0.0f;
    float maximumHeight_ = 0.0f;
};

} // namespace

void RecordTerrainGrass(const RenderSurfaceView& view, IRenderSurfaceCommands& commands, TerrainStore& store) {
    // 前向きの pass だけで描く（深度もそこで書く。理由は Render3D_TerrainKitGrass.hlsl の冒頭）。
    if (view.pass != RenderSurfacePass::Forward) {
        return;
    }
    const TerrainGrassSettings& settings = store.Grass();
    const TerrainRenderResources& render = store.Render();
    const auto& surfaces = store.Surfaces().Surfaces();
    // 数据図を見ている間は葉で隠さない。
    if (!settings.enabled || settings.density <= 0.0f || store.Overlay() != TerrainOverlay::None ||
        render.surfaceParams == 0u || render.surfacesUploaded != surfaces.size() ||
        std::none_of(surfaces.begin(), surfaces.end(),
                     [](const CookedSurface& surface) { return surface.grass.density > 0.0f; })) {
        return;
    }

    std::vector<GrassPatchInstance> patches{};
    const FrustumPlanes frustum(view.viewProjection);
    PatchSelector selector(view.cameraPosition, frustum, settings.firstRangeMeters, patches);
    const float tileSize = store.Settings().grid.tileSizeMeters;
    for (const auto& [tile, runtime] : store.Tiles()) {
        if (runtime.heights.empty() || runtime.uploadPending) {
            continue;
        }
        selector.SelectTile(static_cast<float>(tile.x) * tileSize, static_cast<float>(tile.z) * tileSize, tileSize,
                            runtime.layer, runtime.minHeight, runtime.maxHeight);
    }
    if (patches.empty()) {
        return;
    }
    const auto distanceToCamera = [&](const GrassPatchInstance& patch) {
        const float half =
            kBaseSpacing * static_cast<float>(kPatchCells) * static_cast<float>(1u << patch.level) * 0.5f;
        const float dx = patch.originX + half - view.cameraPosition[0];
        const float dz = patch.originZ + half - view.cameraPosition[2];
        return dx * dx + dz * dz;
    };
    std::sort(patches.begin(), patches.end(), [&](const GrassPatchInstance& a, const GrassPatchInstance& b) {
        return distanceToCamera(a) < distanceToCamera(b);
    });

    GrassDrawConstants constants{};
    constants.heightMin = store.Settings().grid.heightMin;
    constants.heightRange = store.Settings().grid.heightRange;
    constants.tileSize = tileSize;
    constants.tileCells = static_cast<float>(store.Settings().grid.tileCells);
    constants.firstRange = settings.firstRangeMeters;
    constants.density = settings.density;
    constants.surfaceCount = static_cast<uint32_t>(surfaces.size());
    constants.colorMapReady = render.colorMap != 0u && render.colorMapsUploaded >= store.Tiles().size() ? 1u : 0u;
    constants.windStrength = settings.windStrength;
    constants.windSpeed = settings.windSpeed;
    constants.waterLevel = store.SeaLevel();

    RenderSurfaceDraw draw{};
    draw.programId = kGrassProgram;
    draw.textures[0] = render.heightTexture;
    draw.textures[1] = render.normalTexture;
    draw.textures[2] = render.controlTexture;
    draw.textures[3] = render.surfaceColor;
    draw.textures[4] = render.surfaceDetail;
    draw.textures[5] = render.surfaceParams;
    draw.textures[6] = render.colorMap;
    draw.indexCount = kGroupsPerPatch;
    draw.instances = patches.data();
    draw.instanceStride = sizeof(GrassPatchInstance);
    draw.instanceCount = static_cast<uint32_t>(patches.size());
    draw.constants = &constants;
    draw.constantBytes = sizeof(constants);
    (void)commands.Draw(draw);
}

} // namespace HIKARI::TERRAINKIT
