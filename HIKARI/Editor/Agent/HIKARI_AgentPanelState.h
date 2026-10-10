#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

#include "Editor/Agent/HIKARI_AgentSession.h"

namespace HIKARI::EDITOR::AGENT {

// 编辑器の智能体面板の状態。場景控制器が持ち、描画と毎フレームの進行を行う。
struct AgentPanelState {
    struct Operation {
        std::string action{};
        std::string message{};
        bool ok = false;
    };
    // 続けられる会話（Library/EditorAI/AgentConversations.json）。中身は CLI が保存している。
    struct Conversation {
        AgentKind kind = AgentKind::ClaudeCode;
        std::string id{};
        std::string title{};
        int64_t updatedAt = 0;
    };

    AgentSession session{};
    AgentKind kind = AgentKind::ClaudeCode;
    std::array<std::filesystem::path, 2> executables{};
    std::array<AgentLoginCheck, 2> loginChecks{};
    bool detected = false;
    char prompt[4096]{};
    char model[64]{};
    char effort[16]{};
    bool includeEditorState = true;
    bool chrome = false;
    // 利用者が CLI に設定した他の MCP 服务器・插件も読み込む。
    bool userTools = false;
    // 項目ごとに覚える選択（Library/EditorAI/AgentPanel.json）。Codex の模型一覧は前の会話の報告を残す。
    std::filesystem::path preferencesFile{};
    std::vector<AgentModelOption> codexModels{};
    // 智能体ごとの模型と推理强度（下の model / effort は今の智能体の分）。
    std::array<std::pair<std::string, std::string>, 2> remembered{};
    // 次の会話の開始で続ける会話 ID（一覧から選んだ時）。
    std::string resumeSession{};
    std::string error{};
    // 依頼を CLI が処理している間。本地桥の修改はこの間の依頼の撤销组に入る。
    bool turnActive = false;
    // 実行中の依頼で智能体が記録した場面の撤销组。終わったら lastTurnUndoGroup に移る（「撤销这一轮」）。
    uint64_t turnUndoGroup = 0;
    uint64_t lastTurnUndoGroup = 0;
    // 智能体（本地桥）が実行した修改操作の新しい順。
    std::deque<Operation> operations{};
    std::vector<Conversation> conversations{};
    std::filesystem::path conversationsFile{};
};

} // namespace HIKARI::EDITOR::AGENT
