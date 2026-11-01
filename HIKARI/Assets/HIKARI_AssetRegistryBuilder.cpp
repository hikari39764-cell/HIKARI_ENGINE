#include "HIKARI_AssetRegistryBuilder.h"

#include <filesystem>
#include <future>
#include <thread>
#include <algorithm>
#include <utility>
#include "Core/HIKARI_StartupTimer.h"
#include <memory>
#include <system_error>

#include <json.hpp>

#include "Assets/Formats/HIKARI_HmatFormat.h"
#include "RenderContracts/Texture/HIKARI_HtexFormat.h"
#include "Assets/Formats/HIKARI_HvfxcFormat.h"
#include "Assets/Importers/Policy/HIKARI_ModelImportPolicy.h"
#include "Assets/Importers/Policy/HIKARI_TextureImportPolicy.h"
#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"
#include "Assets/Material/Authoring/HIKARI_MaterialInstanceResolver.h"
#include "Assets/Material/HIKARI_MaterialBuiltInAssets.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Material/HIKARI_MaterialAssetLookup.h"
#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingAsset.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Shader/Internal/HIKARI_ShaderAssetRegistryAdmission.h"
#include "Assets/Sprite/HIKARI_SpriteAssetSerialization.h"
#include "Assets/Sprite/HIKARI_SpriteMaterialSerialization.h"
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI {

namespace {

void ResolveTextureDescriptorMetadata(const AssetDatabase& assetDatabase, const AssetRecord& record,
                                      TextureAssetDescriptor& descriptor) {
    const nlohmann::json diagnostics = nlohmann::json::parse(record.artifactManifest.diagnosticsJson, nullptr, false);
    if (diagnostics.is_object()) {
        const auto textureIt = diagnostics.find("texture");
        if (textureIt != diagnostics.end() && textureIt->is_object()) {
            const nlohmann::json& texture = *textureIt;
            const auto boolMember = [&texture](const char* key) noexcept -> const nlohmann::json* {
                const auto value = texture.find(key);
                return value != texture.end() && value->is_boolean() ? &(*value) : nullptr;
            };
            const nlohmann::json* hasAlphaValue = boolMember("sourceHasAlphaChannel");
            const nlohmann::json* meaningfulValue = boolMember("sourceHasMeaningfulAlpha");
            const nlohmann::json* translucentValue = boolMember("sourceHasTranslucentAlpha");
            const nlohmann::json* cutoutValue = boolMember("sourceHasCutoutAlpha");
            // Older artifacts may not contain the full alpha
            // diagnostic set. Keep those Unknown so Auto mode takes
            // the safe blended path instead of erasing transparency.
            if (hasAlphaValue != nullptr && meaningfulValue != nullptr) {
                const bool hasAlpha = hasAlphaValue->get<bool>();
                const bool meaningful = meaningfulValue->get<bool>();
                descriptor.alphaClass = ASSETS::IMPORT_POLICY::ResolveTextureAssetAlphaClass(
                    hasAlpha, meaningful, translucentValue != nullptr && translucentValue->get<bool>(),
                    cutoutValue != nullptr && cutoutValue->get<bool>());
            }
        }
    }
    if (descriptor.alphaClass == TextureAssetAlphaClass::Unknown && descriptor.usage == TextureUsage::Mask) {
        descriptor.alphaClass = TextureAssetAlphaClass::Cutout;
    }

    std::filesystem::path artifactPath = descriptor.sourcePath;
    if (!artifactPath.is_absolute()) {
        artifactPath = assetDatabase.GetProjectRoot() / artifactPath;
    }
    if (artifactPath.extension() != ".htex" && artifactPath.extension() != ".HTEX") {
        return;
    }
    HtexFileInfo info{};
    std::string message{};
    if (InspectHtexFile(artifactPath.lexically_normal(), info, message)) {
        descriptor.width = info.width;
        descriptor.height = info.height;
    }
}

bool ResolveSpriteArtifactPath(const std::filesystem::path& projectRoot, std::string_view artifact,
                               std::string_view expectedExtension, std::filesystem::path& outPath) {
    const std::filesystem::path relative = TEXT::PathFromUtf8(artifact);
    if (artifact.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory() ||
        TEXT::ToLowerAsciiCopy(TEXT::PathToUtf8(relative.extension())) != expectedExtension) {
        return false;
    }
    for (const std::filesystem::path& part : relative) {
        if (part == ".." || part == ".") {
            return false;
        }
    }
    const std::filesystem::path resolved = (projectRoot / relative).lexically_normal();
    std::error_code error{};
    if (!std::filesystem::is_regular_file(resolved, error) || error) {
        return false;
    }
    outPath = resolved;
    return true;
}

bool LoadMaterialSourceJson(const AssetDatabase& assetDatabase, const AssetRecord& record, MaterialAssetData& outData,
                            MaterialCompileResult& outCompileResult) {
    const std::filesystem::path sourcePath = assetDatabase.ResolveSourcePath(record);
    std::string loadError{};
    if (LoadMaterialAssetData(sourcePath, outData, loadError)) {
        outCompileResult = CompileMaterialAsset(outData, ASSETS::MATERIAL::MakeMaterialCompileOptions(assetDatabase));
        return true;
    }

    HIKARI_LOG_WARN("[AssetRegistryBuilder][Material] source JSON read failed: " + loadError);
    return false;
}

bool LoadRawMaterialDataForRegistry(const AssetDatabase& assetDatabase, const AssetRecord& record,
                                    MaterialAssetData& outData, MaterialCompileResult& outCompileResult) {
    // Cook 済み HMAT を優先し、無い場合だけ source JSON を読む。
    const std::string hmatArtifact =
        ASSETS::SEMANTICS::FindAssetArtifactPath(record, ASSETS::SEMANTICS::AssetArtifactKind::Material);
    if (!hmatArtifact.empty()) {
        const std::filesystem::path hmatPath = (assetDatabase.GetProjectRoot() / hmatArtifact).lexically_normal();

        std::string readMessage{};
        if (ReadHmatFile(hmatPath, outData, outCompileResult, readMessage)) {
            return true;
        }

        HIKARI_LOG_WARN("[AssetRegistryBuilder][Material][WARN] HMAT read failed: " + readMessage);
        return LoadMaterialSourceJson(assetDatabase, record, outData, outCompileResult);
    }

    // 旧 asset 互換のため JSON 読み込み経路は残す。
    HIKARI_LOG_WARN("[AssetRegistryBuilder][Material] HMAT artifact missing, fallback to source JSON: " +
                    record.sourcePath.generic_string());
    return LoadMaterialSourceJson(assetDatabase, record, outData, outCompileResult);
}

struct PreparedMaterialRead {
    MaterialAssetData source;
    MaterialCompileResult compile;
    bool loaded = false;
};

bool LoadMaterialDataForRegistry(const AssetDatabase& assetDatabase, const AssetRecord& record,
                                 MaterialAssetData& outData, MaterialCompileResult& outCompileResult,
                                 PreparedMaterialRead* prepared = nullptr) {
    MaterialAssetData source{};
    MaterialCompileResult sourceCompile{};
    if (prepared) {
        if (!prepared->loaded)
            return false;
        source = std::move(prepared->source);
        sourceCompile = std::move(prepared->compile);
    } else if (!LoadRawMaterialDataForRegistry(assetDatabase, record, source, sourceCompile)) {
        return false;
    }
    if (source.kind != MaterialAssetKind::Instance) {
        outData = std::move(source);
        outCompileResult = std::move(sourceCompile);
        return true;
    }

    const ASSETS::MATERIAL::MaterialInstanceResolveResult resolved = ASSETS::MATERIAL::ResolveMaterialInstance(
        source,
        [&assetDatabase](const AssetGuid& guid, MaterialAssetData& outParent, std::string& outError) {
            const AssetRecord* parent = assetDatabase.FindByGuid(guid);
            if (parent == nullptr || parent->type != AssetType::Material) {
                outError = "parent Material asset record was not found";
                return false;
            }
            MaterialCompileResult ignoredCompile{};
            if (!LoadRawMaterialDataForRegistry(assetDatabase, *parent, outParent, ignoredCompile)) {
                outError = "parent Material asset data could not be loaded";
                return false;
            }
            outError.clear();
            return true;
        },
        16u, ASSETS::MATERIAL::MakeMaterialCompileOptions(assetDatabase));
    if (!resolved.success) {
        HIKARI_LOG_WARN("[AssetRegistryBuilder][Material] Material Instance "
                        "composition failed: " +
                        record.guid.value);
        return false;
    }
    outData = resolved.resolvedMaterial;
    outCompileResult = resolved.compile;
    return true;
}

std::string FindSharedBrdfLutPath(const AssetDatabase& assetDatabase) {
    const std::filesystem::path relativePath = std::filesystem::path("Library") / "Generated" / "IBL" / "brdf_lut.dds";
    const std::filesystem::path absolutePath = (assetDatabase.GetProjectRoot() / relativePath).lexically_normal();

    std::error_code ec{};
    if (std::filesystem::exists(absolutePath, ec) && !ec) {
        return relativePath.generic_string();
    }

    return {};
}

bool AppendEngineContentTextureManifest(AssetRegistry& registry, const CONTENT::EngineContentContext& content,
                                        const char* relativeManifestPath, const char* domainLabel) {
    const std::filesystem::path manifestPath = content.Resolve(relativeManifestPath);
    nlohmann::json root{};
    std::string readError{};
    if (manifestPath.empty() || !SERIALIZATION::JSON::ReadJsonFile(manifestPath, root, &readError) ||
        !root.is_object() || root.value("schema", std::string{}) != "hikari.engine_content_assets" ||
        root.value("version", 0u) != 1u || !root.contains("assets") || !root["assets"].is_array()) {
        HIKARI_LOG_ERROR(std::string("[AssetRegistryBuilder][EngineContent] invalid ") + domainLabel +
                         " asset catalog: " + readError);
        return false;
    }

    bool ok = true;
    for (const nlohmann::json& asset : root["assets"]) {
        if (!asset.is_object() || asset.value("type", std::string{}) != "Texture") {
            HIKARI_LOG_ERROR(std::string("[AssetRegistryBuilder][EngineContent] "
                                         "unsupported ") +
                             domainLabel + " asset catalog entry");
            ok = false;
            continue;
        }

        const std::string id = asset.value("id", std::string{});
        const std::filesystem::path sourcePath = content.Resolve(asset.value("path", std::string{}));
        std::error_code ec{};
        if (id.empty() || sourcePath.empty() || !std::filesystem::is_regular_file(sourcePath, ec) || ec) {
            HIKARI_LOG_ERROR(std::string("[AssetRegistryBuilder][EngineContent] ") + domainLabel +
                             " texture is missing: " + id);
            ok = false;
            continue;
        }

        auto descriptor = std::make_unique<TextureAssetDescriptor>();
        descriptor->id.value = id;
        descriptor->type = AssetType::Texture;
        descriptor->sourcePath = sourcePath.generic_string();
        descriptor->version = root.value("version", 1u);
        descriptor->dimension = TextureAssetDimension::Texture2D;
        descriptor->colorSpace = ASSETS::IMPORT_POLICY::ParseTextureColorSpace(asset.value("colorSpace", std::string{}),
                                                                               TextureAssetColorSpace::Srgb);
        descriptor->usage =
            ASSETS::IMPORT_POLICY::ParseTextureUsage(asset.value("usage", std::string{}), TextureUsage::BaseColor);
        descriptor->compression = ASSETS::IMPORT_POLICY::ParseTextureCompression(
            asset.value("compression", std::string{}), TextureCompression::None);
        descriptor->mipPolicy = ASSETS::IMPORT_POLICY::ParseTextureMipPolicy(asset.value("mipPolicy", std::string{}),
                                                                             TextureMipPolicy::Preserve);
        HtexFileInfo textureInfo{};
        std::string textureError{};
        if (!InspectHtexFile(sourcePath, textureInfo, textureError) ||
            textureInfo.colorSpace != descriptor->colorSpace || textureInfo.usage != descriptor->usage ||
            textureInfo.compression != descriptor->compression) {
            HIKARI_LOG_ERROR("[AssetRegistryBuilder][EngineContent] invalid texture artifact: " + id + " " +
                             textureError);
            ok = false;
            continue;
        }
        descriptor->width = textureInfo.width;
        descriptor->height = textureInfo.height;
        ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
    }
    return ok;
}

bool AppendBuiltInMaterials(AssetRegistry& registry, const AssetDatabase& assetDatabase) {
    // プロシージャルグラフィックスの「デフォルト白」マテリアル。
    // ディスク資産を持たず、AssetGuid 文字列をそのまま ID にする。
    PbrMaterialAssetData white{};
    white.materialName = "Default White";
    white.baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
    white.metallicFactor = 0.0f;
    white.roughnessFactor = 0.95f;

    auto descriptor = std::make_unique<MaterialAssetDescriptor>();
    descriptor->id.value = std::string(ASSETS::MATERIAL::kBuiltInWhiteMaterialAssetId);
    descriptor->type = AssetType::Material;
    descriptor->sourcePath = descriptor->id.value;
    descriptor->version = 1u;
    descriptor->sourceData = MakeStandardPbrMaterialAssetData(white);
    descriptor->compileResult =
        CompileMaterialAsset(descriptor->sourceData, ASSETS::MATERIAL::MakeMaterialCompileOptions(assetDatabase));
    descriptor->data = ProjectPbrMaterialAssetData(descriptor->sourceData);
    bool ok = registry.RegisterDescriptor(std::move(descriptor));

    // チェッカー(デバッグ)用テクスチャ。
    // マテリアル ID とレジストリキーが衝突しないよう
    // テクスチャは専用 ID を使う。
    const std::string checkerTextureId = std::string(ASSETS::MATERIAL::kBuiltInCheckerMaterialAssetId) + "/texture";
    auto checkerTexture = std::make_unique<TextureAssetDescriptor>();
    checkerTexture->id.value = checkerTextureId;
    checkerTexture->type = AssetType::Texture;
    checkerTexture->sourcePath = std::string(ASSETS::MATERIAL::kBuiltInCheckerMaterialAssetId);
    checkerTexture->version = 1u;
    checkerTexture->dimension = TextureAssetDimension::Texture2D;
    checkerTexture->colorSpace = TextureAssetColorSpace::Srgb;
    checkerTexture->usage = TextureUsage::BaseColor;
    checkerTexture->compression = TextureCompression::None;
    checkerTexture->mipPolicy = TextureMipPolicy::Preserve;
    ok = registry.RegisterDescriptor(std::move(checkerTexture)) && ok;

    // チェッカー(デバッグ)マテリアル。
    PbrMaterialAssetData checker{};
    checker.materialName = "Checker (Debug)";
    checker.baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
    checker.baseColorTexture.useTexture = true;
    checker.baseColorTexture.textureAssetGuid.value = checkerTextureId;
    checker.metallicFactor = 0.0f;
    checker.roughnessFactor = 0.95f;

    auto checkerMaterial = std::make_unique<MaterialAssetDescriptor>();
    checkerMaterial->id.value = std::string(ASSETS::MATERIAL::kBuiltInCheckerMaterialAssetId);
    checkerMaterial->type = AssetType::Material;
    checkerMaterial->sourcePath = checkerMaterial->id.value;
    checkerMaterial->version = 1u;
    checkerMaterial->sourceData = MakeStandardPbrMaterialAssetData(checker);
    checkerMaterial->compileResult =
        CompileMaterialAsset(checkerMaterial->sourceData, ASSETS::MATERIAL::MakeMaterialCompileOptions(assetDatabase));
    checkerMaterial->data = ProjectPbrMaterialAssetData(checkerMaterial->sourceData);
    ok = registry.RegisterDescriptor(std::move(checkerMaterial)) && ok;
    return ok;
}

bool AppendBuiltInTerrainMaterials(AssetRegistry& registry, const AssetDatabase& assetDatabase) {
    PbrMaterialAssetData sand{};
    sand.materialName = "Terrain Surface (Stylized Layers)";
    sand.baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
    sand.baseColorTexture.useTexture = true;
    sand.baseColorTexture.textureAssetGuid.value = std::string(ASSETS::MATERIAL::kBuiltInTerrainLayerAlbedoTextureId);
    // Terrain shader が world-space tiling と atlas rect を所有する。
    // uv1 は coverage 専用のため、マテリアル側では変換しない。
    sand.baseColorTexture.uvScale = {1.0f, 1.0f};
    sand.normalTexture.useTexture = true;
    sand.normalTexture.textureAssetGuid.value = std::string(ASSETS::MATERIAL::kBuiltInTerrainLayerNormalTextureId);
    sand.normalTexture.uvScale = {1.0f, 1.0f};
    sand.normalScale = 1.0f;
    sand.metallicFactor = 0.0f;
    sand.roughnessFactor = 0.92f;
    sand.doubleSided = true;

    auto descriptor = std::make_unique<MaterialAssetDescriptor>();
    descriptor->id.value = std::string(ASSETS::MATERIAL::kBuiltInSandMaterialAssetId);
    descriptor->type = AssetType::Material;
    descriptor->sourcePath = descriptor->id.value;
    descriptor->version = 1u;
    descriptor->sourceData = MakeStandardPbrMaterialAssetData(sand);
    descriptor->compileResult =
        CompileMaterialAsset(descriptor->sourceData, ASSETS::MATERIAL::MakeMaterialCompileOptions(assetDatabase));
    descriptor->data = ProjectPbrMaterialAssetData(descriptor->sourceData);
    return registry.RegisterDescriptor(std::move(descriptor));
}

} // namespace

bool RegisterEngineContentTextures(const CONTENT::EngineContentContext& content, AssetRegistry& registry) {
    if (!content.IsOpen()) {
        return true;
    }

    bool ok = AppendEngineContentTextureManifest(registry, content, "Vfx/BuiltinAssets.json", "VFX");
    // 地形を使わない書き出しは Terrain と Sand の領域のファイルを運ばない（書き出しはフォルダだけ作ることがある）。
    // 領域の中にファイルが一つも無い時だけ省いたものとして扱い、ファイルがあって目録が壊れている時は従来どおり失敗にする。
    const auto hasFiles = [](const std::filesystem::path& root) {
        std::error_code ec{};
        if (root.empty() || !std::filesystem::is_directory(root, ec) || ec) {
            return false;
        }
        for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec)) {
                return true;
            }
        }
        return false;
    };
    for (const auto& [domain, manifest] : {std::pair{"Sand", "Sand/BuiltinAssets.json"}, std::pair{"Terrain", "Terrain/BuiltinAssets.json"}}) {
        if (!hasFiles(content.Resolve(domain))) {
            HIKARI_LOG_INFO(std::string("[AssetRegistryBuilder][EngineContent] ") + domain + " content is not packaged; skipped.");
            continue;
        }
        ok = AppendEngineContentTextureManifest(registry, content, manifest, domain) && ok;
    }
    ok = AppendEngineContentTextureManifest(registry, content, "Painterly/BuiltinAssets.json", "Painterly") && ok;
    return ok;
}

