#pragma once

#include "Editor/Panels/HIKARI_InputActionMapEditor.h"

namespace HIKARI::INPUT {
class InputService;
}

namespace HIKARI {

class InputActionMapPanel {
public:
    void RequestOpen() noexcept;
    void DrawModal(INPUT::InputService& inputService);
    bool IsDirty() const noexcept { return editor_.IsDirty(); }

private:
    InputActionMapEditor editor_{};
    bool openRequested_ = false;
};

} // namespace HIKARI
