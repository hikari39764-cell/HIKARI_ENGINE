#include "Assets/Importers/HIKARI_ModelImportInternal.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Assets/Importers/HIKARI_TextureImportBackend_DirectXTex.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/IO/HIKARI_FileReplacementTransaction.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace HIKARI::MODEL_IMPORT_INTERNAL {
bool ReadTextureAlphaDiagnostics(const std::filesystem::path& projectRoot, const std::string& textureGuid,
                                 TextureCookDiagnostic& inOutDiagnostic) {
    if (textureGuid.empty()) {
        return false;
    }

    const std::filesystem::path reportPath = projectRoot / "Library" / "Imported" / textureGuid / "import_report.json";
    nlohmann::json report;
    if (!SERIALIZATION::JSON::ReadJsonFile(reportPath, report) || !report.is_object()) {
        return false;
    }

    if (!report.contains("diagnostics") || !report["diagnostics"].is_object()) {
        return false;
    }
    const nlohmann::json& diagnostics = report["diagnostics"];
    if (!diagnostics.contains("texture") || !diagnostics["texture"].is_object()) {
        return false;
    }
    const nlohmann::json& texture = diagnostics["texture"];
    if (!texture.contains("sourceHasMeaningfulAlpha") || !texture.contains("sourceHasTranslucentAlpha") ||
        !texture.contains("sourceHasCutoutAlpha") || !texture.contains("sourceAlphaTranslucentRatio") ||
        !texture.contains("sourceAlphaCutoutRatio")) {
        return false;
    }
    inOutDiagnostic.sourceHasMeaningfulAlpha = texture.value("sourceHasMeaningfulAlpha", false);
    inOutDiagnostic.sourceHasTranslucentAlpha = texture.value("sourceHasTranslucentAlpha", false);
    inOutDiagnostic.sourceHasCutoutAlpha = texture.value("sourceHasCutoutAlpha", false);
    inOutDiagnostic.sourceAlphaNonOpaqueRatio = texture.value("sourceAlphaNonOpaqueRatio", 0.0f);
    inOutDiagnostic.sourceAlphaTranslucentRatio = texture.value("sourceAlphaTranslucentRatio", 0.0f);
    inOutDiagnostic.sourceAlphaCutoutRatio = texture.value("sourceAlphaCutoutRatio", 0.0f);
    return true;
}

void ApplyTextureAlphaSettings(const TextureImportSettings& settings, TextureCookDiagnostic& inOutDiagnostic) {
    inOutDiagnostic.sourceHasMeaningfulAlpha = settings.sourceHasMeaningfulAlpha;
    inOutDiagnostic.sourceHasTranslucentAlpha = settings.sourceHasTranslucentAlpha;
    inOutDiagnostic.sourceHasCutoutAlpha = settings.sourceHasCutoutAlpha;
    inOutDiagnostic.sourceAlphaNonOpaqueRatio = settings.sourceAlphaNonOpaqueRatio;
    inOutDiagnostic.sourceAlphaTranslucentRatio = settings.sourceAlphaTranslucentRatio;
    inOutDiagnostic.sourceAlphaCutoutRatio = settings.sourceAlphaCutoutRatio;
}

bool IsCutoutDominantAlpha(const TextureCookDiagnostic& texture) {
    if (!texture.sourceHasCutoutAlpha) {
        return false;
    }

    const float cutoutRatio = texture.sourceAlphaCutoutRatio;
    const float translucentRatio = texture.sourceAlphaTranslucentRatio;
    if (cutoutRatio <= 0.0f && translucentRatio <= 0.0f) {
        return !texture.sourceHasTranslucentAlpha;
    }

    // BLEND と記録された抜き材質を、実際の alpha 分布に合わせて MASK へ寄せる。
    return cutoutRatio >= translucentRatio;
}

bool InspectSourceTextureAlpha(const std::filesystem::path& absoluteTexturePath,
                               TextureCookDiagnostic& inOutDiagnostic) {
    if (absoluteTexturePath.empty()) {
        return false;
    }

    TextureImportSettings settings{};
    settings.usage = TextureUsage::BaseColor;
    std::string message{};
    if (!InspectTextureAlphaWithDirectXTex(absoluteTexturePath, settings, message)) {
        return false;
    }

    ApplyTextureAlphaSettings(settings, inOutDiagnostic);
    return true;
}

