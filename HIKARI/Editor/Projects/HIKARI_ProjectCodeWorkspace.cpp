#include "Editor/Projects/HIKARI_ProjectCodeWorkspace.h"
#include "Core/Hashing/HIKARI_Sha256.h"
#include "Core/IO/HIKARI_FileReplacementTransaction.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"

#include <Windows.h>
#include <algorithm>
#include <fstream>
#include <set>
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
void WriteText(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!stream)
        throw std::runtime_error("Cannot write staged project source.");
}
} // namespace
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
nlohmann::json ReadFile(const std::filesystem::path& root, const std::string& relative) {
    const auto path = ResolveCodeFile(root, relative);
    if (!std::filesystem::is_regular_file(path))
        return {{"path", relative}, {"sha256", ""}, {"exists", false}, {"content", ""}};
    if (std::filesystem::file_size(path) > kFileBudget)
        throw std::runtime_error("Code file exceeds 1 MiB.");
    std::ifstream input(path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(input)), {});
    if (input.bad())
        throw std::runtime_error("Cannot read project code.");
    HASH::Sha256Digest hash;
    if (!HASH::ComputeSha256(content, hash))
        throw std::runtime_error("Cannot fingerprint project code.");
    return {{"path", relative}, {"sha256", HASH::ToHex(hash)}, {"exists", true}, {"content", content}};
}
nlohmann::json Prepare(const std::filesystem::path& project, const std::filesystem::path& sdk,
                       const std::filesystem::path& job, const nlohmann::json& edits) {
    if (!edits.is_array() || edits.empty() || edits.size() > 64)
        throw std::runtime_error("A code task needs 1 to 64 file edits.");
    if (std::filesystem::exists(job))
        throw std::runtime_error("Code task directory already exists.");
    const auto before = Snapshot(project);
    std::set<std::filesystem::path> edited;
    size_t editBytes = 0;
    bool changed = false;
    for (const auto& edit : edits) {
        const auto name = edit.at("path").get<std::string>();
        auto admitted = ResolveCodeFile(project, name);
        // Win32 names are case-insensitive; reject aliases within the batch.
        auto key = admitted.wstring();
        CharLowerBuffW(key.data(), static_cast<DWORD>(key.size()));
        if (!edited.insert(key).second)
            throw std::runtime_error("Duplicate code edit: " + name);
        const auto expected = edit.at("baseSha256").get<std::string>();
        const auto current = ReadFile(project, name);
        if (current["sha256"] != expected)
            throw std::runtime_error("Source conflict: " + name + ". Read the current file before editing.");
        if (!edit.contains("content") || !edit["content"].is_string())
            throw std::runtime_error("Every code edit needs UTF-8 content.");
        const auto bytes = edit["content"].get_ref<const std::string&>().size();
        if (bytes > kFileBudget || (editBytes += bytes) > 4u * kFileBudget)
            throw std::runtime_error("Code edits exceed their content budget.");
        changed |= !current["exists"].get<bool>() || current["content"] != edit["content"];
    }
    if (!changed)
        throw std::runtime_error("The proposal does not change any project source.");
    const auto workspace = job / "workspace";
    std::filesystem::create_directories(workspace);
    for (const auto& [name, hash] : before.items()) {
        const auto source = ResolveCodeFile(project, name);
        const auto target = ResolveCodeFile(workspace, name);
        std::filesystem::create_directories(target.parent_path());
        std::filesystem::copy_file(source, target);
        if (HashFile(target) != hash.get<std::string>())
            throw std::runtime_error("Source changed while creating the candidate: " + name);
    }
    for (const auto& edit : edits)
        WriteText(ResolveCodeFile(workspace, edit["path"].get<std::string>()), edit["content"].get<std::string>());
    const auto candidate = Snapshot(workspace);
    const auto descriptor = nlohmann::json::parse(ReadFile(workspace, "Project.hikari")["content"].get<std::string>());
    if (descriptor.value("gameProjectManifest", std::string{}) != "CMakeLists.txt")
        throw std::runtime_error("Code tasks currently require a project-root CMakeLists.txt manifest.");
    nlohmann::json paths = nlohmann::json::array();
    for (const auto& edit : edits)
        paths.push_back(edit["path"]);
    nlohmann::json prepared{{"version", 1},
                            {"projectRoot", TEXT::PathToUtf8(std::filesystem::absolute(project))},
                            {"sdkRoot", TEXT::PathToUtf8(std::filesystem::absolute(sdk))},
                            {"before", before},
                            {"candidate", candidate},
                            {"edits", paths}};
    if (!SERIALIZATION::JSON::WriteJsonFile(job / "request.json", prepared))
        throw std::runtime_error("Cannot save code task request.");
    return prepared;
}
void Apply(const std::filesystem::path& project, const std::filesystem::path& job, const nlohmann::json& prepared) {
    if (Snapshot(project) != prepared.at("before"))
        throw std::runtime_error("Project source changed during this task. Rebase the candidate before applying.");
    const auto workspace = job / "workspace";
    if (Snapshot(workspace) != prepared.at("candidate"))
        throw std::runtime_error("Candidate source changed after compilation. Create and validate a new candidate.");
    std::vector<IO::FileReplacementOperation> replacements;
    for (const auto& name : prepared.at("edits")) {
        const auto relative = name.get<std::string>();
        const auto target = ResolveCodeFile(project, relative);
        const auto staged = ResolveCodeFile(job / "apply", relative);
        std::filesystem::create_directories(staged.parent_path());
        std::filesystem::create_directories(target.parent_path());
        std::filesystem::copy_file(ResolveCodeFile(workspace, relative), staged,
                                   std::filesystem::copy_options::overwrite_existing);
        replacements.push_back({target, staged});
    }
    std::string error;
    if (!IO::CommitFileReplacementTransaction(replacements, error))
        throw std::runtime_error(error);
}
} // namespace HIKARI::EDITOR::PROJECT_CODE
