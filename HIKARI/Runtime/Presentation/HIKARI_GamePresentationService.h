#pragma once

namespace HIKARI::SERVICES {
    bool IsInProcessGamePresentationActive();
    bool IsGamePresentationActive();

    bool BeginInProcessGamePresentation();
    bool BeginInProcessGamePresentationStop();
    bool DrainInProcessGamePresentation();
    bool RestoreEditorPresentation();
    bool EndInProcessGamePresentation();
    bool ConsumeInProcessGameCloseRequest();
    void UpdateGamePresentationPerformanceTitle();
} // namespace HIKARI::SERVICES
