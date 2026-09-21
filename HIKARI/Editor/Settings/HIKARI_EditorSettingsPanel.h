#pragma once

#include "Editor/HIKARI_EditorContext.h"
#include <functional>

namespace HIKARI::EDITOR {

class EditorSettingsPanel {
public:
    void DrawContents(EditorContext& context, const std::function<void()>& aiSettings = {});
private:
    int category_ = 0;
    int logCapacity_ = 0;
    char search_[128]{};
};

} // namespace HIKARI::EDITOR
