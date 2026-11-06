#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <Windows.h>
#include <objbase.h>
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportInputs.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Editor/Assets/HIKARI_AssetImportSettingsService.h"
#include "Assets/HIKARI_AssetRegistryBuilder.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Assets/Material/HIKARI_MaterialBuiltInAssets.h"
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Assets/Cooking/HIKARI_AssetCooker.h"
#include "RenderContracts/Texture/HIKARI_HtexFormat.h"
#include "Assets/Post/HIKARI_PostAssetStore.h"
#include "Assets/Sprite/HIKARI_SpriteAssetStore.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Assets/Formats/HIKARI_HmodelFormat.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"
#include <json.hpp>
#include <DirectXTex.h>
#include "Internal/HIKARI_ContractAssert.h"
#include "Assets/Material/Serialization/HIKARI_MaterialSourceJson.h"

namespace {
namespace fs = std::filesystem;
template <class Predicate>
void Until(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!predicate()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
}
std::string Read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    assert(stream.good());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void WriteTga(const fs::path& path, std::array<unsigned char, 3> rgb) {
    const auto previous = fs::exists(path) ? fs::last_write_time(path) : fs::file_time_type::clock::now();
    std::array<unsigned char, 18> header{};
    header[2] = 2;
    header[12] = 4;
    header[14] = 4;
    header[16] = 32;
    header[17] = 0x28;
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(header.data()), header.size());
    const std::array<unsigned char, 4> pixel{rgb[2], rgb[1], rgb[0], 255};
    for (int i = 0; i < 16; ++i)
        stream.write(reinterpret_cast<const char*>(pixel.data()), pixel.size());
    stream.close();
    assert(stream.good());
    fs::last_write_time(path, previous + std::chrono::seconds(1));
}
// 入力の摘要キャッシュは変更直後のファイルを保存しないため、一時間前に変更されたファイルとして扱わせる。
void BackdateChangeTime(const fs::path& path) {
    const HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(file != INVALID_HANDLE_VALUE);
    FILE_BASIC_INFO info{};
    assert(GetFileInformationByHandleEx(file, FileBasicInfo, &info, sizeof(info)));
    info.ChangeTime.QuadPart -= 36'000'000'000LL;
    assert(SetFileInformationByHandle(file, FileBasicInfo, &info, sizeof(info)));
    CloseHandle(file);
}

#if defined(HIKARI_WITH_EDITOR)
void CheckThinModelImport(HIKARI::AssetDatabase& database, const fs::path& root) {
    // 細い単一 chart でも UV1 の padding 契約を満たし、HMODEL まで発行できる。
    for (const float height : {0.001f, 0.01f, 0.03f}) {
        const auto source = root / "Assets/thin.obj";
        {
            std::ofstream stream(source);
            stream << "v 0 0 0\nv 1 0 0\nv 1 " << height << " 0\nv 0 " << height << " 0\nf 1 2 3\nf 1 3 4\n";
        }
        assert(database.ScanAssets(true));
        const auto guid = database.FindByPath("Assets/thin.obj")->guid;
        assert(database.ImportAsset(guid));
        HIKARI::ModelAsset model;
        std::string error;
        assert(HIKARI::ReadHmodelFile(database.GetImportedDirectory(guid) / "model.hmodel", model, error));
        const auto& primitive = model.meshes.at(0).primitives.at(0);
        assert(primitive.indices.size() == 6 && primitive.hasLightmapUv);
        const auto& layout = primitive.lightmapUvLayout;
        assert(layout.HasGeneratedLayout() && layout.referenceWidth > layout.chartPadding * 2 &&
               layout.referenceHeight > layout.chartPadding * 2);
    }
}

void CheckFbxUnitScale(HIKARI::AssetDatabase& database, const fs::path& root) {
    // cm 単位（UnitScaleFactor 1）の FBX の 100 cm の三角形は 1 m で取り込む。
    {
        std::ofstream stream(root / "Assets/centimeters.fbx");
        stream
            << "; FBX 7.4.0 project file\nFBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n}\n"
               "GlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {\n"
               "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",1\n\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
               "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",2\n"
               "\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",1\n"
               "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n"
               "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",1\n\t}\n}\n"
               "Objects:  {\n\tGeometry: 1000, \"Geometry::Tri\", \"Mesh\" {\n"
               "\t\tVertices: *9 {\n\t\t\ta: 0,0,0,100,0,0,0,100,0\n\t\t}\n"
               "\t\tPolygonVertexIndex: *3 {\n\t\t\ta: 0,1,-3\n\t\t}\n\t\tGeometryVersion: 124\n\t}\n"
               "\tModel: 2000, \"Model::Tri\", \"Mesh\" {\n\t\tVersion: 232\n\t}\n}\n"
               "Connections:  {\n\tC: \"OO\",1000,2000\n\tC: \"OO\",2000,0\n}\n";
    }
    assert(database.ScanAssets(true));
    const auto guid = database.FindByPath("Assets/centimeters.fbx")->guid;
    assert(database.ImportAsset(guid));
    HIKARI::ModelAsset model;
    std::string error;
    assert(HIKARI::ReadHmodelFile(database.GetImportedDirectory(guid) / "model.hmodel", model, error));
    const auto extent = model.bounds.max - model.bounds.min;
    const float longest = (std::max)({extent.x, extent.y, extent.z});
    assert(longest > 0.99f && longest < 1.01f);
}

