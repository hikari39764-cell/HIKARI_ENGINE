#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "HIKARI_TerrainKitEditorCommon.h"

// 実例集の配置：地形の上へ筆で個体を置き、消す。一筆で撤销一件。変更は触った格子の前後で持つ。
namespace HIKARI::TERRAINKIT {
namespace {

using namespace EDITOR_PROJECT;

constexpr float kCellMeters = 256.0f; // 引擎の既定の格子（WorldCellAt と同じ）
constexpr EditorWorkspaceColor kInstanceColor{0.55f, 0.95f, 0.6f, 1.0f};

struct CellKey {
    uint64_t set = 0u;
    int32_t x = 0;
    int32_t z = 0;
    auto operator<=>(const CellKey&) const = default;
};

int32_t CellIndex(float value) noexcept { return static_cast<int32_t>(std::floor(value / kCellMeters)); }

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

    explicit InstanceUndo(std::vector<Change> changes) : changes_(std::move(changes)) {}
    void Undo() override { Apply(&Change::before); }
    void Redo() override { Apply(&Change::after); }

private:
    void Apply(std::vector<GameProjectWorldInstance> Change::* side) {
        const WorldView world = TerrainStore::Get().World();
        for (const Change& change : changes_) {
            (void)world.ReplaceInstanceCell(ComponentTypeId{change.key.set}, change.key.x, change.key.z, change.*side);
        }
    }

    std::vector<Change> changes_;
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
        if (!changes.empty() && shared.services != nullptr) {
            shared.services->RecordUndo(label, std::make_unique<InstanceUndo>(std::move(changes)));
        }
    }

private:
    std::map<CellKey, std::vector<GameProjectWorldInstance>> before_{};
};

GameProjectQuaternion Multiply(const GameProjectQuaternion& a, const GameProjectQuaternion& b) noexcept {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

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
        const CellKey key{setId, CellIndex(x), CellIndex(z)};
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
        const float yaw = NextRandom(random) * std::numbers::pi_v<float>;
        const GameProjectQuaternion spin{0.0f, std::sin(yaw), 0.0f, std::cos(yaw)};
        const float align = std::clamp(brush.alignToGround, 0.0f, 1.0f);
        // 軸は上向きと法線（-slopeX, 1, -slopeZ）の外積。inverseLength が法線の y。
        const float axisLength = std::sqrt(slopeX * slopeX + slopeZ * slopeZ);
        const float tilt = std::acos(std::clamp(inverseLength, -1.0f, 1.0f)) * align * 0.5f;
        const GameProjectQuaternion lean =
            axisLength > 1.0e-4f ? GameProjectQuaternion{-slopeZ / axisLength * std::sin(tilt), 0.0f,
                                                         slopeX / axisLength * std::sin(tilt), std::cos(tilt)}
                                 : GameProjectQuaternion{};
        const float scale = brush.scaleMin + (brush.scaleMax - brush.scaleMin) * NextRandom(random);
        instances.push_back(WORLDINSTANCE::Encode({x, *height, z}, Multiply(lean, spin), {scale, scale, scale},
                                                  static_cast<uint32_t>(NextRandomBits(random)),
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
    for (int32_t z = CellIndex(centerZ - radius); z <= CellIndex(centerZ + radius); ++z) {
        for (int32_t x = CellIndex(centerX - radius); x <= CellIndex(centerX + radius); ++x) {
            const auto cell = world.ReadInstanceCell(ComponentTypeId{setId}, x, z);
            kept.clear();
            for (const GameProjectWorldInstance& instance : cell) {
                const float dx = instance.position.x - centerX;
                const float dz = instance.position.z - centerZ;
                if (dx * dx + dz * dz >= radiusSquared) {
                    kept.push_back(instance);
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

class InstanceTool final : public IEditorViewportTool {
public:
    explicit InstanceTool(std::shared_ptr<EditorShared> shared) : shared_(std::move(shared)) {}
    const char* GetToolId() const noexcept override { return kInstanceToolId; }
    const char* GetDisplayName() const noexcept override { return "Place Instances"; }
    void OnDeactivate() override { Finish(); }

    void Update(const EditorViewportInput& input, IEditorViewportCanvas& canvas) override {
        TerrainStore& store = TerrainStore::Get();
        const bool hasSet = store.World().FindInstanceSet(ComponentTypeId{shared_->selectedInstanceSet}) != nullptr;
        canvas.DrawHint(hasSet ? "Place Instances: drag to place, Shift erases, Ctrl + wheel sets the radius"
                               : "Place Instances: add or select an instance set in the Terrain window first",
                        kInstanceColor);
        if (!store.IsOpen() || !hasSet) {
            return;
        }
        InstanceBrushSettings& brush = shared_->instanceBrush;
        AdjustBrushRadius(input, brush.radiusMeters);
        float hit[3]{};
        const bool hasHit = RayToGround(input, store, hit);
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
    void Finish() {
        if (recorder_.has_value()) {
            recorder_->Finish(*shared_, "Edit Instances");
            recorder_.reset();
        }
    }

    std::shared_ptr<EditorShared> shared_;
    std::optional<InstanceEditRecorder> recorder_{};
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
