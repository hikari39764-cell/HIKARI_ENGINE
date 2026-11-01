#include "Assets/Importers/HIKARI_SpriteAssetImporter.h"
#include "Core/IO/HIKARI_PathNormalization.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <utility>

#include "RenderContracts/Texture/HIKARI_HtexFormat.h"
#include "Assets/Importers/Policy/HIKARI_TextureImportPolicy.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Assets/Sprite/HIKARI_SpriteAssetSerialization.h"
#include "Assets/Tasks/HIKARI_AssetTaskService.h"
#include "Core/IO/HIKARI_FileReplacementTransaction.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/Paths/HIKARI_ProjectPath.h"

namespace HIKARI {
namespace {

constexpr uint32_t kMaximumPageEdge = 8192u;

struct TemporaryOutputs {
    bool enabled = false;
    bool keep = false;
    std::vector<std::filesystem::path> paths;

    ~TemporaryOutputs() {
        if (!enabled || keep) {
            return;
        }
        std::error_code error{};
        for (const std::filesystem::path& path : paths) {
            std::filesystem::remove(path, error);
            error.clear();
        }
    }
};

std::filesystem::path GetPageArtifactPath(const AssetImportContext& context, size_t index) {
    std::ostringstream filename{};
    filename << "sprite_page_" << std::setw(3) << std::setfill('0') << index << ".htex";
    return context.importedDirectory / filename.str();
}

std::filesystem::path GetNormalPageArtifactPath(const AssetImportContext& context, size_t index) {
    std::ostringstream filename{};
    filename << "sprite_page_" << std::setw(3) << std::setfill('0') << index << "_normal.htex";
    return context.importedDirectory / filename.str();
}

bool ValidatePageArtifact(const std::filesystem::path& path, const SPRITE::SpritePage& page,
                          const std::vector<SPRITE::SpriteFrame>& frames, bool normalPage, std::string& outError) {
    HtexFileInfo info{};
    if (!InspectHtexFile(path, info, outError)) {
        return false;
    }
    const TextureAssetColorSpace expectedColorSpace =
        normalPage ? TextureAssetColorSpace::Linear : TextureAssetColorSpace::Srgb;
    const TextureUsage expectedUsage = normalPage ? TextureUsage::Normal : TextureUsage::UI;
    const DXGI_FORMAT expectedFormat = normalPage ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    if (info.width != page.width || info.height != page.height || info.depth != 1u || info.arraySize != 1u ||
        info.mipLevels != 1u || info.dimension != HtexTextureDimension::Texture2D ||
        info.colorSpace != expectedColorSpace || info.usage != expectedUsage ||
        info.compression != TextureCompression::None || info.format != expectedFormat) {
        outError = normalPage ? "Sprite normal page HTEX must preserve dimensions and use linear RGBA8 without "
                                "compression or mips: "
                              : "Sprite color page HTEX must preserve dimensions and use sRGB RGBA8 without "
                                "compression or mips: ";
        outError += TEXT::PathToUtf8(path);
        return false;
    }
    if (normalPage) {
        return true;
    }
    const bool hasEmptyFrame =
        std::any_of(frames.begin(), frames.end(), [](const SPRITE::SpriteFrame& frame) { return frame.empty; });
    if (!hasEmptyFrame) {
        return true;
    }
    HtexTexture texture{};
    if (!ReadHtexFile(path, texture, outError) || texture.subresources.size() != 1u ||
        texture.subresources.front().rowPitch < static_cast<uint64_t>(page.width) * 4u) {
        return false;
    }
    const HtexSubresource& pixels = texture.subresources.front();
    for (const SPRITE::SpriteFrame& frame : frames) {
        if (!frame.empty) {
            continue;
        }
        const uint64_t alphaOffset =
            static_cast<uint64_t>(frame.y) * pixels.rowPitch + static_cast<uint64_t>(frame.x) * 4u + 3u;
        if (alphaOffset >= pixels.data.size() || pixels.data[static_cast<size_t>(alphaOffset)] != 0u) {
            outError = "empty Sprite frames must reference a transparent 1x1 page tile";
            return false;
        }
    }
    return true;
}

bool IsSafeOldPageArtifact(const std::filesystem::path& projectRoot, const std::filesystem::path& importedDirectory,
                           const AssetArtifactDesc& artifact, std::filesystem::path& outPath) {
    const std::filesystem::path relative = TEXT::PathFromUtf8(artifact.path);
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
        return false;
    }
    for (const std::filesystem::path& part : relative) {
        if (part == "..") {
            return false;
        }
    }
    outPath = (projectRoot / relative).lexically_normal();
    return PATHS::IsContainedPath(importedDirectory, outPath);
}

} // namespace