void CheckFbxSkinnedMeshNodes(HIKARI::AssetDatabase& database, const fs::path& root) {
    // 網格の節点が原点に無い蒙皮（Body は 1 m 上、その子の Eyes はさらに 0.5 m 上。どちらも Root の骨に重み 1）も、描画と
    // 同じ式（表面の節点の global × 関節の global × 逆バインド）で bind 姿勢の位置に戻る。
    std::ofstream(root / "Assets/skinned_nodes.fbx") << R"FBX(; FBX 7.4.0 project file
FBXHeaderExtension:  {
  FBXHeaderVersion: 1003
  FBXVersion: 7400
}
GlobalSettings:  {
  Version: 1000
  Properties70:  {
    P: "UpAxis", "int", "Integer", "",1
    P: "UpAxisSign", "int", "Integer", "",1
    P: "FrontAxis", "int", "Integer", "",2
    P: "FrontAxisSign", "int", "Integer", "",1
    P: "CoordAxis", "int", "Integer", "",0
    P: "CoordAxisSign", "int", "Integer", "",1
    P: "UnitScaleFactor", "double", "Number", "",1
  }
}
Objects:  {
  Geometry: 1001, "Geometry::Body", "Mesh" {
    Vertices: *9 {
      a: 0,0,0,10,0,0,0,10,0
    }
    PolygonVertexIndex: *3 {
      a: 0,1,-3
    }
    GeometryVersion: 124
  }
  Geometry: 1002, "Geometry::Eyes", "Mesh" {
    Vertices: *9 {
      a: 0,0,0,10,0,0,0,10,0
    }
    PolygonVertexIndex: *3 {
      a: 0,1,-3
    }
    GeometryVersion: 124
  }
  Model: 2001, "Model::Body", "Mesh" {
    Version: 232
    Properties70:  {
      P: "Lcl Translation", "Lcl Translation", "", "A",0,100,0
    }
  }
  Model: 2002, "Model::Eyes", "Mesh" {
    Version: 232
    Properties70:  {
      P: "Lcl Translation", "Lcl Translation", "", "A",0,50,0
    }
  }
  NodeAttribute: 3001, "NodeAttribute::Root", "LimbNode" {
    TypeFlags: "Skeleton"
  }
  Model: 3002, "Model::Root", "LimbNode" {
    Version: 232
  }
  Deformer: 4001, "Deformer::SkinBody", "Skin" {
    Version: 101
  }
  Deformer: 4002, "SubDeformer::ClusterBody", "Cluster" {
    Version: 100
    Indexes: *3 {
      a: 0,1,2
    }
    Weights: *3 {
      a: 1,1,1
    }
    Transform: *16 {
      a: 1,0,0,0,0,1,0,0,0,0,1,0,0,100,0,1
    }
    TransformLink: *16 {
      a: 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1
    }
  }
  Deformer: 4003, "Deformer::SkinEyes", "Skin" {
    Version: 101
  }
  Deformer: 4004, "SubDeformer::ClusterEyes", "Cluster" {
    Version: 100
    Indexes: *3 {
      a: 0,1,2
    }
    Weights: *3 {
      a: 1,1,1
    }
    Transform: *16 {
      a: 1,0,0,0,0,1,0,0,0,0,1,0,0,150,0,1
    }
    TransformLink: *16 {
      a: 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1
    }
  }
}
Connections:  {
  C: "OO",2001,0
  C: "OO",2002,2001
  C: "OO",3002,0
  C: "OO",3001,3002
  C: "OO",1001,2001
  C: "OO",1002,2002
  C: "OO",4001,1001
  C: "OO",4002,4001
  C: "OO",3002,4002
  C: "OO",4003,1002
  C: "OO",4004,4003
  C: "OO",3002,4004
}
)FBX";
    assert(database.ScanAssets(true));
    const auto guid = database.FindByPath("Assets/skinned_nodes.fbx")->guid;
    assert(database.ImportAsset(guid));
    HIKARI::ModelAsset model;
    std::string error;
    assert(HIKARI::ReadHmodelFile(database.GetImportedDirectory(guid) / "model.hmodel", model, error));
    std::vector<HIKARI::MATH::Mat4> globals(model.nodes.size());
    for (size_t index = 0; index < model.nodes.size(); ++index) {
        const HIKARI::ModelNode& node = model.nodes[index];
        assert(node.parent < static_cast<int>(index));
        const HIKARI::MATH::Mat4 local = node.hasLocalMatrix ? node.localMatrix : node.localTransform.GetLocalMatrix();
        globals[index] = node.parent >= 0 ? globals[static_cast<size_t>(node.parent)] * local : local;
    }
    float lowest = 1.0e9f;
    float highest = -1.0e9f;
    size_t vertices = 0;
    for (size_t index = 0; index < model.nodes.size(); ++index) {
        const HIKARI::ModelNode& node = model.nodes[index];
        if (node.meshIndex < 0 || node.skinIndex < 0)
            continue;
        const HIKARI::SkeletonAsset& skin = model.skins.at(static_cast<size_t>(node.skinIndex));
        for (const auto& primitive : model.meshes.at(static_cast<size_t>(node.meshIndex)).primitives)
            for (const auto& vertex : primitive.skinnedVertices) {
                HIKARI::MATH::Vec3 skinned{};
                for (int influence = 0; influence < 4; ++influence) {
                    if (vertex.weights[influence] <= 0.0f)
                        continue;
                    const HIKARI::SkeletonJoint& joint = skin.joints.at(vertex.joints[influence]);
                    assert(joint.nodeIndex >= 0);
                    skinned = skinned + HIKARI::MATH::TransformPoint(globals[static_cast<size_t>(joint.nodeIndex)] *
                                                                         joint.inverseBindMatrix,
                                                                     vertex.position) *
                                            vertex.weights[influence];
                }
                const float height = HIKARI::MATH::TransformPoint(globals[index], skinned).y;
                lowest = (std::min)(lowest, height);
                highest = (std::max)(highest, height);
                ++vertices;
            }
    }
    assert(vertices == 6u && lowest > 0.99f && lowest < 1.01f && highest > 1.59f && highest < 1.61f);
}

void CheckGltfSkinnedMeshNodes(HIKARI::AssetDatabase& database, const fs::path& root) {
    // 網格の節点が 1 m 上にある蒙皮の glTF（関節は原点、逆バインドは単位、重みは全て 1）。glTF は網格の節点の変換を無視
    // するので、描画と同じ式で bind 姿勢に戻すと高さは 0〜0.1 m。節点は根が最後に並ぶ（親が子より後）。既定の取り込み
    // （簇几何あり・光照贴图 UV 生成あり）で、蒙皮の図元が静的と見なされて展開され、LOD 簡略化が範囲外へ書いていた形。
    constexpr uint32_t kGrid = 8u;
    constexpr uint32_t kVertexCount = (kGrid + 1u) * (kGrid + 1u);
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<uint8_t> joints(kVertexCount * 4u, 0u);
    std::vector<float> weights;
    for (uint32_t y = 0; y <= kGrid; ++y)
        for (uint32_t x = 0; x <= kGrid; ++x) {
            positions.insert(positions.end(), {0.1f * x / kGrid, 0.1f * y / kGrid, 0.0f});
            normals.insert(normals.end(), {0.0f, 0.0f, 1.0f});
            weights.insert(weights.end(), {1.0f, 0.0f, 0.0f, 0.0f});
        }
    std::vector<uint16_t> indices;
    for (uint32_t y = 0; y < kGrid; ++y)
        for (uint32_t x = 0; x < kGrid; ++x) {
            const auto corner = static_cast<uint16_t>(y * (kGrid + 1u) + x);
            const auto above = static_cast<uint16_t>(corner + kGrid + 1u);
            indices.insert(indices.end(),
                           {corner, static_cast<uint16_t>(corner + 1u), above, static_cast<uint16_t>(corner + 1u),
                            static_cast<uint16_t>(above + 1u), above});
        }
    const std::array<float, 16> inverseBind{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    std::string bytes;
    nlohmann::json views = nlohmann::json::array();
    const auto append = [&](const void* data, size_t size) {
        views.push_back({{"buffer", 0}, {"byteOffset", bytes.size()}, {"byteLength", size}});
        bytes.append(static_cast<const char*>(data), size);
        return static_cast<int>(views.size() - 1u);
    };
    const int positionView = append(positions.data(), positions.size() * sizeof(float));
    const int normalView = append(normals.data(), normals.size() * sizeof(float));
    const int jointView = append(joints.data(), joints.size());
    const int weightView = append(weights.data(), weights.size() * sizeof(float));
    const int indexView = append(indices.data(), indices.size() * sizeof(uint16_t));
    const int inverseBindView = append(inverseBind.data(), sizeof(inverseBind));
    std::ofstream(root / "Assets/skinned_nodes.bin", std::ios::binary) << bytes;

    // 構造は固定の文字列で書き、緩衝の長さと視図だけ後から入れる（頂点 81、索引 384 は上の格子から決まる）。
    nlohmann::json gltf = nlohmann::json::parse(R"JSON({
        "asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [2]}],
        "nodes": [{"name": "Body", "mesh": 0, "skin": 0, "translation": [0.0, 1.0, 0.0]}, {"name": "Root"},
                  {"name": "Scene", "children": [0, 1]}],
        "skins": [{"joints": [1], "inverseBindMatrices": 5}],
        "materials": [{"name": "Opaque"}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "JOINTS_0": 2, "WEIGHTS_0": 3},
                                    "indices": 4, "material": 0}]}],
        "buffers": [{"uri": "skinned_nodes.bin"}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 81, "type": "VEC3", "min": [0, 0, 0], "max": [0.1, 0.1, 0]},
            {"bufferView": 1, "componentType": 5126, "count": 81, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5121, "count": 81, "type": "VEC4"},
            {"bufferView": 3, "componentType": 5126, "count": 81, "type": "VEC4"},
            {"bufferView": 4, "componentType": 5123, "count": 384, "type": "SCALAR"},
            {"bufferView": 5, "componentType": 5126, "count": 1, "type": "MAT4"}]})JSON");
    assert(kVertexCount == 81u && indices.size() == 384u && positionView == 0 && normalView == 1 && jointView == 2 &&
           weightView == 3 && indexView == 4 && inverseBindView == 5);
    gltf["buffers"][0]["byteLength"] = bytes.size();
    gltf["bufferViews"] = views;
    std::ofstream(root / "Assets/skinned_nodes.gltf") << gltf.dump();

    assert(database.ScanAssets(true));
    const auto guid = database.FindByPath("Assets/skinned_nodes.gltf")->guid;
    assert(database.ImportAsset(guid));
    assert(fs::exists(database.GetImportedDirectory(guid) / "clustered_mesh.hcmesh"));
    HIKARI::ModelAsset model;
    std::string error;
    assert(HIKARI::ReadHmodelFile(database.GetImportedDirectory(guid) / "model.hmodel", model, error));
    std::vector<HIKARI::MATH::Mat4> globals(model.nodes.size());
    std::vector<bool> resolved(model.nodes.size(), false);
    const auto resolve = [&](auto& self, size_t index) -> const HIKARI::MATH::Mat4& {
        if (!resolved[index]) {
            const HIKARI::ModelNode& node = model.nodes[index];
            const HIKARI::MATH::Mat4 local =
                node.hasLocalMatrix ? node.localMatrix : node.localTransform.GetLocalMatrix();
            globals[index] = node.parent >= 0 ? self(self, static_cast<size_t>(node.parent)) * local : local;
            resolved[index] = true;
        }
        return globals[index];
    };
    float lowest = 1.0e9f;
    float highest = -1.0e9f;
    size_t vertices = 0;
    for (size_t index = 0; index < model.nodes.size(); ++index) {
        const HIKARI::ModelNode& node = model.nodes[index];
        if (node.meshIndex < 0 || node.skinIndex < 0)
            continue;
        const HIKARI::SkeletonAsset& skin = model.skins.at(static_cast<size_t>(node.skinIndex));
        for (const auto& primitive : model.meshes.at(static_cast<size_t>(node.meshIndex)).primitives) {
            assert(primitive.layout == HIKARI::VertexLayoutKind::SkinnedPNTTJW);
            assert(primitive.skinnedVertices.size() == primitive.staticVertices.size());
            for (const uint32_t vertexIndex : primitive.indices)
                assert(vertexIndex < primitive.skinnedVertices.size());
            for (const auto& vertex : primitive.skinnedVertices) {
                const HIKARI::SkeletonJoint& joint = skin.joints.at(vertex.joints[0]);
                const HIKARI::MATH::Vec3 skinned = HIKARI::MATH::TransformPoint(
                    resolve(resolve, static_cast<size_t>(joint.nodeIndex)) * joint.inverseBindMatrix, vertex.position);
                const float height = HIKARI::MATH::TransformPoint(resolve(resolve, index), skinned).y;
                lowest = (std::min)(lowest, height);
                highest = (std::max)(highest, height);
                ++vertices;
            }
        }
    }
    assert(vertices == kVertexCount && lowest > -0.001f && lowest < 0.001f && highest > 0.099f && highest < 0.101f);
}