bool ResolveTextureToHtex(const std::filesystem::path& projectRoot, const std::filesystem::path& sourceMetaRoot,
                          const std::filesystem::path& libraryRoot, TextureAsset3D& texture,
                          AssetDependencyDesc& outDependency, bool& outHasDependency,
                          TextureCookDiagnostic& inOutDiagnostic) {
    outHasDependency = false;
    if (texture.sourcePath.empty()) {
        return false;
    }

    const std::filesystem::path absoluteTexturePath =
        PROJECT_PATHS::ResolveProjectPath(projectRoot, texture.sourcePath);
    const std::filesystem::path relativeTexturePath =
        PROJECT_PATHS::MakeProjectRelativePath(projectRoot, absoluteTexturePath);
    const std::filesystem::path metaPath = ASSETS::SEMANTICS::MakeSourceMetaPath(sourceMetaRoot, relativeTexturePath);
    if (metaPath.empty()) {
        return false;
    }

    nlohmann::json metaJson;
    if (!SERIALIZATION::JSON::ReadJsonFile(metaPath, metaJson) || !metaJson.is_object()) {
        return false;
    }

    const std::string guid = metaJson.value("guid", "");
    if (guid.empty()) {
        return false;
    }
    outDependency.guid.value = guid;
    outDependency.path = relativeTexturePath.generic_string();
    outDependency.role = "Texture";
    outHasDependency = true;
    inOutDiagnostic.guid = guid;
    inOutDiagnostic.dependencyResolved = true;
    if (!ReadTextureAlphaDiagnostics(projectRoot, guid, inOutDiagnostic)) {
        // 古い texture report の場合でも、model cook は baseColor alpha を見落とさない。
        InspectSourceTextureAlpha(absoluteTexturePath, inOutDiagnostic);
    }
    nlohmann::json manifestJson;
    const std::filesystem::path artifactManifestPath = ASSETS::SEMANTICS::MakeArtifactManifestPath(libraryRoot, guid);
    if (artifactManifestPath.empty() || !SERIALIZATION::JSON::ReadJsonFile(artifactManifestPath, manifestJson) ||
        !manifestJson.is_object()) {
        return false;
    }

    if (!manifestJson.contains("artifacts") || !manifestJson["artifacts"].is_array()) {
        return false;
    }

    // テクスチャ meta の MainTexture が HTEX なら、モデル内参照を実行時向けに差し替える。
    for (const auto& artifact : manifestJson["artifacts"]) {
        if (!artifact.is_object()) {
            continue;
        }

        AssetArtifactDesc descriptor{};
        descriptor.role = artifact.value("role", "");
        descriptor.format = artifact.value("format", "");
        descriptor.path = artifact.value("path", "");
        if (ASSETS::SEMANTICS::MatchesAssetArtifact(descriptor, ASSETS::SEMANTICS::AssetArtifactKind::MainTexture) &&
            !descriptor.path.empty()) {
            texture.sourcePath = descriptor.path;
            return true;
        }
    }
    return false;
}

bool HasMaterialThinSurfaceCookHint(const MaterialAsset& material) {
    return MATERIAL_POLICY::HasThinTransparentSurfaceHint(material) ||
           SURFACE_POLICY::HasThinSurfaceCue(material.name) ||
           SURFACE_POLICY::HasThinSurfaceCue(material.shaderProfileId) ||
           SURFACE_POLICY::HasThinSurfaceCue(material.defaultMaterialFxProfileId);
}

bool HasMaterialOpticalTransparencyCookHint(const MaterialAsset& material) {
    return MATERIAL_POLICY::HasOpticalTransparencyCue(material.name) ||
           MATERIAL_POLICY::HasOpticalTransparencyCue(material.shaderProfileId) ||
           MATERIAL_POLICY::HasOpticalTransparencyCue(material.defaultMaterialFxProfileId);
}

