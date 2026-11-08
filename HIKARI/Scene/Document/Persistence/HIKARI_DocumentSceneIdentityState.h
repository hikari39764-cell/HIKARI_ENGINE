#pragma once

#include <cstdint>
#include <string>

#include "Core/HIKARI_AssetGuid.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"

namespace HIKARI {

struct DocumentSceneIdentityState {
    std::string sceneId{};
    std::string scenePath{};
    SceneSerializer serializer{};
    SceneDocument document{};
    // 分区場景の格子ファイルの最後に読み書きした内容（保存で変わった格子だけ書く）。
    SceneCellFileState cells{};
    uint64_t documentRevision = 0;
    AssetGuid currentSceneAssetGuid{};
    bool documentDirty = false;
    std::string loadError{};
};

} // namespace HIKARI