void CheckImportedSpecular(HIKARI::AssetDatabase& database, const fs::path& root) {
    // 実際の glTF ローダー、正規化、HTEX/HMODEL 出力を一度に通す。
    WriteTga(root / "Assets/specular.tga", {0, 189, 0});
    const std::array<float, 24> vertices{0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const std::array<uint16_t, 3> indices{0, 1, 2};
    {
        std::ofstream stream(root / "Assets/material.bin", std::ios::binary);
        stream.write(reinterpret_cast<const char*>(vertices.data()), sizeof(vertices));
        stream.write(reinterpret_cast<const char*>(indices.data()), sizeof(indices));
        assert(stream.good());
    }
    {
        std::ofstream stream(root / "Assets/material.gltf");
        stream << R"({
                "asset":{"version":"2.0"}, "extensionsUsed":["KHR_materials_specular"],
                "buffers":[{"uri":"material.bin","byteLength":102}],
                "bufferViews":[{"buffer":0,"byteLength":36},
                    {"buffer":0,"byteOffset":36,"byteLength":36},
                    {"buffer":0,"byteOffset":72,"byteLength":24},
                    {"buffer":0,"byteOffset":96,"byteLength":6}],
                "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
                    {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
                    {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
                    {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],
                "images":[{"uri":"specular.tga"}], "textures":[{"source":0}],
                "materials":[{"name":"colored-reflection","pbrMetallicRoughness":{"metallicFactor":0,"roughnessFactor":0.55},
                    "extensions":{"KHR_materials_specular":{"specularFactor":0.25,
                        "specularColorFactor":[2,4,1],"specularColorTexture":{"index":0}}}}],
                "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}],
                "nodes":[{"mesh":0}], "scenes":[{"nodes":[0]}], "scene":0
            })";
        assert(stream.good());
    }
    assert(database.ScanAssets(true));
    const auto* textureRecord = database.FindByPath("Assets/specular.tga");
    assert(textureRecord);
    const auto textureGuid = textureRecord->guid;
    assert(database.ImportAsset(textureGuid));
    const auto* record = database.FindByPath("Assets/material.gltf");
    assert(record);
    const auto guid = record->guid;
    const bool imported = database.ImportAsset(guid);
    if (!imported)
        std::fprintf(stderr, "%s\n", database.FindByGuid(guid)->lastImportMessage.c_str());
    assert(imported);
    HIKARI::ModelAsset model;
    std::string error;
    assert(HIKARI::ReadHmodelFile(database.GetImportedDirectory(guid) / "model.hmodel", model, error));
    assert(model.materials.size() == 1);
    const auto& material = model.materials.front();
    assert(material.specularFactor == 0.25f);
    assert(material.specularColorFactor.x == 2 && material.specularColorFactor.y == 4 &&
           material.specularColorFactor.z == 1);
    assert(material.roughnessFactor == 0.55f && material.metallicFactor == 0);
    // 緑の画像やファイル名から MR テクスチャへ勝手に再解釈しない。
    assert(material.specularColorTexture.textureIndex >= 0);
    assert(material.specularTexture.textureIndex == -1 && material.metallicRoughnessTexture.textureIndex == -1);
    const auto& texture = model.textures.at(material.specularColorTexture.textureIndex);
    assert(fs::path(texture.sourcePath).extension() == ".htex");
    // 依存 texture の内容変更は、時刻を元に戻してもモデルへ伝播する。摘要がキャッシュに載った後でも同じ。
    const auto sourceTexture = root / "Assets/specular.tga";
    BackdateChangeTime(sourceTexture);
    for (int pass = 0; pass < 2; ++pass) // 一度目で摘要を保存し、二度目はキャッシュから読む。
        assert(database.ScanAssets(true) && !database.FindByGuid(guid)->importOutdated);
    const auto originalTime = fs::last_write_time(sourceTexture);
    WriteTga(sourceTexture, {180, 30, 20});
    fs::last_write_time(sourceTexture, originalTime);
    assert(database.ScanAssets(true));
    assert(database.FindByGuid(textureGuid)->importOutdated && database.FindByGuid(guid)->importOutdated);
    const auto beforeDependencyUpdate = Read(database.GetImportedDirectory(guid) / "model.hmodel");
    assert(!database.ImportAsset(guid));
    assert(Read(database.GetImportedDirectory(guid) / "model.hmodel") == beforeDependencyUpdate);
    assert(database.ImportAsset(textureGuid) && database.ImportAsset(guid));
    assert(!database.FindByGuid(guid)->importOutdated);
    // 設定だけの更新も依存元へ通知し、再スキャンで消えない。
    const auto expected = database.FindByGuid(textureGuid)->meta;
    auto edited = expected;
    auto settings = nlohmann::json::parse(edited.importSettingsJson);
    settings["mipPolicy"] = "None";
    edited.importSettingsJson = settings.dump();
    assert(database.UpdateImportSettings(textureGuid, expected, edited, error));
    assert(database.FindByGuid(guid)->importOutdated);
    assert(database.ScanAssets(true) && database.FindByGuid(guid)->importOutdated);
    assert(database.ImportAsset(textureGuid) && database.ImportAsset(guid));
    // BIN の内容を変更した時も再インポートが必要。削除時は失敗し、前の成果物を残す。
    const auto modelArtifact = database.GetImportedDirectory(guid) / "model.hmodel";
    const auto accepted = Read(modelArtifact);
    fs::remove(root / "Assets/material.bin");
    assert(database.ScanAssets(true) && database.FindByGuid(guid)->importOutdated);
    assert(!database.ImportAsset(guid) && Read(modelArtifact) == accepted);
    assert(HIKARI::GetImportState(*database.FindByGuid(guid)) == HIKARI::AssetImportState::ImportFailed);
}

void CheckObjDependencies(HIKARI::AssetDatabase& database, const fs::path& root) {
    using namespace HIKARI;
    const auto materialPath = root / "Assets/paint.mtl";
    const auto texturePath = root / "Assets/roughness.tga";
    {
        std::ofstream stream(root / "Assets/paint.obj");
        stream << "mtllib paint.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\n"
                  "usemtl paint\nf 1/1 2/2 3/3\n";
    }
    const auto writeMaterial = [&](const char* color) {
        std::ofstream stream(materialPath);
        stream << "newmtl paint\nKd " << color << "\nmap_Pr roughness.tga\n";
        assert(stream.good());
    };
    writeMaterial("1 0 0");
    WriteTga(texturePath, {64, 64, 64});
    assert(database.ScanAssets(true));
    const auto textureGuid = database.FindByPath("Assets/roughness.tga")->guid;
    assert(database.ImportAsset(textureGuid));
    const auto guid = database.FindByPath("Assets/paint.obj")->guid;
    const auto artifact = database.GetImportedDirectory(guid) / "model.hmodel";
    assert(database.QueueImportAssets({guid}, "First OBJ import"));
    const auto taskId = database.GetQueuedImportStatus().activeTaskIds.front();
    Until([&] {
        const auto task = database.GetAssetTaskService().FindSnapshot(taskId);
        return task && task->state == AssetTaskState::Finalizing;
    });
    const auto timestamp = fs::last_write_time(materialPath);
    writeMaterial("0 1 0");
    fs::last_write_time(materialPath, timestamp);
    Until([&] {
        database.PumpAssetTasks();
        return !database.GetQueuedImportStatus().active;
    });
    assert(database.GetQueuedImportStatus().failed == 1 && !fs::exists(artifact));
    assert(database.ImportAsset(guid));
    ModelAsset model;
    std::string error;
    assert(ReadHmodelFile(artifact, model, error));
    const auto material = std::find_if(model.materials.begin(), model.materials.end(),
                                       [](const auto& value) { return value.name == "paint"; });
    assert(material != model.materials.end() && material->baseColorFactor.y == 1);
    // MR packing が sourcePath を生成物へ置き換えても、元画像の依存を失わない。
    const auto textureTime = fs::last_write_time(texturePath);
    WriteTga(texturePath, {192, 192, 192});
    fs::last_write_time(texturePath, textureTime);
    assert(database.ScanAssets(true) && database.FindByGuid(guid)->importOutdated);
    assert(database.ImportAsset(textureGuid) && database.ImportAsset(guid));
    assert(!database.FindByGuid(guid)->importOutdated);
    // 欠けたテクスチャは代替で取り込み、そのまま最新。ファイルが現れたら取り込みが古くなる。
    {
        std::ofstream stream(materialPath);
        stream << "newmtl paint\nKd 0 1 0\nmap_Kd albedo.tga\nmap_Pr roughness.tga\n";
    }
    assert(database.ScanAssets(true) && database.ImportAsset(guid));
    assert(database.ScanAssets(true) && !database.FindByGuid(guid)->importOutdated);
    WriteTga(root / "Assets/albedo.tga", {200, 100, 50});
    assert(database.ScanAssets(true) && database.FindByGuid(guid)->importOutdated);
    assert(database.ImportAsset(database.FindByPath("Assets/albedo.tga")->guid) && database.ImportAsset(guid));
    assert(!database.FindByGuid(guid)->importOutdated);
}

struct SpritePageSourcePaths {
    fs::path color{};
    fs::path normal{};
};

bool CopySpritePage(const fs::path& sourceDirectory, const fs::path& destinationDirectory, std::string_view file,
                    fs::path& outPath) {
    const fs::path relative = HIKARI::TEXT::PathFromUtf8(file);
    if (file.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
        return false;
    }
    for (const fs::path& part : relative) {
        if (part == ".." || part == ".") {
            return false;
        }
    }
    const fs::path source = sourceDirectory / relative;
    outPath = destinationDirectory / relative;
    std::error_code error{};
    fs::create_directories(outPath.parent_path(), error);
    if (error || !fs::is_regular_file(source, error) || error) {
        return false;
    }
    fs::copy_file(source, outPath, fs::copy_options::overwrite_existing, error);
    return !error;
}

void LoadPngRgba8(const fs::path& path, uint32_t& outWidth, uint32_t& outHeight, std::vector<uint8_t>& outPixels) {
    DirectX::TexMetadata metadata{};
    DirectX::ScratchImage decoded{};
    const auto flags = static_cast<DirectX::WIC_FLAGS>(DirectX::WIC_FLAGS_FORCE_RGB | DirectX::WIC_FLAGS_IGNORE_SRGB);
    HRESULT hr = DirectX::LoadFromWICFile(path.wstring().c_str(), flags, &metadata, decoded);
    assert(SUCCEEDED(hr) && metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE2D);
    if (metadata.format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        DirectX::ScratchImage rgba{};
        hr = DirectX::Convert(decoded.GetImages(), decoded.GetImageCount(), metadata, DXGI_FORMAT_R8G8B8A8_UNORM,
                              DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, rgba);
        assert(SUCCEEDED(hr));
        decoded = std::move(rgba);
        metadata = decoded.GetMetadata();
    }
    const DirectX::Image* image = decoded.GetImage(0u, 0u, 0u);
    assert(image != nullptr && image->pixels != nullptr && image->width > 0u && image->height > 0u &&
           image->rowPitch >= image->width * 4u);
    outWidth = static_cast<uint32_t>(image->width);
    outHeight = static_cast<uint32_t>(image->height);
    outPixels.resize(static_cast<size_t>(outWidth) * outHeight * 4u);
    for (uint32_t y = 0u; y < outHeight; ++y) {
        std::copy_n(image->pixels + static_cast<size_t>(y) * image->rowPitch, static_cast<size_t>(outWidth) * 4u,
                    outPixels.data() + static_cast<size_t>(y) * outWidth * 4u);
    }
}

void CheckPngMatchesHtex(const fs::path& pngPath, const fs::path& htexPath, HIKARI::TextureUsage expectedUsage,
                         HIKARI::TextureAssetColorSpace expectedColorSpace) {
    uint32_t sourceWidth = 0u;
    uint32_t sourceHeight = 0u;
    std::vector<uint8_t> sourcePixels{};
    LoadPngRgba8(pngPath, sourceWidth, sourceHeight, sourcePixels);

    HIKARI::HtexTexture htex{};
    std::string error{};
    assert(HIKARI::ReadHtexFile(htexPath, htex, error));
    assert(htex.width == sourceWidth && htex.height == sourceHeight && htex.depth == 1u && htex.arraySize == 1u &&
           htex.mipLevels == 1u && htex.dimension == HIKARI::HtexTextureDimension::Texture2D &&
           htex.usage == expectedUsage && htex.colorSpace == expectedColorSpace &&
           htex.compression == HIKARI::TextureCompression::None &&
           htex.format == (expectedColorSpace == HIKARI::TextureAssetColorSpace::Srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                                                                                      : DXGI_FORMAT_R8G8B8A8_UNORM));
    assert(htex.subresources.size() == 1u);
    const HIKARI::HtexSubresource& subresource = htex.subresources.front();
    assert(subresource.rowPitch >= static_cast<uint64_t>(sourceWidth) * 4u);
    for (uint32_t y = 0u; y < sourceHeight; ++y) {
        const uint8_t* sourceRow = sourcePixels.data() + static_cast<size_t>(y) * sourceWidth * 4u;
        const uint8_t* htexRow = subresource.data.data() + static_cast<size_t>(y) * subresource.rowPitch;
        assert(std::equal(sourceRow, sourceRow + static_cast<size_t>(sourceWidth) * 4u, htexRow));
    }
}

void CheckSpriteAssetIntegration(const fs::path& sourceManifestPath, const fs::path& testRoot) {
    using namespace HIKARI;
    nlohmann::json sourceJson{};
    assert(SERIALIZATION::JSON::ReadJsonFile(sourceManifestPath, sourceJson));
    assert(sourceJson.is_object() && sourceJson.contains("pages") && sourceJson.at("pages").is_array() &&
           !sourceJson.at("pages").empty());

    const fs::path projectRoot = testRoot / sourceManifestPath.stem();
    const fs::path assetsRoot = projectRoot / "Assets";
    std::error_code error{};
    fs::create_directories(assetsRoot, error);
    assert(!error);
    const fs::path fixtureDescriptor =
        fs::path(HIKARI_CONTRACT_SOURCE_ROOT) / "Tests/Fixtures/Projects/EmptyProject" / "Project.hikari";
    fs::copy_file(fixtureDescriptor, projectRoot / "Project.hikari", fs::copy_options::overwrite_existing, error);
    assert(!error);

    std::vector<SpritePageSourcePaths> pageSources{};
    pageSources.reserve(sourceJson.at("pages").size());
    for (const nlohmann::json& page : sourceJson.at("pages")) {
        assert(page.is_object() && page.contains("file") && page.at("file").is_string());
        SpritePageSourcePaths paths{};
        assert(CopySpritePage(sourceManifestPath.parent_path(), assetsRoot, page.at("file").get<std::string>(),
                              paths.color));
        if (page.contains("normalFile")) {
            assert(page.at("normalFile").is_string());
            assert(CopySpritePage(sourceManifestPath.parent_path(), assetsRoot,
                                  page.at("normalFile").get<std::string>(), paths.normal));
        }
        pageSources.push_back(std::move(paths));
    }

    const fs::path manifestPath = assetsRoot / sourceManifestPath.filename();
    fs::copy_file(sourceManifestPath, manifestPath, fs::copy_options::overwrite_existing, error);
    assert(!error);

    AssetDatabase database{};
    assert(database.Initialize(projectRoot) && database.ScanAssets(true));
    const auto spriteRecords = database.CollectByType(AssetType::Sprite);
    assert(spriteRecords.size() == 1u);
    const AssetGuid spriteGuid = spriteRecords.front()->guid;

    const fs::path scenePath = assetsRoot / "Scenes" / "SpriteIntegration.hscene";
    fs::create_directories(scenePath.parent_path(), error);
    assert(!error);
    SceneDocument scene{};
    scene.sceneName = "SpriteIntegration";
    SceneObjectData spriteObject{};
    spriteObject.id = SceneObjectId{1u};
    spriteObject.name = "Sprite";
    spriteObject.components.push_back(
        {"WorldSpriteComponent", {{"spriteAssetGuid", spriteGuid.value}}, SceneComponentId{1u}});
    scene.objects.push_back(std::move(spriteObject));
    assert(SceneSerializer{}.SaveToFile(TEXT::PathToUtf8(scenePath), scene));
    assert(database.ScanAssets(true));
    const AssetRecord* sceneRecord = database.FindByPath("Assets/Scenes/SpriteIntegration.hscene");
    assert(sceneRecord != nullptr && sceneRecord->type == AssetType::Scene);
    const AssetGuid sceneGuid = sceneRecord->guid;

    if (!database.ImportAsset(spriteGuid)) {
        const AssetRecord* failed = database.FindByGuid(spriteGuid);
        std::fprintf(stderr, "%s\n", failed ? failed->lastImportMessage.c_str() : "Sprite import record vanished");
        assert(false);
    }
    assert(database.ImportAsset(sceneGuid));

    SPRITE::SpriteAssetStore spriteStore{};
    spriteStore.Configure(&database);
    std::string errorMessage{};
    const auto sprite = spriteStore.Load(spriteGuid, errorMessage);
    assert(sprite && sprite->pages.size() == pageSources.size() && !sprite->frames.empty() && !sprite->clips.empty());
    const size_t sourceRotatedCount = static_cast<size_t>(
        std::count_if(sourceJson.at("frames").begin(), sourceJson.at("frames").end(),
                      [](const nlohmann::json& frame) { return frame.is_object() && frame.value("rotated", false); }));
    const size_t importedRotatedCount = static_cast<size_t>(std::count_if(
        sprite->frames.begin(), sprite->frames.end(), [](const SPRITE::SpriteFrame& frame) { return frame.rotated; }));
    assert(importedRotatedCount == sourceRotatedCount);
    for (size_t index = 0u; index < sprite->pages.size(); ++index) {
        const SPRITE::SpritePage& page = sprite->pages[index];
        CheckPngMatchesHtex(pageSources[index].color, projectRoot / TEXT::PathFromUtf8(page.artifactPath),
                            TextureUsage::UI, TextureAssetColorSpace::Srgb);
        assert(page.colorAlphaClass != TextureAssetAlphaClass::Unknown);
        if (!pageSources[index].normal.empty()) {
            assert(!page.normalArtifactPath.empty() && !page.normalTextureAssetId.empty());
            CheckPngMatchesHtex(pageSources[index].normal, projectRoot / TEXT::PathFromUtf8(page.normalArtifactPath),
                                TextureUsage::Normal, TextureAssetColorSpace::Linear);
        } else {
            assert(page.normalArtifactPath.empty() && page.normalTextureAssetId.empty());
        }
    }

    AssetRegistry registry{};
    AssetRegistryBuilder registryBuilder{};
    assert(registryBuilder.RebuildRegistry(database, registry));
    const AssetDescriptor* spriteDescriptor = registry.FindDescriptor(spriteGuid.value);
    assert(spriteDescriptor != nullptr && spriteDescriptor->type == AssetType::Sprite);
    for (const SPRITE::SpritePage& page : sprite->pages) {
        const auto* colorDescriptor = registry.FindAs<TextureAssetDescriptor>(AssetGuid{page.textureAssetId});
        assert(colorDescriptor != nullptr && colorDescriptor->alphaClass == page.colorAlphaClass);
        if (!page.normalTextureAssetId.empty()) {
            const auto* normalDescriptor =
                registry.FindAs<TextureAssetDescriptor>(AssetGuid{page.normalTextureAssetId});
            assert(normalDescriptor != nullptr && normalDescriptor->usage == TextureUsage::Normal &&
                   normalDescriptor->alphaClass == TextureAssetAlphaClass::Unknown);
        }
    }

    const fs::path cookedRoot = testRoot / (sourceManifestPath.stem().string() + "-cooked");
    ASSETS::COOKING::AssetCookOptions cookOptions{};
    cookOptions.projectRoot = projectRoot;
    cookOptions.contentRoot = cookedRoot;
    cookOptions.startupSceneGuid = sceneGuid.value;
    cookOptions.sceneGuids = {sceneGuid.value};
    const ASSETS::COOKING::AssetCookResult cookResult = ASSETS::COOKING::AssetCooker{}.Cook(cookOptions);
    if (!cookResult.success) {
        std::fprintf(stderr, "%s\n", cookResult.message.c_str());
    }
    assert(cookResult.success);

    AssetDatabase cookedDatabase{};
    assert(cookedDatabase.Initialize(cookedRoot) && cookedDatabase.IsCookedContent() &&
           cookedDatabase.ScanAssets(false));
    const AssetRecord* cookedSceneRecord = cookedDatabase.FindByGuid(sceneGuid);
    assert(cookedSceneRecord != nullptr && cookedSceneRecord->type == AssetType::Scene);
    const AssetRecord* cookedSpriteRecord = cookedDatabase.FindByGuid(spriteGuid);
    assert(cookedSpriteRecord != nullptr && cookedSpriteRecord->type == AssetType::Sprite &&
           cookedSpriteRecord->sourcePath.extension() == ".hspritec");
    const auto cookedRuntime =
        std::find_if(cookedSpriteRecord->artifactManifest.artifacts.begin(),
                     cookedSpriteRecord->artifactManifest.artifacts.end(), [](const AssetArtifactDesc& artifact) {
                         return ASSETS::SEMANTICS::MatchesAssetArtifact(
                             artifact, ASSETS::SEMANTICS::AssetArtifactKind::SpriteRuntime);
                     });
    assert(cookedRuntime != cookedSpriteRecord->artifactManifest.artifacts.end());
    size_t expectedCookedArtifactCount = 1u;
    for (const SpritePageSourcePaths& paths : pageSources) {
        expectedCookedArtifactCount += paths.normal.empty() ? 1u : 2u;
    }
    assert(cookedSpriteRecord->artifactManifest.artifacts.size() == expectedCookedArtifactCount);
    for (const AssetDependencyDesc& dependency : cookedSpriteRecord->artifactManifest.dependencies) {
        assert(dependency.role != "SpriteSourcePage");
    }
    assert(!fs::exists(cookedRoot / "Assets" / manifestPath.filename()));
    for (const SpritePageSourcePaths& paths : pageSources) {
        assert(!fs::exists(cookedRoot / "Assets" / fs::relative(paths.color, assetsRoot)));
        if (!paths.normal.empty()) {
            assert(!fs::exists(cookedRoot / "Assets" / fs::relative(paths.normal, assetsRoot)));
        }
    }

    SPRITE::SpriteAssetStore cookedStore{};
    cookedStore.Configure(&cookedDatabase);
    const auto cookedSprite = cookedStore.Load(spriteGuid, errorMessage);
    assert(cookedSprite && cookedSprite->frames.size() == sprite->frames.size() &&
           cookedSprite->defaultClip == sprite->defaultClip && cookedSprite->pages.size() == sprite->pages.size());
    AssetRegistry cookedRegistry{};
    assert(registryBuilder.RebuildRegistry(cookedDatabase, cookedRegistry));
    for (const SPRITE::SpritePage& page : cookedSprite->pages) {
        const auto* descriptor = cookedRegistry.FindAs<TextureAssetDescriptor>(AssetGuid{page.textureAssetId});
        assert(descriptor != nullptr && descriptor->alphaClass == page.colorAlphaClass);
    }
}
#endif
} // namespace

