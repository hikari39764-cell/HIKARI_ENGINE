#include "Assets/Models/HIKARI_ModelAsset.h"

#include <utility>

namespace HIKARI {

const std::string& ModelAsset::GetName() const { return id.value; }

const std::string& ModelAsset::GetSourcePath() const { return sourcePath; }

ModelAsset::State ModelAsset::GetState() const { return state; }

void ModelAsset::SetName(std::string name) { id.value = std::move(name); }

void ModelAsset::SetSourcePath(std::string path) { sourcePath = std::move(path); }

void ModelAsset::SetState(State stateValue) { state = stateValue; }

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

size_t ModelAsset::GetSkinCount() const { return skins.size(); }

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

const AnimationClip* ModelAsset::FindAnimationClip(AnimationClipId clipId) const {
    if (!clipId.IsValid()) {
        return nullptr;
    }
    for (size_t index = 0u; index < animations.size(); ++index) {
        if (GetAnimationClipId(index) == clipId) {
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

AnimationClipId ModelAsset::GetAnimationClipId(size_t index) const noexcept {
    if (index >= animations.size())
        return {};

    // Use the occurrence among clips with the same name instead of the
    // absolute array index. Reimporting an unrelated clip before this one
    // must not invalidate serialized Animator references.
    size_t duplicateOrdinal = 0u;
    for (size_t candidate = 0u; candidate < index; ++candidate) {
        if (animations[candidate].name == animations[index].name) {
            ++duplicateOrdinal;
        }
    }
    return MakeAnimationClipId(animations[index].name, duplicateOrdinal);
}

} // namespace HIKARI
