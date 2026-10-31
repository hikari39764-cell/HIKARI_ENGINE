#include "Project/HIKARI_ProjectDescriptor.h"
#include "Core/IO/HIKARI_PathNormalization.h"
#include "Core/Text/HIKARI_OutMessage.h"

#include <limits>
#include <system_error>
#include <utility>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"

namespace HIKARI {
namespace {

bool LooksLikeDescriptorPath(const std::filesystem::path& path) {
    return path.filename() == std::filesystem::path(kProjectDescriptorFilename) || path.extension() == ".hikari";
}

} // namespace

std::filesystem::path ResolveProjectDescriptorPath(const std::filesystem::path& projectPath) {
    if (projectPath.empty()) {
        return {};
    }

    std::filesystem::path descriptorPath = projectPath;
    std::error_code ec{};
    if (std::filesystem::is_directory(descriptorPath, ec) ||
        (!LooksLikeDescriptorPath(descriptorPath) && !std::filesystem::is_regular_file(descriptorPath, ec))) {
        descriptorPath /= kProjectDescriptorFilename;
    }

    ec.clear();
    std::filesystem::path normalized = std::filesystem::weakly_canonical(descriptorPath, ec);
    if (ec || normalized.empty()) {
        ec.clear();
        normalized = std::filesystem::absolute(descriptorPath, ec);
    }
    return ec || normalized.empty() ? descriptorPath.lexically_normal() : normalized.lexically_normal();
}

std::filesystem::path FindProjectDescriptor(const std::filesystem::path& startPath) {
    if (startPath.empty()) {
        return {};
    }

    std::filesystem::path current = startPath;
    std::error_code ec{};
    if (std::filesystem::is_regular_file(current, ec)) {
        current = current.parent_path();
    }
    ec.clear();
    current = std::filesystem::absolute(current, ec);
    if (ec) {
        current = startPath.lexically_normal();
    }

    while (!current.empty()) {
        const std::filesystem::path candidate = current / kProjectDescriptorFilename;
        ec.clear();
        if (std::filesystem::is_regular_file(candidate, ec) && !ec) {
            return ResolveProjectDescriptorPath(candidate);
        }

        const std::filesystem::path parent = current.parent_path();
        if (parent.empty() || parent == current) {
            break;
        }
        current = parent;
    }
    return {};
}

bool LoadProjectDescriptor(const std::filesystem::path& descriptorPath, ProjectDescriptor& outDescriptor,
                           std::string* outMessage) {
    nlohmann::json root{};
    std::string readMessage{};
    if (!SERIALIZATION::JSON::ReadJsonFile(descriptorPath, root, &readMessage) || !root.is_object()) {
        SetMessage(outMessage,
                   readMessage.empty() ? "project descriptor root must be an object" : std::move(readMessage));
        return false;
    }

    if (!root.contains("formatVersion") || !root["formatVersion"].is_number_unsigned()) {
        SetMessage(outMessage, "Project.hikari requires an unsigned formatVersion");
        return false;
    }
    const uint64_t rawFormatVersion = root["formatVersion"].get<uint64_t>();
    if (rawFormatVersion == 0u || rawFormatVersion > static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)()) ||
        rawFormatVersion > kProjectDescriptorFormatVersion) {
        SetMessage(outMessage, "unsupported Project.hikari formatVersion: " + std::to_string(rawFormatVersion));
        return false;
    }
    const uint32_t formatVersion = static_cast<uint32_t>(rawFormatVersion);

    if (!root.contains("name") || !root["name"].is_string()) {
        SetMessage(outMessage, "Project.hikari requires a string name");
        return false;
    }

    std::string name = root["name"].get<std::string>();
    if (name.empty()) {
        SetMessage(outMessage, "Project.hikari name must not be empty");
        return false;
    }

    std::filesystem::path gameProjectManifest{"CMakeLists.txt"};
    if (root.contains("gameProjectManifest")) {
        if (!root["gameProjectManifest"].is_string()) {
            SetMessage(outMessage, "Project.hikari gameProjectManifest must be a string");
            return false;
        }
        gameProjectManifest = root["gameProjectManifest"].get<std::string>();
    }
    if (!PATHS::IsSafeRelativePath(gameProjectManifest)) {
        SetMessage(outMessage, "Project.hikari gameProjectManifest must be a safe "
                               "project-relative path");
        return false;
    }

    outDescriptor.formatVersion = formatVersion;
    outDescriptor.name = std::move(name);
    outDescriptor.gameProjectManifest = std::move(gameProjectManifest);
    SetMessage(outMessage, {});
    return true;
}

bool SaveProjectDescriptor(const std::filesystem::path& descriptorPath, const ProjectDescriptor& descriptor,
                           std::string* outMessage) {
    if (descriptor.name.empty()) {
        SetMessage(outMessage, "Project.hikari name must not be empty");
        return false;
    }
    if (descriptor.formatVersion == 0u || descriptor.formatVersion > kProjectDescriptorFormatVersion) {
        SetMessage(outMessage, "Project.hikari formatVersion is unsupported");
        return false;
    }
    if (!PATHS::IsSafeRelativePath(descriptor.gameProjectManifest)) {
        SetMessage(outMessage, "Project.hikari gameProjectManifest must be a safe "
                               "project-relative path");
        return false;
    }

    const nlohmann::json root{
        {"formatVersion", descriptor.formatVersion},
        {"gameProjectManifest", descriptor.gameProjectManifest.generic_string()},
        {"name", descriptor.name},
    };
    return SERIALIZATION::JSON::WriteJsonFile(descriptorPath, root, outMessage);
}

} // namespace HIKARI
