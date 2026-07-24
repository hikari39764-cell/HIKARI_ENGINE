#include "Assets/Models/Loading/Assimp/HIKARI_AssimpMaterialReader.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <assimp/material.h>
#include <assimp/scene.h>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Assets/Models/HIKARI_ModelAssetLookup.h"
#include "Assets/Models/Loading/Assimp/HIKARI_AssimpTypeConversion.h"
#include "Assets/Models/Loading/HIKARI_ModelSourcePath.h"

namespace HIKARI::ASSETS::MODELS::ASSIMP {

    namespace {
        std::string SanitizeAssimpPath(std::string value) {
            std::replace(value.begin(), value.end(), '\\', '/');
            constexpr const char* kFileUriPrefix = "file://";
            if (value.rfind(kFileUriPrefix, 0) == 0) {
                value.erase(0, std::char_traits<char>::length(kFileUriPrefix));
            }
            return value;
        }

        bool IsEmbeddedTextureReference(const std::string& value) {
            return !value.empty() && value.front() == '*';
        }

        bool IsUnsupportedUri(const std::string& value) {
            const size_t scheme = value.find("://");
            return scheme != std::string::npos && value.rfind("file://", 0) != 0;
        }

        float Clamp01(float value) {
            return std::clamp(value, 0.0f, 1.0f);
        }

        float RoughnessFromPhongShininess(float shininess) {
            if (!std::isfinite(shininess) || shininess <= 0.0f) {
                return 1.0f;
            }
            return Clamp01(std::sqrt(2.0f / (shininess + 2.0f)));
        }

        int AddTexture(
            ModelAsset& asset,
            std::unordered_map<std::string, int>& textureToIndex,
            const std::filesystem::path& sourceDirectory,
            std::string rawPath,
            const char* usageName) {

            rawPath = SanitizeAssimpPath(std::move(rawPath));
            if (rawPath.empty()) {
                return -1;
            }
            if (IsEmbeddedTextureReference(rawPath)) {
                ++asset.importDiagnostics.unsupportedFeatureCount;
                ++asset.importDiagnostics.unresolvedTextureCount;
                asset.importDiagnostics.messages.push_back(
                    "[Assimp] embedded texture is not extracted yet usage=" +
                    std::string(usageName) +
                    " ref=" + rawPath);
                return -1;
            }
            if (IsUnsupportedUri(rawPath)) {
                ++asset.importDiagnostics.unsupportedFeatureCount;
                ++asset.importDiagnostics.unresolvedTextureCount;
                asset.importDiagnostics.messages.push_back(
                    "[Assimp] unsupported texture URI usage=" +
                    std::string(usageName) +
                    " uri=" + rawPath);
                return -1;
            }

            std::filesystem::path texturePath(rawPath);
            if (texturePath.is_relative()) {
                texturePath = sourceDirectory / texturePath;
            }
            const std::string normalizedPath = NormalizeModelSourcePath(texturePath);
            const auto found = textureToIndex.find(normalizedPath);
            if (found != textureToIndex.end()) {
                return found->second;
            }

            TextureAsset3D texture{};
            texture.name = texturePath.filename().string();
            texture.sourcePath = normalizedPath;
            const int textureIndex = static_cast<int>(asset.textures.size());
            asset.textures.push_back(std::move(texture));
            textureToIndex.emplace(normalizedPath, textureIndex);
            return textureIndex;
        }

