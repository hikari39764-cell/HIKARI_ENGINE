#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJson.h"

#include <exception>
#include <utility>

#include <json.hpp>

#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJsonInternal.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"

namespace HIKARI::VFX::SERIALIZATION {
namespace {
using Json = nlohmann::json;
}

bool ParseVfxSourceJson(std::string_view jsonText, VfxAssetData& outAsset, std::string& outError) {
    try {
        const Json root = Json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
        if (root.is_discarded()) {
            outError = "failed to parse VFX source JSON";
            return false;
        }
        VfxAssetData parsed{};
        if (!INTERNAL::ReadVfxSourceDocument(root, parsed, outError)) {
            return false;
        }
        outAsset = std::move(parsed);
        return true;
    } catch (const std::exception& exception) {
        outError = "failed to read VFX source JSON: " + std::string(exception.what());
        return false;
    }
}

bool SerializeVfxSourceJson(const VfxAssetData& asset, std::string& outJsonText, std::string& outError) {
    if (!INTERNAL::ValidateVfxSourceAsset(asset, outError)) {
        return false;
    }
    try {
        outJsonText = INTERNAL::WriteVfxSourceDocument(asset).dump(2) + '\n';
        outError.clear();
        return true;
    } catch (const std::exception& exception) {
        outError = "failed to serialize VFX source JSON: " + std::string(exception.what());
        return false;
    }
}

bool LoadVfxSourceJson(const std::filesystem::path& path, VfxAssetData& outAsset, std::string& outError) {
    Json root{};
    if (!::HIKARI::SERIALIZATION::JSON::ReadJsonFile(path, root, &outError)) {
        return false;
    }
    try {
        VfxAssetData parsed{};
        if (!INTERNAL::ReadVfxSourceDocument(root, parsed, outError)) {
            return false;
        }
        outAsset = std::move(parsed);
        return true;
    } catch (const std::exception& exception) {
        outError = "failed to read VFX source file: " + std::string(exception.what());
        return false;
    }
}

bool SaveVfxSourceJson(const std::filesystem::path& path, const VfxAssetData& asset, std::string& outError) {
    if (!INTERNAL::ValidateVfxSourceAsset(asset, outError)) {
        return false;
    }
    return ::HIKARI::SERIALIZATION::JSON::WriteJsonFile(path, INTERNAL::WriteVfxSourceDocument(asset), &outError);
}

} // namespace HIKARI::VFX::SERIALIZATION