std::vector<bool> BuildMaterialOpticalTransparencyUsageHints(const ModelAsset& model) {
    std::vector<bool> hints(model.materials.size(), false);
    for (const ModelNode& node : model.nodes) {
        if (node.meshIndex < 0 || static_cast<size_t>(node.meshIndex) >= model.meshes.size()) {
            continue;
        }

        const MeshAsset& mesh = model.meshes[static_cast<size_t>(node.meshIndex)];
        const bool nodeHasOpticalCue = MATERIAL_POLICY::HasOpticalTransparencyCue(node.name);
        const bool meshHasOpticalCue = MATERIAL_POLICY::HasOpticalTransparencyCue(mesh.name);
        for (const MeshPrimitive& primitive : mesh.primitives) {
            if (primitive.materialIndex >= hints.size()) {
                continue;
            }
            const bool primitiveHasOpticalCue = MATERIAL_POLICY::HasOpticalTransparencyCue(primitive.name);
            if (nodeHasOpticalCue || meshHasOpticalCue || primitiveHasOpticalCue) {
                hints[primitive.materialIndex] = true;
            }
        }
    }
    return hints;
}

void SetMaterialAlphaMode(MaterialAsset& material, AlphaMode mode) {
    const bool sourceDoubleSided = material.doubleSided;

    material.alphaMode = mode;
    material.featureBits &= ~MATERIAL_FEATURES::AlphaMask;
    material.featureBits &= ~MATERIAL_FEATURES::ThinTransparentSurface;

    if (mode == AlphaMode::Mask) {
        material.featureBits |= MATERIAL_FEATURES::AlphaMask;
        material.doubleSided = true;
        return;
    }

    if (mode == AlphaMode::Blend) {
        material.featureBits |= MATERIAL_FEATURES::ThinTransparentSurface;
        material.doubleSided = true;
        return;
    }

    material.doubleSided = sourceDoubleSided;
}

MaterialAlphaPolicyStats ApplyCanonicalBaseColorAlphaMaterialPolicy(
    ModelAsset& model, const std::vector<TextureCookDiagnostic>& textureDiagnostics) {
    MaterialAlphaPolicyStats stats{};
    const std::vector<bool> opticalUsageHints = BuildMaterialOpticalTransparencyUsageHints(model);

    for (size_t materialIndex = 0; materialIndex < model.materials.size(); ++materialIndex) {
        MaterialAsset& material = model.materials[materialIndex];
        const AlphaMode originalMode = material.alphaMode;
        const bool originalDoubleSided = material.doubleSided;

        const int textureIndex = material.baseColorTexture.textureIndex;
        const TextureCookDiagnostic* texture = nullptr;
        if (textureIndex >= 0 && static_cast<size_t>(textureIndex) < textureDiagnostics.size()) {
            texture = &textureDiagnostics[static_cast<size_t>(textureIndex)];
        }

        const bool factorAlpha = material.baseColorFactor.w < 0.999f;
        const bool textureAlpha = texture != nullptr && texture->sourceHasMeaningfulAlpha;
        const bool textureCutout = textureAlpha && IsCutoutDominantAlpha(*texture);
        const bool textureTranslucent = textureAlpha && texture->sourceHasTranslucentAlpha && !textureCutout;
        const bool explicitThinCue = HasMaterialThinSurfaceCookHint(material);
        const bool opticalTransparencyCue =
            HasMaterialOpticalTransparencyCookHint(material) || opticalUsageHints[materialIndex];
        const bool sourceRequestedMask = originalMode == AlphaMode::Mask;
        const bool sourceRequestedBlend = originalMode == AlphaMode::Blend;
        const bool needsOpticalOpacityFallback =
            sourceRequestedBlend && opticalTransparencyCue && !factorAlpha && !textureAlpha;

        const bool cutoutSurface = textureCutout && (sourceRequestedMask || sourceRequestedBlend || explicitThinCue);
        const bool translucentSurface = factorAlpha ||
                                        (textureTranslucent && (sourceRequestedBlend || explicitThinCue)) ||
                                        (sourceRequestedBlend && opticalTransparencyCue);

        AlphaMode resolvedMode = AlphaMode::Opaque;
        if (cutoutSurface && !factorAlpha) {
            resolvedMode = AlphaMode::Mask;
        } else if (translucentSurface) {
            resolvedMode = AlphaMode::Blend;
        }

        SetMaterialAlphaMode(material, resolvedMode);
        if (resolvedMode == AlphaMode::Blend && needsOpticalOpacityFallback) {
            material.baseColorFactor.w = MATERIAL_POLICY::kOpticalTransparencyFallbackOpacity;
            ++stats.opticalOpacityFallbackCount;
        }

        if (resolvedMode == AlphaMode::Opaque) {
            ++stats.opaqueCount;
            if (originalMode != AlphaMode::Opaque) {
                ++stats.normalizedToOpaqueCount;
            }
            if (originalDoubleSided && material.doubleSided) {
                ++stats.opaqueDoubleSidedPreservedCount;
            }
        } else if (resolvedMode == AlphaMode::Mask) {
            ++stats.maskCount;
            if (originalMode != AlphaMode::Mask) {
                ++stats.normalizedToMaskCount;
            }
        } else {
            ++stats.blendCount;
            if (originalMode != AlphaMode::Blend) {
                ++stats.normalizedToBlendCount;
            }
        }
    }

    return stats;
}