        bool ReadTextureSlot(
            ModelAsset& asset,
            std::unordered_map<std::string, int>& textureToIndex,
            const std::filesystem::path& sourceDirectory,
            const aiMaterial& material,
            const std::string& materialName,
            const std::vector<aiTextureType>& textureTypes,
            TextureSlot& outSlot,
            const char* usageName) {

            for (aiTextureType textureType : textureTypes) {
                if (material.GetTextureCount(textureType) == 0u) {
                    continue;
                }

                aiString texturePath{};
                unsigned int uvIndex = 0u;
                if (material.GetTexture(textureType, 0u, &texturePath, nullptr, &uvIndex) != AI_SUCCESS) {
                    continue;
                }

                const int textureIndex = AddTexture(
                    asset,
                    textureToIndex,
                    sourceDirectory,
                    ToString(texturePath),
                    usageName);
                if (textureIndex < 0) {
                    return false;
                }

                outSlot.textureIndex = textureIndex;
                outSlot.texCoord = static_cast<int>((std::min)(uvIndex, 1u));
                if (uvIndex > 1u) {
                    ++asset.importDiagnostics.unsupportedFeatureCount;
                    asset.importDiagnostics.messages.push_back(
                        "[Assimp] texture uses UV set above 1; HIKARI currently keeps UV0/UV1 only. material=" +
                        materialName +
                        " usage=" + std::string(usageName) +
                        " texCoord=" + std::to_string(uvIndex));
                }

                aiUVTransform uvTransform{};
                if (material.Get(AI_MATKEY_UVTRANSFORM(textureType, 0), uvTransform) == AI_SUCCESS) {
                    const bool hasUvTransform =
                        std::abs(uvTransform.mTranslation.x) > 1.0e-6f ||
                        std::abs(uvTransform.mTranslation.y) > 1.0e-6f ||
                        std::abs(uvTransform.mScaling.x - 1.0f) > 1.0e-6f ||
                        std::abs(uvTransform.mScaling.y - 1.0f) > 1.0e-6f ||
                        std::abs(uvTransform.mRotation) > 1.0e-6f;
                    if (hasUvTransform) {
                        outSlot.uvScale = { uvTransform.mScaling.x, uvTransform.mScaling.y };
                        outSlot.uvOffset = { uvTransform.mTranslation.x, uvTransform.mTranslation.y };
                        outSlot.uvRotation = uvTransform.mRotation;
                    }
                }
                if (material.GetTextureCount(textureType) > 1u) {
                    ++asset.importDiagnostics.unsupportedFeatureCount;
                    asset.importDiagnostics.messages.push_back(
                        "[Assimp] only first texture is used usage=" +
                        std::string(usageName) +
                        " count=" + std::to_string(material.GetTextureCount(textureType)));
                }
                return true;
            }
            return false;
        }
    } // namespace

