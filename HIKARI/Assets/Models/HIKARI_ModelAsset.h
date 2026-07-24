#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Animation/Assets/HIKARI_AnimationAssetTypes.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Models/HIKARI_ModelAssetTypes.h"
#include "Assets/Models/Policies/HIKARI_ModelMaterialPolicy.h"
#include "Assets/Models/Policies/HIKARI_ModelSurfacePolicy.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/HIKARI_Mesh.h"

namespace HIKARI {

    class ModelAsset {
    public:
        enum class State {
            Unloaded,
            Loaded,
            Failed
        };

        const std::string& GetName() const;
        const std::string& GetSourcePath() const;
        State GetState() const;

        void SetName(std::string name);
        void SetSourcePath(std::string path);
        void SetState(State state);

        bool HasSkeleton() const;
        bool HasSkinnedMesh() const;
        bool HasAnimations() const;
        size_t GetSkinCount() const;
        size_t GetAnimationCount() const;
        const SkeletonAsset* FindSkin(int skinIndex) const;
        const AnimationClip* FindAnimationClip(std::string_view name) const;
        const AnimationClip* FindAnimationClip(AnimationClipId id) const;
        const AnimationClip* GetAnimationClip(size_t index) const;
        AnimationClipId GetAnimationClipId(size_t index) const noexcept;
        float GetAnimationDuration(std::string_view name) const;

        // 旧 CPU draw-item 路径仍依赖这组运行时资源；显式标出 legacy，避免与源模型数据混淆。
        Mesh* GetLegacyRuntimeMesh();
        const Mesh* GetLegacyRuntimeMesh() const;
        Material* GetLegacyRuntimeMaterial();
        const Material* GetLegacyRuntimeMaterial() const;
        void SetLegacyRuntimeMesh(std::unique_ptr<Mesh> mesh);
        void SetLegacyRuntimeMaterial(std::unique_ptr<Material> material);

    public:
        AssetId id{};
        std::string sourcePath;
        State state = State::Unloaded;
        std::vector<ModelNode> nodes;
        std::vector<MeshAsset> meshes;
        std::vector<MaterialAsset> materials;
        std::vector<TextureAsset3D> textures;
        std::vector<SkeletonAsset> skins;
        std::vector<AnimationClip> animations;
        ModelImportDiagnostics importDiagnostics{};
        int defaultSceneRootNode = -1;
        Bounds bounds{};

    private:
        std::unique_ptr<Mesh> legacyRuntimeMesh_;
        std::unique_ptr<Material> legacyRuntimeMaterial_;
    };

} // namespace HIKARI
