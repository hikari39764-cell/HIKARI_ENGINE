#include "Assets/Sprite/HIKARI_SpriteAssetSerialization.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <json.hpp>

#include "Assets/Sprite/HIKARI_SpriteJsonInternal.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Serialization/Json/HIKARI_JsonStrict.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI::SPRITE {
namespace {

constexpr uint32_t kMaximumPageEdge = 8192u;
constexpr uint32_t kMaximumPageCount = 64u;
constexpr uint32_t kMaximumFrameDurationMs = 600000u;

bool IsContainedPath(const std::filesystem::path& root, const std::filesystem::path& path) {
    std::error_code error{};
    const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(root, error);
    if (error) {
        return false;
    }
    const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, error);
    if (error) {
        return false;
    }
    const std::string rootKey = TEXT::ToLowerAsciiCopy(TEXT::PathToUtf8(canonicalRoot));
    const std::string pathKey = TEXT::ToLowerAsciiCopy(TEXT::PathToUtf8(canonicalPath));
    return pathKey.size() > rootKey.size() && pathKey.compare(0u, rootKey.size(), rootKey) == 0 &&
           pathKey[rootKey.size()] == '/';
}

bool ResolvePagePath(const std::filesystem::path& sourcePath, std::string_view file, std::filesystem::path& outPath,
                     std::string& outError, std::string_view fieldPath) {
    const std::filesystem::path relative = TEXT::PathFromUtf8(file);
    if (file.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory() ||
        TEXT::ToLowerAsciiCopy(TEXT::PathToUtf8(relative.extension())) != ".png") {
        return JSONSTRICT::Fail(outError, std::string(fieldPath) + " must be a relative PNG path");
    }
    for (const std::filesystem::path& part : relative) {
        if (part == "..") {
            return JSONSTRICT::Fail(outError, std::string(fieldPath) + " cannot contain '..'");
        }
    }
    const std::filesystem::path candidate = (sourcePath.parent_path() / relative).lexically_normal();
    if (!IsContainedPath(sourcePath.parent_path(), candidate)) {
        return JSONSTRICT::Fail(outError, std::string(fieldPath) + " resolves outside the Sprite source directory");
    }
    std::error_code error{};
    if (!std::filesystem::is_regular_file(candidate, error) || error) {
        return JSONSTRICT::Fail(outError, std::string(fieldPath) + " does not point to an existing PNG file");
    }
    outPath = candidate;
    return true;
}

uint32_t ReadBigEndian32(const uint8_t* value) {
    return (static_cast<uint32_t>(value[0]) << 24u) | (static_cast<uint32_t>(value[1]) << 16u) |
           (static_cast<uint32_t>(value[2]) << 8u) | static_cast<uint32_t>(value[3]);
}

bool ReadPngRgba8Header(const std::filesystem::path& path, uint32_t& outWidth, uint32_t& outHeight,
                        std::string& outError) {
    constexpr std::array<uint8_t, 8> kSignature{0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au};
    std::array<uint8_t, 33> header{};
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open() ||
        !stream.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size())) ||
        !std::equal(kSignature.begin(), kSignature.end(), header.begin()) ||
        ReadBigEndian32(header.data() + 8u) != 13u || header[12u] != 'I' || header[13u] != 'H' || header[14u] != 'D' ||
        header[15u] != 'R' || header[24u] != 8u || header[25u] != 6u || header[26u] != 0u || header[27u] != 0u) {
        return JSONSTRICT::Fail(outError,
                                "page must be a valid non-interlaced PNG RGBA8 image: " + TEXT::PathToUtf8(path));
    }
    outWidth = ReadBigEndian32(header.data() + 16u);
    outHeight = ReadBigEndian32(header.data() + 20u);
    if (outWidth == 0u || outHeight == 0u || outWidth > kMaximumPageEdge || outHeight > kMaximumPageEdge) {
        return JSONSTRICT::Fail(outError,
                                "page dimensions must be between 1 and 8192 pixels: " + TEXT::PathToUtf8(path));
    }
    return true;
}

bool RejectEvents(const nlohmann::json& node, std::string_view path, std::string& outError) {
    if (node.is_object() && node.contains("events")) {
        return JSONSTRICT::Fail(outError, std::string(path) + ".events is not supported in Sprite schema v1");
    }
    return true;
}

} // namespace