// 後処理: エンジン内蔵の効果と既定プロファイルが読め、プロジェクトの効果・プロファイルは導入して解決できる。
void CheckPostAssets(HIKARI::AssetDatabase& database, const fs::path& root) {
    using namespace HIKARI;
    std::string error;
    const fs::path content = fs::path(HIKARI_CONTRACT_SOURCE_ROOT) / "HIKARI/EngineContent/Post";
    for (const POST::BuiltinPostContent& builtin : POST::GetBuiltinPostContent()) {
        POST::PostEffectDefinition effect;
        POST::PostProfileAsset profile;
        const std::string stem(builtin.fileStem);
        assert(POST::LoadPostEffectDefinition(content / "Effects" / (stem + ".hposteffect"), effect, error));
        assert(POST::LoadPostProfileAsset(content / "Profiles" / (stem + ".hpostprofile"), profile, error));
        assert(profile.effectId == builtin.effectId && POST::ValidatePostProfileValues(profile, effect, error));
        assert(effect.screenWarp.has_value() == (builtin.effectId == "engine.post.effect.crtdy"));
    }

    const nlohmann::json effectJson = {
        {"formatVersion", 1},
        {"name", "Test Warp"},
        {"passes", {{{"name", "Resolve"}, {"programId", "HIKARI.EngineShader.Post_CRTDY"}}}},
        {"params",
         {{{"key", "strength"},
           {"type", "Float"},
           {"ref", {{"slot", 2}, {"channel", 1}}},
           {"defaultValues", {0.5}},
           {"minValues", {0}},
           {"maxValues", {1}}},
          {{"key", "tint"},
           {"type", "Color3"},
           {"ref", {{"slot", 3}, {"channel", 0}}},
           {"defaultValues", {1, 0.5, 0.25}},
           {"minValues", {0, 0, 0}},
           {"maxValues", {1, 1, 1}}}}},
        {"screenWarp", {{"strength", "strength"}, {"curvature", "strength"}}},
    };
    assert(SERIALIZATION::JSON::WriteJsonFile(root / "Assets/Post/Warp.hposteffect", effectJson));
    POST::PostProfileAsset profile;
    profile.name = "Warm";
    assert(database.ScanAssets(true));
    const auto* effectRecord = database.FindByPath("Assets/Post/Warp.hposteffect");
    assert(effectRecord && effectRecord->type == AssetType::PostEffect && database.ImportAsset(effectRecord->guid));
    profile.effectId = effectRecord->guid.value;
    profile.values["strength"] = {0.75f, 0.0f, 0.0f, 0.0f};
    assert(POST::SavePostProfileAsset(root / "Assets/Post/Warm.hpostprofile", profile, error));
    assert(database.ScanAssets(true));
    const auto* profileRecord = database.FindByPath("Assets/Post/Warm.hpostprofile");
    assert(profileRecord && profileRecord->type == AssetType::PostProfile && database.ImportAsset(profileRecord->guid));
    const std::string profileId = profileRecord->guid.value;

    POST::PostAssetStore store;
    store.Configure(&database);
    const auto loadedProfile = store.LoadProfile(profileId, error);
    const auto loadedEffect = loadedProfile ? store.LoadEffect(loadedProfile->effectId, error) : nullptr;
    assert(loadedEffect && POST::CollectPostProfileDependencies(*loadedProfile).size() == 1u);
    const POST::PostParameterBlock block = POST::ResolvePostParameters(*loadedEffect, loadedProfile.get());
    assert(block[2].y == 0.75f && block[3].x == 1.0f && block[3].y == 0.5f && block[3].z == 0.25f);
    assert(POST::ReadPostParameter(*loadedEffect, block, "strength") == 0.75f);

    // 未保存の草稿は保存前でも描画側の読み込みに反映する。
    auto draft = std::make_shared<POST::PostProfileAsset>(*loadedProfile);
    draft->values["strength"] = {0.25f, 0.0f, 0.0f, 0.0f};
    store.SetProfileDraft(profileId, draft);
    assert(store.LoadProfile(profileId, error)->values.at("strength")[0] == 0.25f);
    store.ClearProfileDraft();
    assert(store.LoadProfile(profileId, error)->values.at("strength")[0] == 0.75f);

    // 旧形式・範囲外の参照・効果に無いパラメーターは変換せずに拒否する。
    POST::PostEffectDefinition rejected;
    auto outdated = effectJson;
    outdated["formatVersion"] = 0;
    assert(!POST::ReadPostEffectDefinition(outdated, rejected, error));
    auto outsideBlock = effectJson;
    outsideBlock["params"][0]["ref"]["slot"] = POST::kPostParameterSlotCount;
    assert(!POST::ReadPostEffectDefinition(outsideBlock, rejected, error));
    auto unknownValue = *loadedProfile;
    unknownValue.values["missing"] = {};
    assert(!POST::ValidatePostProfileValues(unknownValue, *loadedEffect, error));
}

