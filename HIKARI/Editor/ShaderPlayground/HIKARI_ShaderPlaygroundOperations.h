#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

namespace HIKARI::EDITOR {

// shader.play <spec>：プロジェクトの中の試験の設定（.json）を ShaderPlayground で描き、
// Library/ShaderPlayground/<name>/ に sheet.png・result.json などを書く（HIKARI_ShaderPlaygroundRunner.h）。
void RegisterShaderPlaygroundOperations(EditorOperationRegistry& registry);

// 最後に実行した shader.play。結果の窓は誰が実行したか（窓・控制台・智能体）に関わらずこれを表示する。
struct ShaderPlayRecord {
    std::string spec;
    std::filesystem::path output; // 書き出しのフォルダ。設定が読めなかったときは空
    bool ok = false;
    std::string message;
    uint64_t revision = 0u; // 実行のたびに増える
};
const ShaderPlayRecord& GetLastShaderPlay();

} // namespace HIKARI::EDITOR
