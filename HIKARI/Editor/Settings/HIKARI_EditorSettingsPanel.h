#pragma once

#include "Editor/HIKARI_EditorContext.h"
#include <functional>

namespace HIKARI::EDITOR {

class EditorSettingsPanel {
public:
    // bridgeActions は本地桥の節の下に描く（智能体説明の書き込みなど、操作表を持つ側の操作）。
    void DrawContents(EditorContext& context, const std::function<void()>& bridgeActions = {});

private:
    int category_ = 0;
    int logCapacity_ = 0;
    char search_[128]{};
};

} // namespace HIKARI::EDITOR
