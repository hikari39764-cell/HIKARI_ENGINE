#include "HIKARI_TerrainKitScatter.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <system_error>
#include <utility>

#include <json.hpp>

#include "HIKARI_TerrainKitStore.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace TERRAIN::DATA;

constexpr char kSceneFileName[] = "terrain.scatter.json";
constexpr uint32_t kAssetFormatVersion = 1u;
constexpr float kReservedRadiusMeters = 1.0f; // 手で置いた個体の周りを空ける半径（拡縮を掛ける）

struct TileArea {
    double x0 = 0.0;
    double z0 = 0.0;
    double size = 0.0;

    bool Contains(float x, float z) const noexcept { return x >= x0 && z >= z0 && x < x0 + size && z < z0 + size; }
};

TileArea AreaOf(const TerrainStore& store, TileCoord tile) noexcept {
    const TerrainGrid& grid = store.Settings().grid;
    return {static_cast<double>(tile.x) * grid.tileSizeMeters, static_cast<double>(tile.z) * grid.tileSizeMeters,
            static_cast<double>(grid.tileSizeMeters)};
}

std::filesystem::path Utf8Path(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

bool HandPlaced(const GameProjectWorldInstance& instance) noexcept {
    return (instance.flags & static_cast<uint16_t>(GameProjectWorldInstanceFlags::HandPlaced)) != 0u;
}

} // namespace

void TerrainScatter::Clear() {
    rules_ = {};
    assetPath_.clear();
    excludedSeeds_.clear();
    rulesDirty_ = false;
    sceneDirty_ = false;
}

void TerrainScatter::Load(const std::filesystem::path& cellDirectory, const std::filesystem::path& projectRoot) {
    Clear();
    const std::filesystem::path scenePath = cellDirectory / kSceneFileName;
    std::error_code ec{};
    nlohmann::json scene{};
    std::string message{};
    if (!std::filesystem::exists(scenePath, ec) || !ReadTerrainJson(scenePath, scene, message)) {
        return;
    }
    assetPath_ = scene.value("asset", std::string{});
    excludedSeeds_ = scene.value("excludedSeeds", std::vector<uint32_t>{});
    std::sort(excludedSeeds_.begin(), excludedSeeds_.end());
    nlohmann::json asset{};
    if (!assetPath_.empty() && ReadTerrainJson(projectRoot / Utf8Path(assetPath_), asset, message) &&
        asset.value("formatVersion", 0u) == kAssetFormatVersion) {
        JSONFIELDS::Read(asset, rules_, kScatterRuleSetFields);
    }
}

bool TerrainScatter::Save(const std::filesystem::path& cellDirectory, const std::filesystem::path& projectRoot,
                          bool force, std::string& error) {
    std::string message{};
    if (rulesDirty_ || (force && !rules_.rules.empty())) {
        if (assetPath_.empty()) {
            assetPath_ = "Assets/Terrain/" + cellDirectory.stem().generic_string() + ".hscatter";
            sceneDirty_ = true;
        }
        const std::filesystem::path assetPath = projectRoot / Utf8Path(assetPath_);
        std::error_code ec{};
        std::filesystem::create_directories(assetPath.parent_path(), ec);
        nlohmann::json asset{{"formatVersion", kAssetFormatVersion}};
        JSONFIELDS::Write(asset, rules_, kScatterRuleSetFields);
        if (!WriteTerrainJson(assetPath, asset, message)) {
            error = "Could not write the scatter rules: " + message;
            return false;
        }
        rulesDirty_ = false;
    }
    if (sceneDirty_ || (force && (!assetPath_.empty() || !excludedSeeds_.empty()))) {
        const nlohmann::json scene{{"asset", assetPath_}, {"excludedSeeds", excludedSeeds_}};
        if (!WriteTerrainJson(cellDirectory / kSceneFileName, scene, message)) {
            error = "Could not write the scatter settings: " + message;
            return false;
        }
        sceneDirty_ = false;
    }
    return true;
}

void TerrainScatter::SetRules(ScatterRuleSet rules) {
    rules_ = std::move(rules);
    rulesDirty_ = true;
}

void TerrainScatter::AddExcluded(std::span<const uint32_t> seeds) {
    if (seeds.empty()) {
        return;
    }
    excludedSeeds_.insert(excludedSeeds_.end(), seeds.begin(), seeds.end());
    std::sort(excludedSeeds_.begin(), excludedSeeds_.end());
    excludedSeeds_.erase(std::unique(excludedSeeds_.begin(), excludedSeeds_.end()), excludedSeeds_.end());
    sceneDirty_ = true;
}

