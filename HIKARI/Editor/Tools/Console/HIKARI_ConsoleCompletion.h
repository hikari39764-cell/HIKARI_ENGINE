#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "imgui.h"

namespace HIKARI::RUNTIME {
class RuntimeDebugService;
}

namespace HIKARI::EDITOR {

class EditorOperationRegistry;

// 候補と説明の出どころ。実行時コマンドは World が無ければ nullptr。
struct ConsoleSources {
    const EditorOperationRegistry& operations;
    const RUNTIME::RuntimeDebugService* service = nullptr;
};

// コマンド名の補完と、入力欄の上に出す候補・引数の説明。候補は入力が変わった時だけ作り直す。
class ConsoleCompletion {
public:
    // Tab 一回分。入力欄に入れる文字列を返す（候補が無ければ空）。
    // 候補が複数ならまず全候補に共通する部分まで補い、それ以上伸びなければ候補を順に入れる。
    std::string Complete(const ConsoleSources& sources, std::string_view text);
    // 名前を打っている間は候補を、名前が決まった後はその操作の引数を、入力欄の真上に重ねる。
    // レイアウトは動かさず、出力欄の末尾を隠すだけにする。
    void DrawHints(const ConsoleSources& sources, std::string_view text, const ImVec2& inputMin, float width);

private:
    struct Candidate {
        std::string name{};
        std::string summary{};
    };
    // 説明の一行。候補の一覧にも、操作の引数の一覧にも使う。
    struct HintRow {
        std::string name{};
        std::string type{};
        std::string flag{};
        std::string text{};
        bool highlighted = false;
        bool required = false;
    };

    void Update(const ConsoleSources& sources, std::string_view text);
    void RefreshOperationNames(const EditorOperationRegistry& operations);
    std::vector<HintRow> BuildHintRows(const ConsoleSources& sources, std::string_view text) const;
    std::string CommonPrefix() const;

    // 人に見える操作の名前と要約。登録表の版が変わった時だけ作り直す。
    std::optional<uint64_t> operationRevision_{};
    std::vector<Candidate> operationNames_{};
    // 候補を作った時の入力。Tab で入力を入れ替えても、候補は元の入力のまま順に回す。
    std::string candidateQuery_{};
    std::string appliedText_{};
    std::vector<Candidate> candidates_{};
    std::optional<size_t> completionIndex_{};
};

} // namespace HIKARI::EDITOR