bool AssetRegistryBuilder::RebuildRegistry(const AssetDatabase& assetDatabase, AssetRegistry& liveRegistry) const {
    AssetRegistry registry{};
    bool ok = RegisterEngineContentTextures(CONTENT::GetEngineContentContext(), registry);
    ok = AppendBuiltInMaterials(registry, assetDatabase) && ok;
    ok = AppendBuiltInTerrainMaterials(registry, assetDatabase) && ok;

    const CONTENT::EngineContentContext& content = CONTENT::GetEngineContentContext();
    const ASSETS::SHADER::INTERNAL::ShaderAssetRegistryAdmissionContext shaderContext{
        assetDatabase.GetProjectRoot(), content.IsOpen() ? content.Resolve("Shaders") : std::filesystem::path{},
        assetDatabase.IsCookedContent() ? ASSETS::SHADER::INTERNAL::ShaderAssetRegistryAdmissionMode::Cooked
                                        : ASSETS::SHADER::INTERNAL::ShaderAssetRegistryAdmissionMode::Authoring};

    const auto records = assetDatabase.CollectAll();
    std::vector<PreparedMaterialRead> preparedMaterials;
    if (assetDatabase.IsCookedContent()) {
        CORE::StartupTimer materialTimer("Assets.read_cooked_materials");
        preparedMaterials.resize(records.size());
        // 不変の cooked snapshot だけを読む。全 worker の終了後、同じ順で
        // registry に公開する。Editor の import/authoring は並列化しない。
        const size_t workerCount = (std::min)(size_t(4), size_t((std::max)(1u, std::thread::hardware_concurrency())));
        std::vector<std::future<void>> workers;
        for (size_t worker = 0; worker < workerCount; ++worker) {
            workers.push_back(std::async(std::launch::async, [&, worker] {
                for (size_t index = worker; index < records.size(); index += workerCount) {
                    const auto* record = records[index];
                    if (!record || !record->guid.IsValid() || record->duplicateGuid ||
                        record->type != AssetType::Material)
                        continue;
                    auto& prepared = preparedMaterials[index];
                    prepared.loaded =
                        LoadRawMaterialDataForRegistry(assetDatabase, *record, prepared.source, prepared.compile);
                }
            }));
        }
        for (auto& worker : workers)
            worker.get();
    }
    for (size_t recordIndex = 0; recordIndex < records.size(); ++recordIndex) {
        const AssetRecord* record = records[recordIndex];
        if (!record || !record->guid.IsValid() || record->duplicateGuid) {
            continue;
        }

        if (record->type == AssetType::Texture) {
            auto descriptor = std::make_unique<TextureAssetDescriptor>();
            descriptor->id.value = record->guid.value;
            descriptor->type = AssetType::Texture;
            descriptor->sourcePath = ASSETS::SEMANTICS::FindAssetArtifactPath(
                *record, ASSETS::SEMANTICS::AssetArtifactKind::MainTexture, true);
            if (descriptor->sourcePath.empty()) {
                descriptor->sourcePath = record->sourcePath.generic_string();
            }
            descriptor->version = record->meta.importerVersion;

            const TextureImportSettings settings = ASSETS::IMPORT_POLICY::ResolveTextureImportSettings(
                record->sourcePath, record->meta.importSettingsJson);
            descriptor->dimension = settings.dimension;
            descriptor->colorSpace = settings.colorSpace;
            descriptor->usage = settings.usage;
            descriptor->compression = settings.compression;
            descriptor->mipPolicy = settings.mipPolicy;
            ResolveTextureDescriptorMetadata(assetDatabase, *record, *descriptor);
            ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
        } else if (record->type == AssetType::Sprite) {
            const std::string spriteArtifact =
                ASSETS::SEMANTICS::FindAssetArtifactPath(*record, ASSETS::SEMANTICS::AssetArtifactKind::SpriteRuntime);
            std::filesystem::path spritePath{};
            SPRITE::SpriteAssetData spriteData{};
            std::string loadError{};
            if (!record->lastImportSucceeded ||
                !ResolveSpriteArtifactPath(assetDatabase.GetProjectRoot(), spriteArtifact, ".hspritec", spritePath) ||
                !SPRITE::LoadSpriteAssetData(spritePath, spriteData, loadError)) {
                HIKARI_LOG_WARN("[AssetRegistryBuilder][Sprite] normalized Sprite artifact is unavailable: " +
                                record->guid.value + (loadError.empty() ? std::string{} : " (" + loadError + ")"));
                continue;
            }

            bool pagesValid = true;
            std::vector<std::unique_ptr<TextureAssetDescriptor>> pageDescriptors{};
            pageDescriptors.reserve(spriteData.pages.size());
            const auto appendPageTexture = [&](std::string_view assetId, std::string_view artifactPath,
                                               TextureAssetColorSpace colorSpace, TextureUsage usage,
                                               TextureAssetAlphaClass alphaClass, DXGI_FORMAT format,
                                               uint32_t expectedWidth, uint32_t expectedHeight) {
                std::filesystem::path pagePath{};
                HtexFileInfo info{};
                if (!ResolveSpriteArtifactPath(assetDatabase.GetProjectRoot(), artifactPath, ".htex", pagePath) ||
                    !InspectHtexFile(pagePath, info, loadError) || info.width != expectedWidth ||
                    info.height != expectedHeight || info.dimension != HtexTextureDimension::Texture2D ||
                    info.colorSpace != colorSpace || info.usage != usage ||
                    info.compression != TextureCompression::None || info.mipLevels != 1u || info.format != format) {
                    return false;
                }
                auto texture = std::make_unique<TextureAssetDescriptor>();
                texture->id.value = std::string(assetId);
                texture->type = AssetType::Texture;
                texture->sourcePath = std::string(artifactPath);
                texture->version = record->meta.importerVersion;
                texture->dimension = TextureAssetDimension::Texture2D;
                texture->colorSpace = colorSpace;
                texture->usage = usage;
                texture->alphaClass = alphaClass;
                texture->compression = TextureCompression::None;
                texture->mipPolicy = TextureMipPolicy::None;
                texture->width = info.width;
                texture->height = info.height;
                pageDescriptors.push_back(std::move(texture));
                return true;
            };
            for (const SPRITE::SpritePage& page : spriteData.pages) {
                if (page.textureAssetId != SPRITE::MakeSpritePageTextureAssetId(record->guid.value, page.id) ||
                    !appendPageTexture(page.textureAssetId, page.artifactPath, TextureAssetColorSpace::Srgb,
                                       TextureUsage::UI, page.colorAlphaClass, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                                       page.width, page.height)) {
                    pagesValid = false;
                    break;
                }
                const bool hasNormal = !page.normalTextureAssetId.empty() || !page.normalArtifactPath.empty();
                if (hasNormal && (page.normalTextureAssetId !=
                                      SPRITE::MakeSpritePageNormalTextureAssetId(record->guid.value, page.id) ||
                                  !appendPageTexture(page.normalTextureAssetId, page.normalArtifactPath,
                                                     TextureAssetColorSpace::Linear, TextureUsage::Normal,
                                                     TextureAssetAlphaClass::Unknown, DXGI_FORMAT_R8G8B8A8_UNORM,
                                                     page.width, page.height))) {
                    pagesValid = false;
                    break;
                }
            }
            if (!pagesValid) {
                HIKARI_LOG_WARN("[AssetRegistryBuilder][Sprite] page texture artifact is unavailable or mismatched: " +
                                record->guid.value + (loadError.empty() ? std::string{} : " (" + loadError + ")"));
                continue;
            }
            for (std::unique_ptr<TextureAssetDescriptor>& descriptor : pageDescriptors) {
                ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
            }
            auto descriptor = std::make_unique<AssetDescriptor>();
            descriptor->id.value = record->guid.value;
            descriptor->type = AssetType::Sprite;
            descriptor->sourcePath = spriteArtifact;
            descriptor->version = record->meta.importerVersion;
            ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
        } else if (record->type == AssetType::SpriteMaterial) {
            const std::string materialArtifact = ASSETS::SEMANTICS::FindAssetArtifactPath(
                *record, ASSETS::SEMANTICS::AssetArtifactKind::SpriteMaterialRuntime);
            std::filesystem::path materialPath{};
            SPRITE::SpriteMaterialAssetData materialData{};
            std::string loadError{};
            if (!record->lastImportSucceeded ||
                !ResolveSpriteArtifactPath(assetDatabase.GetProjectRoot(), materialArtifact, ".hspritematerialc",
                                           materialPath) ||
                !SPRITE::LoadSpriteMaterialAssetArtifact(materialPath, record->guid, materialData, loadError)) {
                HIKARI_LOG_WARN("[AssetRegistryBuilder][SpriteMaterial] normalized material artifact is unavailable: " +
                                record->guid.value + (loadError.empty() ? std::string{} : " (" + loadError + ")"));
                continue;
            }
            auto descriptor = std::make_unique<AssetDescriptor>();
            descriptor->id.value = record->guid.value;
            descriptor->type = AssetType::SpriteMaterial;
            descriptor->sourcePath = materialArtifact;
            descriptor->version = record->meta.importerVersion;
            ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
        } else if (record->type == AssetType::Model) {
            auto descriptor = std::make_unique<ModelAssetDescriptor>();
            descriptor->id.value = record->guid.value;
            descriptor->type = AssetType::Model;
            descriptor->sourcePath = ASSETS::SEMANTICS::FindAssetArtifactPath(
                *record, ASSETS::SEMANTICS::AssetArtifactKind::MainModel, true);
            if (descriptor->sourcePath.empty()) {
                descriptor->sourcePath = record->sourcePath.generic_string();
            }
            descriptor->clusteredGeometryPath = ASSETS::SEMANTICS::FindAssetArtifactPath(
                *record, ASSETS::SEMANTICS::AssetArtifactKind::ClusteredGeometry);
            descriptor->collisionGeometryPath = ASSETS::SEMANTICS::FindAssetArtifactPath(
                *record, ASSETS::SEMANTICS::AssetArtifactKind::CollisionGeometry);
            descriptor->version = record->meta.importerVersion;
            const ASSETS::IMPORT_POLICY::ModelImportPolicy policy =
                ASSETS::IMPORT_POLICY::ResolveModelImportPolicy(record->sourcePath, record->meta.importSettingsJson);
            descriptor->importer = policy.importer;
            ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
        } else if (record->type == AssetType::Sky) {
            auto descriptor = std::make_unique<SkyAssetDescriptor>();
            descriptor->id.value = record->guid.value;
            descriptor->type = AssetType::Sky;
            const std::string skyArtifact = ASSETS::SEMANTICS::FindAssetArtifactPath(
                *record, ASSETS::SEMANTICS::AssetArtifactKind::SkyCubemap, true);
            if (skyArtifact.empty()) {
                HIKARI_LOG_WARN("[AssetRegistryBuilder][Sky] missing SkyCubemap artifact, fallback to source: " +
                                record->sourcePath.generic_string());
                descriptor->sourcePath = record->sourcePath.generic_string();
            } else {
                descriptor->sourcePath = skyArtifact;
            }
            descriptor->version = record->meta.importerVersion;
            descriptor->textureAssetId.clear();
            descriptor->preferredMode = SkyMode::Cubemap;
            descriptor->irradiancePath = ASSETS::SEMANTICS::FindAssetArtifactPath(
                *record, ASSETS::SEMANTICS::AssetArtifactKind::IblIrradiance, true);
            descriptor->prefilteredPath = ASSETS::SEMANTICS::FindAssetArtifactPath(
                *record, ASSETS::SEMANTICS::AssetArtifactKind::IblPrefiltered, true);
            descriptor->brdfLutPath =
                ASSETS::SEMANTICS::FindAssetArtifactPath(*record, ASSETS::SEMANTICS::AssetArtifactKind::BrdfLut, true);
            if (descriptor->brdfLutPath.empty()) {
                descriptor->brdfLutPath = FindSharedBrdfLutPath(assetDatabase);
            }
            descriptor->hasIbl = !descriptor->irradiancePath.empty() || !descriptor->prefilteredPath.empty();

            nlohmann::json settings = nlohmann::json::parse(record->meta.importSettingsJson, nullptr, false);
            if (settings.is_object()) {
                descriptor->prefilteredMipCount = settings.value("prefilteredMipCount", 7u);
            }
            ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
        } else if (record->type == AssetType::Material) {
            auto descriptor = std::make_unique<MaterialAssetDescriptor>();
            descriptor->id.value = record->guid.value;
            descriptor->type = AssetType::Material;
            descriptor->sourcePath =
                ASSETS::SEMANTICS::FindAssetArtifactPath(*record, ASSETS::SEMANTICS::AssetArtifactKind::Material);
            if (descriptor->sourcePath.empty()) {
                descriptor->sourcePath = record->sourcePath.generic_string();
            }
            descriptor->version = record->meta.importerVersion;

            if (!LoadMaterialDataForRegistry(assetDatabase, *record, descriptor->sourceData, descriptor->compileResult,
                                             preparedMaterials.empty() ? nullptr : &preparedMaterials[recordIndex])) {
                PbrMaterialAssetData fallback{};
                fallback.materialName =
                    record->displayName.empty() ? record->sourcePath.stem().string() : record->displayName;
                descriptor->sourceData = MakeStandardPbrMaterialAssetData(fallback);
                descriptor->compileResult = CompileMaterialAsset(
                    descriptor->sourceData, ASSETS::MATERIAL::MakeMaterialCompileOptions(assetDatabase));
                HIKARI_LOG_WARN("[AssetRegistryBuilder][Material] using fallback material data: " +
                                fallback.materialName);
            }
            descriptor->data = ProjectPbrMaterialAssetData(descriptor->sourceData);
            ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
        } else if (record->type == AssetType::ModelMaterialBinding) {
            auto descriptor = std::make_unique<ModelMaterialBindingAssetDescriptor>();
            descriptor->id.value = record->guid.value;
            descriptor->type = AssetType::ModelMaterialBinding;
            descriptor->sourcePath = record->sourcePath.generic_string();
            descriptor->version = record->meta.importerVersion;
            const std::filesystem::path sourcePath = assetDatabase.ResolveSourcePath(*record);
            std::string loadError{};
            if (!ASSETS::MODELS::LoadModelMaterialBindingAsset(sourcePath, descriptor->data, loadError)) {
                HIKARI_LOG_WARN("[AssetRegistryBuilder][ModelMaterialBinding] "
                                "source read failed: " +
                                loadError);
                ok = false;
                continue;
            }
            ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
        } else if (record->type == AssetType::VfxEffect) {
            auto descriptor = std::make_unique<VfxAssetDescriptor>();
            descriptor->id.value = record->guid.value;
            descriptor->type = AssetType::VfxEffect;
            descriptor->sourcePath = ASSETS::SEMANTICS::FindAssetArtifactPath(
                *record, ASSETS::SEMANTICS::AssetArtifactKind::VfxProgram, true);
            if (descriptor->sourcePath.empty()) {
                HIKARI_LOG_WARN("[AssetRegistryBuilder][VFX] compiled HVFX artifact "
                                "is missing; runtime effect skipped until import succeeds: " +
                                record->guid.value);
                continue;
            }
            descriptor->version = record->meta.importerVersion;

            const std::filesystem::path programPath =
                (assetDatabase.GetProjectRoot() / descriptor->sourcePath).lexically_normal();
            VFX::VfxProgram program{};
            std::string readError{};
            if (!VFX::ReadHvfxcFile(programPath, program, readError)) {
                HIKARI_LOG_WARN("[AssetRegistryBuilder][VFX] compiled HVFX read failed; "
                                "runtime effect skipped until import succeeds: " +
                                record->guid.value + " (" + readError + ")");
                continue;
            }
            descriptor->program = std::make_shared<const VFX::VfxProgram>(std::move(program));
            ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
        } else if (record->type == AssetType::Shader) {
            ok = ASSETS::SHADER::INTERNAL::AppendProjectShaderAssetDescriptor(shaderContext, *record, registry) && ok;
        }
    }

    if (!ok) {
        return false;
    }
    liveRegistry.SwapContents(registry);
    return true;
}

} // namespace HIKARI
