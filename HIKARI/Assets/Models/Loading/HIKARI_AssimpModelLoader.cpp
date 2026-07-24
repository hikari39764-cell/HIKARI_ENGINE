#include "Assets/Models/Loading/HIKARI_AssimpModelLoader.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Assets/Models/Loading/Assimp/HIKARI_AssimpMaterialReader.h"
#include "Assets/Models/Loading/Assimp/HIKARI_AssimpMeshReader.h"
#include "Assets/Models/Loading/Assimp/HIKARI_AssimpSceneReader.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::ASSETS::MODELS {

    bool LoadAssimpModelSource(ModelAsset& asset) {
        const std::filesystem::path sourcePath(asset.GetSourcePath());
        const std::filesystem::path sourceDirectory = sourcePath.parent_path();

        Assimp::Importer importer{};
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

        constexpr unsigned int kPostProcessFlags =
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_CalcTangentSpace |
            aiProcess_JoinIdenticalVertices |
            aiProcess_LimitBoneWeights |
            aiProcess_ImproveCacheLocality |
            aiProcess_SortByPType |
            aiProcess_FindInvalidData |
            aiProcess_ValidateDataStructure;

        const aiScene* scene = importer.ReadFile(sourcePath.string(), kPostProcessFlags);
        if (scene == nullptr || scene->mRootNode == nullptr || scene->mNumMeshes == 0u) {
            asset.importDiagnostics.messages.push_back(
                "[Assimp] failed to read source: " +
                std::string(importer.GetErrorString()));
            return false;
        }

        asset.nodes.clear();
        asset.meshes.clear();
        asset.materials.clear();
        asset.textures.clear();
        asset.skins.clear();
        asset.animations.clear();
        asset.defaultSceneRootNode = -1;
        asset.bounds = {};
        asset.importDiagnostics = {};
        asset.importDiagnostics.sourceFormat = "Assimp/FBX";
        asset.importDiagnostics.objectCount = scene->mNumMeshes;
        asset.importDiagnostics.groupCount = scene->mRootNode->mNumChildren;

        ASSIMP::ReadMaterials(*scene, sourceDirectory, asset);

        std::unordered_map<std::string, int> nodeNameToIndex;
        asset.defaultSceneRootNode =
            ASSIMP::ReadSceneHierarchy(*scene, asset, nodeNameToIndex);

        std::vector<int> assimpMeshToModelMesh(scene->mNumMeshes, -1);
        std::vector<int> meshToSkin(scene->mNumMeshes, -1);
        ASSIMP::ReadMeshes(
            *scene,
            nodeNameToIndex,
            assimpMeshToModelMesh,
            meshToSkin,
            asset);

        ASSIMP::FinalizeSceneMeshBindings(
            meshToSkin,
            assimpMeshToModelMesh,
            asset);
        ASSIMP::ReadAnimations(*scene, nodeNameToIndex, asset);

        BOUNDS::EnsureModelBounds(asset);

        if (asset.meshes.empty()) {
            asset.importDiagnostics.messages.push_back(
                "[Assimp] no renderable triangle mesh was produced");
            return false;
        }
        return true;
    }

} // namespace HIKARI::ASSETS::MODELS