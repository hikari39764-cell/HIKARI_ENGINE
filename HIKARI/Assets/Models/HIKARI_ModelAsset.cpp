#include "Assets/Models/HIKARI_ModelAsset.h"

#include <utility>

namespace HIKARI {

    const std::string& ModelAsset::GetName() const {
        return id.value;
    }

    const std::string& ModelAsset::GetSourcePath() const {
        return sourcePath;
    }

    ModelAsset::State ModelAsset::GetState() const {
        return state;
    }

    void ModelAsset::SetName(std::string name) {
        id.value = std::move(name);
    }

    void ModelAsset::SetSourcePath(std::string path) {
        sourcePath = std::move(path);
    }

    void ModelAsset::SetState(State stateValue) {
        state = stateValue;
    }

    bool ModelAsset::HasSkeleton() const {
        return !skins.empty();
    }

    bool ModelAsset::HasSkinnedMesh() const {
        for (const MeshAsset& mesh : meshes) {
            for (const MeshPrimitive& primitive : mesh.primitives) {
                if (primitive.layout == VertexLayoutKind::SkinnedPNTTJW || !primitive.skinnedVertices.empty()) {
                    return true;
                }
            }
        }
        return false;
    }

    bool ModelAsset::HasAnimations() const {
        return !animations.empty();
    }

    size_t ModelAsset::GetSkinCount() const {
        return skins.size();
    }

    size_t ModelAsset::GetAnimationCount() const {
        return animations.size();
    }

    const SkeletonAsset* ModelAsset::FindSkin(int skinIndex) const {
        if (skinIndex < 0 || skinIndex >= static_cast<int>(skins.size())) {
            return nullptr;
        }
        return &skins[static_cast<size_t>(skinIndex)];
    }

    const AnimationClip* ModelAsset::FindAnimationClip(std::string_view name) const {
        for (const AnimationClip& clip : animations) {
            if (clip.name == name) {
                return &clip;
            }
        }
        return nullptr;
    }

    const AnimationClip* ModelAsset::FindAnimationClip(
        AnimationClipId id) const {
        if (!id.IsValid()) {
            return nullptr;
        }
        for (size_t index = 0u; index < animations.size(); ++index) {
            if (GetAnimationClipId(index) == id) {
                return &animations[index];
            }
        }
        return nullptr;
    }

    const AnimationClip* ModelAsset::GetAnimationClip(size_t index) const {
        if (index >= animations.size()) {
            return nullptr;
        }
        return &animations[index];
    }

    AnimationClipId ModelAsset::GetAnimationClipId(
        size_t index) const noexcept {
        if (index >= animations.size()) return {};

        // Use the occurrence among clips with the same name instead of the
        // absolute array index. Reimporting an unrelated clip before this one
        // must not invalidate serialized Animator references.
        size_t duplicateOrdinal = 0u;
        for (size_t candidate = 0u; candidate < index; ++candidate) {
            if (animations[candidate].name == animations[index].name) {
                ++duplicateOrdinal;
            }
        }
        return MakeAnimationClipId(
            animations[index].name,
            duplicateOrdinal);
    }

    float ModelAsset::GetAnimationDuration(std::string_view name) const {
        const AnimationClip* clip = FindAnimationClip(name);
        return clip != nullptr ? clip->durationSec : 0.0f;
    }

    Mesh* ModelAsset::GetLegacyRuntimeMesh() { return legacyRuntimeMesh_.get(); }
    const Mesh* ModelAsset::GetLegacyRuntimeMesh() const { return legacyRuntimeMesh_.get(); }
    Material* ModelAsset::GetLegacyRuntimeMaterial() { return legacyRuntimeMaterial_.get(); }
    const Material* ModelAsset::GetLegacyRuntimeMaterial() const { return legacyRuntimeMaterial_.get(); }
    void ModelAsset::SetLegacyRuntimeMesh(std::unique_ptr<Mesh> mesh) { legacyRuntimeMesh_ = std::move(mesh); }
    void ModelAsset::SetLegacyRuntimeMaterial(std::unique_ptr<Material> material) { legacyRuntimeMaterial_ = std::move(material); }

} // namespace HIKARI