    void ReadMaterials(
            const aiScene& scene,
            const std::filesystem::path& sourceDirectory,
            ModelAsset& asset) {

            std::unordered_map<std::string, int> textureToIndex;
            asset.materials.reserve(scene.mNumMaterials);

            for (unsigned int i = 0; i < scene.mNumMaterials; ++i) {
                const aiMaterial* source = scene.mMaterials[i];
                if (source == nullptr) {
                    continue;
                }

                MaterialAsset material{};

                aiString materialName{};
                if (source->Get(AI_MATKEY_NAME, materialName) == AI_SUCCESS) {
                    material.name = ToString(materialName);
                }
                if (material.name.empty()) {
                    material.name = "AssimpMaterial" + std::to_string(i);
                }

                aiColor4D color{};
                if (aiGetMaterialColor(source, AI_MATKEY_BASE_COLOR, &color) == AI_SUCCESS ||
                    aiGetMaterialColor(source, AI_MATKEY_COLOR_DIFFUSE, &color) == AI_SUCCESS) {
                    material.baseColorFactor = ToVec4(color);
                }

                if (aiGetMaterialColor(source, AI_MATKEY_COLOR_SPECULAR, &color) == AI_SUCCESS) {
                    material.specularColorFactor = { color.r, color.g, color.b };
                    material.specularFactor = (std::max)({ color.r, color.g, color.b, 0.0f });
                }

                float value = 0.0f;
                if (source->Get(AI_MATKEY_METALLIC_FACTOR, value) == AI_SUCCESS) {
                    material.metallicFactor = Clamp01(value);
                }
                if (source->Get(AI_MATKEY_ROUGHNESS_FACTOR, value) == AI_SUCCESS) {
                    material.roughnessFactor = Clamp01(value);
                } else if (source->Get(AI_MATKEY_SHININESS, value) == AI_SUCCESS) {
                    material.roughnessFactor = RoughnessFromPhongShininess(value);
                }
                if (source->Get(AI_MATKEY_SPECULAR_FACTOR, value) == AI_SUCCESS) {
                    material.specularFactor = Clamp01(value);
                }
                if (source->Get(AI_MATKEY_OPACITY, value) == AI_SUCCESS) {
                    material.baseColorFactor.w = Clamp01(value);
                    if (material.baseColorFactor.w < 0.999f) {
                        material.alphaMode = AlphaMode::Blend;
                    }
                }

                int boolValue = 0;
                if (source->Get(AI_MATKEY_TWOSIDED, boolValue) == AI_SUCCESS) {
                    material.doubleSided = boolValue != 0;
                }

                int shadingMode = 0;
                if (source->Get(AI_MATKEY_SHADING_MODEL, shadingMode) == AI_SUCCESS &&
                    (shadingMode == aiShadingMode_NoShading || shadingMode == aiShadingMode_Unlit)) {
                    material.featureBits |= MATERIAL_FEATURES::Unlit;
                }

                if (aiGetMaterialColor(source, AI_MATKEY_COLOR_EMISSIVE, &color) == AI_SUCCESS) {
                    material.emissiveFactor = { color.r, color.g, color.b };
                    if (std::abs(color.r) > 1.0e-6f ||
                        std::abs(color.g) > 1.0e-6f ||
                        std::abs(color.b) > 1.0e-6f) {
                        material.featureBits |= MATERIAL_FEATURES::Emissive;
                    }
                }
                if (source->Get(AI_MATKEY_EMISSIVE_INTENSITY, value) == AI_SUCCESS) {
                    material.emissiveStrength = (std::max)(0.0f, value);
                }

                ReadTextureSlot(
                    asset,
                    textureToIndex,
                    sourceDirectory,
                    *source,
                    material.name,
                    { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE },
                    material.baseColorTexture,
                    "BaseColor");
                ReadTextureSlot(
                    asset,
                    textureToIndex,
                    sourceDirectory,
                    *source,
                    material.name,
                    { aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA, aiTextureType_HEIGHT },
                    material.normalTexture,
                    "Normal");
                ReadTextureSlot(
                    asset,
                    textureToIndex,
                    sourceDirectory,
                    *source,
                    material.name,
                    { aiTextureType_GLTF_METALLIC_ROUGHNESS },
                    material.metallicRoughnessTexture,
                    "MetallicRoughness");
                if (source->GetTextureCount(aiTextureType_METALNESS) > 0u ||
                    source->GetTextureCount(aiTextureType_DIFFUSE_ROUGHNESS) > 0u ||
                    source->GetTextureCount(aiTextureType_SHININESS) > 0u ||
                    source->GetTextureCount(aiTextureType_MAYA_SPECULAR_ROUGHNESS) > 0u) {
                    ++asset.importDiagnostics.unsupportedFeatureCount;
                    asset.importDiagnostics.messages.push_back(
                        "[Assimp] separate metal/roughness/gloss maps are preserved as diagnostics; HIKARI currently expects glTF packed metallicRoughness texture. material=" +
                        material.name);
                }
                ReadTextureSlot(
                    asset,
                    textureToIndex,
                    sourceDirectory,
                    *source,
                    material.name,
                    { aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP },
                    material.occlusionTexture,
                    "Occlusion");
                if (ReadTextureSlot(
                    asset,
                    textureToIndex,
                    sourceDirectory,
                    *source,
                    material.name,
                    { aiTextureType_EMISSION_COLOR, aiTextureType_EMISSIVE },
                    material.emissiveTexture,
                    "Emissive")) {
                    material.featureBits |= MATERIAL_FEATURES::Emissive;
                }
                ReadTextureSlot(
                    asset,
                    textureToIndex,
                    sourceDirectory,
                    *source,
                    material.name,
                    { aiTextureType_MAYA_SPECULAR_COLOR, aiTextureType_SPECULAR, aiTextureType_MAYA_SPECULAR },
                    material.specularColorTexture,
                    "SpecularColor");

                if (source->GetTextureCount(aiTextureType_OPACITY) > 0u) {
                    material.alphaMode = AlphaMode::Mask;
                    material.featureBits |= MATERIAL_FEATURES::AlphaMask;
                    material.doubleSided = true;
                    ++asset.importDiagnostics.unsupportedFeatureCount;
                    asset.importDiagnostics.messages.push_back(
                        "[Assimp] opacity texture is detected, but HIKARI does not have a dedicated opacity slot yet. material=" +
                        material.name);
                }

                const TextureAsset3D* baseColorTexture =
                    ::HIKARI::ASSETS::MODELS::FindModelTexture(
                        asset,
                        material.baseColorTexture);
                const std::string_view baseColorTextureName =
                    baseColorTexture != nullptr ? std::string_view(baseColorTexture->name) : std::string_view{};
                const std::string_view baseColorTexturePath =
                    baseColorTexture != nullptr ? std::string_view(baseColorTexture->sourcePath) : std::string_view{};
                if (MATERIAL_POLICY::HasThinTransparentCue(material.name) ||
                    MATERIAL_POLICY::HasThinTransparentCue(baseColorTextureName) ||
                    MATERIAL_POLICY::HasThinTransparentCue(baseColorTexturePath)) {
                    material.featureBits |= MATERIAL_FEATURES::ThinTransparentSurface;
                }

                asset.materials.push_back(std::move(material));
            }

            if (asset.materials.empty()) {
                MaterialAsset fallback{};
                fallback.name = "DefaultAssimpMaterial";
                asset.materials.push_back(std::move(fallback));
            }
        }


} // namespace HIKARI::ASSETS::MODELS::ASSIMP
