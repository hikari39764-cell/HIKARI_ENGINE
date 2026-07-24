#include "Assets/Models/Loading/HIKARI_ObjModelLoader.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Assets/Models/Loading/Obj/HIKARI_ObjMaterialLibrary.h"
#include "Assets/Models/Loading/Obj/HIKARI_ObjText.h"
#include "Assets/Models/Processing/HIKARI_ModelGeometryPostprocess.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::ASSETS::MODELS {

    namespace {
        struct ObjKey {
            int pos = -1;
            int uv = -1;
            int normal = -1;

            bool operator==(const ObjKey& rhs) const {
                return pos == rhs.pos && uv == rhs.uv && normal == rhs.normal;
            }
        };

        struct ObjKeyHash {
            size_t operator()(const ObjKey& key) const {
                const size_t h1 = std::hash<int>{}(key.pos);
                const size_t h2 = std::hash<int>{}(key.uv);
                const size_t h3 = std::hash<int>{}(key.normal);
                return h1 ^ (h2 << 1) ^ (h3 << 2);
            }
        };

    } // namespace

    bool LoadObjModelSource(ModelAsset& asset) {
        std::ifstream file(asset.GetSourcePath());
        if (!file.is_open()) {
            return false;
        }

        std::vector<MATH::Vec3> positions;
        std::vector<MATH::Vec3> normals;
        std::vector<std::array<float, 2>> uvs;
        std::vector<std::filesystem::path> mtllibPaths;

        struct ObjPrimitiveBuilder {
            std::string objectName;
            std::string groupName;
            std::string materialName;
            std::vector<Vertex3D> vertices;
            std::vector<uint32_t> indices;
            std::unordered_map<ObjKey, uint32_t, ObjKeyHash> uniqueMap;
            bool missingNormal = false;
        };

        std::vector<ObjPrimitiveBuilder> builders;
        std::unordered_map<std::string, size_t> builderByKey;
        std::string currentObject = "Object";
        std::string currentGroup = "Group";
        std::string currentMaterial = "Default";
        std::unordered_set<std::string> objectNames{ currentObject };
        std::unordered_set<std::string> groupNames{ currentGroup };
        uint32_t triangulatedPolygonCount = 0;
        uint32_t malformedFaceCount = 0;
        bool smoothingEnabled = true;

        auto resolveObjIndex = [](int raw, size_t count) -> int {
            if (raw > 0) {
                return raw - 1;
            }
            if (raw < 0) {
                const int resolved = static_cast<int>(count) + raw;
                return resolved >= 0 ? resolved : -1;
            }
            return -1;
        };

        auto parseObjToken = [&](const std::string& token, ObjKey& key) -> bool {
            std::stringstream ss(token);
            std::string part;
            try {
                if (!std::getline(ss, part, '/')) return false;
                key.pos = part.empty() ? -1 : resolveObjIndex(std::stoi(part), positions.size());

                if (std::getline(ss, part, '/')) {
                    key.uv = part.empty() ? -1 : resolveObjIndex(std::stoi(part), uvs.size());
                }
                if (std::getline(ss, part, '/')) {
                    key.normal = part.empty() ? -1 : resolveObjIndex(std::stoi(part), normals.size());
                }
            } catch (...) {
                return false;
            }
            return key.pos >= 0;
        };

        auto getBuilder = [&]() -> ObjPrimitiveBuilder& {
            const std::string key = currentObject + "\n" + currentGroup + "\n" + currentMaterial;
            auto found = builderByKey.find(key);
            if (found != builderByKey.end()) {
                return builders[found->second];
            }

            ObjPrimitiveBuilder builder{};
            builder.objectName = currentObject;
            builder.groupName = currentGroup;
            builder.materialName = currentMaterial;
            const size_t index = builders.size();
            builders.push_back(std::move(builder));
            builderByKey.emplace(key, index);
            return builders.back();
        };

        auto appendVertex = [&](ObjPrimitiveBuilder& builder, const ObjKey& key, const MATH::Vec3& faceNormal) -> uint32_t {
            const bool forceUnique = !smoothingEnabled && key.normal < 0;
            if (!forceUnique) {
                auto found = builder.uniqueMap.find(key);
                if (found != builder.uniqueMap.end()) {
                    return found->second;
                }
            }

            Vertex3D vertex{};
            vertex.position = positions[static_cast<size_t>(key.pos)];
            if (key.normal >= 0 && key.normal < static_cast<int>(normals.size())) {
                vertex.normal = normals[static_cast<size_t>(key.normal)];
            } else {
                builder.missingNormal = true;
                vertex.normal = smoothingEnabled ? MATH::Vec3{} : faceNormal;
            }
            if (key.uv >= 0 && key.uv < static_cast<int>(uvs.size())) {
                vertex.uv0 = { uvs[static_cast<size_t>(key.uv)][0], 1.0f - uvs[static_cast<size_t>(key.uv)][1] };
            }
            vertex.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };

            const uint32_t newIndex = static_cast<uint32_t>(builder.vertices.size());
            builder.vertices.push_back(vertex);
            if (!forceUnique) {
                builder.uniqueMap.emplace(key, newIndex);
            }
            return newIndex;
        };

        std::string line;
        while (std::getline(file, line)) {
            if (line.size() < 2) continue;
            std::stringstream ss(line);
            std::string tag;
            ss >> tag;

            if (tag == "v") {
                MATH::Vec3 p{};
                ss >> p.x >> p.y >> p.z;
                positions.push_back(p);
            } else if (tag == "mtllib") {
                std::string mtlName;
                std::getline(ss, mtlName);
                mtlName = OBJ::TrimText(mtlName);
                if (!mtlName.empty()) {
                    const std::filesystem::path objPath(asset.GetSourcePath());
                    mtllibPaths.push_back((objPath.parent_path() / mtlName).lexically_normal());
                }
            } else if (tag == "usemtl") {
                std::string mtlName;
                ss >> mtlName;
                if (!mtlName.empty()) {
                    currentMaterial = mtlName;
                }
            } else if (tag == "o") {
                std::string name;
                std::getline(ss, name);
                name = OBJ::TrimText(name);
                currentObject = name.empty() ? "Object" : name;
                objectNames.insert(currentObject);
            } else if (tag == "g") {
                std::string name;
                std::getline(ss, name);
                name = OBJ::TrimText(name);
                currentGroup = name.empty() ? "Group" : name;
                groupNames.insert(currentGroup);
            } else if (tag == "s") {
                std::string smoothing;
                ss >> smoothing;
                smoothingEnabled = !(smoothing == "off" || smoothing == "0");
            } else if (tag == "vn") {
                MATH::Vec3 n{};
                ss >> n.x >> n.y >> n.z;
                normals.push_back(n);
            } else if (tag == "vt") {
                std::array<float, 2> uv{};
                ss >> uv[0] >> uv[1];
                uvs.push_back(uv);
            } else if (tag == "f") {
                std::vector<ObjKey> faceKeys;
                std::string token;
                while (ss >> token) {
                    ObjKey key{};
                    if (parseObjToken(token, key)) {
                        faceKeys.push_back(key);
                    }
                }
                if (faceKeys.size() < 3) {
                    ++malformedFaceCount;
                    continue;
                }

                ObjPrimitiveBuilder& builder = getBuilder();
                triangulatedPolygonCount += static_cast<uint32_t>(faceKeys.size() - 2u);
                for (size_t i = 1; i + 1 < faceKeys.size(); ++i) {
                    const ObjKey tri[3] = { faceKeys[0], faceKeys[i], faceKeys[i + 1] };
                    if (tri[0].pos < 0 || tri[0].pos >= static_cast<int>(positions.size()) ||
                        tri[1].pos < 0 || tri[1].pos >= static_cast<int>(positions.size()) ||
                        tri[2].pos < 0 || tri[2].pos >= static_cast<int>(positions.size())) {
                        return false;
                    }

                    const MATH::Vec3 p0 = positions[static_cast<size_t>(tri[0].pos)];
                    const MATH::Vec3 p1 = positions[static_cast<size_t>(tri[1].pos)];
                    const MATH::Vec3 p2 = positions[static_cast<size_t>(tri[2].pos)];
                    MATH::Vec3 faceNormal = MATH::Normalize(MATH::Cross(p1 - p0, p2 - p0));
                    if (MATH::Length(faceNormal) <= 1.0e-6f) {
                        faceNormal = { 0.0f, 1.0f, 0.0f };
                    }
                    for (const ObjKey& key : tri) {
                        builder.indices.push_back(appendVertex(builder, key, faceNormal));
                    }
                }
            }
        }

        bool hasGeometry = false;
        for (const ObjPrimitiveBuilder& builder : builders) {
            hasGeometry = hasGeometry || (!builder.vertices.empty() && !builder.indices.empty());
        }
        if (!hasGeometry) {
            return false;
        }

        asset.importDiagnostics = {};
        asset.importDiagnostics.sourceFormat = "OBJ";
        asset.importDiagnostics.objectCount = static_cast<uint32_t>(objectNames.size());
        asset.importDiagnostics.groupCount = static_cast<uint32_t>(groupNames.size());
        asset.importDiagnostics.triangulatedPolygonCount = triangulatedPolygonCount;
        if (malformedFaceCount > 0u) {
            asset.importDiagnostics.unsupportedFeatureCount += malformedFaceCount;
            asset.importDiagnostics.messages.push_back("[OBJ] malformed faces skipped: " + std::to_string(malformedFaceCount));
        }
        asset.nodes.clear();
        asset.meshes.clear();
        asset.materials.clear();
        asset.textures.clear();
        asset.skins.clear();
        asset.animations.clear();
        asset.defaultSceneRootNode = 0;

        OBJ::MaterialLibrary materialLibrary;
        OBJ::LoadMaterialLibraries(mtllibPaths, asset, materialLibrary);

        ModelNode rootNode{};
        rootNode.name = "OBJ Root";
        rootNode.meshIndex = 0;
        asset.nodes.push_back(std::move(rootNode));

        OBJ::TextureIndexByPath textureIndexByPath;
        OBJ::MaterialIndexByName materialIndexByName;

        MeshAsset meshAsset{};
        meshAsset.name = asset.GetName().empty() ? "OBJ Mesh" : asset.GetName();
        for (ObjPrimitiveBuilder& builder : builders) {
            if (builder.vertices.empty() || builder.indices.empty()) {
                continue;
            }

            MeshPrimitive primitive{};
            primitive.name = builder.objectName + "/" + builder.groupName + "/" + builder.materialName;
            primitive.layout = VertexLayoutKind::StaticPNTT;
            primitive.materialIndex = OBJ::ResolveMaterialIndex(
                builder.materialName,
                materialLibrary,
                asset,
                materialIndexByName,
                textureIndexByPath);
            primitive.indices = std::move(builder.indices);
            primitive.staticVertices = std::move(builder.vertices);
            if (builder.missingNormal) {
                GenerateModelPrimitiveNormals(primitive);
                ++asset.importDiagnostics.missingNormalGeneratedCount;
            }
            if (ModelMaterialHasNormalTexture(asset, primitive.materialIndex)) {
                GenerateModelPrimitiveTangents(primitive);
                ++asset.importDiagnostics.missingTangentGeneratedCount;
            }
            primitive.bounds = BOUNDS::ComputePrimitiveBounds(primitive);

            meshAsset.primitives.push_back(std::move(primitive));
            ++asset.importDiagnostics.clusteredStaticPrimitiveCount;
        }

        if (meshAsset.primitives.empty()) {
            return false;
        }
        meshAsset.bounds = BOUNDS::ComputeMeshBounds(meshAsset);
        asset.meshes.push_back(std::move(meshAsset));
        if (asset.materials.empty()) {
            OBJ::ResolveMaterialIndex(
                "Default",
                materialLibrary,
                asset,
                materialIndexByName,
                textureIndexByPath);
        }


        for (const TextureAsset3D& texture : asset.textures) {
            if (!texture.sourcePath.empty() && !std::filesystem::exists(texture.sourcePath)) {
                ++asset.importDiagnostics.unresolvedTextureCount;
            }
        }
        BOUNDS::EnsureModelBounds(asset);

        return true;
    }


} // namespace HIKARI::ASSETS::MODELS
