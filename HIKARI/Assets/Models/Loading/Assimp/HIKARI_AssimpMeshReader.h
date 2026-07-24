#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct aiScene;

namespace HIKARI {
    class ModelAsset;
}

namespace HIKARI::ASSETS::MODELS::ASSIMP {

    void ReadMeshes(
        const aiScene& scene,
        const std::unordered_map<std::string, int>& nodeNameToIndex,
        std::vector<int>& assimpMeshToModelMesh,
        std::vector<int>& meshToSkin,
        ModelAsset& asset);

} // namespace HIKARI::ASSETS::MODELS::ASSIMP
