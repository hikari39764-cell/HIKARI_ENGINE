#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace HIKARI::EDITOR {

// コンソールに打ったコマンドの履歴。プロジェクトごとにファイルへ書き、↑/↓ で辿る。
class ConsoleHistory {
public:
    static constexpr size_t kMaximumLines = 200;
    // 入力欄に入る長さ。実行時コマンドとして送る行は RuntimeDebugService::Execute が自分の上限で断る。
    static constexpr size_t kMaximumLineBytes = 2048;

    // path のファイルから読み直す（無ければ空）。以降の Add はこの path へ保存する。
    void Load(std::filesystem::path path);
    // 空行と直前と同じ行は積まない。積んだら保存し、辿る位置を末尾へ戻す。
    void Add(std::string_view line);
    // 辿り始めに今の入力を覚える。direction が負なら古い方へ、正なら新しい方へ（末尾まで来たら覚えた入力）。
    // 動けなければ nullptr。
    const std::string* Step(int direction, std::string_view current);

private:
    void Save() const;

    std::filesystem::path path_{};
    std::vector<std::string> lines_{};
    // lines_.size() は履歴の外（これから打つ行）。
    size_t cursor_ = 0;
    std::string draft_{};
};

} // namespace HIKARI::EDITOR
