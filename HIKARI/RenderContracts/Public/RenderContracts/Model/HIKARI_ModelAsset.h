#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "RenderContracts/Model/HIKARI_AnimationAssetTypes.h"
#include "Core/HIKARI_AssetGuid.h"
#include "RenderContracts/Model/HIKARI_ModelAssetTypes.h"
#include "RenderContracts/Model/Policies/HIKARI_ModelMaterialPolicy.h"
#include "RenderContracts/Model/Policies/HIKARI_ModelSurfacePolicy.h"

namespace HIKARI {

class ModelAsset {
public:
    enum class State { Unloaded, Loaded, Failed };

    const std::string& GetName() const;
    const std::string& GetSourcePath() const;
    State GetState() const;

    void SetName(std::string name);
    void SetSourcePath(std::string path);
    void SetState(State state);

    bool HasSkinnedMesh() const;
    const SkeletonAsset* FindSkin(int skinIndex) const;
    const AnimationClip* FindAnimationClip(std::string_view name) const;
    const AnimationClip* FindAnimationClip(AnimationClipId clipId) const;
    const AnimationClip* GetAnimationClip(size_t index) const;
    AnimationClipId GetAnimationClipId(size_t index) const noexcept;

public:
    AssetGuid id{};
    std::string sourcePath;
    State state = State::Unloaded;
    // Monotonic in-memory source generation. Runtime caches may retain
    // resolved node/clip pointers only while this value still matches;
    // it is intentionally not serialized into HMODEL payloads.
    uint64_t sourceRevision = 1u;
    std::vector<ModelNode> nodes;
    std::vector<MeshAsset> meshes;
    std::vector<MaterialAsset> materials;
    std::vector<MaterialImportSourceSlots> materialImportSources;
    std::vector<TextureAsset3D> textures;
    std::vector<SkeletonAsset> skins;
    std::vector<AnimationClip> animations;
    ModelImportDiagnostics importDiagnostics{};
    int defaultSceneRootNode = -1;
    Bounds bounds{};
};

} // namespace HIKARI
