#include "Assets/Models/Loading/Assimp/HIKARI_AssimpSceneReader.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <assimp/scene.h>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Assets/Models/Loading/Assimp/HIKARI_AssimpTypeConversion.h"

namespace HIKARI::ASSETS::MODELS::ASSIMP {

    namespace {
        int ReadAssimpNodeRecursive(
            const aiNode& source,
            int parent,
            ModelAsset& asset,
            std::unordered_map<std::string, int>& nodeNameToIndex) {

            ModelNode node{};
            node.name = source.mName.length > 0
                ? ToString(source.mName)
                : "AssimpNode" + std::to_string(asset.nodes.size());
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
                meshNode.name = asset.nodes[static_cast<size_t>(nodeIndex)].name +
                    "/Mesh" +
                    std::to_string(meshSlot);
                meshNode.parent = nodeIndex;
                meshNode.hasLocalMatrix = true;
                meshNode.localMatrix = MATH::Mat4::Identity();
                meshNode.localTransform.scale = { 1.0f, 1.0f, 1.0f };
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

        void AssignSkinsToMeshNodes(const std::vector<int>& meshToSkin, ModelAsset& asset) {
            for (ModelNode& node : asset.nodes) {
                if (node.meshIndex < 0 || node.meshIndex >= static_cast<int>(meshToSkin.size())) {
                    continue;
                }
                node.skinIndex = meshToSkin[static_cast<size_t>(node.meshIndex)];
            }
        }

        void RemapNodeMeshIndices(
            const std::vector<int>& assimpMeshToModelMesh,
            ModelAsset& asset) {

            uint32_t missingMeshReferenceCount = 0;
            for (ModelNode& node : asset.nodes) {
                if (node.meshIndex < 0) {
                    continue;
                }

                const int assimpMeshIndex = node.meshIndex;
                if (assimpMeshIndex >= 0 &&
                    assimpMeshIndex < static_cast<int>(assimpMeshToModelMesh.size()) &&
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

        void ReadAssimpAnimations(
            const aiScene& scene,
            const std::unordered_map<std::string, int>& nodeNameToIndex,
            ModelAsset& asset) {

            for (unsigned int animationIndex = 0; animationIndex < scene.mNumAnimations; ++animationIndex) {
                const aiAnimation* source = scene.mAnimations[animationIndex];
                if (source == nullptr) {
                    continue;
                }

                const double ticksPerSecond = source->mTicksPerSecond > 0.0
                    ? source->mTicksPerSecond
                    : 25.0;

                AnimationClip clip{};
                clip.name = source->mName.length > 0
                    ? ToString(source->mName)
                    : "AssimpClip" + std::to_string(animationIndex);
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

    int ReadSceneHierarchy(
        const aiScene& scene,
        ModelAsset& asset,
        std::unordered_map<std::string, int>& nodeNameToIndex) {

        return ReadAssimpNodeRecursive(*scene.mRootNode, -1, asset, nodeNameToIndex);
    }

    void FinalizeSceneMeshBindings(
        const std::vector<int>& meshToSkin,
        const std::vector<int>& assimpMeshToModelMesh,
        ModelAsset& asset) {

        AssignSkinsToMeshNodes(meshToSkin, asset);
        RemapNodeMeshIndices(assimpMeshToModelMesh, asset);
    }

    void ReadAnimations(
        const aiScene& scene,
        const std::unordered_map<std::string, int>& nodeNameToIndex,
        ModelAsset& asset) {

        ReadAssimpAnimations(scene, nodeNameToIndex, asset);
    }

} // namespace HIKARI::ASSETS::MODELS::ASSIMP