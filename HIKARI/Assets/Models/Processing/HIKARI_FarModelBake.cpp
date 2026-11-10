#include "HIKARI_FarModelBake.h"

#include <Windows.h>
#include <DirectXTex.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <execution>
#include <fstream>
#include <map>
#include <memory>
#include <vector>

#include <json.hpp>

#include "HIKARI_FarModelCard.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "RenderContracts/Model/HIKARI_ModelBounds.h"

namespace HIKARI {
namespace {

using namespace ASSETS::MODELS;

// 水平の切片が受け持つ高さ。根元は幹だけなので除く。
constexpr float kSliceBottom = 0.2f;
constexpr uint32_t kSliceCount = 3u;
// 太陽の影の光線が大量配置を無視する面の印（HIKARI_SceneSurfaceContract.hlsli の HIKARI_FAR_MODEL_METALLIC）。
// 面が木の中心にあるので、印がないと光線が自分の枝を通って真っ黒になる。
constexpr double kFarModelMetallic = 1.0 / 255.0;

bool WritePng(const std::filesystem::path& path, uint32_t size, const std::vector<uint8_t>& rgba, bool srgb,
              std::string& outMessage) {
    DirectX::Image image{};
    image.width = size;
    image.height = size;
    image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    image.rowPitch = size_t(size) * 4u;
    image.slicePitch = image.rowPitch * size;
    image.pixels = const_cast<uint8_t*>(rgba.data());
    if (FAILED(DirectX::SaveToWICFile(image, srgb ? DirectX::WIC_FLAGS_FORCE_SRGB : DirectX::WIC_FLAGS_FORCE_LINEAR,
                                      DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), path.c_str()))) {
        outMessage = "Cannot write: " + path.generic_string();
        return false;
    }
    return true;
}

bool WriteCardsGltf(const std::vector<BakeCard>& cards, uint32_t atlasPixels, const FarModelBakeRequest& request,
                    std::string& outMessage) {
    struct QuadVertex {
        float position[3];
        float normal[3];
        float tangent[4];
        float uv[2];
    };
    std::vector<QuadVertex> vertices{};
    std::vector<uint16_t> indices{};
    Vec3 low{FLT_MAX, FLT_MAX, FLT_MAX};
    Vec3 high{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (const BakeCard& card : cards) {
        const uint16_t base = uint16_t(vertices.size());
        const float inner = float(card.cell - kFarModelCardMargin * 2u);
        const float u0 = float(card.originX + kFarModelCardMargin);
        const float v0 = float(card.originY + kFarModelCardMargin);
        constexpr float kCorners[4][2]{{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
        for (const auto& corner : kCorners) {
            const Vec3 position =
                card.center + card.right * (corner[0] * card.halfU) + card.up * (corner[1] * card.halfV);
            low = {(std::min)(low.x, position.x), (std::min)(low.y, position.y), (std::min)(low.z, position.z)};
            high = {(std::max)(high.x, position.x), (std::max)(high.y, position.y), (std::max)(high.z, position.z)};
            vertices.push_back(QuadVertex{{position.x, position.y, position.z},
                                          {card.toCamera.x, card.toCamera.y, card.toCamera.z},
                                          {card.right.x, card.right.y, card.right.z, 1.0f},
                                          {(u0 + (corner[0] * 0.5f + 0.5f) * inner) / float(atlasPixels),
                                           (v0 + (0.5f - corner[1] * 0.5f) * inner) / float(atlasPixels)}});
        }
        for (const int offset : {0, 1, 2, 0, 2, 3}) {
            indices.push_back(uint16_t(base + offset));
        }
    }
    const std::string binName = request.name + ".bin";
    const uint32_t vertexBytes = uint32_t(vertices.size() * sizeof(QuadVertex));
    const uint32_t indexBytes = uint32_t(indices.size() * sizeof(uint16_t));
    std::ofstream bin(request.outputDirectory / binName, std::ios::binary | std::ios::trunc);
    bin.write(reinterpret_cast<const char*>(vertices.data()), vertexBytes);
    bin.write(reinterpret_cast<const char*>(indices.data()), indexBytes);
    if (!bin.good()) {
        outMessage = "Cannot write: " + (request.outputDirectory / binName).generic_string();
        return false;
    }
    bin.close();

    const auto attribute = [&](size_t offset, const char* type) {
        return nlohmann::json{{"bufferView", 0},
                              {"byteOffset", offset},
                              {"componentType", 5126},
                              {"count", vertices.size()},
                              {"type", type}};
    };
    nlohmann::json positionAccessor = attribute(offsetof(QuadVertex, position), "VEC3");
    positionAccessor["min"] = {low.x, low.y, low.z};
    positionAccessor["max"] = {high.x, high.y, high.z};
    nlohmann::json document{};
    document["asset"] = {{"version", "2.0"}, {"generator", "HIKARI far model bake"}};
    document["scene"] = 0;
    document["scenes"] = nlohmann::json::array({nlohmann::json{{"nodes", {0}}}});
    document["nodes"] = nlohmann::json::array({nlohmann::json{{"mesh", 0}, {"name", request.name}}});
    nlohmann::json primitive{};
    primitive["attributes"] = {{"POSITION", 0}, {"NORMAL", 1}, {"TANGENT", 2}, {"TEXCOORD_0", 3}};
    primitive["indices"] = 4;
    primitive["material"] = 0;
    document["meshes"] = nlohmann::json::array(
        {nlohmann::json{{"name", request.name}, {"primitives", nlohmann::json::array({primitive})}}});
    nlohmann::json material{};
    material["name"] = request.name;
    material["doubleSided"] = true;
    material["alphaMode"] = "MASK";
    material["alphaCutoff"] = 0.5;
    material["pbrMetallicRoughness"] = {
        {"baseColorTexture", {{"index", 0}}}, {"metallicFactor", kFarModelMetallic}, {"roughnessFactor", 0.9}};
    material["normalTexture"] = {{"index", 1}};
    document["materials"] = nlohmann::json::array({material});
    document["textures"] = nlohmann::json::array({nlohmann::json{{"source", 0}}, nlohmann::json{{"source", 1}}});
    document["images"] = nlohmann::json::array(
        {nlohmann::json{{"uri", request.name + "_color.png"}}, nlohmann::json{{"uri", request.name + "_normal.png"}}});
    document["buffers"] =
        nlohmann::json::array({nlohmann::json{{"uri", binName}, {"byteLength", vertexBytes + indexBytes}}});
    document["bufferViews"] = nlohmann::json::array(
        {nlohmann::json{{"buffer", 0},
                        {"byteOffset", 0},
                        {"byteLength", vertexBytes},
                        {"byteStride", sizeof(QuadVertex)},
                        {"target", 34962}},
         nlohmann::json{{"buffer", 0}, {"byteOffset", vertexBytes}, {"byteLength", indexBytes}, {"target", 34963}}});
    document["accessors"] = nlohmann::json::array(
        {positionAccessor, attribute(offsetof(QuadVertex, normal), "VEC3"),
         attribute(offsetof(QuadVertex, tangent), "VEC4"), attribute(offsetof(QuadVertex, uv), "VEC2"),
         nlohmann::json{{"bufferView", 1}, {"componentType", 5123}, {"count", indices.size()}, {"type", "SCALAR"}}});
    std::ofstream gltf(request.outputDirectory / (request.name + ".gltf"), std::ios::binary | std::ios::trunc);
    gltf << document.dump(1);
    if (!gltf.good()) {
        outMessage = "Cannot write the far model glTF.";
        return false;
    }
    return true;
}

} // namespace

bool BakeFarModel(const ModelAsset& model, const FarModelBakeRequest& request, std::string& outMessage) {
    if (request.meshIndex >= model.meshes.size() || request.atlasPixels < 64u || request.name.empty()) {
        outMessage = "Far model bake needs a valid mesh, atlas size and name.";
        return false;
    }
    // 元の貼り絵を読み、部品の材質へ割り当てる。
    std::map<int, std::unique_ptr<CpuImage>> images{};
    std::vector<BakeMaterial> materials(model.materials.size() + 1u);
    for (size_t index = 0u; index < model.materials.size(); ++index) {
        const MaterialAsset& source = model.materials[index];
        BakeMaterial& material = materials[index];
        material.factor[0] = source.baseColorFactor.x;
        material.factor[1] = source.baseColorFactor.y;
        material.factor[2] = source.baseColorFactor.z;
        material.factor[3] = source.baseColorFactor.w;
        material.cutoff = source.alphaMode == AlphaMode::Opaque ? 0.0f
                          : source.alphaMode == AlphaMode::Mask ? source.alphaCutoff
                                                                : 0.5f;
        const int textureIndex = source.baseColorTexture.textureIndex;
        if (textureIndex < 0 || size_t(textureIndex) >= model.textures.size() || !request.resolveTexture) {
            continue;
        }
        auto found = images.find(textureIndex);
        if (found == images.end()) {
            const std::filesystem::path path = request.resolveTexture(model.textures[size_t(textureIndex)]);
            auto image = std::make_unique<CpuImage>();
            if (!path.empty() && !LoadCpuImage(path, *image, outMessage)) {
                return false;
            }
            found = images.emplace(textureIndex, std::move(image)).first;
        }
        material.baseColor = found->second->width != 0u ? found->second.get() : nullptr;
    }

    // この網を指す最初の節の位置で、部品を一つの三角形の列にする。描画と同じ置き方。
    MATH::Mat4 nodeMatrix = MATH::Mat4::Identity();
    const auto nodeGlobals = BOUNDS::BuildModelNodeGlobals(model);
    for (size_t index = 0u; index < model.nodes.size(); ++index) {
        if (model.nodes[index].meshIndex == int(request.meshIndex)) {
            nodeMatrix = nodeGlobals[index];
            break;
        }
    }
    Vec3 low{FLT_MAX, FLT_MAX, FLT_MAX};
    Vec3 high{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    std::vector<BakeTriangle> triangles{};
    for (const MeshPrimitive& primitive : model.meshes[request.meshIndex].primitives) {
        if (primitive.layout != VertexLayoutKind::StaticPNTT) {
            outMessage = "Far model bake does not support skinned meshes.";
            return false;
        }
        const uint32_t material = (std::min)(primitive.materialIndex, uint32_t(model.materials.size()));
        const auto read = [&](uint32_t index) {
            const Vertex3D& vertex = primitive.staticVertices[index];
            const Vec3 position = MATH::TransformPoint(nodeMatrix, vertex.position);
            low = {(std::min)(low.x, position.x), (std::min)(low.y, position.y), (std::min)(low.z, position.z)};
            high = {(std::max)(high.x, position.x), (std::max)(high.y, position.y), (std::max)(high.z, position.z)};
            return BakeVertex{position, MATH::Normalize(MATH::TransformVector(nodeMatrix, vertex.normal)), vertex.uv0};
        };
        for (size_t index = 0u; index + 2u < primitive.indices.size(); index += 3u) {
            triangles.push_back(BakeTriangle{{read(primitive.indices[index]), read(primitive.indices[index + 1u]),
                                              read(primitive.indices[index + 2u])},
                                             material});
        }
    }
    if (triangles.empty()) {
        outMessage = "The mesh has no triangles.";
        return false;
    }

    const float centerX = (low.x + high.x) * 0.5f;
    const float centerZ = (low.z + high.z) * 0.5f;
    const float height = (std::max)(high.y - low.y, 1.0e-3f);
    const auto radialExtent = [&](float minHeight, float maxHeight) {
        float extent = 0.0f;
        for (const BakeTriangle& triangle : triangles) {
            for (const BakeVertex& vertex : triangle.vertices) {
                if (vertex.position.y >= minHeight && vertex.position.y <= maxHeight) {
                    extent = (std::max)(extent, std::hypot(vertex.position.x - centerX, vertex.position.z - centerZ));
                }
            }
        }
        return extent;
    };
    const float radius = (std::max)(radialExtent(-FLT_MAX, FLT_MAX), 1.0e-3f);

    // 貼り絵は四分割。縦の面 3 枚で三つ、残りの四分の一を切片で分ける。
    const uint32_t atlas = request.atlasPixels;
    const uint32_t cell = atlas / 2u;
    std::vector<BakeCard> cards{};
    for (uint32_t index = 0u; index < 3u; ++index) {
        const float angle = float(index) * 3.14159265f / 3.0f;
        BakeCard card{};
        card.toCamera = {std::sin(angle), 0.0f, std::cos(angle)};
        card.up = {0.0f, 1.0f, 0.0f};
        card.right = MATH::Cross(card.up, card.toCamera);
        card.center = {centerX, low.y + height * 0.5f, centerZ};
        card.halfU = radius * 1.02f;
        card.halfV = height * 0.51f;
        card.originX = (index % 2u) * cell;
        card.originY = (index / 2u) * cell;
        card.cell = cell;
        cards.push_back(card);
    }
    // 高さ別の水平の切片。半径がその高さの枝に合うので、水平に見ても輪郭の外へ出ず、見下ろすと重なって木の形になる。
    for (uint32_t index = 0u; index < kSliceCount; ++index) {
        const float bandHeight = height * (1.0f - kSliceBottom) / float(kSliceCount);
        BakeCard card{};
        card.minHeight = low.y + height * kSliceBottom + bandHeight * float(index);
        card.maxHeight = card.minHeight + bandHeight;
        card.toCamera = {0.0f, 1.0f, 0.0f};
        card.up = {0.0f, 0.0f, -1.0f};
        card.right = {1.0f, 0.0f, 0.0f};
        card.center = {centerX, (card.minHeight + card.maxHeight) * 0.5f, centerZ};
        card.halfU = card.halfV = (std::max)(radialExtent(card.minHeight, card.maxHeight), radius * 0.1f) * 1.02f;
        card.slice = true;
        card.originX = cell + (index % 2u) * (cell / 2u);
        card.originY = cell + (index / 2u) * (cell / 2u);
        card.cell = cell / 2u;
        cards.push_back(card);
    }

    std::vector<uint8_t> colorAtlas(size_t(atlas) * atlas * 4u, 0u);
    std::vector<uint8_t> normalAtlas(size_t(atlas) * atlas * 4u, 255u);
    std::for_each(std::execution::par, cards.begin(), cards.end(), [&](const BakeCard& card) {
        RenderCardIntoAtlas(card, triangles, materials, atlas, colorAtlas.data(), normalAtlas.data());
    });

    std::error_code fileError{};
    std::filesystem::create_directories(request.outputDirectory, fileError);
    return WritePng(request.outputDirectory / (request.name + "_color.png"), atlas, colorAtlas, true, outMessage) &&
           WritePng(request.outputDirectory / (request.name + "_normal.png"), atlas, normalAtlas, false, outMessage) &&
           WriteCardsGltf(cards, atlas, request, outMessage);
}

} // namespace HIKARI