void CheckImportSettings(HIKARI::AssetDatabase& database, const fs::path& root, const HIKARI::AssetGuid& guid) {
    using namespace HIKARI;
    std::string error;
    EDITOR::AssetImportSettingsDraft draft;
    draft.Reset(*database.FindByGuid(guid));
    const auto saved = Read(database.GetMetaPathForSource("Assets/reference.tga"));
    auto settings = nlohmann::json::parse(draft.edited.importSettingsJson);
    draft.edited.importSettingsJson = settings.dump(4);
    assert(!draft.IsDirty()); // JSON の空白だけでは変更扱いにしない。
    settings["mipPolicy"] = "None";
    draft.edited.importSettingsJson = settings.dump();
    assert(draft.IsDirty() && Read(database.GetMetaPathForSource("Assets/reference.tga")) == saved);
    auto result = EDITOR::ApplyAssetImportSettings(database, guid, draft);
    assert(result.ok && result.code == "PENDING" && !draft.IsDirty());
    auto busy = draft;
    busy.edited.displayName = "Should not save while busy";
    assert(!EDITOR::ApplyAssetImportSettings(database, guid, busy).ok && busy.IsDirty());
    Until([&] {
        database.PumpAssetTasks();
        return !database.GetQueuedImportStatus().active;
    });
    assert(database.GetQueuedImportStatus().succeeded == 1);
    HtexFileInfo info;
    assert(InspectHtexFile(database.GetImportedDirectory(guid) / "texture.htex", info, error) && info.mipLevels == 1);
    assert(database.ScanAssets(true) && !database.FindByGuid(guid)->importOutdated);
    // 重起動相当の新 DB でも、保存済み設定との差を検出する。
    draft.Reset(*database.FindByGuid(guid));
    settings["mipPolicy"] = "Generate";
    draft.edited.importSettingsJson = settings.dump();
    assert(database.UpdateImportSettings(guid, draft.baseline, draft.edited, error));
    AssetDatabase reopened;
    assert(reopened.Initialize(root) && reopened.ScanAssets(false));
    assert(reopened.FindByGuid(guid)->importOutdated);
    assert(!database.UpdateImportSettings(guid, draft.baseline, draft.edited, error)); // 古い草稿で上書きしない。
    draft.Reset(*database.FindByGuid(guid));
    auto invalid = draft.edited;
    invalid.importSettingsJson = "[]";
    assert(!database.UpdateImportSettings(guid, draft.baseline, invalid, error));
    invalid.importSettingsJson = R"({"mipPolicy":5})";
    assert(!database.UpdateImportSettings(guid, draft.baseline, invalid, error));
    assert(database.ImportAsset(guid));
    assert(database.ScanAssets(true) && !database.FindByGuid(guid)->importOutdated);
    EDITOR::EditorOperationRegistry operations;
    EDITOR::RegisterAssetImportSettingsOperations(operations, [&] { return &database; });
    const auto* operation = operations.Find("asset.apply_import_settings");
    assert(operation && operation->permission == EDITOR::EditorOperationPermission::ModifyProjectFiles &&
           !operation->requiresDocumentSession);
}

