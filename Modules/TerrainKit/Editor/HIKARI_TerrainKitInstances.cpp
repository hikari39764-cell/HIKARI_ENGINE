#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "HIKARI_TerrainKitEditorCommon.h"

// 実例集の配置：地形の上へ筆で個体を置き、消し、一つずつ動かす。一筆で撤销一件。変更は触った格子の前後で持つ。
// 規則の個体を消すか動かすと、その種を除外して覚える。規則を計算し直しても元の場所へは戻らない。
namespace HIKARI::TERRAINKIT {
namespace {

using namespace EDITOR_PROJECT;

constexpr EditorWorkspaceColor kInstanceColor{0.55f, 0.95f, 0.6f, 1.0f};

struct CellKey {
    uint64_t set = 0u;
    int32_t x = 0;
    int32_t z = 0;
    auto operator<=>(const CellKey&) const = default;
};

constexpr float kPickRadiusMeters = 4.0f; // 動かす時に個体を掴める距離

uint64_t NextRandomBits(uint64_t& state) noexcept {
    state += 0x9E3779B97F4A7C15ull;
    uint64_t value = state;
    value = (value ^ (value >> 30u)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27u)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31u);
}

float NextRandom(uint64_t& state) noexcept {
    return static_cast<float>(NextRandomBits(state) >> 40u) / static_cast<float>(1ull << 24u);
}

class InstanceUndo final : public IEditorUndoEntry {
public:
    struct Change {
        CellKey key{};
        std::vector<GameProjectWorldInstance> before{};
        std::vector<GameProjectWorldInstance> after{};
    };

    InstanceUndo(std::vector<Change> changes, std::vector<uint32_t> excluded)
        : changes_(std::move(changes)), excluded_(std::move(excluded)) {}
    void Undo() override {
        Apply(&Change::before);
        TerrainStore::Get().Scatter().RemoveExcluded(excluded_);
    }
    void Redo() override {
        Apply(&Change::after);
        TerrainStore::Get().Scatter().AddExcluded(excluded_);
    }

private:
    void Apply(std::vector<GameProjectWorldInstance> Change::* side) {
        const WorldView world = TerrainStore::Get().World();
        for (const Change& change : changes_) {
            (void)world.ReplaceInstanceCell(ComponentTypeId{change.key.set}, change.key.x, change.key.z, change.*side);
        }
    }

    std::vector<Change> changes_;
    std::vector<uint32_t> excluded_;
};

// 一筆の間に触った格子の変更前を覚えて、終わりに撤销一件へまとめる。
class InstanceEditRecorder {
public:
    void Remember(const WorldView& world, CellKey key) {
        if (!before_.contains(key)) {
            const auto cell = world.ReadInstanceCell(ComponentTypeId{key.set}, key.x, key.z);
            before_.emplace(key, std::vector<GameProjectWorldInstance>(cell.begin(), cell.end()));
        }
    }

    // 規則の個体を外した。手で置いた個体には種の意味がないので覚えない。
    void Exclude(const GameProjectWorldInstance& instance) {
        if ((instance.flags & static_cast<uint16_t>(GameProjectWorldInstanceFlags::HandPlaced)) == 0u) {
            excluded_.push_back(instance.seed);
        }
    }

    bool Empty() const noexcept { return before_.empty(); }

