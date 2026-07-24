#include "Assets/Models/Loading/Gltf/HIKARI_GltfSceneReader.h"

#include <unordered_map>
#include <utility>

#include "Assets/Models/HIKARI_ModelAsset.h"

namespace HIKARI::ASSETS::MODELS::GLTF {

    namespace {
        using nlohmann::json;

        MATH::Mat4 ReadGltfNodeMatrix(const json& matrixNode) {
            MATH::Mat4 out = MATH::Mat4::Identity();
            // glTF stores node.matrix in column-major order.
            // Convert it into HIKARI Mat4 layout here.
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    out.m[col][row] = matrixNode[static_cast<size_t>(col * 4 + row)].get<float>();
                }
            }
            return out;
        }

        void ReadGltfNodes(const json& root, ModelAsset& asset) {
            if (!root.contains("nodes") || !root["nodes"].is_array()) {
                return;
            }

            const json& nodes = root["nodes"];
            asset.nodes.resize(nodes.size());
            for (size_t i = 0; i < nodes.size(); ++i) {
                const json& node = nodes[i];
                ModelNode modelNode{};
                modelNode.name = node.value("name", "");
                modelNode.localMatrix = MATH::Mat4::Identity();

                if (node.contains("matrix") && node["matrix"].is_array() && node["matrix"].size() >= 16) {
                    modelNode.hasLocalMatrix = true;
                    modelNode.localMatrix = ReadGltfNodeMatrix(node["matrix"]);
                } else {
                    if (node.contains("translation") && node["translation"].is_array() && node["translation"].size() >= 3) {
                        modelNode.localTransform.position = {
                            node["translation"][0].get<float>(),
                            node["translation"][1].get<float>(),
                            node["translation"][2].get<float>()
                        };
                    }
                    if (node.contains("scale") && node["scale"].is_array() && node["scale"].size() >= 3) {
                        modelNode.localTransform.scale = {
                            node["scale"][0].get<float>(),
                            node["scale"][1].get<float>(),
                            node["scale"][2].get<float>()
                        };
                    }
                    if (node.contains("rotation") && node["rotation"].is_array() && node["rotation"].size() >= 4) {
                        modelNode.localTransform.rotation = {
                            node["rotation"][0].get<float>(),
                            node["rotation"][1].get<float>(),
                            node["rotation"][2].get<float>(),
                            node["rotation"][3].get<float>()
                        };
                    }
                }

                modelNode.meshIndex = node.value("mesh", -1);
                modelNode.skinIndex = node.value("skin", -1);
                asset.nodes[i] = std::move(modelNode);
            }

            for (size_t i = 0; i < nodes.size(); ++i) {
                const json& node = nodes[i];
                if (!node.contains("children") || !node["children"].is_array()) {
                    continue;
                }
                for (const auto& child : node["children"]) {
                    const int childIndex = child.get<int>();
                    if (childIndex >= 0 && childIndex < static_cast<int>(asset.nodes.size())) {
                        asset.nodes[i].children.push_back(childIndex);
                        asset.nodes[static_cast<size_t>(childIndex)].parent = static_cast<int>(i);
                    }
                }
            }
        }


        void ReadGltfSkins(
            const json& root,
            const json& accessors,
            const json& bufferViews,
            const std::vector<std::vector<uint8_t>>& loadedBuffers,
            ModelAsset& asset) {
            if (!root.contains("skins") || !root["skins"].is_array()) {
                return;
            }

            for (const auto& skinNode : root["skins"]) {
                if (!skinNode.is_object() || !skinNode.contains("joints") || !skinNode["joints"].is_array()) {
                    continue;
                }

                std::vector<MATH::Mat4> inverseBindMatrices;
                const int inverseBindAccessor = skinNode.value("inverseBindMatrices", -1);
                if (inverseBindAccessor >= 0) {
                    ReadMatrixAccessor(inverseBindAccessor, accessors, bufferViews, loadedBuffers, inverseBindMatrices);
                }

                SkeletonAsset skeleton{};
                skeleton.name = skinNode.value("name", "");
                skeleton.skeletonRootNode = skinNode.value("skeleton", -1);

                std::unordered_map<int, int> nodeToJoint;
                const json& joints = skinNode["joints"];
                for (size_t jointIndex = 0; jointIndex < joints.size(); ++jointIndex) {
                    if (!joints[jointIndex].is_number_integer()) {
                        continue;
                    }

                    const int nodeIndex = joints[jointIndex].get<int>();
                    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(asset.nodes.size())) {
                        continue;
                    }

                    SkeletonJoint joint{};
                    joint.name = asset.nodes[static_cast<size_t>(nodeIndex)].name;
                    joint.nodeIndex = nodeIndex;
                    joint.inverseBindMatrix = (jointIndex < inverseBindMatrices.size())
                        ? inverseBindMatrices[jointIndex]
                        : MATH::Mat4::Identity();

                    const int compactJointIndex = static_cast<int>(skeleton.joints.size());
                    nodeToJoint[nodeIndex] = compactJointIndex;
                    skeleton.joints.push_back(std::move(joint));
                }

                for (SkeletonJoint& joint : skeleton.joints) {
                    if (joint.nodeIndex < 0 || joint.nodeIndex >= static_cast<int>(asset.nodes.size())) {
                        continue;
                    }

                    const int parentNode = asset.nodes[static_cast<size_t>(joint.nodeIndex)].parent;
                    const auto foundParentJoint = nodeToJoint.find(parentNode);
                    joint.parentJoint = (foundParentJoint != nodeToJoint.end()) ? foundParentJoint->second : -1;
                }

                asset.skins.push_back(std::move(skeleton));
            }
        }

    } // namespace

    void ReadSceneHierarchyAndSkins(
        const Json& root,
        const Json& accessors,
        const Json& bufferViews,
        const BufferStorage& buffers,
        ModelAsset& asset) {

        ReadGltfNodes(root, asset);
        ReadGltfSkins(root, accessors, bufferViews, buffers, asset);
    }

} // namespace HIKARI::ASSETS::MODELS::GLTF
