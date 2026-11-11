#include "Project/HIKARI_SdkInterface.h"

#include <cstdint>
#include <system_error>
#include <utility>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Serialization/Json/HIKARI_JsonStrict.h"
#include "Core/Text/HIKARI_OutMessage.h"
#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI {

// 構築が書く表（<構築木>/generated/HIKARISdk/HIKARI_SdkInterfaceTable.cpp）。
extern const SdkInterfaceEntry kHostSdkInterface[];
extern const size_t kHostSdkInterfaceCount;

namespace {

constexpr uint32_t kSdkInterfaceFormatVersion = 1u;
// 理由に名前を並べるファイルの数（残りは数だけ書く）。
constexpr size_t kListedFiles = 4u;

} // namespace

const SdkInterfaceFiles& GetHostSdkInterface() {
    static const SdkInterfaceFiles files = [] {
        SdkInterfaceFiles table{};
        for (size_t index = 0u; index < kHostSdkInterfaceCount; ++index) {
            table.emplace(kHostSdkInterface[index].path, kHostSdkInterface[index].hash);
        }
        return table;
    }();
    return files;
}

bool LoadSdkInterfaceFile(const std::filesystem::path& path, SdkInterfaceFiles& outFiles, std::string* outMessage) {
    nlohmann::json root{};
    std::string error{};
    if (!SERIALIZATION::JSON::ReadJsonFile(path, root, &error) || !root.is_object()) {
        SetMessage(outMessage, error.empty() ? "the interface file root must be an object" : error);
        return false;
    }
    if (!JSONSTRICT::RequireFormatVersion(root, kSdkInterfaceFormatVersion, error)) {
        SetMessage(outMessage, error);
        return false;
    }
    const auto files = root.find("files");
    if (files == root.end() || !files->is_object()) {
        SetMessage(outMessage, "root.files must be an object");
        return false;
    }
    SdkInterfaceFiles loaded{};
    for (const auto& [name, hash] : files->items()) {
        if (!hash.is_string()) {
            SetMessage(outMessage, "root.files." + name + " must be a string");
            return false;
        }
        loaded.emplace(name, hash.get<std::string>());
    }
    outFiles = std::move(loaded);
    SetMessage(outMessage, {});
    return true;
}

std::filesystem::path MakeLibraryInterfacePath(const std::filesystem::path& library) {
    std::filesystem::path path = library;
    path.replace_extension(kLibraryInterfaceExtension);
    return path;
}

bool IsLibraryInterfaceCurrent(const std::filesystem::path& library, const SdkInterfaceFiles& provided,
                               std::string* outMessage) {
    const std::filesystem::path record = MakeLibraryInterfacePath(library);
    std::error_code ec{};
    if (!std::filesystem::is_regular_file(record, ec)) {
        SetMessage(outMessage, "it has no record of the engine interface it was built against (" +
                                   TEXT::PathToUtf8(record.filename()) + " is missing next to the library)");
        return false;
    }
    SdkInterfaceFiles used{};
    std::string error{};
    if (!LoadSdkInterfaceFile(record, used, &error)) {
        SetMessage(outMessage, "its interface record cannot be read (" + error + ")");
        return false;
    }
    std::string listed{};
    size_t different = 0u;
    for (const auto& [name, hash] : used) {
        const auto current = provided.find(name);
        if (current != provided.end() && current->second == hash) {
            continue;
        }
        if (++different <= kListedFiles) {
            listed += (listed.empty() ? "" : ", ") + name;
        }
    }
    if (different == 0u) {
        SetMessage(outMessage, {});
        return true;
    }
    SetMessage(outMessage, "it was built against a different version of " + std::to_string(different) +
                               " engine interface file(s): " + listed +
                               (different > kListedFiles ? " and " + std::to_string(different - kListedFiles) + " more"
                                                         : std::string{}));
    return false;
}

} // namespace HIKARI
