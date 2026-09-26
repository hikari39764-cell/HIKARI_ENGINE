#include "Scene/Prefab/HIKARI_PrefabDocument.h"

#include <unordered_map>
#include <unordered_set>

#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

namespace HIKARI {
bool ValidatePrefabDocument(const PrefabDocument& document, std::string& outError) {
    outError.clear();
    if (document.version != 2 || document.rootObject.id.value == 0 || document.rootObject.parent) {
        outError = "Prefab requires version 2 and a nonzero, unparented root.";
        return false;
    }
    std::unordered_map<uint64_t, uint64_t> parents{{document.rootObject.id.value, 0}};
    for (const auto& object : document.descendants) {
        if (object.id.value == 0 || !object.parent || !parents.emplace(object.id.value, object.parent->value).second) {
            outError = "Prefab descendant has a missing parent, zero ID or duplicate ID.";
            return false;
        }
    }
    // Each resolved chain is cached: validation remains linear for deep hierarchies.
    std::unordered_set<uint64_t> resolved{document.rootObject.id.value};
    for (const auto& object : document.descendants) {
        std::unordered_set<uint64_t> path;
        uint64_t cursor = object.id.value;
        while (!resolved.contains(cursor)) {
            const auto found = parents.find(cursor);
            if (found == parents.end() || !path.insert(cursor).second) {
                outError = "Prefab hierarchy contains an external parent or a cycle.";
                return false;
            }
            cursor = found->second;
        }
        resolved.insert(path.begin(), path.end());
    }
    return true;
}

bool CapturePrefabDocument(const SceneDocument& scene, SceneObjectId rootId, std::string_view name,
                           PrefabDocument& outDocument, std::string& outError) {
    const auto* root = FindSceneObjectData(scene, rootId);
    if (root == nullptr) {
        outError = "Prefab root does not exist in the scene.";
        return false;
    }
    PrefabDocument captured;
    captured.prefabName = name;
    captured.rootObject = *root;
    captured.rootObject.parent.reset();
    const auto ids = CollectObjectSubtreeIds(scene, rootId);
    std::unordered_map<uint64_t, uint64_t> localIds;
    for (const auto id : ids)
        localIds.emplace(id, id);
    for (const auto& object : scene.objects) {
        if (object.id != rootId && ids.contains(object.id.value))
            captured.descendants.push_back(object);
    }
    uint32_t externalReferenceCount = 0;
    const auto prepare = [&](SceneObjectData& object) {
        object.sourcePrefabId.clear();
        object.editorLocked = false;
        object.editorFolder.clear();
        for (auto& component : object.components) {
            externalReferenceCount += SCENE::SERIALIZATION::RemapSceneObjectReferences(component.properties, localIds);
        }
    };
    prepare(captured.rootObject);
    for (auto& object : captured.descendants)
        prepare(object);
    if (externalReferenceCount != 0) {
        outError = "Prefab has " + std::to_string(externalReferenceCount) +
                   " scene reference(s) outside the selected hierarchy. Include the targets or clear those bindings.";
        return false;
    }
    if (!ValidatePrefabDocument(captured, outError))
        return false;
    outDocument = std::move(captured);
    return true;
}

bool InstantiatePrefabDocument(const PrefabDocument& prefab, std::string_view prefabId, SceneDocument& scene,
                               uint64_t& nextId, SceneObjectId& outRoot, std::string& outError) {
    outRoot = {};
    if (!ValidatePrefabDocument(prefab, outError))
        return false;
    std::vector<SceneObjectData> objects;
    objects.reserve(prefab.descendants.size() + 1);
    objects.push_back(prefab.rootObject);
    objects.insert(objects.end(), prefab.descendants.begin(), prefab.descendants.end());
    objects.front().sourcePrefabId = prefabId;
    if (objects.front().name.empty())
        objects.front().name = prefab.prefabName;
    std::unordered_map<uint64_t, uint64_t> ids;
    if (!AppendClonedSceneObjects(objects, scene, nextId, ids, true)) {
        outError = "Could not allocate prefab instance identities.";
        return false;
    }
    outRoot.value = ids.at(prefab.rootObject.id.value);
    return true;
}
} // namespace HIKARI
