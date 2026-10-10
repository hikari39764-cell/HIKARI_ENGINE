#include "Editor/Projects/HIKARI_ProjectCodeWorkspace.h"
#include "Core/Hashing/HIKARI_Sha256.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"

#include <Windows.h>
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace HIKARI::EDITOR::PROJECT_CODE {
namespace {
constexpr uintmax_t kFileBudget = 1024u * 1024u;
constexpr uintmax_t kProjectBudget = 32u * 1024u * 1024u;
std::string HashFile(const std::filesystem::path& path) {
    HASH::Sha256Digest hash;
    std::string error;
    if (!HASH::ComputeFileSha256(path, hash, error))
        throw std::runtime_error(error);
    return HASH::ToHex(hash);
}
void RejectReparsePoint(const std::filesystem::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Code tasks cannot traverse links or reparse points: " + TEXT::PathToUtf8(path));
}
bool IsCodePath(const std::filesystem::path& relative) {
    if (relative.empty() || relative.is_absolute() || relative.has_root_name())
        return false;
    for (const auto& part : relative) {
        const auto text = TEXT::PathToUtf8(part);
        if (text.empty() || text == "." || text == ".." || text.find_first_of(":\0", 0, 2) != std::string::npos ||
            text.back() == '.' || text.back() == ' ')
            return false;
        if (text.find_first_of("<>\"|?*") != std::string::npos ||
            std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32; }))
            return false;
        auto stem = part.filename().wstring();
        stem.resize(stem.find(L'.') == std::wstring::npos ? stem.size() : stem.find(L'.'));
        CharLowerBuffW(stem.data(), static_cast<DWORD>(stem.size()));
        if (stem == L"con" || stem == L"prn" || stem == L"aux" || stem == L"nul" ||
            (stem.size() == 4 && (stem.starts_with(L"com") || stem.starts_with(L"lpt")) && stem[3] >= L'1' &&
             stem[3] <= L'9'))
            return false;
    }
    const auto value = TEXT::PathToUtf8(relative.generic_wstring());
    if (value == "Project.hikari" || value == "CMakeLists.txt" || value == "CMakePresets.json")
        return true;
    const auto first = relative.begin()->string();
    if (first != "Source" && first != "Tests" && first != "Tools" && first != "cmake")
        return false;
    const auto extension = relative.extension().string();
    return extension == ".cpp" || extension == ".cc" || extension == ".cxx" || extension == ".c" || extension == ".h" ||
           extension == ".hpp" || extension == ".hh" || extension == ".hxx" || extension == ".inl" ||
           extension == ".json" || extension == ".cmake" || extension == ".ps1" || extension == ".py";
}
std::filesystem::path ResolveCodeFile(const std::filesystem::path& root, const std::string& value) {
    const auto relative = TEXT::PathFromUtf8(value);
    if (!IsCodePath(relative))
        throw std::runtime_error("Unsupported project code path: " + value);
    auto resolved = std::filesystem::absolute(root).lexically_normal();
    for (auto ancestor = resolved; !ancestor.empty(); ancestor = ancestor.parent_path()) {
        RejectReparsePoint(ancestor);
        if (ancestor == ancestor.root_path())
            break;
    }
    for (const auto& part : relative) {
        resolved /= part;
        RejectReparsePoint(resolved);
    }
    return resolved;
}
nlohmann::json Snapshot(const std::filesystem::path& root) {
    auto result = nlohmann::json::object();
    uintmax_t total = 0;
    auto include = [&](const std::filesystem::path& relative) {
        const auto path = ResolveCodeFile(root, TEXT::PathToUtf8(relative));
        const auto bytes = std::filesystem::file_size(path);
        if (bytes > kFileBudget || total + bytes > kProjectBudget || result.size() >= 2048)
            throw std::runtime_error("Code task exceeds its 1 MiB/file, 32 MiB/project or 2048-file budget.");
        total += bytes;
        result[TEXT::PathToUtf8(relative.generic_wstring())] = HashFile(path);
    };
    for (const auto* filename : {"Project.hikari", "CMakeLists.txt", "CMakePresets.json"}) {
        if (std::filesystem::is_regular_file(root / filename))
            include(filename);
    }
    for (const auto* folder : {"Source", "Tests", "Tools", "cmake"}) {
        const auto directory = root / folder;
        RejectReparsePoint(directory);
        if (!std::filesystem::is_directory(directory))
            continue;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
            RejectReparsePoint(entry.path());
            const auto relative = entry.path().lexically_relative(root);
            if (entry.is_regular_file() && IsCodePath(relative))
                include(relative);
        }
    }
    return result;
}
nlohmann::json ReadDescriptor(const std::filesystem::path& root) {
    const auto path = ResolveCodeFile(root, "Project.hikari");
    std::ifstream input(path, std::ios::binary);
    const auto descriptor = nlohmann::json::parse(input, nullptr, false);
    if (!descriptor.is_object())
        throw std::runtime_error("Cannot read Project.hikari.");
    return descriptor;
}
} // namespace

nlohmann::json PrepareBuild(const std::filesystem::path& project, const std::filesystem::path& sdk,
                            const std::filesystem::path& job) {
    if (std::filesystem::exists(job))
        throw std::runtime_error("Build task directory already exists.");
    const auto before = Snapshot(project);
    const auto workspace = job / "workspace";
    std::filesystem::create_directories(workspace);
    for (const auto& [name, hash] : before.items()) {
        const auto source = ResolveCodeFile(project, name);
        const auto target = ResolveCodeFile(workspace, name);
        std::filesystem::create_directories(target.parent_path());
        std::filesystem::copy_file(source, target);
        if (HashFile(target) != hash.get<std::string>())
            throw std::runtime_error("Source changed while copying it for the build: " + name);
    }
    if (ReadDescriptor(workspace).value("gameProjectManifest", std::string{}) != "CMakeLists.txt")
        throw std::runtime_error("Project builds currently require a project-root CMakeLists.txt manifest.");
    nlohmann::json prepared{{"version", 1},
                            {"projectRoot", TEXT::PathToUtf8(std::filesystem::absolute(project))},
                            {"sdkRoot", TEXT::PathToUtf8(std::filesystem::absolute(sdk))},
                            {"source", before}};
    if (!SERIALIZATION::JSON::WriteJsonFile(job / "request.json", prepared))
        throw std::runtime_error("Cannot save the build request.");
    return prepared;
}
} // namespace HIKARI::EDITOR::PROJECT_CODE
