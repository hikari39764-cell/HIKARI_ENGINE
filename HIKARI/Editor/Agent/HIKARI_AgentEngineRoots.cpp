#include "Editor/Agent/HIKARI_AgentEngineRoots.h"

#include <algorithm>
#include <system_error>

#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Core/Paths/HIKARI_ExecutablePath.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace HIKARI::EDITOR::AGENT {
namespace {

bool IsFile(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

void Lower(std::wstring& text) {
    if (!text.empty())
        CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
}

// 比べるための形：絶対パス、実在する部分は実体（接合点・短い名前を解く）、\ 区切り、小文字、末尾の \ なし。
std::wstring ComparablePath(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path full = std::filesystem::weakly_canonical(path, error);
    if (error)
        full = std::filesystem::absolute(path, error).lexically_normal();
    std::wstring text = full.make_preferred().native();
    if (text.starts_with(L"\\\\?\\"))
        text.erase(0u, 4u);
    while (text.size() > 3u && text.back() == L'\\')
        text.pop_back();
    Lower(text);
    return text;
}

// 文の中のパスと比べる形：/ を \ に、続く \ を一つに、小文字。
std::wstring FoldSeparators(std::wstring text) {
    std::replace(text.begin(), text.end(), L'/', L'\\');
    text.erase(std::unique(text.begin(), text.end(), [](wchar_t a, wchar_t b) { return a == L'\\' && b == L'\\'; }),
               text.end());
    Lower(text);
    return text;
}

// path が text の中に、続く名前の途中でない形で出てくるか（D:\PJ\HIKARI_Engine と D:\PJ\HIKARI_Engine2 を分ける）。
bool ContainsPath(const std::wstring& text, const std::wstring& path) {
    constexpr std::wstring_view kBoundaries = L"\\\"'` \t\r\n;|&<>()[]{},";
    for (size_t found = text.find(path); found != std::wstring::npos; found = text.find(path, found + 1u)) {
        const size_t end = found + path.size();
        if (end == text.size() || path.ends_with(L'\\') || kBoundaries.find(text[end]) != std::wstring_view::npos)
            return true;
    }
    return false;
}

std::wstring Widen(std::string_view text) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>((std::max)(length, 0)), L'\0');
    if (length > 0)
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

} // namespace

std::vector<std::filesystem::path> FindEngineRoots() {
    std::filesystem::path install = PLATFORM::GetExecutableDirectory();
    const std::filesystem::path parent = install.parent_path();
    if (IsFile(parent / "HIKARI_ProjectLauncher.exe") || IsFile(parent / "hikari.exe"))
        install = parent;
    std::vector<std::filesystem::path> roots{install};
    std::error_code error;
    for (std::filesystem::path folder = install.parent_path(); folder.has_relative_path();
         folder = folder.parent_path()) {
        if (IsFile(folder / "CMakeLists.txt") && std::filesystem::is_directory(folder / "HIKARI" / "Editor", error)) {
            roots.push_back(folder);
            break;
        }
    }
    return roots;
}

bool IsInsideAgentRoot(const std::filesystem::path& candidate, const std::filesystem::path& root,
                       const std::filesystem::path& base) {
    if (candidate.empty() || root.empty())
        return false;
    const std::wstring path = ComparablePath(candidate.is_absolute() || base.empty() ? candidate : base / candidate);
    std::wstring prefix = ComparablePath(root);
    if (path == prefix)
        return true;
    if (!prefix.ends_with(L'\\'))
        prefix += L'\\';
    return path.starts_with(prefix);
}

bool TouchesEngineRoots(const std::vector<std::filesystem::path>& roots, const std::filesystem::path& candidate,
                        const std::filesystem::path& base) {
    if (candidate.empty())
        return false;
    const std::filesystem::path full = candidate.is_absolute() || base.empty() ? candidate : base / candidate;
    return std::any_of(roots.begin(), roots.end(), [&full](const std::filesystem::path& root) {
        return IsInsideAgentRoot(full, root) || IsInsideAgentRoot(root, full);
    });
}

bool MentionsEngineRoots(const std::vector<std::filesystem::path>& roots, std::string_view text) {
    if (text.empty())
        return false;
    const std::wstring folded = FoldSeparators(Widen(text));
    for (const std::filesystem::path& root : roots) {
        const std::wstring path = FoldSeparators(ComparablePath(root));
        if (path.empty())
            continue;
        if (ContainsPath(folded, path))
            return true;
        if (path.size() > 2u && path[1] == L':' && ContainsPath(folded, L"\\" + path.substr(0u, 1u) + path.substr(2u)))
            return true;
    }
    return false;
}

std::optional<std::string> FindEngineOutputParameter(const EditorOperationDefinition& definition,
                                                     const nlohmann::json& parameters,
                                                     const std::vector<std::filesystem::path>& roots) {
    for (const EditorOperationParameter& parameter : definition.parameters) {
        if (!parameter.schema.is_object() || parameter.schema.value("format", std::string{}) != kOutputPathFormat)
            continue;
        const auto found = parameters.find(parameter.name);
        if (found == parameters.end() || !found->is_string())
            continue;
        const std::string value = found->get<std::string>();
        if (TouchesEngineRoots(roots, TEXT::PathFromUtf8(value)))
            return value;
    }
    return std::nullopt;
}

} // namespace HIKARI::EDITOR::AGENT