    void Finish(EditorShared& shared, std::string_view label) {
        const WorldView world = TerrainStore::Get().World();
        std::vector<InstanceUndo::Change> changes{};
        changes.reserve(before_.size());
        for (auto& [key, before] : before_) {
            const auto cell = world.ReadInstanceCell(ComponentTypeId{key.set}, key.x, key.z);
            changes.push_back(
                {key, std::move(before), std::vector<GameProjectWorldInstance>(cell.begin(), cell.end())});
        }
        before_.clear();
        TerrainStore::Get().Scatter().AddExcluded(excluded_);
        if (!changes.empty() && shared.services != nullptr) {
            shared.services->RecordUndo(label,
                                        std::make_unique<InstanceUndo>(std::move(changes), std::move(excluded_)));
        }
        excluded_.clear();
    }

private:
    std::map<CellKey, std::vector<GameProjectWorldInstance>> before_{};
    std::vector<uint32_t> excluded_{};
};

// 円の中へ個体を足す。間隔より近い場所と急すぎる場所は飛ばす。
uint32_t PlaceInto(const WorldView& world, const TerrainStore& store, uint64_t setId, float centerX, float centerZ,
                   const InstanceBrushSettings& brush, uint64_t& random, InstanceEditRecorder& recorder) {
    constexpr float kProbeMeters = 1.0f;
    const float radius = (std::max)(brush.radiusMeters, 0.1f);
    const float target = (std::max)(brush.density, 0.0f) * std::numbers::pi_v<float> * radius * radius / 100.0f;
    auto candidates = static_cast<uint32_t>(target);
    if (NextRandom(random) < target - static_cast<float>(candidates)) {
        ++candidates;
    }
    const float spacingSquared = brush.spacingMeters * brush.spacingMeters;
    const float cosineLimit = std::cos(brush.maxSlopeDegrees * std::numbers::pi_v<float> / 180.0f);
    std::map<CellKey, std::vector<GameProjectWorldInstance>> working{};
    uint32_t placed = 0u;
    for (uint32_t index = 0u; index < candidates; ++index) {
        const float angle = NextRandom(random) * 2.0f * std::numbers::pi_v<float>;
        const float distance = radius * std::sqrt(NextRandom(random));
        const float x = centerX + std::cos(angle) * distance;
        const float z = centerZ + std::sin(angle) * distance;
        const std::optional<float> height = store.HeightAt(x, z);
        const std::optional<float> east = store.HeightAt(x + kProbeMeters, z);
        const std::optional<float> north = store.HeightAt(x, z + kProbeMeters);
        if (!height.has_value() || !east.has_value() || !north.has_value()) {
            continue;
        }
        const float slopeX = (*east - *height) / kProbeMeters;
        const float slopeZ = (*north - *height) / kProbeMeters;
        const float inverseLength = 1.0f / std::sqrt(slopeX * slopeX + slopeZ * slopeZ + 1.0f);
        if (inverseLength < cosineLimit) {
            continue;
        }
        const CellKey key{setId, InstanceCellIndex(x), InstanceCellIndex(z)};
        auto found = working.find(key);
        if (found == working.end()) {
            recorder.Remember(world, key);
            const auto cell = world.ReadInstanceCell(ComponentTypeId{setId}, key.x, key.z);
            found = working.emplace(key, std::vector<GameProjectWorldInstance>(cell.begin(), cell.end())).first;
        }
        std::vector<GameProjectWorldInstance>& instances = found->second;
        const bool crowded =
            std::any_of(instances.begin(), instances.end(), [&](const GameProjectWorldInstance& other) {
                const float dx = other.position.x - x;
                const float dz = other.position.z - z;
                return dx * dx + dz * dz < spacingSquared;
            });
        if (crowded) {
            continue;
        }
        // 向きはランダムな方位に、地面の傾きの分だけ倒す。
        float rotation[4]{};
        TERRAIN::DATA::GroundAlignedRotation(NextRandom(random) * 2.0f * std::numbers::pi_v<float>, slopeX, slopeZ,
                                             brush.alignToGround, rotation);
        const float scale = brush.scaleMin + (brush.scaleMax - brush.scaleMin) * NextRandom(random);
        instances.push_back(WORLDINSTANCE::Encode({x, *height, z}, {rotation[0], rotation[1], rotation[2], rotation[3]},
                                                  {scale, scale, scale}, static_cast<uint32_t>(NextRandomBits(random)),
                                                  GameProjectWorldInstanceFlags::HandPlaced));
        ++placed;
    }
    for (const auto& [key, instances] : working) {
        (void)world.ReplaceInstanceCell(ComponentTypeId{setId}, key.x, key.z, instances);
    }
    return placed;
}

// 円の中の個体を消す。
uint32_t EraseFrom(const WorldView& world, uint64_t setId, float centerX, float centerZ, float radius,
                   InstanceEditRecorder& recorder) {
    const float radiusSquared = radius * radius;
    uint32_t erased = 0u;
    std::vector<GameProjectWorldInstance> kept{};
    for (int32_t z = InstanceCellIndex(centerZ - radius); z <= InstanceCellIndex(centerZ + radius); ++z) {
        for (int32_t x = InstanceCellIndex(centerX - radius); x <= InstanceCellIndex(centerX + radius); ++x) {
            const auto cell = world.ReadInstanceCell(ComponentTypeId{setId}, x, z);
            kept.clear();
            for (const GameProjectWorldInstance& instance : cell) {
                const float dx = instance.position.x - centerX;
                const float dz = instance.position.z - centerZ;
                if (dx * dx + dz * dz >= radiusSquared) {
                    kept.push_back(instance);
                } else {
                    recorder.Exclude(instance);
                }
            }
            if (kept.size() == cell.size()) {
                continue;
            }
            erased += static_cast<uint32_t>(cell.size() - kept.size());
            recorder.Remember(world, CellKey{setId, x, z});
            (void)world.ReplaceInstanceCell(ComponentTypeId{setId}, x, z, kept);
        }
    }
    return erased;
}

// 地点に一番近い個体の居場所。掴める距離に無ければ空。
struct PickedInstance {
    int32_t cellX = 0;
    int32_t cellZ = 0;
    size_t index = 0u;
};

std::optional<PickedInstance> PickInstance(const WorldView& world, uint64_t setId, float x, float z) {
    std::optional<PickedInstance> best{};
    float bestSquared = kPickRadiusMeters * kPickRadiusMeters;
    for (int32_t cellZ = InstanceCellIndex(z - kPickRadiusMeters); cellZ <= InstanceCellIndex(z + kPickRadiusMeters);
         ++cellZ) {
        for (int32_t cellX = InstanceCellIndex(x - kPickRadiusMeters);
             cellX <= InstanceCellIndex(x + kPickRadiusMeters); ++cellX) {
            const auto cell = world.ReadInstanceCell(ComponentTypeId{setId}, cellX, cellZ);
            for (size_t index = 0u; index < cell.size(); ++index) {
                const float dx = cell[index].position.x - x;
                const float dz = cell[index].position.z - z;
                const float squared = dx * dx + dz * dz;
                if (squared < bestSquared) {
                    bestSquared = squared;
                    best = PickedInstance{cellX, cellZ, index};
                }
            }
        }
    }
    return best;
}

// 掴んだ個体を地点へ動かして手で置いた個体にする。格子をまたげば移し替える。次の居場所を返す。
PickedInstance MoveInstance(const WorldView& world, const TerrainStore& store, uint64_t setId,
                            const PickedInstance& picked, float x, float z, InstanceEditRecorder& recorder) {
    const auto source = world.ReadInstanceCell(ComponentTypeId{setId}, picked.cellX, picked.cellZ);
    if (picked.index >= source.size()) {
        return picked;
    }
    GameProjectWorldInstance moved = source[picked.index];
    if (moved.position.x == x && moved.position.z == z) {
        return picked;
    }
    recorder.Remember(world, CellKey{setId, picked.cellX, picked.cellZ});
    recorder.Exclude(moved);
    moved.position = {x, store.HeightAt(x, z).value_or(moved.position.y), z};
    moved.flags |= static_cast<uint16_t>(GameProjectWorldInstanceFlags::HandPlaced);
    const int32_t cellX = InstanceCellIndex(x);
    const int32_t cellZ = InstanceCellIndex(z);
    std::vector<GameProjectWorldInstance> instances(source.begin(), source.end());
    if (cellX == picked.cellX && cellZ == picked.cellZ) {
        instances[picked.index] = moved;
        (void)world.ReplaceInstanceCell(ComponentTypeId{setId}, cellX, cellZ, instances);
        return picked;
    }
    instances.erase(instances.begin() + static_cast<std::ptrdiff_t>(picked.index));
    (void)world.ReplaceInstanceCell(ComponentTypeId{setId}, picked.cellX, picked.cellZ, instances);
    recorder.Remember(world, CellKey{setId, cellX, cellZ});
    const auto target = world.ReadInstanceCell(ComponentTypeId{setId}, cellX, cellZ);
    instances.assign(target.begin(), target.end());
    instances.push_back(moved);
    (void)world.ReplaceInstanceCell(ComponentTypeId{setId}, cellX, cellZ, instances);
    return PickedInstance{cellX, cellZ, instances.size() - 1u};
}

class InstanceTool final : public IEditorViewportTool {
public:
    explicit InstanceTool(std::shared_ptr<EditorShared> shared) : shared_(std::move(shared)) {}
    const char* GetToolId() const noexcept override { return kInstanceToolId; }
    const char* GetDisplayName() const noexcept override { return "Place Instances"; }
    void OnDeactivate() override { Finish(); }

