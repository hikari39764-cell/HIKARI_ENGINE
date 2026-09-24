#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

struct PrefabDocument {
    uint32_t version = 2;
    std::string prefabName{};
    SceneObjectData rootObject{};
    std::vector<SceneObjectData> descendants{};
};

bool ValidatePrefabDocument(const PrefabDocument& document, std::string& outError);
bool CapturePrefabDocument(const SceneDocument& scene, SceneObjectId rootId, std::string_view name,
                           PrefabDocument& outDocument, std::string& outError);
bool InstantiatePrefabDocument(const PrefabDocument& prefab, std::string_view prefabId, SceneDocument& scene,
                               uint64_t& nextId, SceneObjectId& outRoot, std::string& outError);

} // namespace HIKARI
