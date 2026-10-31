#include "Project/Paths/HIKARI_ProjectDataFiles.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

#include <Windows.h>

#include "Core/IO/HIKARI_NativeFilePath.h"
#include "Core/Text/HIKARI_Utf8.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "HIKARI/Project/ProjectDataTypes.h"

namespace HIKARI::PROJECT_DATA {
namespace {

bool IsMissingError(const std::error_code& error) noexcept {
    if (error == std::errc::no_such_file_or_directory ||
        error.default_error_condition() == std::errc::no_such_file_or_directory) {
        return true;
    }
    return error.value() == ERROR_FILE_NOT_FOUND || error.value() == ERROR_PATH_NOT_FOUND;
}

bool IsValidRelativePath(std::string_view text, std::filesystem::path& outRelative) {
    if (text.empty() || text.size() > kMaximumProjectDataPathBytes || text.find('\0') != std::string_view::npos ||
        text.find(':') != std::string_view::npos || !TEXT::IsValidUtf8(text)) {
        return false;
    }

    const std::filesystem::path relative = TEXT::PathFromUtf8(text);
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
        return false;
    }
    for (const auto& part : relative) {
        const std::string component = TEXT::PathToUtf8(part);
        if (component.empty() || component == "." || component == ".." || component.back() == '.' ||
            component.back() == ' ' || component.find_first_of("<>\"|?*") != std::string::npos ||
            TEXT::IsWindowsReservedFileName(component) ||
            std::any_of(component.begin(), component.end(), [](unsigned char value) { return value < 32u; })) {
            return false;
        }
    }
    outRelative = relative.lexically_normal();
    return !outRelative.empty() && outRelative != ".";
}

ProjectDataFileCode InspectExistingComponent(const std::filesystem::path& path, bool requireDirectory,
                                             std::string& outMessage) {
    std::error_code error{};
    const std::filesystem::path nativePath = IO::NativeFilePath(path);
    const std::filesystem::file_status status = std::filesystem::symlink_status(nativePath, error);
    if (status.type() == std::filesystem::file_type::not_found || IsMissingError(error)) {
        return ProjectDataFileCode::Resolved;
    }
    if (error) {
        outMessage = "Could not inspect project Data path: " + error.message();
        return ProjectDataFileCode::IoFailure;
    }
    if (!std::filesystem::exists(status)) {
        return ProjectDataFileCode::Resolved;
    }

    const DWORD attributes = GetFileAttributesW(nativePath.c_str());
    if (status.type() == std::filesystem::file_type::symlink ||
        (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u)) {
        outMessage = "Project Data paths cannot traverse links or reparse points: " + TEXT::PathToUtf8(path);
        return ProjectDataFileCode::ReparsePoint;
    }
    if (requireDirectory && !std::filesystem::is_directory(status)) {
        outMessage = "A project Data path parent is not a directory: " + TEXT::PathToUtf8(path);
        return ProjectDataFileCode::NotDirectory;
    }
    return ProjectDataFileCode::Resolved;
}

} // namespace

ProjectDataFileCode ResolveProjectDataFile(const std::filesystem::path& dataRoot, std::string_view relativePath,
                                           std::filesystem::path& outPath, std::string& outMessage) {
    outPath.clear();
    outMessage.clear();
    std::filesystem::path relative{};
    if (dataRoot.empty() || !IsValidRelativePath(relativePath, relative)) {
        outMessage = "Project Data path must be a valid UTF-8 relative path below Data/.";
        return ProjectDataFileCode::InvalidRelativePath;
    }

    std::error_code error{};
    const std::filesystem::path absoluteRoot = std::filesystem::absolute(dataRoot, error).lexically_normal();
    if (error || absoluteRoot.empty()) {
        outMessage = "Could not resolve the project Data root: " + error.message();
        return ProjectDataFileCode::IoFailure;
    }

    std::filesystem::path current = absoluteRoot.root_path();
    for (const auto& part : absoluteRoot.relative_path()) {
        current /= part;
        const auto code = InspectExistingComponent(current, true, outMessage);
        if (code != ProjectDataFileCode::Resolved) {
            return code;
        }
    }
    for (auto iterator = relative.begin(); iterator != relative.end(); ++iterator) {
        current /= *iterator;
        const bool hasMore = std::next(iterator) != relative.end();
        const auto code = InspectExistingComponent(current, hasMore, outMessage);
        if (code != ProjectDataFileCode::Resolved) {
            return code;
        }
    }

    outPath = current.lexically_normal();
    return ProjectDataFileCode::Resolved;
}