bool NearlyEqual(float lhs, float rhs) { return std::abs(lhs - rhs) <= 1.0e-5f; }

bool HaveMatchingUvSemantics(const TextureSlot& lhs, const TextureSlot& rhs) {
    return lhs.texCoord == rhs.texCoord && NearlyEqual(lhs.uvScale.x, rhs.uvScale.x) &&
           NearlyEqual(lhs.uvScale.y, rhs.uvScale.y) && NearlyEqual(lhs.uvOffset.x, rhs.uvOffset.x) &&
           NearlyEqual(lhs.uvOffset.y, rhs.uvOffset.y) && NearlyEqual(lhs.uvRotation, rhs.uvRotation);
}

const TextureAsset3D* FindTexture(const ModelAsset& model, const TextureSlot& slot) {
    if (slot.textureIndex < 0 || static_cast<size_t>(slot.textureIndex) >= model.textures.size()) {
        return nullptr;
    }
    return &model.textures[static_cast<size_t>(slot.textureIndex)];
}

std::filesystem::path ResolveTextureSourcePath(const ModelAsset& model, const TextureSlot& slot,
                                               const std::filesystem::path& projectRoot) {
    const TextureAsset3D* texture = FindTexture(model, slot);
    if (texture == nullptr || texture->sourcePath.empty()) {
        return {};
    }
    return PROJECT_PATHS::ResolveProjectPath(projectRoot, texture->sourcePath);
}