void CheckPainterlyEngineDependencies(const fs::path& root) {
    using namespace HIKARI;
    using namespace HIKARI::ASSETS::MATERIAL;
    auto& content = CONTENT::GetEngineContentContext();
    assert(content.Open(HIKARI_CONTRACT_ENGINE_CONTENT_ROOT));
    AssetRegistry textures{};
    assert(RegisterEngineContentTextures(content, textures));
    for (const auto id : {kBuiltInPainterlyStrokeMaskTextureId, kBuiltInPainterlyStrokeNormalTextureId}) {
        const auto* texture = textures.FindAs<TextureAssetDescriptor>(AssetGuid{std::string(id)});
        assert(texture && texture->colorSpace == TextureAssetColorSpace::Linear && texture->width == 1024u);
    }

    const auto project = root / "PainterlyDependencies";
    fs::create_directories(project / "Assets");
    fs::copy_file(fs::path(HIKARI_CONTRACT_SOURCE_ROOT) / "Tests/Fixtures/Projects/EmptyProject/Project.hikari",
                  project / "Project.hikari");
    std::string error;
    MaterialAssetData material = CreateMaterialFromTemplate(MaterialTemplateId::PainterlyShell);
    const auto materialPath = project / "Assets/Painterly.material.json";
    assert(MATERIAL_SERIALIZATION::SaveMaterialSourceJson(materialPath, material, error));
    AssetDatabase database;
    assert(database.Initialize(project) && database.ScanAssets(true));
    const AssetGuid materialGuid = database.FindByPath("Assets/Painterly.material.json")->guid;
    assert(database.ImportAsset(materialGuid));
    const auto* imported = database.FindByGuid(materialGuid);
    assert(std::any_of(
        imported->artifactManifest.dependencies.begin(), imported->artifactManifest.dependencies.end(),
        [](const auto& dependency) { return dependency.guid.value == kBuiltInPainterlyStrokeMaskTextureId; }));

    SceneDocument scene;
    scene.sceneName = "PainterlyDependencies";
    SceneObjectData object;
    object.id = SceneObjectId{1u};
    object.components.push_back(
        {"ProceduralMeshComponent", {{"materialSelection", materialGuid.value}}, SceneComponentId{1u}});
    scene.objects.push_back(std::move(object));
    const auto usage = AnalyzeAssetUsage(scene, database);
    assert(usage.missingReferences.empty() && usage.IsUsed(materialGuid));
    const auto scenePath = project / "Assets/Painterly.hscene";
    assert(SceneSerializer{}.SaveToFile(TEXT::PathToUtf8(scenePath), scene) && database.ScanAssets(true));
    const AssetGuid sceneGuid = database.FindByPath("Assets/Painterly.hscene")->guid;
    assert(database.ImportAsset(sceneGuid));
    ASSETS::COOKING::AssetCookOptions options;
    options.projectRoot = project;
    options.contentRoot = root / "PainterlyCooked";
    options.sceneGuids = {sceneGuid.value};
    options.startupSceneGuid = sceneGuid.value;
    const auto cooked = ASSETS::COOKING::AssetCooker{}.Cook(options);
    if (!cooked.success)
        std::fprintf(stderr, "%s\n", cooked.message.c_str());
    assert(cooked.success);

    // 同じ engine.* 接頭辞でも catalog に無い参照は許可しない。
    for (auto& resource : material.graph.resources)
        if (resource.assetGuid.value == kBuiltInPainterlyStrokeMaskTextureId)
            resource.assetGuid.value = "engine.painterly.texture.missing";
    assert(MATERIAL_SERIALIZATION::SaveMaterialSourceJson(materialPath, material, error) && database.ScanAssets(true));
    assert(!database.ImportAsset(materialGuid));
    assert(!AnalyzeAssetUsage(scene, database).missingReferences.empty());

    // どのコンポーネントのどのフィールドでも、GUID 形式の値は参照として数える。欠落ならフィールドの経路が役割に残る。
    const std::string danglingGuid(32, 'a');
    scene.objects.front().components.push_back(
        {"Test.Anything",
         nlohmann::json::parse(R"({"layers":[{"textureAssetGuid":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}]})"),
         SceneComponentId{2u}});
    const auto dangling = AnalyzeAssetUsage(scene, database);
    assert(std::any_of(dangling.missingReferences.begin(), dangling.missingReferences.end(),
                       [&](const AssetMissingReference& missing) {
                           return missing.assetId == danglingGuid &&
                                  missing.role == "Test.Anything.layers[0].textureAssetGuid";
                       }));

    // 中身を読めない資産は使用中のまま、理由つきで残す（削除保護・書き出し・焼き込みの依存ハッシュが判断に使う）。
    {
        std::ofstream broken(materialPath, std::ios::trunc);
        broken << "{ not a material";
    }
    const auto unreadable = AnalyzeAssetUsage(scene, database);
    assert(unreadable.IsUsed(materialGuid) && unreadable.unreadableReferences.size() == 1u &&
           unreadable.unreadableReferences.front().assetId == materialGuid.value &&
           !unreadable.unreadableReferences.front().reason.empty());
    content.Close();
}

