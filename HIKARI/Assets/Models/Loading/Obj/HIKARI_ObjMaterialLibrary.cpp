#include "Assets/Models/Loading/Obj/HIKARI_ObjMaterialLibrary.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Assets/Models/Loading/HIKARI_ModelSourcePath.h"
#include "Assets/Models/Loading/Obj/HIKARI_ObjText.h"

namespace HIKARI::ASSETS::MODELS::OBJ {

    namespace {
        std::string ExtractTexturePath(std::stringstream& stream) {
            std::vector<std::string> tokens;
            std::string token;
            while (stream >> token) {
                tokens.push_back(token);
            }

            while (!tokens.empty() &&
                !tokens.front().empty() &&
                tokens.front()[0] == '-') {
                tokens.erase(tokens.begin());
                if (!tokens.empty()) {
                    tokens.erase(tokens.begin());
                }
            }
            if (tokens.empty()) {
                return {};
            }

            std::string path = tokens.front();
            for (size_t i = 1; i < tokens.size(); ++i) {
                path += " " + tokens[i];
            }
            return TrimText(path);
        }

        float RoughnessFromPhongExponent(float exponent) {
            exponent = (std::max)(0.0f, exponent);
            return (std::clamp)(
                std::sqrt(2.0f / (exponent + 2.0f)),
                0.04f,
                1.0f);
        }

        bool ReadMaterialLibrary(
            const std::filesystem::path& libraryPath,
            MaterialLibrary& outMaterials) {

            outMaterials.clear();
            std::ifstream file(libraryPath);
            if (!file.is_open()) {
                return false;
            }

            MaterialDescription* current = nullptr;
            std::string line;
            while (std::getline(file, line)) {
                std::stringstream stream(line);
                std::string tag;
                stream >> tag;
                if (tag.empty() || tag[0] == '#') {
                    continue;
                }

                if (tag == "newmtl") {
                    std::string materialName;
                    stream >> materialName;
                    if (materialName.empty()) {
                        current = nullptr;
                        continue;
                    }

                    MaterialDescription description{};
                    description.name = materialName;
                    auto [it, _] =
                        outMaterials.emplace(materialName, std::move(description));
                    current = &it->second;
                    continue;
                }
                if (current == nullptr) {
                    continue;
                }

                if (tag == "Kd") {
                    stream >>
                        current->baseColor.x >>
                        current->baseColor.y >>
                        current->baseColor.z;
                } else if (tag == "d") {
                    stream >> current->alpha;
                    current->alpha = (std::clamp)(current->alpha, 0.0f, 1.0f);
                    current->baseColor.w = current->alpha;
                    current->hasAlpha = current->alpha < 0.999f;
                } else if (tag == "Tr") {
                    float transparency = 0.0f;
                    stream >> transparency;
                    current->alpha =
                        (std::clamp)(1.0f - transparency, 0.0f, 1.0f);
                    current->baseColor.w = current->alpha;
                    current->hasAlpha = current->alpha < 0.999f;
                } else if (tag == "Ns") {
                    float exponent = 0.0f;
                    stream >> exponent;
                    current->roughness = RoughnessFromPhongExponent(exponent);
                } else if (tag == "map_Kd") {
                    const std::string texturePath = ExtractTexturePath(stream);
                    if (!texturePath.empty()) {
                        current->baseColorMapPath = NormalizeModelSourcePath(
                            libraryPath.parent_path() / texturePath);
                    }
                } else if (tag == "map_Bump" || tag == "bump") {
                    const std::string texturePath = ExtractTexturePath(stream);
                    if (!texturePath.empty()) {
                        current->normalMapPath = NormalizeModelSourcePath(
                            libraryPath.parent_path() / texturePath);
                    }
                } else if (tag == "map_Ns" || tag == "map_Pr") {
                    const std::string texturePath = ExtractTexturePath(stream);
                    if (!texturePath.empty()) {
                        current->roughnessMapPath = NormalizeModelSourcePath(
                            libraryPath.parent_path() / texturePath);
                    }
                }
            }
            return !outMaterials.empty();
        }

        int AddTexture(
            const std::string& path,
            const std::string& name,
            ModelAsset& asset,
            TextureIndexByPath& textureIndexByPath) {

            if (path.empty()) {
                return -1;
            }

            const auto found = textureIndexByPath.find(path);
            if (found != textureIndexByPath.end()) {
                return found->second;
            }

            TextureAsset3D texture{};
            texture.name = name;
            texture.sourcePath = path;
            const int index = static_cast<int>(asset.textures.size());
            asset.textures.push_back(std::move(texture));
            textureIndexByPath.emplace(path, index);
            return index;
        }
    } // namespace

    void LoadMaterialLibraries(
        const std::vector<std::filesystem::path>& libraryPaths,
        ModelAsset& asset,
        MaterialLibrary& outLibrary) {

        outLibrary.clear();
        for (const std::filesystem::path& libraryPath : libraryPaths) {
            MaterialLibrary parsed;
            if (!ReadMaterialLibrary(libraryPath, parsed)) {
                ++asset.importDiagnostics.unsupportedFeatureCount;
                asset.importDiagnostics.messages.push_back(
                    "[OBJ] mtllib not found or empty: " +
                    libraryPath.generic_string());
                continue;
            }

            for (auto& pair : parsed) {
                outLibrary[pair.first] = std::move(pair.second);
            }
        }
    }

    uint32_t ResolveMaterialIndex(
        const std::string& materialName,
        const MaterialLibrary& library,
        ModelAsset& asset,
        MaterialIndexByName& materialIndexByName,
        TextureIndexByPath& textureIndexByPath) {

        const std::string name = materialName.empty() ? "Default" : materialName;
        const auto found = materialIndexByName.find(name);
        if (found != materialIndexByName.end()) {
            return found->second;
        }

        MaterialDescription description{};
        description.name = name;
        if (const auto source = library.find(name); source != library.end()) {
            description = source->second;
        }

        MaterialAsset material{};
        material.name = description.name.empty() ? name : description.name;
        material.baseColorFactor = description.baseColor;
        material.roughnessFactor = description.roughness;
        material.metallicFactor = description.metallic;
        if (description.hasAlpha) {
            material.alphaMode = AlphaMode::Blend;
        }

        const int baseColorIndex = AddTexture(
            description.baseColorMapPath,
            material.name + "_baseColor",
            asset,
            textureIndexByPath);
        if (baseColorIndex >= 0) {
            material.baseColorTexture.textureIndex = baseColorIndex;
        }

        const int normalIndex = AddTexture(
            description.normalMapPath,
            material.name + "_normal",
            asset,
            textureIndexByPath);
        if (normalIndex >= 0) {
            material.normalTexture.textureIndex = normalIndex;
        }

        const int roughnessIndex = AddTexture(
            description.roughnessMapPath,
            material.name + "_roughness",
            asset,
            textureIndexByPath);
        if (roughnessIndex >= 0) {
            material.metallicRoughnessTexture.textureIndex = roughnessIndex;
        }

        const uint32_t index = static_cast<uint32_t>(asset.materials.size());
        asset.materials.push_back(std::move(material));
        materialIndexByName.emplace(name, index);
        return index;
    }

} // namespace HIKARI::ASSETS::MODELS::OBJ
