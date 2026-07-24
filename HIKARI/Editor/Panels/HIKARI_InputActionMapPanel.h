#pragma once

#include "Editor/Panels/HIKARI_InputActionMapEditor.h"

namespace HIKARI::INPUT { class InputService; }

namespace HIKARI {

class InputActionMapPanel {
public:
    void RequestOpen() noexcept;
    void DrawLauncher(INPUT::InputService& inputService);
    void DrawModal(INPUT::InputService& inputService);

private:
    InputActionMapEditor editor_{};
    bool openRequested_ = false;
};

} // namespace HIKARI