void TerrainScatter::RemoveExcluded(std::span<const uint32_t> seeds) {
    if (seeds.empty()) {
        return;
    }
    for (const uint32_t seed : seeds) {
        const auto found = std::lower_bound(excludedSeeds_.begin(), excludedSeeds_.end(), seed);
        if (found != excludedSeeds_.end() && *found == seed) {
            excludedSeeds_.erase(found);
        }
    }
    sceneDirty_ = true;
}

std::vector<ScatterReserved> TerrainScatter::CollectReserved(const TerrainStore& store, TileCoord tile) const {
    std::vector<ScatterReserved> reserved{};
    const WorldView& world = store.World();
    std::array<ComponentTypeId, 64> ids{};
    const uint32_t count = (std::min)(world.GetInstanceSetIds(ids), static_cast<uint32_t>(ids.size()));
    const TileArea area = AreaOf(store, tile);
    const double margin = static_cast<double>(kSurfaceRuleHeightMargin) * SampleSpacing(store.Settings().grid);
    for (uint32_t index = 0u; index < count; ++index) {
        for (int32_t z = InstanceCellIndex(area.z0 - margin); z <= InstanceCellIndex(area.z0 + area.size + margin);
             ++z) {
            for (int32_t x = InstanceCellIndex(area.x0 - margin); x <= InstanceCellIndex(area.x0 + area.size + margin);
                 ++x) {
                for (const GameProjectWorldInstance& instance : world.ReadInstanceCell(ids[index], x, z)) {
                    if (HandPlaced(instance)) {
                        reserved.push_back({instance.position.x, instance.position.z,
                                            kReservedRadiusMeters * WORLDINSTANCE::DecodeHalf(instance.scale[0])});
                    }
                }
            }
        }
    }
    return reserved;
}

void TerrainScatter::Apply(const TerrainStore& store, const ScatterRuleSet& rules, TileCoord tile,
                           std::span<const ScatterPoint> points) const {
    const WorldView& world = store.World();
    const TileArea area = AreaOf(store, tile);
    // 同じ瓦片へ何度も出る名前を毎回引かないよう、規則ごとの ID を先に求める。
    const auto idsOf = [&](const auto& list) {
        std::vector<ComponentTypeId> ids{};
        ids.reserve(list.size());
        for (const auto& rule : list) {
            ids.push_back(MakeComponentTypeId(rule.instanceSet.c_str()));
        }
        return ids;
    };
    const std::vector<ComponentTypeId> ruleSets = idsOf(rules.rules);
    const std::vector<ComponentTypeId> lineSets = idsOf(rules.lines);
    struct CellKey {
        uint64_t set = 0u;
        int32_t x = 0;
        int32_t z = 0;
        auto operator<=>(const CellKey&) const = default;
    };
    std::map<CellKey, std::vector<GameProjectWorldInstance>> generated{};
    for (const ScatterPoint& point : points) {
        if (std::binary_search(excludedSeeds_.begin(), excludedSeeds_.end(), point.seed)) {
            continue;
        }
        const ComponentTypeId set = point.line ? lineSets[point.rule] : ruleSets[point.rule];
        generated[CellKey{set.value, InstanceCellIndex(point.x), InstanceCellIndex(point.z)}].push_back(
            WORLDINSTANCE::Encode({point.x, point.y, point.z},
                                  {point.rotation[0], point.rotation[1], point.rotation[2], point.rotation[3]},
                                  {point.scale, point.scale, point.scale}, point.seed));
    }
    std::array<ComponentTypeId, 64> ids{};
    const uint32_t count = (std::min)(world.GetInstanceSetIds(ids), static_cast<uint32_t>(ids.size()));
    std::vector<GameProjectWorldInstance> next{};
    for (uint32_t index = 0u; index < count; ++index) {
        const ComponentTypeId set = ids[index];
        for (int32_t z = InstanceCellIndex(area.z0); z <= InstanceCellIndex(area.z0 + area.size - 0.001); ++z) {
            for (int32_t x = InstanceCellIndex(area.x0); x <= InstanceCellIndex(area.x0 + area.size - 0.001); ++x) {
                const auto existing = world.ReadInstanceCell(set, x, z);
                next.clear();
                for (const GameProjectWorldInstance& instance : existing) {
                    if (HandPlaced(instance) || !area.Contains(instance.position.x, instance.position.z)) {
                        next.push_back(instance);
                    }
                }
                const auto found = generated.find(CellKey{set.value, x, z});
                if (found != generated.end()) {
                    next.insert(next.end(), found->second.begin(), found->second.end());
                }
                const bool same = next.size() == existing.size() &&
                                  (next.empty() || std::memcmp(next.data(), existing.data(),
                                                               next.size() * sizeof(GameProjectWorldInstance)) == 0);
                if (!same) {
                    (void)world.ReplaceInstanceCell(set, x, z, next);
                }
            }
        }
    }
}

} // namespace HIKARI::TERRAINKIT
