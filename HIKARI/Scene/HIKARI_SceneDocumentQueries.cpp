#include "Scene/HIKARI_SceneDocumentQueries.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Assets/Material/HIKARI_MaterialBuiltInAssets.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

namespace HIKARI {

std::unordered_set<uint64_t> CollectObjectSubtreeIds(const SceneDocument& document, SceneObjectId rootId) {
    std::unordered_map<uint64_t, std::vector<uint64_t>> children;
    for (const auto& object : document.objects) {
        if (object.parent)
            children[object.parent->value].push_back(object.id.value);
    }
    std::unordered_set<uint64_t> ids;
    std::vector<uint64_t> pending{rootId.value};
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (!ids.insert(id).second)
            continue;
        if (const auto found = children.find(id); found != children.end()) {
            pending.insert(pending.end(), found->second.begin(), found->second.end());
        }
    }
    return ids;
}

SceneObjectChanges DiffSceneObjects(std::span<const SceneObjectData> before, std::span<const SceneObjectData> after) {
    std::unordered_map<uint64_t, const SceneObjectData*> previous;
    previous.reserve(before.size());
    for (const auto& object : before) {
        previous.emplace(object.id.value, &object);
    }
    SceneObjectChanges changes;
    for (const auto& object : after) {
        const auto found = previous.find(object.id.value);
        if (found == previous.end()) {
            changes.added.push_back(object.id);
            continue;
        }
        if (!(*found->second == object)) {
            changes.changed.push_back(object.id);
        }
        previous.erase(found);
    }
    for (const auto& object : before) {
        if (previous.contains(object.id.value)) {
            changes.removed.push_back(object.id);
        }
    }
    return changes;
}

bool AppendClonedSceneObjects(std::span<const SceneObjectData> source, SceneDocument& destination, uint64_t& nextId,
                              std::unordered_map<uint64_t, uint64_t>& outIds, bool detachExternalParents) {
    outIds.clear();
    if (source.empty())
        return false;
    std::unordered_set<uint64_t> used;
    for (const auto& object : destination.objects)
        used.insert(object.id.value);
    std::unordered_map<uint64_t, uint64_t> ids;
    uint64_t candidate = nextId == 0 ? 1 : nextId;
    for (const auto& object : source) {
        if (object.id.value == 0 || ids.contains(object.id.value))
            return false;
        while (used.contains(candidate)) {
            if (++candidate == 0)
                candidate = 1;
        }
        ids.emplace(object.id.value, candidate);
        used.insert(candidate);
        if (++candidate == 0)
            candidate = 1;
    }
    std::vector<SceneObjectData> clones(source.begin(), source.end());
    for (auto& object : clones) {
        object.id.value = ids.at(object.id.value);
        if (object.parent) {
            if (const auto parent = ids.find(object.parent->value); parent != ids.end()) {
                object.parent->value = parent->second;
            } else if (detachExternalParents) {
                object.parent.reset();
            }
        }
        for (auto& component : object.components) {
            SCENE::SERIALIZATION::RemapSceneObjectReferences(component.properties, ids);
        }
    }
    destination.objects.reserve(destination.objects.size() + clones.size());
    for (auto& object : clones)
        destination.objects.push_back(std::move(object));
    nextId = candidate;
    outIds = std::move(ids);
    return true;
}

SceneObjectId FindScenePlayer(const SceneDocument& document) noexcept {
    for (const std::string_view type : {"CharacterInputComponent", "CharacterLocomotionComponent"}) {
        for (const SceneObjectData& object : document.objects) {
            for (const SceneComponentData& component : object.components) {
                if (component.type == type) {
                    return object.id;
                }
            }
        }
    }
    return {};
}

SceneObjectId AllocateSceneObjectId(const SceneDocument& document) {
    std::unordered_set<uint64_t> usedIds{};
    uint64_t maximumId = 0u;
    for (const SceneObjectData& object : document.objects) {
        if (object.id.value == 0u) {
            continue;
        }
        usedIds.insert(object.id.value);
        maximumId = (std::max)(maximumId, object.id.value);
    }
    uint64_t candidate = maximumId == (std::numeric_limits<uint64_t>::max)() ? 1u : maximumId + 1u;
    while (candidate == 0u || usedIds.contains(candidate)) {
        ++candidate;
        if (candidate == 0u) {
            candidate = 1u;
        }
    }
    return SceneObjectId{candidate};
}

SceneObjectData* FindSceneObjectData(SceneDocument& document, SceneObjectId id) noexcept {
    const auto found = std::find_if(document.objects.begin(), document.objects.end(),
                                    [id](const SceneObjectData& object) { return object.id == id; });
    return found != document.objects.end() ? &*found : nullptr;
}

const SceneObjectData* FindSceneObjectData(const SceneDocument& document, SceneObjectId id) noexcept {
    const auto found = std::find_if(document.objects.begin(), document.objects.end(),
                                    [id](const SceneObjectData& object) { return object.id == id; });
    return found != document.objects.end() ? &*found : nullptr;
}

SceneObjectData BuildDefaultGridFloorSceneObject(SceneDocument& document) {
    SceneObjectData object{};
    object.id = AllocateSceneObjectId(document);
    object.name = "Grid Floor";

    object.components.push_back(SceneComponentData{
        "ModelComponent",
        {{"assetId", ""}, {"visible", true}, {"castShadow", false}, {"receiveShadow", true}, {"renderStatic", true}}});
    object.components.push_back(
        SceneComponentData{"ProceduralMeshComponent",
                           {{"kind", "GridPlane"},
                            {"width", 100.0f},
                            {"height", 1.0f},
                            {"depth", 100.0f},
                            {"segmentsX", 50u},
                            {"segmentsY", 50u},
                            {"segmentsZ", 1u},
                            {"sphereSlices", 32u},
                            {"sphereStacks", 16u},
                            {"doubleSided", true},
                            {"generateTangents", true},
                            {"materialSelection", std::string(ASSETS::MATERIAL::kBuiltInCheckerMaterialAssetId)}}});
    object.components.push_back(
        SceneComponentData{"ColliderComponent",
                           {{"enabled", true},
                            {"fitMode", "Geometry"},
                            {"center", {0.0f, 0.0f, 0.0f}},
                            {"rotation", {0.0f, 0.0f, 0.0f}},
                            {"trigger", false},
                            {"material", {{"friction", 0.5f}, {"restitution", 0.0f}, {"density", 1.0f}}},
                            {"filter", {{"layer", 1u}, {"mask", 0xFFFFFFFFu}}},
                            {"shape", "Box"},
                            {"size", {100.0f, 0.02f, 100.0f}},
                            {"radius", 0.01f},
                            {"height", 0.02f}}});

    NormalizeSceneComponentIds(object);
    return object;
}

} // namespace HIKARI
