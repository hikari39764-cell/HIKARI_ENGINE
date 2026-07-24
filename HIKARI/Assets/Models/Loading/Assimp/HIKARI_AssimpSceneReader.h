#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct aiScene;

namespace HIKARI {
    class ModelAsset;
}

namespace HIKARI::ASSETS::MODELS::ASSIMP {

    int ReadSceneHierarchy(
        const aiScene& scene,
        ModelAsset& asset,
        std::unordered_map<std::string, int>& nodeNameToIndex);

    void FinalizeSceneMeshBindings(
        const std::vector<int>& meshToSkin,
        const std::vector<int>& assimpMeshToModelMesh,
        ModelAsset& asset);

    void ReadAnimations(
        const aiScene& scene,
        const std::unordered_map<std::string, int>& nodeNameToIndex,
        ModelAsset& asset);

} // namespace HIKARI::ASSETS::MODELS::ASSIMP
