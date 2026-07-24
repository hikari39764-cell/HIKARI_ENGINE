#include "Assets/Models/Loading/Assimp/HIKARI_AssimpMeshReader.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <assimp/scene.h>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Assets/Models/Loading/Assimp/HIKARI_AssimpTypeConversion.h"
#include "Assets/Models/Processing/HIKARI_ModelGeometryPostprocess.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::ASSETS::MODELS::ASSIMP {

    namespace {
        struct VertexInfluence {
            uint16_t joint = 0;
            float weight = 0.0f;
        };

        void AppendInfluence(
            std::vector<std::vector<VertexInfluence>>& influences,
            unsigned int vertexId,
            uint16_t joint,
            float weight) {

            if (vertexId >= influences.size() || weight <= 0.0f) {
                return;
            }
            influences[vertexId].push_back({ joint, weight });
        }

        void WriteTopInfluences(
            const std::vector<VertexInfluence>& source,
            SkinnedVertex3D& outVertex) {

            std::array<VertexInfluence, 4> best{};
            for (const VertexInfluence& influence : source) {
                for (size_t i = 0; i < best.size(); ++i) {
                    if (influence.weight <= best[i].weight) {
                        continue;
                    }
                    for (size_t move = best.size() - 1u; move > i; --move) {
                        best[move] = best[move - 1u];
                    }
                    best[i] = influence;
                    break;
                }
            }

            float sum = 0.0f;
            for (const VertexInfluence& influence : best) {
                sum += influence.weight;
            }
            if (sum <= 1.0e-6f) {
                outVertex.joints[0] = 0;
                outVertex.weights[0] = 1.0f;
                return;
            }

            for (size_t i = 0; i < best.size(); ++i) {
                outVertex.joints[i] = best[i].joint;
                outVertex.weights[i] = best[i].weight / sum;
            }
        }

        void ReadAssimpMesh(
            const aiScene& scene,
            unsigned int meshIndex,
            const std::unordered_map<std::string, int>& nodeNameToIndex,
            std::vector<int>& assimpMeshToModelMesh,
            std::vector<int>& meshToSkin,
            ModelAsset& asset) {

            const aiMesh* source = scene.mMeshes[meshIndex];
            if (source == nullptr || source->mNumVertices == 0u) {
                return;
            }

            MeshPrimitive primitive{};
            primitive.name = source->mName.length > 0
                ? ToString(source->mName)
                : "AssimpPrimitive" + std::to_string(meshIndex);
            primitive.materialIndex = source->mMaterialIndex < asset.materials.size()
                ? source->mMaterialIndex
                : 0u;
            primitive.layout = source->HasBones()
                ? VertexLayoutKind::SkinnedPNTTJW
                : VertexLayoutKind::StaticPNTT;

            primitive.staticVertices.resize(source->mNumVertices);
            for (unsigned int i = 0; i < source->mNumVertices; ++i) {
                Vertex3D vertex{};
                vertex.position = ToVec3(source->mVertices[i]);
                if (source->HasNormals()) {
                    vertex.normal = MATH::Normalize(ToVec3(source->mNormals[i]));
                } else {
                    vertex.normal = { 0.0f, 1.0f, 0.0f };
                }
                if (source->HasTangentsAndBitangents()) {
                    vertex.tangent = {
                        source->mTangents[i].x,
                        source->mTangents[i].y,
                        source->mTangents[i].z,
                        1.0f
                    };
                    const MATH::Vec3 normal = MATH::Normalize(vertex.normal);
                    const MATH::Vec3 tangent = MATH::Normalize({ vertex.tangent.x, vertex.tangent.y, vertex.tangent.z });
                    const MATH::Vec3 bitangent = ToVec3(source->mBitangents[i]);
                    vertex.tangent.w = MATH::Dot(MATH::Cross(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
                } else {
                    vertex.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
                }
                if (source->HasTextureCoords(0u) && source->mTextureCoords[0] != nullptr) {
                    vertex.uv0 = { source->mTextureCoords[0][i].x, source->mTextureCoords[0][i].y };
                }
                if (source->HasTextureCoords(1u) && source->mTextureCoords[1] != nullptr) {
                    vertex.uv1 = { source->mTextureCoords[1][i].x, source->mTextureCoords[1][i].y };
                }
                if (source->HasVertexColors(0u) && source->mColors[0] != nullptr) {
                    vertex.color0 = ToVec4(source->mColors[0][i]);
                }
                primitive.staticVertices[i] = vertex;
            }

            primitive.indices.reserve(static_cast<size_t>(source->mNumFaces) * 3u);
            for (unsigned int faceIndex = 0; faceIndex < source->mNumFaces; ++faceIndex) {
                const aiFace& face = source->mFaces[faceIndex];
                if (face.mNumIndices != 3u) {
                    ++asset.importDiagnostics.unsupportedPrimitiveModeCount;
                    continue;
                }
                primitive.indices.push_back(face.mIndices[0]);
                primitive.indices.push_back(face.mIndices[1]);
                primitive.indices.push_back(face.mIndices[2]);
            }

            if (!source->HasNormals()) {
                GenerateModelPrimitiveNormals(primitive);
                ++asset.importDiagnostics.missingNormalGeneratedCount;
            }
            if (!source->HasTangentsAndBitangents() &&
                primitive.materialIndex < asset.materials.size() &&
                asset.materials[primitive.materialIndex].normalTexture.textureIndex >= 0) {
                GenerateModelPrimitiveTangents(primitive);
                ++asset.importDiagnostics.missingTangentGeneratedCount;
            }

            if (source->HasBones()) {
                std::vector<std::vector<VertexInfluence>> influences(source->mNumVertices);
                SkeletonAsset skeleton{};
                skeleton.name = primitive.name + "_Skeleton";

                std::unordered_map<int, int> nodeToJoint;
                for (unsigned int boneIndex = 0; boneIndex < source->mNumBones; ++boneIndex) {
                    const aiBone* bone = source->mBones[boneIndex];
                    if (bone == nullptr) {
                        continue;
                    }

                    const std::string boneName = ToString(bone->mName);
                    SkeletonJoint joint{};
                    joint.name = boneName;
                    joint.inverseBindMatrix = ToMat4(bone->mOffsetMatrix);

                    const auto foundNode = nodeNameToIndex.find(boneName);
                    if (foundNode != nodeNameToIndex.end()) {
                        joint.nodeIndex = foundNode->second;
                    }

                    const uint16_t compactJoint = static_cast<uint16_t>(skeleton.joints.size());
                    if (joint.nodeIndex >= 0) {
                        nodeToJoint[joint.nodeIndex] = compactJoint;
                    }
                    skeleton.joints.push_back(std::move(joint));

                    for (unsigned int weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
                        const aiVertexWeight& weight = bone->mWeights[weightIndex];
                        AppendInfluence(influences, weight.mVertexId, compactJoint, weight.mWeight);
                    }
                }

                for (SkeletonJoint& joint : skeleton.joints) {
                    if (joint.nodeIndex < 0 || joint.nodeIndex >= static_cast<int>(asset.nodes.size())) {
                        continue;
                    }
                    const int parentNode = asset.nodes[static_cast<size_t>(joint.nodeIndex)].parent;
                    const auto foundParentJoint = nodeToJoint.find(parentNode);
                    joint.parentJoint = foundParentJoint != nodeToJoint.end() ? foundParentJoint->second : -1;
                }

                primitive.skinnedVertices.resize(source->mNumVertices);
                for (unsigned int i = 0; i < source->mNumVertices; ++i) {
                    const Vertex3D& staticVertex = primitive.staticVertices[i];
                    SkinnedVertex3D skinned{};
                    skinned.position = staticVertex.position;
                    skinned.normal = staticVertex.normal;
                    skinned.tangent = staticVertex.tangent;
                    skinned.uv0 = staticVertex.uv0;
                    skinned.uv1 = staticVertex.uv1;
                    skinned.color0 = staticVertex.color0;
                    WriteTopInfluences(influences[i], skinned);
                    primitive.skinnedVertices[i] = skinned;
                }

                meshToSkin[meshIndex] = static_cast<int>(asset.skins.size());
                asset.skins.push_back(std::move(skeleton));
            }

            primitive.bounds = BOUNDS::ComputePrimitiveBounds(primitive);

            MeshAsset mesh{};
            mesh.name = source->mName.length > 0
                ? ToString(source->mName)
                : "AssimpMesh" + std::to_string(meshIndex);
            mesh.primitives.push_back(std::move(primitive));
            mesh.bounds = BOUNDS::ComputeMeshBounds(mesh);
            const int modelMeshIndex = static_cast<int>(asset.meshes.size());
            asset.meshes.push_back(std::move(mesh));
            if (meshIndex < assimpMeshToModelMesh.size()) {
                assimpMeshToModelMesh[meshIndex] = modelMeshIndex;
            }
            ++asset.importDiagnostics.clusteredStaticPrimitiveCount;
        }


    } // namespace

    void ReadMeshes(
        const aiScene& scene,
        const std::unordered_map<std::string, int>& nodeNameToIndex,
        std::vector<int>& assimpMeshToModelMesh,
        std::vector<int>& meshToSkin,
        ModelAsset& asset) {

        asset.meshes.reserve(scene.mNumMeshes);
        for (unsigned int meshIndex = 0; meshIndex < scene.mNumMeshes; ++meshIndex) {
            ReadAssimpMesh(
                scene,
                meshIndex,
                nodeNameToIndex,
                assimpMeshToModelMesh,
                meshToSkin,
                asset);
        }
    }

} // namespace HIKARI::ASSETS::MODELS::ASSIMP