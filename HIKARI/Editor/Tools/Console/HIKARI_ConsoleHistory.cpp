#include "Editor/Tools/Console/HIKARI_ConsoleHistory.h"

#include <fstream>
#include <system_error>
#include <utility>

#include "Core/HIKARI_Logger.h"
#include "Core/IO/HIKARI_NativeFilePath.h"
#include "Core/Text/HIKARI_Utf8.h"
#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI::EDITOR {

void ConsoleHistory::Load(std::filesystem::path path) {
    path_ = std::move(path);
    lines_.clear();
    std::ifstream input(IO::NativeFilePath(path_), std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        // 手で編集されたファイルから、入力欄に入らない行や壊れた文字列を持ち込まない。
        if (!line.empty() && line.size() <= kMaximumLineBytes && TEXT::IsValidUtf8(line))
            lines_.push_back(std::move(line));
    }
    if (lines_.size() > kMaximumLines)
        lines_.erase(lines_.begin(), lines_.end() - static_cast<std::ptrdiff_t>(kMaximumLines));
    cursor_ = lines_.size();
}

void ConsoleHistory::Add(std::string_view line) {
    if (!line.empty() && (lines_.empty() || lines_.back() != line)) {
        lines_.emplace_back(line);
        if (lines_.size() > kMaximumLines)
            lines_.erase(lines_.begin());
        Save();
    }
    cursor_ = lines_.size();
}

const std::string* ConsoleHistory::Step(int direction, std::string_view current) {
    if (direction < 0) {
        if (cursor_ == 0u)
            return nullptr;
        if (cursor_ == lines_.size())
            draft_ = current;
        --cursor_;
        return &lines_[cursor_];
    }
    if (cursor_ >= lines_.size())
        return nullptr;
    ++cursor_;
    return cursor_ == lines_.size() ? &draft_ : &lines_[cursor_];
}

void ConsoleHistory::Save() const {
    if (path_.empty())
        return;
    std::error_code error;
    std::filesystem::create_directories(IO::NativeFilePath(path_.parent_path()), error);
    std::ofstream output(IO::NativeFilePath(path_), std::ios::binary | std::ios::trunc);
    for (const std::string& line : lines_)
        output << line << '\n';
    // 履歴は便宜のための保存なので、書けなくても操作は止めず記録だけ残す。
    if (!output)
        HIKARI_LOG_WARN("[Console] failed to save the command history: " + TEXT::PathToUtf8(path_));
}

} // namespace HIKARI::EDITOR
