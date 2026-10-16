#include "Assets/Sprite/HIKARI_SpriteAssetSerialization.h"
#include "Core/IO/HIKARI_PathNormalization.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

#include <json.hpp>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Serialization/Json/HIKARI_JsonStrict.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Assets/Sprite/HIKARI_SpriteJsonInternal.h"
#include "Assets/Importers/Policy/HIKARI_TextureImportPolicy.h"

namespace HIKARI::SPRITE {
namespace {

constexpr uint32_t kMaximumPageEdge = 8192u;
constexpr uint32_t kMaximumPageCount = 64u;
constexpr uint32_t kMaximumFrameDurationMs = 600000u;
constexpr TEXT::EnumName<TextureAssetAlphaClass> kTextureAssetAlphaClassNames[] = {
    {TextureAssetAlphaClass::Opaque, "Opaque"},
    {TextureAssetAlphaClass::Cutout, "Cutout"},
    {TextureAssetAlphaClass::Translucent, "Translucent"},
};

bool IsSafePageArtifactPath(std::string_view value) {
    const std::filesystem::path path = TEXT::PathFromUtf8(value);
    return PATHS::IsSafeRelativePath(path) && TEXT::ToLowerAsciiCopy(TEXT::PathToUtf8(path.extension())) == ".htex" &&
           std::none_of(path.begin(), path.end(), [](const std::filesystem::path& part) { return part == "."; });
}

std::string MakePageTextureAssetId(std::string_view prefix, std::string_view spriteGuid, std::string_view pageId) {
    constexpr char kHex[] = "0123456789abcdef";
    std::string assetId(prefix);
    assetId.append(spriteGuid);
    assetId.push_back('/');
    assetId.reserve(assetId.size() + pageId.size() * 2u);
    for (const char value : pageId) {
        const auto byte = static_cast<unsigned char>(value);
        assetId.push_back(kHex[byte >> 4u]);
        assetId.push_back(kHex[byte & 0x0fu]);
    }
    return assetId;
}

} // namespace

bool ValidateSpriteRuntimeAsset(const SpriteAssetData& asset, std::string_view sourceGuid, std::string& outError,
                                bool requirePageArtifacts) {
    if (sourceGuid.empty() || !MATH::IsFinite(asset.pixelsPerUnit) || asset.pixelsPerUnit <= 0.0f) {
        return JSONSTRICT::Fail(outError, "Sprite source GUID and pixelsPerUnit must be valid");
    }
    if (asset.pages.empty() || asset.pages.size() > kMaximumPageCount) {
        return JSONSTRICT::Fail(outError, "pages must contain one to 64 entries");
    }
    if (asset.frames.empty()) {
        return JSONSTRICT::Fail(outError, "frames must not be empty");
    }

    std::unordered_set<std::string> pageIds{};
    std::unordered_set<std::string> textureIds{};
    std::unordered_set<std::string> artifactPaths{};
    for (size_t index = 0u; index < asset.pages.size(); ++index) {
        const SpritePage& page = asset.pages[index];
        const bool hasNormal = !page.normalTextureAssetId.empty() || !page.normalArtifactPath.empty();
        if (page.id.empty() || !pageIds.insert(page.id).second || page.width == 0u || page.height == 0u ||
            page.width > kMaximumPageEdge || page.height > kMaximumPageEdge || page.textureAssetId.empty() ||
            page.textureAssetId != MakeSpritePageTextureAssetId(sourceGuid, page.id) ||
            !textureIds.insert(page.textureAssetId).second ||
            (requirePageArtifacts && !TEXT::IsNamedEnum(kTextureAssetAlphaClassNames, page.colorAlphaClass)) ||
            (requirePageArtifacts &&
             (!IsSafePageArtifactPath(page.artifactPath) || !artifactPaths.insert(page.artifactPath).second)) ||
            (hasNormal && (page.normalTextureAssetId != MakeSpritePageNormalTextureAssetId(sourceGuid, page.id) ||
                           !textureIds.insert(page.normalTextureAssetId).second ||
                           (requirePageArtifacts && (!IsSafePageArtifactPath(page.normalArtifactPath) ||
                                                     !artifactPaths.insert(page.normalArtifactPath).second))))) {
            return JSONSTRICT::Fail(outError, "pages[" + std::to_string(index) + "] has invalid identity or settings");
        }
    }

    std::unordered_set<std::string> frameIds{};
    uint32_t commonSourceWidth = 0u;
    uint32_t commonSourceHeight = 0u;
    float commonPivotX = 0.0f;
    float commonPivotY = 0.0f;
    for (size_t index = 0u; index < asset.frames.size(); ++index) {
        const SpriteFrame& frame = asset.frames[index];
        if (frame.id.empty() || !frameIds.insert(frame.id).second || frame.pageIndex >= asset.pages.size() ||
            frame.width == 0u || frame.height == 0u || frame.sourceWidth == 0u || frame.sourceHeight == 0u ||
            !MATH::IsFinite(frame.pivotX) || !MATH::IsFinite(frame.pivotY) ||
            (frame.empty && (frame.width != 1u || frame.height != 1u))) {
            return JSONSTRICT::Fail(outError, "frames[" + std::to_string(index) + "] has invalid identity or size");
        }
        const SpritePage& page = asset.pages[frame.pageIndex];
        const uint32_t trimWidth = frame.rotated ? frame.height : frame.width;
        const uint32_t trimHeight = frame.rotated ? frame.width : frame.height;
        if (frame.width > page.width || frame.height > page.height || frame.x > page.width - frame.width ||
            frame.y > page.height - frame.height || trimWidth > frame.sourceWidth || trimHeight > frame.sourceHeight ||
            frame.trimX > frame.sourceWidth - trimWidth || frame.trimY > frame.sourceHeight - trimHeight) {
            return JSONSTRICT::Fail(outError, "frames[" + std::to_string(index) + "] exceeds its page or sourceSize");
        }
        if (index == 0u) {
            commonSourceWidth = frame.sourceWidth;
            commonSourceHeight = frame.sourceHeight;
            commonPivotX = frame.pivotX;
            commonPivotY = frame.pivotY;
        } else if (frame.sourceWidth != commonSourceWidth || frame.sourceHeight != commonSourceHeight ||
                   frame.pivotX != commonPivotX || frame.pivotY != commonPivotY) {
            return JSONSTRICT::Fail(outError, "all frames must share sourceSize and pivot");
        }
    }

    std::unordered_set<std::string> clipIds{};
    for (size_t clipIndex = 0u; clipIndex < asset.clips.size(); ++clipIndex) {
        const SpriteClip& clip = asset.clips[clipIndex];
        if (clip.id.empty() || !clipIds.insert(clip.id).second || clip.frames.empty()) {
            return JSONSTRICT::Fail(outError,
                                    "clips[" + std::to_string(clipIndex) + "] has invalid identity or frames");
        }
        for (size_t frameIndex = 0u; frameIndex < clip.frames.size(); ++frameIndex) {
            const SpriteClipFrame& occurrence = clip.frames[frameIndex];
            if (occurrence.frameIndex >= asset.frames.size() || occurrence.durationMs == 0u ||
                occurrence.durationMs > kMaximumFrameDurationMs) {
                return JSONSTRICT::Fail(outError, "clips[" + std::to_string(clipIndex) + "].frames[" +
                                                      std::to_string(frameIndex) + "] is invalid");
            }
        }
    }
    if (asset.clips.empty() || !clipIds.contains(asset.defaultClip)) {
        return JSONSTRICT::Fail(outError, "defaultClip must reference an existing clip");
    }
    outError.clear();
    return true;
}

std::string MakeSpritePageTextureAssetId(std::string_view spriteGuid, std::string_view pageId) {
    return MakePageTextureAssetId("hikari.sprite.page/", spriteGuid, pageId);
}

std::string MakeSpritePageNormalTextureAssetId(std::string_view spriteGuid, std::string_view pageId) {
    return MakePageTextureAssetId("hikari.sprite.normal-page/", spriteGuid, pageId);
}

bool WriteSpriteRuntimeAsset(const std::filesystem::path& path, std::string_view sourceGuid,
                             const SpriteAssetData& asset, std::string& outError) {
    if (!ValidateSpriteRuntimeAsset(asset, sourceGuid, outError)) {
        return false;
    }

    nlohmann::json pages = nlohmann::json::array();
    for (const SpritePage& page : asset.pages) {
        pages.push_back({{"id", page.id},
                         {"textureAssetId", page.textureAssetId},
                         {"artifactPath", page.artifactPath},
                         {"normalTextureAssetId", page.normalTextureAssetId},
                         {"normalArtifactPath", page.normalArtifactPath},
                         {"colorAlphaClass", TEXT::EnumToName(kTextureAssetAlphaClassNames, page.colorAlphaClass)},
                         {"size", {page.width, page.height}}});
    }
    nlohmann::json frames = nlohmann::json::array();
    for (const SpriteFrame& frame : asset.frames) {
        frames.push_back({{"id", frame.id},
                          {"pageIndex", frame.pageIndex},
                          {"rect", {frame.x, frame.y, frame.width, frame.height}},
                          {"sourceSize", {frame.sourceWidth, frame.sourceHeight}},
                          {"trimOffset", {frame.trimX, frame.trimY}},
                          {"pivot", {frame.pivotX, frame.pivotY}},
                          {"rotated", frame.rotated},
                          {"empty", frame.empty}});
    }
    nlohmann::json clips = nlohmann::json::array();
    for (const SpriteClip& clip : asset.clips) {
        nlohmann::json occurrences = nlohmann::json::array();
        for (const SpriteClipFrame& frame : clip.frames) {
            occurrences.push_back({{"frameIndex", frame.frameIndex}, {"durationMs", frame.durationMs}});
        }
        clips.push_back({{"id", clip.id}, {"loop", clip.loop}, {"frames", std::move(occurrences)}});
    }
    const nlohmann::json root{{"format", std::string(kRuntimeAssetFormat)},
                              {"schemaVersion", kRuntimeAssetVersion},
                              {"sourceGuid", std::string(sourceGuid)},
                              {"name", asset.name},
                              {"pixelsPerUnit", asset.pixelsPerUnit},
                              {"defaultClip", asset.defaultClip},
                              {"pages", std::move(pages)},
                              {"frames", std::move(frames)},
                              {"clips", std::move(clips)}};
    return SERIALIZATION::JSON::WriteJsonFile(path, root, &outError);
}

bool LoadSpriteAssetData(const std::filesystem::path& path, SpriteAssetData& outAsset, std::string& outError) {
    outError.clear();
    nlohmann::json root{};
    if (!SERIALIZATION::JSON::ReadJsonFile(path, root, &outError)) {
        return false;
    }
    uint32_t schemaVersion = 0u;
    if (!root.is_object() || !root.contains("format") || !root.at("format").is_string() ||
        root.at("format").get<std::string>() != kRuntimeAssetFormat || !root.contains("schemaVersion") ||
        !JSON_INTERNAL::ReadUint32Value(root.at("schemaVersion"), schemaVersion, outError, "sprite.schemaVersion") ||
        schemaVersion != kRuntimeAssetVersion) {
        return JSONSTRICT::Fail(outError, "Unsupported Sprite runtime artifact format or version");
    }
    std::string sourceGuid{};
    if (!JSON_INTERNAL::ReadRequiredString(root, "sourceGuid", sourceGuid, outError, "sprite") || sourceGuid.empty()) {
        return JSONSTRICT::Fail(outError, "Sprite runtime artifact sourceGuid is missing");
    }

    SpriteAssetData asset{};
    if (!JSONSTRICT::ReadString(root, "name", {}, asset.name, outError, "sprite") ||
        !JSONSTRICT::ReadFloat(root, "pixelsPerUnit", 64.0f, asset.pixelsPerUnit, outError, "sprite") ||
        !JSON_INTERNAL::ReadRequiredString(root, "defaultClip", asset.defaultClip, outError, "sprite")) {
        return false;
    }

    const auto pagesIt = root.find("pages");
    if (pagesIt == root.end() || !pagesIt->is_array()) {
        return JSONSTRICT::Fail(outError, "sprite.pages must be an array");
    }
    asset.pages.reserve(pagesIt->size());
    for (size_t index = 0u; index < pagesIt->size(); ++index) {
        const nlohmann::json& node = (*pagesIt)[index];
        const std::string pathPrefix = "sprite.pages[" + std::to_string(index) + "]";
        if (!node.is_object()) {
            return JSONSTRICT::Fail(outError, pathPrefix + " must be an object");
        }
        SpritePage page{};
        std::string pageColorAlphaClass{};
        if (!JSON_INTERNAL::ReadRequiredString(node, "id", page.id, outError, pathPrefix) ||
            !JSON_INTERNAL::ReadRequiredString(node, "textureAssetId", page.textureAssetId, outError, pathPrefix) ||
            !JSON_INTERNAL::ReadRequiredString(node, "artifactPath", page.artifactPath, outError, pathPrefix) ||
            !JSON_INTERNAL::ReadRequiredString(node, "normalTextureAssetId", page.normalTextureAssetId, outError,
                                               pathPrefix) ||
            !JSON_INTERNAL::ReadRequiredString(node, "normalArtifactPath", page.normalArtifactPath, outError,
                                               pathPrefix) ||
            !JSON_INTERNAL::ReadRequiredString(node, "colorAlphaClass", pageColorAlphaClass, outError, pathPrefix)) {
            return false;
        }
        if (!TEXT::TryParseEnum(kTextureAssetAlphaClassNames, pageColorAlphaClass, page.colorAlphaClass)) {
            return JSONSTRICT::Fail(outError, pathPrefix + ".colorAlphaClass is not a supported alpha class");
        }
        if (!node.contains("size") ||
            !JSON_INTERNAL::ReadUintPair(node.at("size"), page.width, page.height, outError, pathPrefix + ".size")) {
            return false;
        }
        asset.pages.push_back(std::move(page));
    }

    const auto framesIt = root.find("frames");
    if (framesIt == root.end() || !framesIt->is_array()) {
        return JSONSTRICT::Fail(outError, "sprite.frames must be an array");
    }
    asset.frames.reserve(framesIt->size());
    for (size_t index = 0u; index < framesIt->size(); ++index) {
        const nlohmann::json& node = (*framesIt)[index];
        const std::string pathPrefix = "sprite.frames[" + std::to_string(index) + "]";
        if (!node.is_object()) {
            return JSONSTRICT::Fail(outError, pathPrefix + " must be an object");
        }
        SpriteFrame frame{};
        if (!JSON_INTERNAL::ReadFrameGeometry(node, frame, true, outError, pathPrefix) || !node.contains("pageIndex") ||
            !JSON_INTERNAL::ReadUint32Value(node.at("pageIndex"), frame.pageIndex, outError,
                                            pathPrefix + ".pageIndex")) {
            return false;
        }
        asset.frames.push_back(std::move(frame));
    }

    const auto clipsIt = root.find("clips");
    if (clipsIt == root.end() || !clipsIt->is_array()) {
        return JSONSTRICT::Fail(outError, "sprite.clips must be an array");
    }
    asset.clips.reserve(clipsIt->size());
    for (size_t index = 0u; index < clipsIt->size(); ++index) {
        const nlohmann::json& node = (*clipsIt)[index];
        const std::string pathPrefix = "sprite.clips[" + std::to_string(index) + "]";
        if (!node.is_object()) {
            return JSONSTRICT::Fail(outError, pathPrefix + " must be an object");
        }
        SpriteClip clip{};
        if (!JSON_INTERNAL::ReadRequiredString(node, "id", clip.id, outError, pathPrefix) || !node.contains("loop") ||
            !JSONSTRICT::ReadBool(node, "loop", false, clip.loop, outError, pathPrefix)) {
            return false;
        }
        const auto occurrencesIt = node.find("frames");
        if (occurrencesIt == node.end() || !occurrencesIt->is_array()) {
            return JSONSTRICT::Fail(outError, pathPrefix + ".frames must be an array");
        }
        clip.frames.reserve(occurrencesIt->size());
        for (size_t frameIndex = 0u; frameIndex < occurrencesIt->size(); ++frameIndex) {
            const nlohmann::json& occurrenceNode = (*occurrencesIt)[frameIndex];
            const std::string occurrencePath = pathPrefix + ".frames[" + std::to_string(frameIndex) + "]";
            if (!occurrenceNode.is_object()) {
                return JSONSTRICT::Fail(outError, occurrencePath + " must be an object");
            }
            SpriteClipFrame occurrence{};
            if (!occurrenceNode.contains("frameIndex") ||
                !JSON_INTERNAL::ReadUint32Value(occurrenceNode.at("frameIndex"), occurrence.frameIndex, outError,
                                                occurrencePath + ".frameIndex") ||
                !occurrenceNode.contains("durationMs") ||
                !JSON_INTERNAL::ReadUint32Value(occurrenceNode.at("durationMs"), occurrence.durationMs, outError,
                                                occurrencePath + ".durationMs")) {
                return false;
            }
            clip.frames.push_back(occurrence);
        }
        asset.clips.push_back(std::move(clip));
    }

    if (!ValidateSpriteRuntimeAsset(asset, sourceGuid, outError)) {
        return false;
    }
    for (SpriteClip& clip : asset.clips) {
        double endTimeMs = 0.0;
        for (SpriteClipFrame& occurrence : clip.frames) {
            endTimeMs += static_cast<double>(occurrence.durationMs);
            if (!std::isfinite(endTimeMs)) {
                return JSONSTRICT::Fail(outError, "Sprite clip duration exceeds the supported numeric range");
            }
            occurrence.endTimeMs = endTimeMs;
        }
    }
    outAsset = std::move(asset);
    outError.clear();
    return true;
}

} // namespace HIKARI::SPRITE