void CheckProjectPostContracts(const std::filesystem::path& testRoot);

int main(int argc, char** argv) {
    using namespace HIKARI;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    assert(SUCCEEDED(com));
    struct ComScope {
        ~ComScope() { CoUninitialize(); }
    } comScope;
    const auto root = fs::temp_directory_path() / ("HikariImportContract-" + std::to_string(GetCurrentProcessId()) +
                                                   "-" + std::to_string(GetTickCount64()));
    assert(fs::create_directory(root));
    struct Cleanup {
        fs::path root;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(root, ec);
        }
    } cleanup{root};
    if (argc == 2 && std::string_view(argv[1]) == "--project-post") {
        CheckProjectPostContracts(root);
        return 0;
    }
    fs::create_directory(root / "Assets");
    const auto source = root / "Assets/reference.tga";
    const auto jsonPath = root / "reader.json";
    nlohmann::json json = {{"unchanged", true}};
    for (const std::string bytes :
         {std::string{}, std::string{"{broken"}, std::string{"{} trailing"}, std::string{"{\"x\":1}\0bad", 12}}) {
        {
            std::ofstream out(jsonPath, std::ios::binary);
            out.write(bytes.data(), bytes.size());
        }
        assert(!SERIALIZATION::JSON::ReadJsonFile(jsonPath, json) && json == nlohmann::json({{"unchanged", true}}));
    }
    {
        std::ofstream out(jsonPath, std::ios::binary);
        out << "\xef\xbb\xbf{\r\n\"value\": [1,2,3],\"unicode\":\"\\u65e5\"}\r\n";
    }
    assert(SERIALIZATION::JSON::ReadJsonFile(jsonPath, json) && json["value"].size() == 3 &&
           json["unicode"] == "\xe6\x97\xa5");
    WriteTga(source, {255, 0, 0});
    AssetDatabase database;
    assert(database.Initialize(root) && database.ScanAssets(true));
    const auto* sourceRecord = database.FindByPath("Assets/reference.tga");
    assert(sourceRecord);
    const auto guid = sourceRecord->guid;
    assert(database.ImportAsset(guid));
    CheckImportSettings(database, root, guid);
    const auto artifact = database.GetImportedDirectory(guid) / "texture.htex";
    const auto meta = database.GetMetaPathForSource("Assets/reference.tga");
    const auto manifest = database.GetArtifactManifestPath(guid);
    auto originalArtifact = Read(artifact), originalMeta = Read(meta), originalManifest = Read(manifest);
    // 主成果物が欠け、補助成果物だけ残る場合も再インポートが必要。
    const auto manifestTimestamp = fs::last_write_time(manifest);
    auto secondaryOnly = nlohmann::json::parse(originalManifest);
    secondaryOnly["artifacts"] = nlohmann::json::array(
        {{{"path", fs::relative(artifact, root).generic_string()}, {"format", "HTEX"}, {"role", "DiagnosticOnly"}}});
    assert(SERIALIZATION::JSON::WriteJsonFile(manifest, secondaryOnly));
    assert(database.ScanAssets(true) && database.FindByGuid(guid)->importOutdated);
    {
        std::ofstream stream(manifest, std::ios::binary);
        stream << originalManifest;
    }
    fs::last_write_time(manifest, manifestTimestamp);
    assert(database.ScanAssets(true));
    const fs::path unicodeSource = L"Assets/材质/テクスチャ.png";
    auto expectedMeta = database.GetSourceMetaRoot() / unicodeSource;
    expectedMeta += ".hikari.asset.json";
    assert(ASSETS::SEMANTICS::MakeSourceMetaPath(database.GetSourceMetaRoot(), unicodeSource) == expectedMeta);

    // 成功した取り込みの後の走査は report を開かない（外から書き換えた report は反映されない）。
    // 失敗一覧がない Library（既存のプロジェクト・削除後）では全 report を読み、一覧を作り直す。
    const auto report = database.GetImportedDirectory(guid) / "import_report.json";
    const auto failureList = database.GetLibraryRoot() / "AssetDatabase/ImportFailures.json";
    const auto overwrite = [](const fs::path& path, const std::string& bytes) {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        assert(stream.good());
    };
    const auto originalReport = Read(report);
    assert(fs::is_regular_file(failureList) && !database.FindByGuid(guid)->importOutdated);
    assert(SERIALIZATION::JSON::WriteJsonFile(report, {{"success", false}, {"message", "report failure"}}));
    assert(database.ScanAssets(true) && database.FindByGuid(guid)->lastImportSucceeded);
    assert(fs::remove(failureList) && database.ScanAssets(true) && fs::is_regular_file(failureList));
    assert(!database.FindByGuid(guid)->lastImportSucceeded &&
           database.FindByGuid(guid)->lastImportMessage == "report failure");
    overwrite(report, originalReport);
    assert(database.ScanAssets(true) && database.FindByGuid(guid)->lastImportSucceeded);
    // 失敗した取り込みは manifest を前回の成功のまま残す。入力が manifest と同じに戻っても、
    // 作り直した database は失敗を表示し、次の成功で消える。
    const auto reopenedRecord = [&]() {
        AssetDatabase reopened;
        assert(reopened.Initialize(root) && reopened.ScanAssets(true));
        return *reopened.FindByGuid(guid);
    };
    const auto originalSource = Read(source);
    overwrite(source, "not a tga");
    assert(!database.ImportAsset(guid));
    const std::string failureMessage = database.FindByGuid(guid)->lastImportMessage;
    assert(!failureMessage.empty() && Read(manifest) == originalManifest);
    overwrite(source, originalSource);
    const auto failedRecord = reopenedRecord();
    assert(!failedRecord.importOutdated && !failedRecord.lastImportSucceeded &&
           failedRecord.lastImportMessage == failureMessage);
    assert(database.ImportAsset(guid) && reopenedRecord().lastImportSucceeded);
    const auto queueAndFinishWorker = [&] {
        assert(database.QueueImportAssets({guid}, "Publication contract"));
        const auto status = database.GetQueuedImportStatus();
        assert(status.activeTaskIds.size() == 1);
        const auto id = status.activeTaskIds[0];
        Until([&] {
            const auto task = database.GetAssetTaskService().FindSnapshot(id);
            return task && task->state == AssetTaskState::Finalizing;
        });
        assert(!database.Initialize(root)); // 処理中の database を別 project へ付け替えない。
    };
    const auto finish = [&] {
        Until([&] {
            database.PumpAssetTasks();
            return !database.GetQueuedImportStatus().active;
        });
    };

    // 同期/非同期の選択、重複排除、失敗集計は同じ契約を使う。
    const AssetGuid absent{"ffffffffffffffffffffffffffffffff"};
    const auto selection = std::vector<AssetGuid>{guid, guid, {}, absent};
    const auto syncBatch = database.ImportAssets(selection);
    assert(syncBatch.attempted == 1 && syncBatch.succeeded == 1 && syncBatch.failed == 1);
    assert(database.QueueImportAssets(selection, "Batch selection parity"));
    finish();
    const auto asyncBatch = database.GetQueuedImportStatus();
    assert(asyncBatch.total == 2 && asyncBatch.finished == 2 && asyncBatch.attempted == 1 &&
           asyncBatch.succeeded == 1 && asyncBatch.failed == 1);

    originalArtifact = Read(artifact);
    originalMeta = Read(meta);
    originalManifest = Read(manifest);

    WriteTga(source, {0, 255, 0});
    queueAndFinishWorker();
    assert(Read(artifact) == originalArtifact); // prepare 中は旧データを読める。
    assert(database.RequestCancelQueuedImport());
    finish();
    assert(Read(artifact) == originalArtifact && Read(meta) == originalMeta && Read(manifest) == originalManifest);
    assert(database.FindByGuid(guid)->importOutdated);

    queueAndFinishWorker();
    finish();
    assert(database.GetQueuedImportStatus().succeeded == 1);
    assert(Read(artifact) != originalArtifact);
    const auto acceptedArtifact = Read(artifact), acceptedMeta = Read(meta), acceptedManifest = Read(manifest);

    queueAndFinishWorker();
    auto externalMeta = nlohmann::json::parse(acceptedMeta);
    externalMeta["displayName"] = "Changed by another editor";
    assert(SERIALIZATION::JSON::WriteJsonFile(meta, externalMeta));
    const auto externalBytes = Read(meta);
    finish();
    assert(database.GetQueuedImportStatus().failed == 1);
    assert(Read(meta) == externalBytes && Read(artifact) == acceptedArtifact);
    {
        std::ofstream restore(meta, std::ios::binary);
        restore << acceptedMeta;
    }

    WriteTga(source, {0, 0, 255});
    queueAndFinishWorker();
    WriteTga(source, {255, 255, 255});
    finish();
    assert(database.GetQueuedImportStatus().failed == 1);
    assert(Read(artifact) == acceptedArtifact && Read(meta) == acceptedMeta && Read(manifest) == acceptedManifest);

    std::ofstream(source, std::ios::binary) << "invalid TGA";
    queueAndFinishWorker();
    finish();
    assert(database.GetQueuedImportStatus().failed == 1);
    assert(Read(artifact) == acceptedArtifact && Read(meta) == acceptedMeta && Read(manifest) == acceptedManifest);
#if defined(HIKARI_WITH_EDITOR)
    CheckThinModelImport(database, root);
    CheckFbxUnitScale(database, root);
    CheckFbxSkinnedMeshNodes(database, root);
    CheckGltfSkinnedMeshNodes(database, root);
    CheckImportedSpecular(database, root);
    CheckObjDependencies(database, root);
#endif
    CheckPostAssets(database, root);
    CheckProjectPostContracts(root);
    CheckPainterlyEngineDependencies(root);
    for (int index = 1; index < argc; ++index) {
        CheckSpriteAssetIntegration(fs::absolute(TEXT::PathFromUtf8(argv[index])),
                                    root / ("SpriteContract-" + std::to_string(index)));
    }
    return 0;
}