ProjectDataFileCode InspectProjectDataTextFile(const std::filesystem::path& path, uint64_t& outByteCount,
                                               std::string& outMessage) {
    outByteCount = 0u;
    outMessage.clear();
    std::error_code error{};
    const std::filesystem::path nativePath = IO::NativeFilePath(path);
    const std::filesystem::file_status status = std::filesystem::symlink_status(nativePath, error);
    if (status.type() == std::filesystem::file_type::not_found || IsMissingError(error)) {
        return ProjectDataFileCode::NotFound;
    }
    if (error) {
        outMessage = "Could not inspect project Data text: " + error.message();
        return ProjectDataFileCode::IoFailure;
    }

    const DWORD attributes = GetFileAttributesW(nativePath.c_str());
    if (status.type() == std::filesystem::file_type::symlink ||
        (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u)) {
        outMessage = "Project Data text cannot be a link or reparse point: " + TEXT::PathToUtf8(path);
        return ProjectDataFileCode::ReparsePoint;
    }
    if (!std::filesystem::is_regular_file(status)) {
        outMessage = "Project Data text is not a regular file: " + TEXT::PathToUtf8(path);
        return ProjectDataFileCode::IoFailure;
    }

    const uintmax_t fileBytes = std::filesystem::file_size(nativePath, error);
    if (error) {
        outMessage = "Could not inspect project Data text size: " + error.message();
        return ProjectDataFileCode::IoFailure;
    }
    if (fileBytes > kMaximumProjectDataTextBytes) {
        outMessage = "Project Data text exceeds the 4 MiB limit.";
        return ProjectDataFileCode::TooLarge;
    }
    outByteCount = static_cast<uint64_t>(fileBytes);
    return ProjectDataFileCode::Resolved;
}

ProjectDataFileCode ReadProjectDataTextFile(const std::filesystem::path& path, uint64_t expectedByteCount,
                                            std::string& outText, std::string& outMessage) {
    outText.clear();
    outMessage.clear();
    if (expectedByteCount > kMaximumProjectDataTextBytes) {
        outMessage = "Project Data text exceeds the 4 MiB limit.";
        return ProjectDataFileCode::TooLarge;
    }

    std::ifstream input(IO::NativeFilePath(path), std::ios::binary);
    if (!input.is_open()) {
        outMessage = "Could not open project Data text: " + TEXT::PathToUtf8(path);
        return ProjectDataFileCode::IoFailure;
    }
    outText.resize(static_cast<size_t>(expectedByteCount));
    if (!outText.empty()) {
        input.read(outText.data(), static_cast<std::streamsize>(outText.size()));
        if (input.gcount() != static_cast<std::streamsize>(outText.size())) {
            outText.clear();
            outMessage = "Project Data text changed or could not be read.";
            return ProjectDataFileCode::IoFailure;
        }
    }
    char extraByte = 0;
    if (input.read(&extraByte, 1)) {
        outText.clear();
        if (expectedByteCount == kMaximumProjectDataTextBytes) {
            outMessage = "Project Data text grew beyond the 4 MiB limit while being read.";
            return ProjectDataFileCode::TooLarge;
        }
        outMessage = "Project Data text changed while being read.";
        return ProjectDataFileCode::IoFailure;
    }
    if (!input.eof()) {
        outText.clear();
        outMessage = "Project Data text could not be read completely.";
        return ProjectDataFileCode::IoFailure;
    }
    if (!IsValidProjectDataText(outText)) {
        outText.clear();
        outMessage = "Project Data text must contain valid UTF-8 without NUL bytes.";
        return ProjectDataFileCode::InvalidUtf8;
    }
    return ProjectDataFileCode::Resolved;
}

bool IsValidProjectDataText(std::string_view text) noexcept {
    return text.size() <= kMaximumProjectDataTextBytes && text.find('\0') == std::string_view::npos &&
           TEXT::IsValidUtf8(text);
}

} // namespace HIKARI::PROJECT_DATA
