#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

// 編集器の本地桥へ外部の智能体（Claude Code、Codex、hikari.exe）がつなぐための接続情報。
// 編集器が <Library>/EditorAI/bridge.json に所有者だけが読める形で書き、hikari.exe が読む。
// このファイルは編集器と hikari.exe の両方に入るので、エンジンの他の部分に依存しない。
namespace HIKARI::EDITOR::AGENT {

inline constexpr uint32_t kAgentProtocolVersion = 2u;

struct AgentConnection {
    uint32_t protocol = kAgentProtocolVersion;
    // 127.0.0.1 の待ち受けポート。
    uint16_t port = 0;
    std::string token{};
    uint32_t processId = 0;
    // UTF-8 のプロジェクトのルート。
    std::string projectRoot{};
};

std::filesystem::path AgentConnectionPath(const std::filesystem::path& libraryRoot);
// プロジェクトの Library。場所は ProjectSettings/asset_pipeline.json の libraryRoot（無ければ Library）。
std::filesystem::path FindProjectLibraryRoot(const std::filesystem::path& projectRoot);
std::filesystem::path FindAgentConnectionPath(const std::filesystem::path& projectRoot);

// 一時ファイルに書いてから置き換える。Windows では所有者だけが読み書きできる DACL を付ける。
bool WriteAgentConnection(const std::filesystem::path& path, const AgentConnection& connection, std::string& outError);
std::optional<AgentConnection> ReadAgentConnection(const std::filesystem::path& path, std::string& outError);
// processId が書いた接続情報だけを消す。別の編集器が上書きしていれば残す。
void RemoveAgentConnection(const std::filesystem::path& path, uint32_t processId);

} // namespace HIKARI::EDITOR::AGENT