    void Update(const EditorViewportInput& input, IEditorViewportCanvas& canvas) override {
        TerrainStore& store = TerrainStore::Get();
        const bool hasSet = store.World().FindInstanceSet(ComponentTypeId{shared_->selectedInstanceSet}) != nullptr;
        const bool moving = shared_->instanceBrush.mode == InstanceBrushMode::Move;
        canvas.DrawHint(!hasSet  ? "Place: pick an instance set on the left, or make a new one"
                        : moving ? "Move: drag an instance to move it. It stays when the rules update"
                                 : "Place: drag to place, Shift erases, [ ] or Ctrl + wheel size",
                        kInstanceColor);
        if (!store.IsOpen() || !hasSet) {
            return;
        }
        InstanceBrushSettings& brush = shared_->instanceBrush;
        AdjustBrushRadius(input, brush.radiusMeters);
        float hit[3]{};
        const bool hasHit = RayToGround(input, store, hit);
        if (moving) {
            UpdateMove(input, canvas, store, hasHit, hit);
            return;
        }
        if (hasHit) {
            DrawGroundCircle(canvas, store, hit, brush.radiusMeters, kInstanceColor);
        }
        const bool begin = input.primaryPressed && hasHit && !recorder_.has_value();
        if (begin) {
            recorder_.emplace();
        }
        if (recorder_.has_value() && input.primaryDown && hasHit) {
            const float dx = hit[0] - last_[0];
            const float dz = hit[2] - last_[1];
            const float step = (std::max)(brush.radiusMeters * 0.5f, 0.1f);
            if (begin || dx * dx + dz * dz >= step * step) {
                last_[0] = hit[0];
                last_[1] = hit[2];
                if (input.shift) {
                    (void)EraseFrom(store.World(), shared_->selectedInstanceSet, hit[0], hit[2], brush.radiusMeters,
                                    *recorder_);
                } else {
                    (void)PlaceInto(store.World(), store, shared_->selectedInstanceSet, hit[0], hit[2], brush,
                                    shared_->instanceRandom, *recorder_);
                }
            }
        }
        if (recorder_.has_value() && !input.primaryDown) {
            Finish();
        }
    }

private:
    void UpdateMove(const EditorViewportInput& input, IEditorViewportCanvas& canvas, const TerrainStore& store,
                    bool hasHit, const float hit[3]) {
        const uint64_t setId = shared_->selectedInstanceSet;
        if (hasHit) {
            DrawGroundCircle(canvas, store, hit, kPickRadiusMeters, kInstanceColor);
        }
        if (input.primaryPressed && hasHit && !recorder_.has_value()) {
            grabbed_ = PickInstance(store.World(), setId, hit[0], hit[2]);
            if (grabbed_.has_value()) {
                recorder_.emplace();
            }
        }
        if (grabbed_.has_value() && input.primaryDown && hasHit) {
            grabbed_ = MoveInstance(store.World(), store, setId, *grabbed_, hit[0], hit[2], *recorder_);
        }
        if (recorder_.has_value() && !input.primaryDown) {
            Finish();
        }
    }

