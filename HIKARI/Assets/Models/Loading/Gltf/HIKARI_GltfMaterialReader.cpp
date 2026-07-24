#include "Assets/Models/Loading/Gltf/HIKARI_GltfMaterialReader.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Assets/Models/HIKARI_ModelAssetLookup.h"
#include "Assets/Models/Loading/HIKARI_ModelSourcePath.h"

namespace HIKARI::ASSETS::MODELS::GLTF {

    namespace {
        using nlohmann::json;

        float ClampGltfUnitFactor(float value) {
            return std::clamp(value, 0.0f, 1.0f);
        }

        MATH::Vec3 ClampGltfUnitColor(const MATH::Vec3& value) {
            return {
                ClampGltfUnitFactor(value.x),
                ClampGltfUnitFactor(value.y),
                ClampGltfUnitFactor(value.z)
            };
        }

    } // namespace

    void ReadMaterials(
        const std::filesystem::path& gltfPath,
        const nlohmann::json& root,
        ModelAsset& asset) {

        if (root.contains("images") && root["images"].is_array()) {
            for (const auto& img : root["images"]) {
                TextureAsset3D tex{};
                tex.name = img.value("name", "");
                const std::string uri = img.value("uri", "");
                if (uri.rfind("data:", 0) == 0 || img.contains("bufferView")) {
                    ++asset.importDiagnostics.unsupportedFeatureCount;
                    asset.importDiagnostics.messages.push_back("[glTF] embedded image requires HTEX source extraction: " + tex.name);
                } else if (!uri.empty()) {
                    tex.sourcePath = NormalizeModelSourcePath(gltfPath.parent_path() / uri);
                }
                asset.textures.push_back(std::move(tex));
            }
        }

        if (root.contains("materials") && root["materials"].is_array()) {
            auto readTextureSlot = [&](const json& textureInfo, TextureSlot& slot) {
                const int textureIndex = textureInfo.value("index", -1);
                if (textureIndex >= 0 && root.contains("textures") && root["textures"].is_array() &&
                    textureIndex < static_cast<int>(root["textures"].size())) {
                    const int imageIndex = root["textures"][static_cast<size_t>(textureIndex)].value("source", -1);
                    slot.textureIndex = imageIndex;
                }
                slot.texCoord = std::clamp(textureInfo.value("texCoord", 0), 0, 1);
                if (textureInfo.contains("extensions") && textureInfo["extensions"].is_object() &&
                    textureInfo["extensions"].contains("KHR_texture_transform") &&
                    textureInfo["extensions"]["KHR_texture_transform"].is_object()) {
                    const json& transform = textureInfo["extensions"]["KHR_texture_transform"];
                    if (transform.contains("scale") && transform["scale"].is_array() && transform["scale"].size() >= 2) {
                        slot.uvScale = {
                            transform["scale"][0].get<float>(),
                            transform["scale"][1].get<float>()
                        };
                    }
                    if (transform.contains("offset") && transform["offset"].is_array() && transform["offset"].size() >= 2) {
                        slot.uvOffset = {
                            transform["offset"][0].get<float>(),
                            transform["offset"][1].get<float>()
                        };
                    }
                    slot.uvRotation = transform.value("rotation", slot.uvRotation);
                    if (transform.contains("texCoord") && transform["texCoord"].is_number_integer()) {
                        slot.texCoord = std::clamp(transform["texCoord"].get<int>(), 0, 1);
                    }
                }
            };

            for (const auto& matNode : root["materials"]) {
                MaterialAsset mat{};
                mat.name = matNode.value("name", "");
                if (matNode.contains("pbrMetallicRoughness")) {
                    const json& pbr = matNode["pbrMetallicRoughness"];
                    if (pbr.contains("baseColorFactor") && pbr["baseColorFactor"].is_array() && pbr["baseColorFactor"].size() >= 4) {
                        mat.baseColorFactor = {
                            pbr["baseColorFactor"][0].get<float>(),
                            pbr["baseColorFactor"][1].get<float>(),
                            pbr["baseColorFactor"][2].get<float>(),
                            pbr["baseColorFactor"][3].get<float>()
                        };
                    }
                    if (pbr.contains("baseColorTexture") && pbr["baseColorTexture"].is_object()) {
                        readTextureSlot(pbr["baseColorTexture"], mat.baseColorTexture);
                    }
                    mat.metallicFactor = pbr.value("metallicFactor", mat.metallicFactor);
                    mat.roughnessFactor = pbr.value("roughnessFactor", mat.roughnessFactor);
                    if (pbr.contains("metallicRoughnessTexture") && pbr["metallicRoughnessTexture"].is_object()) {
                        readTextureSlot(pbr["metallicRoughnessTexture"], mat.metallicRoughnessTexture);
                    }
                }
                if (matNode.contains("normalTexture") && matNode["normalTexture"].is_object()) {
                    readTextureSlot(matNode["normalTexture"], mat.normalTexture);
                    mat.normalTexture.scale = matNode["normalTexture"].value("scale", mat.normalTexture.scale);
                }
                if (matNode.contains("occlusionTexture") && matNode["occlusionTexture"].is_object()) {
                    readTextureSlot(matNode["occlusionTexture"], mat.occlusionTexture);
                    mat.occlusionTexture.strength = matNode["occlusionTexture"].value("strength", mat.occlusionTexture.strength);
                }
                if (matNode.contains("emissiveFactor") && matNode["emissiveFactor"].is_array() && matNode["emissiveFactor"].size() >= 3) {
                    mat.emissiveFactor = {
                        matNode["emissiveFactor"][0].get<float>(),
                        matNode["emissiveFactor"][1].get<float>(),
                        matNode["emissiveFactor"][2].get<float>()
                    };
                }
                if (matNode.contains("emissiveTexture") && matNode["emissiveTexture"].is_object()) {
                    readTextureSlot(matNode["emissiveTexture"], mat.emissiveTexture);
                }

                const std::string alphaMode = matNode.value("alphaMode", "OPAQUE");
                if (alphaMode == "MASK") {
                    mat.alphaMode = AlphaMode::Mask;
                    mat.featureBits |= MATERIAL_FEATURES::AlphaMask;
                } else if (alphaMode == "BLEND") {
                    mat.alphaMode = AlphaMode::Blend;
                } else {
                    mat.alphaMode = AlphaMode::Opaque;
                }
                mat.alphaCutoff = matNode.value("alphaCutoff", mat.alphaCutoff);
                const bool importedDoubleSided = matNode.value("doubleSided", false);
                const TextureAsset3D* baseColorTexture = FindModelTexture(asset, mat.baseColorTexture);
                const std::string_view baseColorTextureName =
                    baseColorTexture != nullptr ? std::string_view(baseColorTexture->name) : std::string_view{};
                const std::string_view baseColorTexturePath =
                    baseColorTexture != nullptr ? std::string_view(baseColorTexture->sourcePath) : std::string_view{};
                if (MATERIAL_POLICY::HasThinTransparentCue(mat.name) ||
                    MATERIAL_POLICY::HasThinTransparentCue(baseColorTextureName) ||
                    MATERIAL_POLICY::HasThinTransparentCue(baseColorTexturePath)) {
                    mat.featureBits |= MATERIAL_FEATURES::ThinTransparentSurface;
                }
                // Opaque 縺ｮ doubleSided 縺ｯ cluster 縺ｮ閭碁擇 cone culling 繧呈ｮｺ縺励ｄ縺吶＞縲・
                // MaterialAsset 縺ｫ縺ｯ蜈・・諢丞峙繧剃ｿ晄戟縺励∝ｮ溯｡梧凾縺ｮ譛牙柑蛹悶・ MATERIAL_POLICY 縺ｫ莉ｻ縺帙ｋ縲・
                mat.doubleSided = importedDoubleSided;

                if (matNode.contains("extensions") && matNode["extensions"].is_object()) {
                    const json& extensions = matNode["extensions"];
                    if (extensions.contains("KHR_materials_unlit")) {
                        mat.featureBits |= MATERIAL_FEATURES::Unlit;
                    }
                    if (extensions.contains("KHR_materials_emissive_strength") && extensions["KHR_materials_emissive_strength"].is_object()) {
                        mat.emissiveStrength = extensions["KHR_materials_emissive_strength"].value("emissiveStrength", mat.emissiveStrength);
                    }
                    if (extensions.contains("KHR_materials_specular") && extensions["KHR_materials_specular"].is_object()) {
                        const json& specular = extensions["KHR_materials_specular"];
                        mat.specularFactor = specular.value("specularFactor", mat.specularFactor);
                        if (specular.contains("specularTexture") && specular["specularTexture"].is_object()) {
                            readTextureSlot(specular["specularTexture"], mat.specularTexture);
                        }
                        if (specular.contains("specularColorFactor") &&
                            specular["specularColorFactor"].is_array() &&
                            specular["specularColorFactor"].size() >= 3) {
                            mat.specularColorFactor = {
                                specular["specularColorFactor"][0].get<float>(),
                                specular["specularColorFactor"][1].get<float>(),
                                specular["specularColorFactor"][2].get<float>()
                            };
                        }
                        if (specular.contains("specularColorTexture") && specular["specularColorTexture"].is_object()) {
                            readTextureSlot(specular["specularColorTexture"], mat.specularColorTexture);
                        }
                    }
                    mat.specularFactor = ClampGltfUnitFactor(mat.specularFactor);
                    mat.specularColorFactor = ClampGltfUnitColor(mat.specularColorFactor);
                    for (auto it = extensions.begin(); it != extensions.end(); ++it) {
                        if (it.key() == "KHR_materials_unlit" ||
                            it.key() == "KHR_materials_emissive_strength" ||
                            it.key() == "KHR_materials_specular") {
                            continue;
                        }
                        ++asset.importDiagnostics.unsupportedFeatureCount;
                        asset.importDiagnostics.unsupportedExtensions.push_back(it.key());
                        asset.importDiagnostics.messages.push_back("[glTF] unsupported material extension: " + it.key());
                    }
                }

                const bool hasEmissiveFactor =
                    std::abs(mat.emissiveFactor.x) > 1e-6f ||
                    std::abs(mat.emissiveFactor.y) > 1e-6f ||
                    std::abs(mat.emissiveFactor.z) > 1e-6f;
                if (hasEmissiveFactor || mat.emissiveTexture.textureIndex >= 0) {
                    mat.featureBits |= MATERIAL_FEATURES::Emissive;
                }
                asset.materials.push_back(std::move(mat));
            }
        }
        if (asset.materials.empty()) {
            asset.materials.push_back(MaterialAsset{});
        }
    }

} // namespace HIKARI::ASSETS::MODELS::GLTF
