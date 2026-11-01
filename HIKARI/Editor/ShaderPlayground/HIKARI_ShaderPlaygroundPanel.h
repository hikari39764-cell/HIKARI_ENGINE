#pragma once

#include <memory>
#include <string_view>

#include "Editor/Tools/HIKARI_EditorTool.h"

namespace HIKARI::EDITOR {

// ShaderPlayground の結果の窓：設定を選んで shader.play を実行し、拼图（A・B・差・問題・前回との差）を切り替えて見る。
// 画素に重ねると生の値を読み、保存を見張って自動で描き直す。着色器は外部の編集器で書く。
inline constexpr std::string_view kShaderPlaygroundToolId = "ShaderPlaygroundTool";

std::unique_ptr<IEditorTool> CreateShaderPlaygroundTool();

} // namespace HIKARI::EDITOR
