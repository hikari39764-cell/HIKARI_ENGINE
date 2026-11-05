#include "Assets/Models/Loading/Assimp/HIKARI_AssimpSceneReader.h"

#include <cmath>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <assimp/scene.h>

#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "RenderContracts/Model/HIKARI_ModelBounds.h"
#include "Assets/Models/Loading/Assimp/HIKARI_AssimpTypeConversion.h"

namespace HIKARI::ASSETS::MODELS::ASSIMP {

namespace {
int ReadAssimpNodeRecursive(const aiNode& source, int parent, ModelAsset& asset,
                            std::unordered_map<std::string, int>& nodeNameToIndex) {
    ModelNode node{};
    node.name = source.mName.length > 0 ? ToString(source.mName) : "AssimpNode" + std::to_string(asset.nodes.size());
    node.parent = parent;
    node.hasLocalMatrix = true;
    node.localMatrix = ToMat4(source.mTransformation);

    aiVector3D scale{};
    aiVector3D translation{};
    aiQuaternion rotation{};
    source.mTransformation.Decompose(scale, rotation, translation);
    node.localTransform.position = ToVec3(translation);
    node.localTransform.rotation = ToQuat(rotation);
    node.localTransform.scale = ToVec3(scale);

    if (source.mNumMeshes > 0u) {
        node.meshIndex = static_cast<int>(source.mMeshes[0]);
    }

    const int nodeIndex = static_cast<int>(asset.nodes.size());
    asset.nodes.push_back(std::move(node));
    if (parent >= 0 && parent < static_cast<int>(asset.nodes.size())) {
        asset.nodes[static_cast<size_t>(parent)].children.push_back(nodeIndex);
    }
    nodeNameToIndex[asset.nodes[static_cast<size_t>(nodeIndex)].name] = nodeIndex;

    for (unsigned int meshSlot = 1u; meshSlot < source.mNumMeshes; ++meshSlot) {
        ModelNode meshNode{};
        meshNode.name = asset.nodes[static_cast<size_t>(nodeIndex)].name + "/Mesh" + std::to_string(meshSlot);
        meshNode.parent = nodeIndex;
        meshNode.hasLocalMatrix = true;
        meshNode.localMatrix = MATH::Mat4::Identity();
        meshNode.localTransform.scale = {1.0f, 1.0f, 1.0f};
        meshNode.meshIndex = static_cast<int>(source.mMeshes[meshSlot]);
        const int meshNodeIndex = static_cast<int>(asset.nodes.size());
        asset.nodes.push_back(std::move(meshNode));
        asset.nodes[static_cast<size_t>(nodeIndex)].children.push_back(meshNodeIndex);
    }

    for (unsigned int childIndex = 0; childIndex < source.mNumChildren; ++childIndex) {
        if (source.mChildren[childIndex] != nullptr) {
            ReadAssimpNodeRecursive(*source.mChildren[childIndex], nodeIndex, asset, nodeNameToIndex);
        }
    }
    return nodeIndex;
}

bool IsNearIdentity(const MATH::Mat4& value) {
    const MATH::Mat4 identity = MATH::Mat4::Identity();
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (std::abs(value.m[column][row] - identity.m[column][row]) > 1.0e-5f)
                return false;
    return true;
}

// 一様で正の拡縮を持つ TRS に分解できるか（法線を同じ行列で回せる）。
bool DecomposeUniform(const MATH::Mat4& value, Transform3D& outTransform) {
    MATH::Vec3 scale{};
    if (!MATH::DecomposeTRS(value, outTransform.position, outTransform.rotation, scale) || scale.x <= 0.0f)
        return false;
    outTransform.scale = scale;
    const float tolerance = 1.0e-4f * scale.x;
    return std::abs(scale.y - scale.x) <= tolerance && std::abs(scale.z - scale.x) <= tolerance;
}

template <typename Vertex>
void BakeVertices(std::vector<Vertex>& vertices, const MATH::Mat4& transform) {
    for (Vertex& vertex : vertices) {
        vertex.position = MATH::TransformPoint(transform, vertex.position);
        vertex.normal = MATH::Normalize(MATH::TransformVector(transform, vertex.normal));
        const MATH::Vec3 tangent =
            MATH::Normalize(MATH::TransformVector(transform, {vertex.tangent.x, vertex.tangent.y, vertex.tangent.z}));
        vertex.tangent = {tangent.x, tangent.y, tangent.z, vertex.tangent.w};
    }
}

void AssignSkinsToMeshNodes(const std::vector<int>& meshToSkin, ModelAsset& asset) {
    for (ModelNode& node : asset.nodes) {
        if (node.meshIndex < 0 || node.meshIndex >= static_cast<int>(meshToSkin.size())) {
            continue;
        }
        node.skinIndex = meshToSkin[static_cast<size_t>(node.meshIndex)];
    }
}

void RemapNodeMeshIndices(const std::vector<int>& assimpMeshToModelMesh, ModelAsset& asset) {
    uint32_t missingMeshReferenceCount = 0;
    for (ModelNode& node : asset.nodes) {
        if (node.meshIndex < 0) {
            continue;
        }

        const int assimpMeshIndex = node.meshIndex;
        if (assimpMeshIndex >= 0 && assimpMeshIndex < static_cast<int>(assimpMeshToModelMesh.size()) &&
            assimpMeshToModelMesh[static_cast<size_t>(assimpMeshIndex)] >= 0) {
            node.meshIndex = assimpMeshToModelMesh[static_cast<size_t>(assimpMeshIndex)];
            continue;
        }

        node.meshIndex = -1;
        node.skinIndex = -1;
        ++missingMeshReferenceCount;
    }

    if (missingMeshReferenceCount > 0u) {
        ++asset.importDiagnostics.unsupportedFeatureCount;
        asset.importDiagnostics.messages.push_back(
            "[Assimp] node mesh references were dropped because the referenced source meshes were not renderable. count=" +
            std::to_string(missingMeshReferenceCount));
    }
}

void ReadAssimpAnimations(const aiScene& scene, const std::unordered_map<std::string, int>& nodeNameToIndex,
                          ModelAsset& asset) {
    for (unsigned int animationIndex = 0; animationIndex < scene.mNumAnimations; ++animationIndex) {
        const aiAnimation* source = scene.mAnimations[animationIndex];
        if (source == nullptr) {
            continue;
        }

        const double ticksPerSecond = source->mTicksPerSecond > 0.0 ? source->mTicksPerSecond : 25.0;

        AnimationClip clip{};
        clip.name = source->mName.length > 0 ? ToString(source->mName) : "AssimpClip" + std::to_string(animationIndex);
        clip.durationSec = static_cast<float>(source->mDuration / ticksPerSecond);

        for (unsigned int channelIndex = 0; channelIndex < source->mNumChannels; ++channelIndex) {
            const aiNodeAnim* channel = source->mChannels[channelIndex];
            if (channel == nullptr) {
                continue;
            }

            const std::string nodeName = ToString(channel->mNodeName);
            const auto foundNode = nodeNameToIndex.find(nodeName);
            if (foundNode == nodeNameToIndex.end()) {
                continue;
            }

            if (channel->mNumPositionKeys > 0u) {
                NodeAnimationChannel out{};
                out.targetNode = foundNode->second;
                out.path = AnimationTargetPath::Translation;
                out.interpolation = AnimationInterpolation::Linear;
                out.vec3Keys.reserve(channel->mNumPositionKeys);
                for (unsigned int keyIndex = 0; keyIndex < channel->mNumPositionKeys; ++keyIndex) {
                    const aiVectorKey& key = channel->mPositionKeys[keyIndex];
                    AnimationKeyframe<MATH::Vec3> frame{};
                    frame.timeSec = static_cast<float>(key.mTime / ticksPerSecond);
                    frame.value = ToVec3(key.mValue);
                    out.vec3Keys.push_back(frame);
                }
                clip.channels.push_back(std::move(out));
            }

            if (channel->mNumRotationKeys > 0u) {
                NodeAnimationChannel out{};
                out.targetNode = foundNode->second;
                out.path = AnimationTargetPath::Rotation;
                out.interpolation = AnimationInterpolation::Linear;
                out.quatKeys.reserve(channel->mNumRotationKeys);
                for (unsigned int keyIndex = 0; keyIndex < channel->mNumRotationKeys; ++keyIndex) {
                    const aiQuatKey& key = channel->mRotationKeys[keyIndex];
                    AnimationKeyframe<MATH::Quat> frame{};
                    frame.timeSec = static_cast<float>(key.mTime / ticksPerSecond);
                    frame.value = ToQuat(key.mValue);
                    out.quatKeys.push_back(frame);
                }
                clip.channels.push_back(std::move(out));
            }

            if (channel->mNumScalingKeys > 0u) {
                NodeAnimationChannel out{};
                out.targetNode = foundNode->second;
                out.path = AnimationTargetPath::Scale;
                out.interpolation = AnimationInterpolation::Linear;
                out.vec3Keys.reserve(channel->mNumScalingKeys);
                for (unsigned int keyIndex = 0; keyIndex < channel->mNumScalingKeys; ++keyIndex) {
                    const aiVectorKey& key = channel->mScalingKeys[keyIndex];
                    AnimationKeyframe<MATH::Vec3> frame{};
                    frame.timeSec = static_cast<float>(key.mTime / ticksPerSecond);
                    frame.value = ToVec3(key.mValue);
                    out.vec3Keys.push_back(frame);
                }
                clip.channels.push_back(std::move(out));
            }
        }

        if (!clip.channels.empty()) {
            asset.animations.push_back(std::move(clip));
        }
    }
}

} // namespace

int ReadSceneHierarchy(const aiScene& scene, ModelAsset& asset, std::unordered_map<std::string, int>& nodeNameToIndex) {
    return ReadAssimpNodeRecursive(*scene.mRootNode, -1, asset, nodeNameToIndex);
}

void FinalizeSceneMeshBindings(const std::vector<int>& meshToSkin, const std::vector<int>& assimpMeshToModelMesh,
                               ModelAsset& asset) {
    AssignSkinsToMeshNodes(meshToSkin, asset);
    RemapNodeMeshIndices(assimpMeshToModelMesh, asset);
}

void ReadAnimations(const aiScene& scene, const std::unordered_map<std::string, int>& nodeNameToIndex,
                    ModelAsset& asset) {
    ReadAssimpAnimations(scene, nodeNameToIndex, asset);
}

void BakeSkinnedMeshNodes(ModelAsset& asset) {
    const size_t count = asset.nodes.size();
    std::vector<MATH::Mat4> globals(count, MATH::Mat4::Identity());
    std::vector<bool> pinned(count, false);
    std::vector<uint32_t> meshUses(asset.meshes.size(), 0u);
    for (size_t index = 0; index < count; ++index) {
        const ModelNode& node = asset.nodes[index];
        // 節点は親より後に並ぶ（ReadAssimpNodeRecursive の深さ優先）。崩れていれば何もしない。
        if (node.parent >= static_cast<int>(index))
            return;
        globals[index] =
            node.parent >= 0 ? globals[static_cast<size_t>(node.parent)] * node.localMatrix : node.localMatrix;
        if (node.meshIndex >= 0 && node.meshIndex < static_cast<int>(meshUses.size()))
            ++meshUses[static_cast<size_t>(node.meshIndex)];
    }
    // 関節と動画の対象の節点は動くので焼かない。
    for (const SkeletonAsset& skin : asset.skins)
        for (const SkeletonJoint& joint : skin.joints)
            if (joint.nodeIndex >= 0 && joint.nodeIndex < static_cast<int>(count))
                pinned[static_cast<size_t>(joint.nodeIndex)] = true;
    for (const AnimationClip& clip : asset.animations)
        for (const NodeAnimationChannel& channel : clip.channels)
            if (channel.targetNode >= 0 && channel.targetNode < static_cast<int>(count))
                pinned[static_cast<size_t>(channel.targetNode)] = true;

    uint32_t baked = 0u;
    for (size_t index = 0; index < count; ++index) {
        ModelNode& node = asset.nodes[index];
        if (node.meshIndex < 0 || node.skinIndex < 0 || pinned[index] || IsNearIdentity(globals[index]))
            continue;
        // 節点の網格（自身と /MeshN の子）が全て蒙皮で、どれも一度だけ使われる時だけ焼く。
        std::vector<size_t> carriers{index};
        bool bakeable = true;
        for (const int child : node.children) {
            const ModelNode& slot = asset.nodes[static_cast<size_t>(child)];
            bakeable = bakeable && slot.meshIndex >= 0 && slot.skinIndex >= 0 && slot.children.empty() &&
                       !pinned[static_cast<size_t>(child)];
            carriers.push_back(static_cast<size_t>(child));
        }
        for (const size_t carrier : carriers) {
            const ModelNode& carried = asset.nodes[carrier];
            bakeable = bakeable && carried.meshIndex < static_cast<int>(meshUses.size()) &&
                       meshUses[static_cast<size_t>(carried.meshIndex)] == 1u &&
                       carried.skinIndex < static_cast<int>(asset.skins.size());
        }
        Transform3D global{};
        Transform3D local{};
        const MATH::Mat4 parentGlobal =
            node.parent >= 0 ? globals[static_cast<size_t>(node.parent)] : MATH::Mat4::Identity();
        const MATH::Mat4 localMatrix = MATH::Inverse(parentGlobal);
        if (!bakeable || !DecomposeUniform(globals[index], global) || !DecomposeUniform(localMatrix, local))
            continue;
        const MATH::Mat4 inverseGlobal = MATH::Inverse(globals[index]);
        for (const size_t carrier : carriers) {
            const ModelNode& carried = asset.nodes[carrier];
            MeshAsset& mesh = asset.meshes[static_cast<size_t>(carried.meshIndex)];
            for (MeshPrimitive& primitive : mesh.primitives) {
                BakeVertices(primitive.staticVertices, globals[index]);
                BakeVertices(primitive.skinnedVertices, globals[index]);
                primitive.bounds = BOUNDS::ComputePrimitiveBounds(primitive);
            }
            mesh.bounds = BOUNDS::ComputeMeshBounds(mesh);
            for (SkeletonJoint& joint : asset.skins[static_cast<size_t>(carried.skinIndex)].joints)
                joint.inverseBindMatrix = joint.inverseBindMatrix * inverseGlobal;
        }
        for (const size_t carrier : carriers)
            globals[carrier] = MATH::Mat4::Identity();
        node.localMatrix = localMatrix;
        node.localTransform = local;
        ++baked;
    }
    if (baked > 0u)
        asset.importDiagnostics.messages.push_back("[Assimp] skinned mesh node transforms baked into vertices nodes=" +
                                                   std::to_string(baked));
}

} // namespace HIKARI::ASSETS::MODELS::ASSIMP
