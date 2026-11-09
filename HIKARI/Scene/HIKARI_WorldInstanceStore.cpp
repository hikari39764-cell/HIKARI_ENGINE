#include "Scene/HIKARI_WorldInstanceStore.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <system_error>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Scene/Serialization/HIKARI_WorldInstanceFile.h"

namespace HIKARI {
namespace {

constexpr uint64_t kInstanceLayoutHash = 0x48494B4152495F31ull;
// objectIdHigh の最上位が立つ個体は物体ではなく実例集の個体。下位に色の揺らぎと風の強さ、objectIdLow が種。
// HIKARI_MassGpuScene.hlsli の読み方と揃える。
constexpr uint32_t kInstanceIdentityBit = 0x80000000u;

uint32_t EncodeUnit8(float value) noexcept {
    return static_cast<uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

template <size_t Capacity>
bool IsTerminatedText(const char (&text)[Capacity]) noexcept {
    return std::find(std::begin(text), std::end(text), '\0') != std::end(text);
}

GameProjectWorldMassRenderProxyDesc MakeRenderProxy(ComponentTypeId id,
                                                    const GameProjectWorldInstanceSetDesc& desc) noexcept {
    GameProjectWorldMassRenderProxyDesc proxy{};
    proxy.typeId = id;
    proxy.layoutHash = kInstanceLayoutHash;
    proxy.schemaVersion = kGameProjectWorldInstanceVersion;
    proxy.geometry = desc.geometry;
    proxy.flags = (desc.flags & static_cast<uint32_t>(GameProjectWorldInstanceSetFlags::ReceiveShadow)) != 0u
                      ? static_cast<uint32_t>(GameProjectWorldMassRenderFlags::ReceiveShadow)
                      : 0u;
    if ((desc.flags & static_cast<uint32_t>(GameProjectWorldInstanceSetFlags::RayTraced)) == 0u) {
        proxy.flags |= static_cast<uint32_t>(GameProjectWorldMassRenderFlags::SkipRayTracing);
    }
    std::memcpy(proxy.modelAssetGuid, desc.modelAssetGuid, sizeof(proxy.modelAssetGuid));
    proxy.meshIndex = desc.meshIndex;
    proxy.primitiveIndex = desc.primitiveIndex;
    proxy.materialIndex = desc.materialIndex;
    return proxy;
}

GameProjectWorldMassRenderProxyDesc MakeFarRenderProxy(ComponentTypeId id,
                                                       const GameProjectWorldInstanceSetDesc& desc) noexcept {
    GameProjectWorldMassRenderProxyDesc proxy = MakeRenderProxy(id, desc);
    proxy.geometry = GameProjectWorldMassRenderGeometry::ModelAsset;
    proxy.flags |= static_cast<uint32_t>(GameProjectWorldMassRenderFlags::SkipRayTracing);
    std::memcpy(proxy.modelAssetGuid, desc.farModelAssetGuid, sizeof(proxy.modelAssetGuid));
    proxy.meshIndex = 0u;
    proxy.primitiveIndex = kGameProjectWorldMassAllPrimitives;
    proxy.materialIndex = 0u;
    return proxy;
}

} // namespace

bool IsValidWorldInstanceSetDesc(const GameProjectWorldInstanceSetDesc& desc) noexcept {
    if (desc.structSize != kGameProjectWorldInstanceSetDescV1Size || desc.version != kGameProjectWorldInstanceVersion ||
        !IsTerminatedText(desc.name) || desc.name[0] == '\0' || !IsTerminatedText(desc.modelAssetGuid) ||
        !IsTerminatedText(desc.farModelAssetGuid) || !MATH::IsFinite(desc.tintVariation) ||
        !MATH::IsFinite(desc.windStrength) || !MATH::IsFinite(desc.minScreenHeight) ||
        !MATH::IsFinite(desc.farDistance) || desc.farDistance < 0.0f) {
        return false;
    }
    const uint32_t known = static_cast<uint32_t>(GameProjectWorldInstanceSetFlags::ReceiveShadow |
                                                 GameProjectWorldInstanceSetFlags::RayTraced);
    if ((desc.flags & ~known) != 0u) {
        return false;
    }
    switch (desc.geometry) {
    case GameProjectWorldMassRenderGeometry::ModelAsset: return desc.modelAssetGuid[0] != '\0';
    case GameProjectWorldMassRenderGeometry::BuiltInBox:
    case GameProjectWorldMassRenderGeometry::BuiltInSphere: return desc.modelAssetGuid[0] == '\0';
    default: return false;
    }
}

bool WorldInstanceStore::AddSet(const GameProjectWorldInstanceSetDesc& desc, ComponentTypeId* outId) {
    if (!IsValidWorldInstanceSetDesc(desc)) {
        return false;
    }
    const ComponentTypeId id = MakeComponentTypeId(desc.name);
    const auto [found, inserted] = sets_.try_emplace(id.value);
    if (inserted) {
        order_.push_back(id.value);
    }
    Set& set = found->second;
    set.desc = desc;
    set.proxy = MakeRenderProxy(id, desc);
    set.farProxy = MakeFarRenderProxy(id, desc);
    set.renderDirty = true;
    setsDirty_ = true;
    ++revision_;
    if (outId != nullptr) {
        *outId = id;
    }
    return true;
}

bool WorldInstanceStore::RemoveSet(ComponentTypeId id) {
    const auto found = sets_.find(id.value);
    if (found == sets_.end()) {
        return false;
    }
    for (const auto& [cell, instances] : found->second.cells) {
        dirtyCells_.insert(cell);
    }
    sets_.erase(found);
    order_.erase(std::remove(order_.begin(), order_.end(), id.value), order_.end());
    setsDirty_ = true;
    ++revision_;
    return true;
}

const GameProjectWorldInstanceSetDesc* WorldInstanceStore::FindSet(ComponentTypeId id) const noexcept {
    const auto found = sets_.find(id.value);
    return found != sets_.end() ? &found->second.desc : nullptr;
}

uint32_t WorldInstanceStore::CopySetIds(std::span<ComponentTypeId> outIds) const noexcept {
    const size_t count = (std::min)(outIds.size(), order_.size());
    for (size_t index = 0u; index < count; ++index) {
        outIds[index] = ComponentTypeId{order_[index]};
    }
    return static_cast<uint32_t>(order_.size());
}

bool WorldInstanceStore::ReplaceCell(ComponentTypeId id, WorldCellCoord cell,
                                     std::span<const GameProjectWorldInstance> instances) {
    const auto found = sets_.find(id.value);
    if (found == sets_.end()) {
        return false;
    }
    for (const GameProjectWorldInstance& instance : instances) {
        if (!MATH::IsFinite(instance.position.x) || !MATH::IsFinite(instance.position.y) ||
            !MATH::IsFinite(instance.position.z)) {
            return false;
        }
    }
    Set& set = found->second;
    const auto existing = set.cells.find(cell);
    const uint64_t previous = existing != set.cells.end() ? existing->second.size() : 0u;
    if (instances.empty()) {
        if (existing == set.cells.end()) {
            return true;
        }
        set.cells.erase(existing);
    } else {
        set.cells[cell].assign(instances.begin(), instances.end());
    }
    set.count = set.count - previous + instances.size();
    set.renderDirty = true;
    dirtyCells_.insert(cell);
    ++revision_;
    return true;
}

std::span<const GameProjectWorldInstance> WorldInstanceStore::ReadCell(ComponentTypeId id,
                                                                       WorldCellCoord cell) const noexcept {
    const auto found = sets_.find(id.value);
    if (found == sets_.end()) {
        return {};
    }
    const auto instances = found->second.cells.find(cell);
    return instances != found->second.cells.end() ? std::span<const GameProjectWorldInstance>(instances->second)
                                                  : std::span<const GameProjectWorldInstance>{};
}

uint64_t WorldInstanceStore::CountOf(ComponentTypeId id) const noexcept {
    const auto found = sets_.find(id.value);
    return found != sets_.end() ? found->second.count : 0u;
}

uint64_t WorldInstanceStore::TotalCount() const noexcept {
    uint64_t total = 0u;
    for (const auto& [value, set] : sets_) {
        total += set.count;
    }
    return total;
}

void WorldInstanceStore::Clear() noexcept {
    sets_.clear();
    order_.clear();
    dirtyCells_.clear();
    savedDirectory_.clear();
    setsDirty_ = false;
    ++revision_;
}

bool WorldInstanceStore::Load(const std::filesystem::path& cellDirectory, std::string& outError) {
    Clear();
    savedDirectory_ = cellDirectory;
    const std::filesystem::path setsPath = cellDirectory / kWorldInstanceSetsFileName;
    std::error_code ec{};
    if (!std::filesystem::is_regular_file(setsPath, ec)) {
        return true;
    }
    std::vector<GameProjectWorldInstanceSetDesc> descriptions{};
    if (!ReadWorldInstanceSetsFile(setsPath, descriptions, outError)) {
        return false;
    }
    for (const GameProjectWorldInstanceSetDesc& desc : descriptions) {
        if (!AddSet(desc)) {
            outError = setsPath.generic_string() + ": invalid instance set " + desc.name;
            Clear();
            return false;
        }
    }
    for (const auto& entry : std::filesystem::directory_iterator(cellDirectory, ec)) {
        const std::string name = TEXT::PathToUtf8(entry.path().filename());
        WorldCellCoord cell{};
        if (!ParseWorldInstanceCellFileName(name, cell)) {
            continue;
        }
        std::vector<WorldInstanceCellRead> parts{};
        if (!ReadWorldInstanceCellFile(entry.path(), cell, parts, outError)) {
            Clear();
            return false;
        }
        for (WorldInstanceCellRead& part : parts) {
            const auto found = sets_.find(part.setId);
            if (found == sets_.end()) {
                outError =
                    name + " refers to an instance set that is not in " + std::string(kWorldInstanceSetsFileName);
                Clear();
                return false;
            }
            found->second.count += part.instances.size();
            found->second.cells[cell] = std::move(part.instances);
        }
    }
    dirtyCells_.clear();
    setsDirty_ = false;
    savedDirectory_ = cellDirectory;
    return true;
}

bool WorldInstanceStore::Save(const std::filesystem::path& cellDirectory, SaveReport& outReport,
                              std::string& outError) {
    outReport = {};
    const bool sameDirectory = savedDirectory_ == cellDirectory;
    std::error_code ec{};
    const std::filesystem::path setsPath = cellDirectory / kWorldInstanceSetsFileName;
    if (sets_.empty() && !std::filesystem::exists(setsPath, ec) && dirtyCells_.empty()) {
        savedDirectory_ = cellDirectory;
        return true;
    }
    if (!sameDirectory || setsDirty_) {
        std::vector<GameProjectWorldInstanceSetDesc> descriptions{};
        descriptions.reserve(order_.size());
        for (const uint64_t value : order_) {
            descriptions.push_back(sets_.at(value).desc);
        }
        if (!WriteWorldInstanceSetsFile(setsPath, descriptions, outError)) {
            return false;
        }
        outReport.setsWritten = true;
    }
    std::set<WorldCellCoord> cells = dirtyCells_;
    if (!sameDirectory) {
        for (const auto& [value, set] : sets_) {
            for (const auto& [cell, instances] : set.cells) {
                cells.insert(cell);
            }
        }
    }
    std::vector<WorldInstanceCellPart> parts{};
    for (const WorldCellCoord cell : cells) {
        parts.clear();
        for (const uint64_t value : order_) {
            const std::span<const GameProjectWorldInstance> instances = ReadCell(ComponentTypeId{value}, cell);
            if (!instances.empty()) {
                parts.push_back({value, instances});
            }
        }
        const std::filesystem::path path = MakeWorldInstanceCellPath(cellDirectory, cell);
        if (parts.empty()) {
            if (std::filesystem::remove(path, ec)) {
                ++outReport.removedCells;
            }
            continue;
        }
        if (!WriteWorldInstanceCellFile(path, cell, parts, outError)) {
            return false;
        }
        ++outReport.writtenCells;
    }
    dirtyCells_.clear();
    setsDirty_ = false;
    savedDirectory_ = cellDirectory;
    return true;
}

void WorldInstanceStore::CollectRenderViews(std::vector<WorldMassRenderStoreView>& outViews) {
    outViews.clear();
    for (const uint64_t value : order_) {
        Set& set = sets_.at(value);
        if (set.count == 0u) {
            continue;
        }
        if (set.renderDirty) {
            const uint32_t identity = kInstanceIdentityBit | (EncodeUnit8(set.desc.minScreenHeight / 0.0255f) << 16u) |
                                      (EncodeUnit8(set.desc.tintVariation) << 8u) | EncodeUnit8(set.desc.windStrength);
            set.renderInstances.clear();
            set.renderInstances.reserve(set.count);
            for (const auto& [cell, instances] : set.cells) {
                for (const GameProjectWorldInstance& instance : instances) {
                    set.renderInstances.push_back(WorldMassRenderInstance{
                        {instance.position.x, instance.position.y, instance.position.z},
                        instance.seed,
                        {WORLDINSTANCE::DecodeSnorm16(instance.rotation[0]),
                         WORLDINSTANCE::DecodeSnorm16(instance.rotation[1]),
                         WORLDINSTANCE::DecodeSnorm16(instance.rotation[2]),
                         WORLDINSTANCE::DecodeSnorm16(instance.rotation[3])},
                        {WORLDINSTANCE::DecodeHalf(instance.scale[0]), WORLDINSTANCE::DecodeHalf(instance.scale[1]),
                         WORLDINSTANCE::DecodeHalf(instance.scale[2])},
                        identity});
                }
            }
            ++set.renderVersion;
            set.renderDirty = false;
        }
        outViews.push_back(WorldMassRenderStoreView{
            set.proxy.typeId,
            &set.proxy,
            set.renderInstances,
            {},
            set.renderVersion,
            set.renderVersion,
            set.renderVersion,
            set.desc.farModelAssetGuid[0] != '\0' && set.desc.farDistance > 0.0f ? &set.farProxy : nullptr,
            set.desc.farDistance});
    }
}

} // namespace HIKARI
