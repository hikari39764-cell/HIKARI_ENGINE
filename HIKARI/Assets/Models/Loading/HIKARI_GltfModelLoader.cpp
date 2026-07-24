#include "Assets/Models/Loading/HIKARI_GltfModelLoader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Assets/Models/Loading/Gltf/HIKARI_GltfAccessorReader.h"
#include "Assets/Models/Loading/Gltf/HIKARI_GltfAnimationReader.h"
#include "Assets/Models/Loading/Gltf/HIKARI_GltfMaterialReader.h"
#include "Assets/Models/Loading/Gltf/HIKARI_GltfSceneReader.h"
#include "Assets/Models/Processing/HIKARI_ModelGeometryPostprocess.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::ASSETS::MODELS {

    namespace {
        using nlohmann::json;

        int DecodeBase64Value(char c) {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        }

        bool DecodeBase64(std::string_view input, std::vector<uint8_t>& out) {
            out.clear();
            int value = 0;
            int bits = -8;
            for (char c : input) {
                if (std::isspace(static_cast<unsigned char>(c)) != 0) {
                    continue;
                }
                if (c == '=') {
                    break;
                }
                const int decoded = DecodeBase64Value(c);
                if (decoded < 0) {
                    return false;
                }
                value = (value << 6) | decoded;
                bits += 6;
                if (bits >= 0) {
                    out.push_back(static_cast<uint8_t>((value >> bits) & 0xFF));
                    bits -= 8;
                }
            }
            return true;
        }

        bool DecodeDataUriBytes(const std::string& uri, std::vector<uint8_t>& out) {
            const size_t comma = uri.find(',');
            if (comma == std::string::npos) {
                return false;
            }
            const std::string header = uri.substr(0, comma);
            if (header.find(";base64") == std::string::npos) {
                return false;
            }
            return DecodeBase64(std::string_view(uri).substr(comma + 1u), out);
        }

        bool ReadBinaryFile(const std::filesystem::path& path, std::vector<uint8_t>& out) {
            std::ifstream ifs(path, std::ios::binary | std::ios::ate);
            if (!ifs.is_open()) {
                return false;
            }
            const std::streamsize size = ifs.tellg();
            if (size <= 0) {
                out.clear();
                return true;
            }
            out.resize(static_cast<size_t>(size));
            ifs.seekg(0, std::ios::beg);
            return ifs.read(reinterpret_cast<char*>(out.data()), size).good();
        }

    } // namespace

    bool LoadGltfModelSource(ModelAsset& asset) {
        const std::filesystem::path gltfPath(asset.GetSourcePath());
        std::ifstream ifs(gltfPath);
        if (!ifs.is_open()) {
            return false;
        }

        json root = json::parse(ifs, nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            return false;
        }
        if (!root.contains("buffers") || !root["buffers"].is_array() || root["buffers"].empty()) {
            return false;
        }
        if (!root.contains("bufferViews") || !root["bufferViews"].is_array()) {
            return false;
        }
        if (!root.contains("accessors") || !root["accessors"].is_array()) {
            return false;
        }
        if (!root.contains("meshes") || !root["meshes"].is_array() || root["meshes"].empty()) {
            return false;
        }

        asset.importDiagnostics = {};

        const json& buffers = root["buffers"];
        const json& bufferViews = root["bufferViews"];
        const json& accessors = root["accessors"];
        const json& meshes = root["meshes"];

        std::vector<std::vector<uint8_t>> loadedBuffers(buffers.size());
        for (size_t i = 0; i < buffers.size(); ++i) {
            const std::string uri = buffers[i].value("uri", "");
            if (uri.empty()) {
                asset.importDiagnostics.messages.push_back("[glTF] buffer without uri is unsupported outside GLB");
                ++asset.importDiagnostics.unsupportedFeatureCount;
                return false;
            }
            if (uri.rfind("data:", 0) == 0) {
                if (!DecodeDataUriBytes(uri, loadedBuffers[i])) {
                    asset.importDiagnostics.messages.push_back("[glTF] data uri buffer decode failed");
                    ++asset.importDiagnostics.unsupportedFeatureCount;
                    return false;
                }
            } else if (!ReadBinaryFile(gltfPath.parent_path() / uri, loadedBuffers[i])) {
                return false;
            }
        }


        asset.nodes.clear();
        asset.meshes.clear();
        asset.materials.clear();
        asset.textures.clear();
        asset.skins.clear();
        asset.animations.clear();
        asset.defaultSceneRootNode = 0;
        asset.importDiagnostics.sourceFormat = "glTF";

        if (root.contains("extensionsUsed") && root["extensionsUsed"].is_array()) {
            for (const auto& extensionNode : root["extensionsUsed"]) {
                if (!extensionNode.is_string()) {
                    continue;
                }
                const std::string extension = extensionNode.get<std::string>();
                if (extension == "KHR_materials_unlit" ||
                    extension == "KHR_materials_emissive_strength" ||
                    extension == "KHR_materials_specular") {
                    continue;
                }
                ++asset.importDiagnostics.unsupportedFeatureCount;
                asset.importDiagnostics.unsupportedExtensions.push_back(extension);
                asset.importDiagnostics.messages.push_back("[glTF] unsupported extension: " + extension);
            }
        }

        GLTF::ReadMaterials(gltfPath, root, asset);


        GLTF::ReadSceneHierarchyAndSkins(
            root,
            accessors,
            bufferViews,
            loadedBuffers,
            asset);


        for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
            const json& meshNode = meshes[meshIndex];
            if (!meshNode.contains("primitives") || !meshNode["primitives"].is_array()) {
                continue;
            }

            MeshAsset meshAsset{};
            meshAsset.name = meshNode.value("name", "Mesh" + std::to_string(meshIndex));

            const json& primitives = meshNode["primitives"];
            for (size_t primitiveIndex = 0; primitiveIndex < primitives.size(); ++primitiveIndex) {
                const json& primitive = primitives[primitiveIndex];
                const int primitiveMode = primitive.value("mode", 4);
                if (primitiveMode != 4) {
                    ++asset.importDiagnostics.unsupportedPrimitiveModeCount;
                    ++asset.importDiagnostics.fallbackPrimitiveCount;
                    asset.importDiagnostics.messages.push_back(
                        "[glTF] unsupported primitive mode skipped: mesh=" +
                        std::to_string(meshIndex) +
                        " primitive=" + std::to_string(primitiveIndex) +
                        " mode=" + std::to_string(primitiveMode));
                    continue;
                }
                if (!primitive.contains("attributes") || !primitive["attributes"].is_object()) {
                    continue;
                }
                const json& attributes = primitive["attributes"];

                std::vector<float> positions;
                std::vector<float> normals;
                std::vector<float> texcoords;
                std::vector<float> texcoords1;
                std::vector<float> tangents;
                std::vector<float> colors4;
                std::vector<float> colors3;
                int vertexCount = 0;
                if (!GLTF::ReadFloatAccessor(accessors, bufferViews, loadedBuffers, attributes.value("POSITION", -1), 3, positions, &vertexCount)) {
                    continue;
                }
                const bool hasNormals = GLTF::ReadFloatAccessor(accessors, bufferViews, loadedBuffers, attributes.value("NORMAL", -1), 3, normals, nullptr);
                if (!hasNormals) {
                    normals.assign(static_cast<size_t>(vertexCount) * 3u, 0.0f);
                }
                if (!GLTF::ReadNormalizedFloatAccessor(accessors, bufferViews, loadedBuffers, attributes.value("TEXCOORD_0", -1), 2, texcoords, nullptr)) {
                    texcoords.assign(static_cast<size_t>(vertexCount) * 2u, 0.0f);
                }
                if (!GLTF::ReadNormalizedFloatAccessor(accessors, bufferViews, loadedBuffers, attributes.value("TEXCOORD_1", -1), 2, texcoords1, nullptr)) {
                    texcoords1.assign(static_cast<size_t>(vertexCount) * 2u, 0.0f);
                }
                GLTF::ReadFloatAccessor(accessors, bufferViews, loadedBuffers, attributes.value("TANGENT", -1), 4, tangents, nullptr);
                const bool hasColor4 = GLTF::ReadNormalizedFloatAccessor(accessors, bufferViews, loadedBuffers, attributes.value("COLOR_0", -1), 4, colors4, nullptr);
                const bool hasColor3 = !hasColor4 &&
                    GLTF::ReadNormalizedFloatAccessor(accessors, bufferViews, loadedBuffers, attributes.value("COLOR_0", -1), 3, colors3, nullptr);

                std::vector<uint32_t> indices;
                if (!GLTF::ReadIndexAccessor(accessors, bufferViews, loadedBuffers, primitive.value("indices", -1), indices)) {
                    indices.resize(static_cast<size_t>(vertexCount));
                    for (int i = 0; i < vertexCount; ++i) {
                        indices[static_cast<size_t>(i)] = static_cast<uint32_t>(i);
                    }
                }
                if (indices.size() < 3u || (indices.size() % 3u) != 0u) {
                    ++asset.importDiagnostics.unsupportedFeatureCount;
                    asset.importDiagnostics.messages.push_back(
                        "[glTF] triangle index count is invalid: mesh=" +
                        std::to_string(meshIndex) +
                        " primitive=" + std::to_string(primitiveIndex));
                    continue;
                }
                bool indicesInRange = true;
                for (uint32_t index : indices) {
                    if (index >= static_cast<uint32_t>(vertexCount)) {
                        indicesInRange = false;
                        break;
                    }
                }
                if (!indicesInRange) {
                    ++asset.importDiagnostics.unsupportedFeatureCount;
                    asset.importDiagnostics.messages.push_back(
                        "[glTF] index accessor references missing vertex: mesh=" +
                        std::to_string(meshIndex) +
                        " primitive=" + std::to_string(primitiveIndex));
                    continue;
                }

                MeshPrimitive primitiveAsset{};
                primitiveAsset.name = "Primitive" + std::to_string(primitiveIndex);
                primitiveAsset.layout = VertexLayoutKind::StaticPNTT;
                const int materialIndex = primitive.value("material", 0);
                primitiveAsset.materialIndex = materialIndex >= 0 &&
                    materialIndex < static_cast<int>(asset.materials.size())
                    ? static_cast<uint32_t>(materialIndex)
                    : 0u;
                primitiveAsset.indices = indices;
                primitiveAsset.hasMorphTargets =
                    primitive.contains("targets") &&
                    primitive["targets"].is_array() &&
                    !primitive["targets"].empty();
                if (primitiveAsset.hasMorphTargets) {
                    ++asset.importDiagnostics.skippedMorphPrimitiveCount;
                    ++asset.importDiagnostics.fallbackPrimitiveCount;
                    asset.importDiagnostics.messages.push_back(
                        "[glTF] morph target primitive uses legacy fallback only: mesh=" +
                        std::to_string(meshIndex) +
                        " primitive=" + std::to_string(primitiveIndex));
                }
                if (primitive.contains("extensions") && primitive["extensions"].is_object()) {
                    for (auto it = primitive["extensions"].begin(); it != primitive["extensions"].end(); ++it) {
                        ++asset.importDiagnostics.unsupportedFeatureCount;
                        ++asset.importDiagnostics.fallbackPrimitiveCount;
                        asset.importDiagnostics.unsupportedExtensions.push_back(it.key());
                        asset.importDiagnostics.messages.push_back("[glTF] unsupported primitive extension: " + it.key());
                    }
                }
                primitiveAsset.staticVertices.reserve(static_cast<size_t>(vertexCount));

                for (int i = 0; i < vertexCount; ++i) {
                    const size_t p = static_cast<size_t>(i) * 3u;
                    const size_t t = static_cast<size_t>(i) * 2u;
                    Vertex3D out{};
                    out.position = { positions[p + 0], positions[p + 1], positions[p + 2] };
                    out.normal = { normals[p + 0], normals[p + 1], normals[p + 2] };
                    out.uv0 = { texcoords[t + 0], texcoords[t + 1] };
                    out.uv1 = { texcoords1[t + 0], texcoords1[t + 1] };
                    if (!tangents.empty()) {
                        const size_t tg = static_cast<size_t>(i) * 4u;
                        out.tangent = { tangents[tg + 0], tangents[tg + 1], tangents[tg + 2], tangents[tg + 3] };
                    } else {
                        out.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
                    }
                    if (hasColor4) {
                        const size_t c = static_cast<size_t>(i) * 4u;
                        out.color0 = { colors4[c + 0], colors4[c + 1], colors4[c + 2], colors4[c + 3] };
                    } else if (hasColor3) {
                        const size_t c = static_cast<size_t>(i) * 3u;
                        out.color0 = { colors3[c + 0], colors3[c + 1], colors3[c + 2], 1.0f };
                    }
                    primitiveAsset.staticVertices.push_back(out);
                }

                if (!hasNormals) {
                    GenerateModelPrimitiveNormals(primitiveAsset);
                    ++asset.importDiagnostics.missingNormalGeneratedCount;
                }
                if (tangents.empty() && ModelMaterialHasNormalTexture(asset, primitiveAsset.materialIndex)) {
                    GenerateModelPrimitiveTangents(primitiveAsset);
                    ++asset.importDiagnostics.missingTangentGeneratedCount;
                }


                if (attributes.contains("JOINTS_0") && attributes.contains("WEIGHTS_0")) {
                    std::vector<std::array<uint16_t, 4>> joints;
                    std::vector<std::array<float, 4>> weights;
                    const bool hasJoints = GLTF::ReadJointAccessor(attributes.value("JOINTS_0", -1), accessors, bufferViews, loadedBuffers, joints);
                    const bool hasWeights = GLTF::ReadWeightAccessor(attributes.value("WEIGHTS_0", -1), accessors, bufferViews, loadedBuffers, weights);
                    if (hasJoints && hasWeights &&
                        joints.size() == static_cast<size_t>(vertexCount) &&
                        weights.size() == static_cast<size_t>(vertexCount)) {
                        primitiveAsset.skinnedVertices.resize(static_cast<size_t>(vertexCount));
                        for (int i = 0; i < vertexCount; ++i) {
                            const Vertex3D& staticVertex = primitiveAsset.staticVertices[static_cast<size_t>(i)];
                            SkinnedVertex3D skinned{};
                            skinned.position = staticVertex.position;
                            skinned.normal = staticVertex.normal;
                            skinned.tangent = staticVertex.tangent;
                            skinned.uv0 = staticVertex.uv0;
                            skinned.uv1 = staticVertex.uv1;
                            skinned.color0 = staticVertex.color0;
                            for (size_t c = 0; c < 4; ++c) {
                                skinned.joints[c] = joints[static_cast<size_t>(i)][c];
                                skinned.weights[c] = weights[static_cast<size_t>(i)][c];
                            }
                            primitiveAsset.skinnedVertices[static_cast<size_t>(i)] = skinned;
                        }
                    }
                }

                primitiveAsset.bounds = BOUNDS::ComputePrimitiveBounds(primitiveAsset);
                meshAsset.primitives.push_back(std::move(primitiveAsset));
                ++asset.importDiagnostics.clusteredStaticPrimitiveCount;
            }

            if (!meshAsset.primitives.empty()) {
                meshAsset.bounds = BOUNDS::ComputeMeshBounds(meshAsset);
                asset.meshes.push_back(std::move(meshAsset));
            }
        }

        GLTF::ReadAnimations(
            root,
            accessors,
            bufferViews,
            loadedBuffers,
            asset);
        BOUNDS::EnsureModelBounds(asset);
        return !asset.meshes.empty();
    }

} // namespace HIKARI::ASSETS::MODELS
