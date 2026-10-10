#pragma once

#include <filesystem>
#include <string>
#include <vector>

// 游戏项目のルートに置く智能体向けの説明：AGENTS.md（Codex などが読む本文）、CLAUDE.md（AGENTS.md を読み込む）、
// .mcp.json（Claude Code が hikari.exe mcp を起動する）。新しい項目の作成時と、設定の「写入智能体说明」で書く。
namespace HIKARI::EDITOR::AGENT {

struct AgentProjectFileInputs {
    std::string projectName{};
    // 見つからなければ空。説明には「hikari.exe が見つからない」と書く。
    std::filesystem::path hikariExecutable{};
    std::filesystem::path sdkRoot{};
};

// 実行中の编辑器の場所から hikari.exe と已安装 SDK を探す。
AgentProjectFileInputs DetectAgentProjectFileInputs(std::string projectName);

// 三つのファイルを書く。既存の AGENTS.md の「## 项目约定」から後ろは利用者が書いた部分として残す。
bool WriteAgentProjectFiles(const std::filesystem::path& projectRoot, const AgentProjectFileInputs& inputs,
                            std::vector<std::filesystem::path>& outWritten, std::string& outError);

} // namespace HIKARI::EDITOR::AGENT
