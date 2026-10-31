#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace HIKARI::PROJECT_DATA {

enum class ProjectDataFileCode : uint32_t {
    Resolved,
    InvalidRelativePath,
    ReparsePoint,
    NotDirectory,
    NotFound,
    TooLarge,
    InvalidUtf8,
    IoFailure,
};

// Resolves a UTF-8 relative path below a Data root. Missing components are
// allowed for callers creating a new file; existing links are rejected.
ProjectDataFileCode ResolveProjectDataFile(const std::filesystem::path& dataRoot, std::string_view relativePath,
                                           std::filesystem::path& outPath, std::string& outMessage);

// Inspects a resolved Data text file and enforces the shared UTF-8 text limit.
ProjectDataFileCode InspectProjectDataTextFile(const std::filesystem::path& path, uint64_t& outByteCount,
                                               std::string& outMessage);

// Reads exactly the previously inspected byte count, rejects growth beyond the
// limit and validates UTF-8 without accepting embedded NUL bytes.
ProjectDataFileCode ReadProjectDataTextFile(const std::filesystem::path& path, uint64_t expectedByteCount,
                                            std::string& outText, std::string& outMessage);

bool IsValidProjectDataText(std::string_view text) noexcept;

} // namespace HIKARI::PROJECT_DATA
