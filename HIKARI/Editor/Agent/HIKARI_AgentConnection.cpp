#include "Editor/Agent/HIKARI_AgentConnection.h"

#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>

#include <json.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#pragma comment(lib, "advapi32.lib")
#endif

namespace HIKARI::EDITOR::AGENT {
namespace {

std::string ToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path FromUtf8(std::string_view text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

bool ReadText(const std::filesystem::path& path, std::string& outText) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    outText.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}

} // namespace

std::filesystem::path AgentConnectionPath(const std::filesystem::path& libraryRoot) {
    return libraryRoot / "EditorAI" / "bridge.json";
}

std::filesystem::path FindProjectLibraryRoot(const std::filesystem::path& projectRoot) {
    std::string libraryRoot = "Library";
    std::string text;
    if (ReadText(projectRoot / "ProjectSettings" / "asset_pipeline.json", text)) {
        const nlohmann::json settings = nlohmann::json::parse(text, nullptr, false);
        if (settings.is_object() && settings.contains("libraryRoot") && settings["libraryRoot"].is_string()) {
            libraryRoot = settings["libraryRoot"].get<std::string>();
        }
    }
    const std::filesystem::path library = FromUtf8(libraryRoot);
    return library.is_absolute() ? library : projectRoot / library;
}

std::filesystem::path FindAgentConnectionPath(const std::filesystem::path& projectRoot) {
    return AgentConnectionPath(FindProjectLibraryRoot(projectRoot));
}

bool WriteAgentConnection(const std::filesystem::path& path, const AgentConnection& connection, std::string& outError) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        outError = "无法创建连接文件目录：" + ToUtf8(path.parent_path());
        return false;
    }
    const std::string text =
        nlohmann::json{
            {"protocol", connection.protocol},
            {"port", connection.port},
            {"token", connection.token},
            {"processId", connection.processId},
            {"projectRoot", connection.projectRoot},
        }
            .dump(2);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
#ifdef _WIN32
    // 保護された DACL で所有者（OWNER RIGHTS）だけに全権を与える。置き換えても DACL は一時ファイルのものが残る。
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;OW)", SDDL_REVISION_1, &descriptor,
                                                              nullptr)) {
        outError = "无法创建连接文件的访问控制";
        return false;
    }
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    const HANDLE file =
        CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, &attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    LocalFree(descriptor);
    if (file == INVALID_HANDLE_VALUE) {
        outError = "无法写入连接文件：" + ToUtf8(temporary);
        return false;
    }
    DWORD written = 0;
    const bool wrote = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
                       written == text.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!wrote || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        outError = "无法写入连接文件：" + ToUtf8(path);
        return false;
    }
    return true;
#else
    (void)text;
    outError = "本地桥仅支持 Windows 编辑器宿主";
    return false;
#endif
}

std::optional<AgentConnection> ReadAgentConnection(const std::filesystem::path& path, std::string& outError) {
    std::string text;
    if (!ReadText(path, text)) {
        // 连接文件は当前用户だけが読める。沙箱の中の命令（Codex など）からは、在っても読めない。
        std::error_code error;
        outError = std::filesystem::exists(path, error)
                       ? "无法读取编辑器的连接文件：" + ToUtf8(path) +
                             "。这个文件只允许当前用户读取，在沙箱里运行的命令读不到；请改用 MCP 的 hikari 工具。"
                       : "找不到编辑器的连接文件：" + ToUtf8(path) + "。请打开项目，并在编辑器设置里启用本地桥。";
        return std::nullopt;
    }
    const nlohmann::json value = nlohmann::json::parse(text, nullptr, false);
    if (!value.is_object() || !value.contains("protocol") || !value["protocol"].is_number_unsigned()) {
        outError = "连接文件格式不正确：" + ToUtf8(path);
        return std::nullopt;
    }
    AgentConnection connection{};
    connection.protocol = value["protocol"].get<uint32_t>();
    if (connection.protocol != kAgentProtocolVersion) {
        outError = "连接文件的协议版本 " + std::to_string(connection.protocol) + " 与 hikari.exe 的版本 " +
                   std::to_string(kAgentProtocolVersion) + " 不一致，请使用同一版本的编辑器。";
        return std::nullopt;
    }
    connection.port = value.value("port", uint16_t{0});
    connection.token = value.value("token", std::string{});
    connection.processId = value.value("processId", uint32_t{0});
    connection.projectRoot = value.value("projectRoot", std::string{});
    if (connection.port == 0u || connection.token.empty()) {
        outError = "连接文件缺少端口或令牌：" + ToUtf8(path);
        return std::nullopt;
    }
    return connection;
}

void RemoveAgentConnection(const std::filesystem::path& path, uint32_t processId) {
    std::string error;
    const std::optional<AgentConnection> current = ReadAgentConnection(path, error);
    if (current && current->processId == processId) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
}

} // namespace HIKARI::EDITOR::AGENT
