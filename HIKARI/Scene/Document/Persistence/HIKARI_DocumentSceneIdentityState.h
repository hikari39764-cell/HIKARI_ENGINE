#pragma once

#include <cstdint>
#include <string>

#include "Assets/HIKARI_AssetGuid.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"

namespace HIKARI {

    struct DocumentSceneIdentityState {
        std::string sceneId{};
        std::string scenePath{};
        SceneSerializer serializer{};
        SceneDocument document{};
        uint64_t documentRevision = 0;
        AssetGuid currentSceneAssetGuid{};
        bool documentDirty = false;
    };

} // namespace HIKARI