bool LoadSpriteSourceDocument(const std::filesystem::path& path, std::string_view sourceGuid,
                              std::string_view fallbackName, SpriteSourceDocument& outDocument, std::string& outError) {
    outError.clear();
    nlohmann::json root{};
    if (!SERIALIZATION::JSON::ReadJsonFile(path, root, &outError)) {
        return false;
    }
    if (!root.is_object() || !root.contains("format") || !root["format"].is_string() ||
        root["format"].get<std::string>() != "hikari.sprite") {
        return JSONSTRICT::Fail(outError, "format must be 'hikari.sprite'");
    }
    uint32_t schemaVersion = 0u;
    if (!root.contains("schemaVersion") ||
        !JSON_INTERNAL::ReadUint32Value(root["schemaVersion"], schemaVersion, outError, "sprite.schemaVersion")) {
        return false;
    }
    if (schemaVersion != 1u) {
        return JSONSTRICT::Fail(outError, "Unsupported Sprite schemaVersion: " + std::to_string(schemaVersion));
    }
    if (!RejectEvents(root, "sprite", outError)) {
        return false;
    }

    SpriteSourceDocument document{};
    SpriteAssetData& asset = document.asset;
    if (!JSONSTRICT::ReadString(root, "name", std::string(fallbackName), asset.name, outError, "sprite") ||
        !JSONSTRICT::ReadFloat(root, "pixelsPerUnit", 64.0f, asset.pixelsPerUnit, outError, "sprite")) {
        return false;
    }
    if (!MATH::IsFinite(asset.pixelsPerUnit) || asset.pixelsPerUnit <= 0.0f) {
        return JSONSTRICT::Fail(outError, "pixelsPerUnit must be a finite positive number");
    }

    const auto pagesIt = root.find("pages");
    if (pagesIt == root.end() || !pagesIt->is_array() || pagesIt->empty() || pagesIt->size() > kMaximumPageCount) {
        return JSONSTRICT::Fail(outError, "pages must contain one to 64 entries");
    }
    std::unordered_map<std::string, uint32_t> pageIndices{};
    asset.pages.reserve(pagesIt->size());
    document.pagePaths.reserve(pagesIt->size());
    document.normalPagePaths.reserve(pagesIt->size());
    for (size_t index = 0u; index < pagesIt->size(); ++index) {
        const nlohmann::json& node = (*pagesIt)[index];
        const std::string fieldPath = "sprite.pages[" + std::to_string(index) + "]";
        if (!node.is_object()) {
            return JSONSTRICT::Fail(outError, fieldPath + " must be an object");
        }
        if (!RejectEvents(node, fieldPath, outError)) {
            return false;
        }
        SpritePage page{};
        std::string file{};
        if (!JSON_INTERNAL::ReadRequiredString(node, "id", page.id, outError, fieldPath) || page.id.empty() ||
            !JSON_INTERNAL::ReadRequiredString(node, "file", file, outError, fieldPath) || !node.contains("size") ||
            !JSON_INTERNAL::ReadUintPair(node.at("size"), page.width, page.height, outError, fieldPath + ".size")) {
            if (outError.empty()) {
                return JSONSTRICT::Fail(outError, fieldPath + " has an empty id");
            }
            return false;
        }
        if (page.width == 0u || page.height == 0u || page.width > kMaximumPageEdge || page.height > kMaximumPageEdge ||
            !pageIndices.emplace(page.id, static_cast<uint32_t>(index)).second) {
            return JSONSTRICT::Fail(outError, fieldPath + " has duplicate identity or invalid dimensions");
        }

        std::filesystem::path pagePath{};
        if (!ResolvePagePath(path, file, pagePath, outError, fieldPath + ".file")) {
            return false;
        }
        uint32_t actualWidth = 0u;
        uint32_t actualHeight = 0u;
        if (!ReadPngRgba8Header(pagePath, actualWidth, actualHeight, outError)) {
            return false;
        }
        if (actualWidth != page.width || actualHeight != page.height) {
            return JSONSTRICT::Fail(outError, fieldPath + ".size does not match the referenced PNG dimensions");
        }
        page.textureAssetId = MakeSpritePageTextureAssetId(sourceGuid, page.id);
        std::filesystem::path normalPagePath{};
        if (node.contains("normalFile")) {
            std::string normalFile{};
            if (!JSON_INTERNAL::ReadRequiredString(node, "normalFile", normalFile, outError, fieldPath) ||
                normalFile.empty() ||
                !ResolvePagePath(path, normalFile, normalPagePath, outError, fieldPath + ".normalFile")) {
                if (outError.empty()) {
                    return JSONSTRICT::Fail(outError, fieldPath + ".normalFile must be a non-empty relative PNG path");
                }
                return false;
            }
            uint32_t normalWidth = 0u;
            uint32_t normalHeight = 0u;
            if (!ReadPngRgba8Header(normalPagePath, normalWidth, normalHeight, outError)) {
                return false;
            }
            if (normalWidth != page.width || normalHeight != page.height) {
                return JSONSTRICT::Fail(outError, fieldPath + ".normalFile dimensions must match the color page");
            }
            page.normalTextureAssetId = MakeSpritePageNormalTextureAssetId(sourceGuid, page.id);
        }
        asset.pages.push_back(std::move(page));
        document.pagePaths.push_back(std::move(pagePath));
        document.normalPagePaths.push_back(std::move(normalPagePath));
    }

    const auto framesIt = root.find("frames");
    if (framesIt == root.end() || !framesIt->is_array() || framesIt->empty() ||
        framesIt->size() > (std::numeric_limits<uint32_t>::max)()) {
        return JSONSTRICT::Fail(outError, "frames must contain one to uint32-max entries");
    }
    std::unordered_map<std::string, uint32_t> frameIndices{};
    asset.frames.reserve(framesIt->size());
    for (size_t index = 0u; index < framesIt->size(); ++index) {
        const nlohmann::json& node = (*framesIt)[index];
        const std::string fieldPath = "sprite.frames[" + std::to_string(index) + "]";
        if (!node.is_object()) {
            return JSONSTRICT::Fail(outError, fieldPath + " must be an object");
        }
        if (!RejectEvents(node, fieldPath, outError)) {
            return false;
        }
        SpriteFrame frame{};
        std::string pageId{};
        if (!JSON_INTERNAL::ReadFrameGeometry(node, frame, false, outError, fieldPath) ||
            !JSON_INTERNAL::ReadRequiredString(node, "pageId", pageId, outError, fieldPath)) {
            return false;
        }
        const auto pageIndex = pageIndices.find(pageId);
        if (pageIndex == pageIndices.end()) {
            return JSONSTRICT::Fail(outError, fieldPath + ".pageId references an unknown page");
        }
        frame.pageIndex = pageIndex->second;
        if (!frameIndices.emplace(frame.id, static_cast<uint32_t>(index)).second) {
            return JSONSTRICT::Fail(outError, fieldPath + ".id is duplicated");
        }
        asset.frames.push_back(std::move(frame));
    }

    const auto clipsIt = root.find("clips");
    if (clipsIt == root.end() || !clipsIt->is_array() || clipsIt->empty()) {
        return JSONSTRICT::Fail(outError, "clips must not be empty");
    }
    std::unordered_set<std::string> clipIds{};
    asset.clips.reserve(clipsIt->size());
    for (size_t index = 0u; index < clipsIt->size(); ++index) {
        const nlohmann::json& node = (*clipsIt)[index];
        const std::string fieldPath = "sprite.clips[" + std::to_string(index) + "]";
        if (!node.is_object()) {
            return JSONSTRICT::Fail(outError, fieldPath + " must be an object");
        }
        if (!RejectEvents(node, fieldPath, outError)) {
            return false;
        }
        SpriteClip clip{};
        if (!JSON_INTERNAL::ReadRequiredString(node, "id", clip.id, outError, fieldPath) || clip.id.empty() ||
            !node.contains("loop") || !JSONSTRICT::ReadBool(node, "loop", false, clip.loop, outError, fieldPath)) {
            if (outError.empty()) {
                return JSONSTRICT::Fail(outError, fieldPath + " is missing a non-empty id or loop value");
            }
            return false;
        }
        if (!clipIds.insert(clip.id).second) {
            return JSONSTRICT::Fail(outError, fieldPath + ".id is duplicated");
        }
        const auto occurrencesIt = node.find("frames");
        if (occurrencesIt == node.end() || !occurrencesIt->is_array() || occurrencesIt->empty()) {
            return JSONSTRICT::Fail(outError, fieldPath + ".frames must not be empty");
        }
        clip.frames.reserve(occurrencesIt->size());
        for (size_t frameIndex = 0u; frameIndex < occurrencesIt->size(); ++frameIndex) {
            const nlohmann::json& occurrenceNode = (*occurrencesIt)[frameIndex];
            const std::string occurrencePath = fieldPath + ".frames[" + std::to_string(frameIndex) + "]";
            if (!occurrenceNode.is_object()) {
                return JSONSTRICT::Fail(outError, occurrencePath + " must be an object");
            }
            if (!RejectEvents(occurrenceNode, occurrencePath, outError)) {
                return false;
            }
            std::string frameId{};
            uint32_t durationMs = 0u;
            if (!JSON_INTERNAL::ReadRequiredString(occurrenceNode, "frameId", frameId, outError, occurrencePath) ||
                !occurrenceNode.contains("durationMs") ||
                !JSON_INTERNAL::ReadUint32Value(occurrenceNode.at("durationMs"), durationMs, outError,
                                                occurrencePath + ".durationMs")) {
                return false;
            }
            if (durationMs == 0u || durationMs > kMaximumFrameDurationMs) {
                return JSONSTRICT::Fail(outError, occurrencePath + ".durationMs must be between 1 and 600000");
            }
            const auto frame = frameIndices.find(frameId);
            if (frame == frameIndices.end()) {
                return JSONSTRICT::Fail(outError, occurrencePath + ".frameId references an unknown frame");
            }
            clip.frames.push_back({frame->second, durationMs});
        }
        asset.clips.push_back(std::move(clip));
    }

    if (!JSONSTRICT::ReadString(root, "defaultClip", asset.clips.front().id, asset.defaultClip, outError, "sprite")) {
        return false;
    }
    if (!ValidateSpriteRuntimeAsset(asset, sourceGuid, outError, false)) {
        return false;
    }
    outDocument = std::move(document);
    outError.clear();
    return true;
}

} // namespace HIKARI::SPRITE