SpriteAssetImporter::SpriteAssetImporter(std::unique_ptr<ITextureImportBackend> backend)
    : backend_(std::move(backend)) {}

ASSETS::SEMANTICS::AssetImporterKind SpriteAssetImporter::GetImporterKind() const noexcept {
    return ASSETS::SEMANTICS::AssetImporterKind::Sprite;
}

AssetMeta SpriteAssetImporter::CreateDefaultMeta(const std::filesystem::path& sourcePath, const AssetGuid& guid) const {
    return ASSETS::SEMANTICS::MakeBaseAssetMeta(sourcePath, guid, GetImporterKind());
}

AssetImportResult SpriteAssetImporter::Import(const AssetRecord& record, const AssetImportContext& context) {
    AssetImportResult result{};
    if (!backend_ || !backend_->IsAvailable()) {
        result.message = "[SpriteAssetImporter] DirectXTex backend is unavailable";
        return result;
    }
    if (!record.guid.IsValid() || record.type != AssetType::Sprite ||
        !ASSETS::SEMANTICS::IsAssetSourceForImporter(record.sourcePath, GetImporterKind())) {
        result.message = "[SpriteAssetImporter] source record has the wrong identity or extension";
        return result;
    }
    if (context.projectRoot.empty() || context.importedDirectory.empty()) {
        result.message = "[SpriteAssetImporter] project root and imported directory are required";
        return result;
    }

    const std::filesystem::path sourcePath = PROJECT_PATHS::ResolveProjectPath(context.projectRoot, record.sourcePath);
    SPRITE::SpriteSourceDocument sourceDocument{};
    if (!SPRITE::LoadSpriteSourceDocument(sourcePath, record.guid.value, TEXT::PathToUtf8(record.sourcePath.stem()),
                                          sourceDocument, result.message)) {
        result.message = "[SpriteAssetImporter] " + result.message;
        return result;
    }
    SPRITE::SpriteAssetData asset = std::move(sourceDocument.asset);

    const std::filesystem::path runtimeFinalPath = context.importedDirectory / "sprite.hspritec";
    std::vector<std::filesystem::path> pageFinalPaths{};
    pageFinalPaths.reserve(asset.pages.size() * 2u);
    for (size_t index = 0u; index < asset.pages.size(); ++index) {
        const std::filesystem::path pageFinalPath = GetPageArtifactPath(context, index);
        asset.pages[index].artifactPath = PROJECT_PATHS::MakeProjectRelativeString(context.projectRoot, pageFinalPath);
        if (asset.pages[index].artifactPath.empty()) {
            result.message = "[SpriteAssetImporter] page artifact escaped the project root";
            return result;
        }
        pageFinalPaths.push_back(pageFinalPath);
        if (!sourceDocument.normalPagePaths[index].empty()) {
            const std::filesystem::path normalFinalPath = GetNormalPageArtifactPath(context, index);
            asset.pages[index].normalArtifactPath =
                PROJECT_PATHS::MakeProjectRelativeString(context.projectRoot, normalFinalPath);
            if (asset.pages[index].normalTextureAssetId.empty() || asset.pages[index].normalArtifactPath.empty()) {
                result.message = "[SpriteAssetImporter] normal page artifact escaped the project root";
                return result;
            }
            pageFinalPaths.push_back(normalFinalPath);
        }
    }
    std::vector<std::filesystem::path> writePaths{};
    writePaths.reserve(pageFinalPaths.size() + 1u);
    TemporaryOutputs temporaryOutputs{};
    temporaryOutputs.enabled = !context.publication;
    const auto prepareOutput = [&](const std::filesystem::path& finalPath) {
        const std::filesystem::path writePath = context.publication
                                                    ? context.StageFile(finalPath)
                                                    : std::filesystem::path(finalPath.string() + ".importing");
        if (!context.publication) {
            temporaryOutputs.paths.push_back(writePath);
        }
        std::error_code error{};
        std::filesystem::remove(writePath, error);
        if (error) {
            return std::filesystem::path{};
        }
        return writePath;
    };

    TextureImportSettings pageSettings{};
    pageSettings.usage = TextureUsage::UI;
    pageSettings.dimension = TextureAssetDimension::Texture2D;
    pageSettings.colorSpace = TextureAssetColorSpace::Srgb;
    pageSettings.compression = TextureCompression::None;
    pageSettings.mipPolicy = TextureMipPolicy::None;
    pageSettings.forcePowerOfTwo = false;
    pageSettings.allowResize = false;
    pageSettings.maxSize = kMaximumPageEdge;

    std::unordered_set<std::string> dependencyPaths{};
    const auto addSourceDependency = [&](const std::filesystem::path& sourcePage) {
        const std::string dependencyPath = PROJECT_PATHS::MakeProjectRelativeString(context.projectRoot, sourcePage);
        if (dependencyPath.empty()) {
            return false;
        }
        if (dependencyPaths.emplace(TEXT::ToLowerAsciiCopy(dependencyPath)).second) {
            result.dependencies.push_back(AssetDependencyDesc{{}, dependencyPath, "SpriteSourcePage"});
        }
        return true;
    };
    for (size_t index = 0u; index < asset.pages.size(); ++index) {
        if (context.task != nullptr && context.task->IsCancellationRequested()) {
            result.message = "[SpriteAssetImporter] import canceled";
            return result;
        }
        std::vector<SPRITE::SpriteFrame> pageFrames{};
        for (const SPRITE::SpriteFrame& frame : asset.frames) {
            if (frame.pageIndex == index) {
                pageFrames.push_back(frame);
            }
        }

        struct PageChannel {
            const std::filesystem::path* sourcePath = nullptr;
            std::filesystem::path finalPath{};
            std::string_view artifactPath{};
            TextureImportSettings settings{};
            ASSETS::SEMANTICS::AssetArtifactKind artifactKind = ASSETS::SEMANTICS::AssetArtifactKind::SpritePageTexture;
            bool normal = false;
        };
        std::array<PageChannel, 2> channels{};
        size_t channelCount = 1u;
        channels[0] = {&sourceDocument.pagePaths[index],
                       GetPageArtifactPath(context, index),
                       asset.pages[index].artifactPath,
                       pageSettings,
                       ASSETS::SEMANTICS::AssetArtifactKind::SpritePageTexture,
                       false};
        if (!sourceDocument.normalPagePaths[index].empty()) {
            TextureImportSettings normalSettings = pageSettings;
            normalSettings.usage = TextureUsage::Normal;
            normalSettings.colorSpace = TextureAssetColorSpace::Linear;
            normalSettings.preserveNormalChannels = true;
            channels[channelCount++] = {&sourceDocument.normalPagePaths[index],
                                        GetNormalPageArtifactPath(context, index),
                                        asset.pages[index].normalArtifactPath,
                                        normalSettings,
                                        ASSETS::SEMANTICS::AssetArtifactKind::SpritePageNormalTexture,
                                        true};
        }
        for (size_t channelIndex = 0u; channelIndex < channelCount; ++channelIndex) {
            const PageChannel& channel = channels[channelIndex];
            TextureImportSettings inspectedSettings = channel.settings;
            std::string inspectError{};
            if (!backend_->Inspect(*channel.sourcePath, inspectedSettings, inspectError) ||
                inspectedSettings.dimension != TextureAssetDimension::Texture2D) {
                result.message = "[SpriteAssetImporter] page inspection failed: " +
                                 (inspectError.empty() ? TEXT::PathToUtf8(*channel.sourcePath) : inspectError);
                return result;
            }
            if (!channel.normal) {
                asset.pages[index].colorAlphaClass =
                    ASSETS::IMPORT_POLICY::ResolveTextureAssetAlphaClass(inspectedSettings);
            }
            const std::filesystem::path pageWritePath = prepareOutput(channel.finalPath);
            if (pageWritePath.empty()) {
                result.message = "[SpriteAssetImporter] failed to prepare page HTEX output";
                return result;
            }
            std::string conversionError{};
            if (!backend_->ConvertToHtexAndDds(*channel.sourcePath, pageWritePath, {}, channel.settings,
                                               conversionError, context.task)) {
                result.message = "[SpriteAssetImporter] page conversion failed: " +
                                 (conversionError.empty() ? TEXT::PathToUtf8(*channel.sourcePath) : conversionError);
                return result;
            }
            if (!ValidatePageArtifact(pageWritePath, asset.pages[index], pageFrames, channel.normal, result.message)) {
                result.message = "[SpriteAssetImporter] " + result.message;
                return result;
            }
            writePaths.push_back(pageWritePath);
            result.artifacts.push_back(
                ASSETS::SEMANTICS::MakeAssetArtifact(channel.artifactKind, std::string(channel.artifactPath)));
            if (!addSourceDependency(*channel.sourcePath)) {
                result.message = "[SpriteAssetImporter] page dependency escaped the project root";
                return result;
            }
        }
    }

    const std::filesystem::path runtimeWritePath = prepareOutput(runtimeFinalPath);
    if (runtimeWritePath.empty()) {
        result.message = "[SpriteAssetImporter] failed to prepare runtime artifact output";
        return result;
    }
    if (!SPRITE::WriteSpriteRuntimeAsset(runtimeWritePath, record.guid.value, asset, result.message)) {
        result.message = "[SpriteAssetImporter] " + result.message;
        return result;
    }
    writePaths.push_back(runtimeWritePath);

    if (!context.publication) {
        std::vector<IO::FileReplacementOperation> replacements{};
        replacements.reserve(writePaths.size());
        for (size_t index = 0u; index < writePaths.size(); ++index) {
            const std::filesystem::path finalPath =
                index < pageFinalPaths.size() ? pageFinalPaths[index] : runtimeFinalPath;
            replacements.push_back({.finalPath = finalPath, .stagedPath = writePaths[index]});
        }
        std::string commitError{};
        if (!IO::CommitFileReplacementTransaction(replacements, commitError)) {
            result.message = "[SpriteAssetImporter] could not publish Sprite artifacts: " + commitError;
            return result;
        }
        temporaryOutputs.keep = true;
        std::error_code error{};
        for (const AssetArtifactDesc& oldArtifact : record.artifactManifest.artifacts) {
            if (!ASSETS::SEMANTICS::MatchesAssetArtifact(oldArtifact,
                                                         ASSETS::SEMANTICS::AssetArtifactKind::SpritePageTexture) &&
                !ASSETS::SEMANTICS::MatchesAssetArtifact(
                    oldArtifact, ASSETS::SEMANTICS::AssetArtifactKind::SpritePageNormalTexture)) {
                continue;
            }
            std::filesystem::path oldPath{};
            if (IsSafeOldPageArtifact(context.projectRoot, context.importedDirectory, oldArtifact, oldPath) &&
                std::find(pageFinalPaths.begin(), pageFinalPaths.end(), oldPath) == pageFinalPaths.end()) {
                std::filesystem::remove(oldPath, error);
                error.clear();
            }
        }
    } else {
        for (const AssetArtifactDesc& oldArtifact : record.artifactManifest.artifacts) {
            if (!ASSETS::SEMANTICS::MatchesAssetArtifact(oldArtifact,
                                                         ASSETS::SEMANTICS::AssetArtifactKind::SpritePageTexture) &&
                !ASSETS::SEMANTICS::MatchesAssetArtifact(
                    oldArtifact, ASSETS::SEMANTICS::AssetArtifactKind::SpritePageNormalTexture)) {
                continue;
            }
            std::filesystem::path oldPath{};
            if (IsSafeOldPageArtifact(context.projectRoot, context.importedDirectory, oldArtifact, oldPath) &&
                std::find(pageFinalPaths.begin(), pageFinalPaths.end(), oldPath) == pageFinalPaths.end()) {
                context.publication->DeleteOnPublish(oldPath);
            }
        }
    }

    result.artifacts.push_back(ASSETS::SEMANTICS::MakeAssetArtifact(
        ASSETS::SEMANTICS::AssetArtifactKind::SpriteRuntime,
        PROJECT_PATHS::MakeProjectRelativeString(context.projectRoot, runtimeFinalPath)));
    result.success = true;
    result.message = "[SpriteAssetImporter] Imported " + std::to_string(asset.frames.size()) + " frame(s), " +
                     std::to_string(asset.clips.size()) + " clip(s), and " + std::to_string(asset.pages.size()) +
                     " page(s).";
    return result;
}

} // namespace HIKARI