    void Finish() {
        grabbed_.reset();
        if (recorder_.has_value()) {
            recorder_->Finish(*shared_, "Edit Instances");
            recorder_.reset();
        }
    }

    std::shared_ptr<EditorShared> shared_;
    std::optional<InstanceEditRecorder> recorder_{};
    std::optional<PickedInstance> grabbed_{};
    float last_[2]{};
};

} // namespace

bool PlaceInstances(EditorShared& shared, ComponentTypeId setId, float x, float z, const InstanceBrushSettings& brush,
                    uint32_t& outPlaced) {
    TerrainStore& store = TerrainStore::Get();
    if (!store.IsOpen() || store.World().FindInstanceSet(setId) == nullptr) {
        return false;
    }
    InstanceEditRecorder recorder{};
    outPlaced = PlaceInto(store.World(), store, setId.value, x, z, brush, shared.instanceRandom, recorder);
    recorder.Finish(shared, "Place Instances");
    return true;
}

bool EraseInstances(EditorShared& shared, ComponentTypeId setId, float x, float z, float radius, uint32_t& outErased) {
    TerrainStore& store = TerrainStore::Get();
    if (!store.IsOpen() || store.World().FindInstanceSet(setId) == nullptr) {
        return false;
    }
    InstanceEditRecorder recorder{};
    outErased = EraseFrom(store.World(), setId.value, x, z, radius, recorder);
    recorder.Finish(shared, "Erase Instances");
    return true;
}

std::unique_ptr<IEditorViewportTool> MakeInstanceTool(std::shared_ptr<EditorShared> shared) {
    return std::make_unique<InstanceTool>(std::move(shared));
}

} // namespace HIKARI::TERRAINKIT