void BuildCanonicalMetallicRoughnessTextures(ModelAsset& model, const AssetImportContext& context,
                                             std::vector<AssetArtifactDesc>& artifacts) {
    const size_t materialCount = (std::min)(model.materials.size(), model.materialImportSources.size());
    for (size_t materialIndex = 0u; materialIndex < materialCount; ++materialIndex) {
        MaterialAsset& material = model.materials[materialIndex];
        if (material.metallicRoughnessTexture.textureIndex >= 0) {
            continue;
        }

        const MaterialImportSourceSlots& sources = model.materialImportSources[materialIndex];
        const TextureSlot* canonicalSlot = nullptr;
        const TextureSlot* sourceSlots[] = {
            &sources.metalnessTexture,
            &sources.roughnessTexture,
            &sources.glossinessTexture,
        };
        bool incompatibleUvSemantics = false;
        for (const TextureSlot* slot : sourceSlots) {
            if (slot->textureIndex < 0) {
                continue;
            }
            if (canonicalSlot == nullptr) {
                canonicalSlot = slot;
            } else if (!HaveMatchingUvSemantics(*canonicalSlot, *slot)) {
                incompatibleUvSemantics = true;
                break;
            }
        }
        if (canonicalSlot == nullptr) {
            continue;
        }
        if (incompatibleUvSemantics) {
            ++model.importDiagnostics.unsupportedFeatureCount;
            model.importDiagnostics.messages.push_back(
                "[MaterialImport] separate metallic and roughness maps use different UV semantics; canonical packing was skipped. material=" +
                material.name);
            continue;
        }

        MetallicRoughnessPackRequest request{};
        request.metalnessSource = ResolveTextureSourcePath(model, sources.metalnessTexture, context.projectRoot);
        request.roughnessSource = ResolveTextureSourcePath(model, sources.roughnessTexture, context.projectRoot);
        if (request.roughnessSource.empty()) {
            request.glossinessSource = ResolveTextureSourcePath(model, sources.glossinessTexture, context.projectRoot);
        } else if (sources.glossinessTexture.textureIndex >= 0) {
            model.importDiagnostics.messages.push_back(
                "[MaterialImport] roughness map takes precedence over glossiness map. material=" + material.name);
        }

        const std::filesystem::path finalPath =
            context.importedDirectory / "MaterialTextures" /
            ("material_" + std::to_string(materialIndex) + "_metallic_roughness.dds");
        const std::filesystem::path stagedPath =
            context.publication ? context.StageFile(finalPath)
                                : finalPath.parent_path() / (finalPath.stem().string() + ".importing.dds");
        std::error_code removeEc{};
        std::filesystem::remove(stagedPath, removeEc);

        std::string packMessage{};
        if (!WritePackedMetallicRoughnessDdsWithDirectXTex(request, stagedPath, packMessage)) {
            if (context.publication)
                context.publication->DiscardFile(finalPath);
            ++model.importDiagnostics.unsupportedFeatureCount;
            model.importDiagnostics.messages.push_back(
                "[MaterialImport] failed to pack metallic-roughness texture. material=" + material.name +
                " reason=" + packMessage);
            continue;
        }

        std::string commitMessage{};
        if (!context.publication && !IO::CommitStagedFile(stagedPath, finalPath, commitMessage)) {
            std::error_code cleanupEc{};
            std::filesystem::remove(stagedPath, cleanupEc);
            ++model.importDiagnostics.unsupportedFeatureCount;
            model.importDiagnostics.messages.push_back(
                "[MaterialImport] failed to install packed metallic-roughness texture. material=" + material.name +
                " reason=" + commitMessage);
            continue;
        }

        TextureAsset3D packedTexture{};
        packedTexture.name = material.name + "_MetallicRoughness";
        packedTexture.sourcePath = PROJECT_PATHS::MakeProjectRelativeString(context.projectRoot, finalPath);
        material.metallicRoughnessTexture = *canonicalSlot;
        material.metallicRoughnessTexture.textureIndex = static_cast<int>(model.textures.size());
        model.textures.push_back(std::move(packedTexture));

        artifacts.push_back(ASSETS::SEMANTICS::MakeAssetArtifact(
            ASSETS::SEMANTICS::AssetArtifactKind::ModelMaterialTexture,
            PROJECT_PATHS::MakeProjectRelativeString(context.projectRoot, finalPath)));
        model.importDiagnostics.messages.push_back(
            "[MaterialImport] packed canonical metallic-roughness texture. material=" + material.name);
    }
}

void ApplyCanonicalSpecularPolicy(ModelAsset& model) {
    for (MaterialAsset& material : model.materials) {
        material.featureBits &= ~MATERIAL_FEATURES::SpecularGlossCompatibility;
        // 旧生成物の応答補正は再インポートで除去する。テクスチャの有無から
        // 材質ワークフローを推測して粗さや反射率を上書きしない。
        material.roughnessFactor = std::clamp(MATH::FiniteOr(material.roughnessFactor, 1.0f), 0.04f, 1.0f);
        material.metallicFactor = std::clamp(MATH::FiniteOr(material.metallicFactor, 0.0f), 0.0f, 1.0f);
        material.specularFactor = std::clamp(MATH::FiniteOr(material.specularFactor, 1.0f), 0.0f, 1.0f);
        // KHR_materials_specular の色係数には上限 1 がない。
        // ソース値を保持し、最終反射率の上限は共有 PBR 評価で適用する。
        for (float* channel :
             {&material.specularColorFactor.x, &material.specularColorFactor.y, &material.specularColorFactor.z}) {
            *channel = (std::max)(0.0f, MATH::FiniteOr(*channel, 1.0f));
        }
    }
}
} // namespace HIKARI::MODEL_IMPORT_INTERNAL
