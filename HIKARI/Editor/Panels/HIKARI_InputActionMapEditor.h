#pragma once

#include <array>
#include <string>
#include <vector>

#include "Input/Assets/HIKARI_InputActionMap.h"

namespace HIKARI::INPUT { class InputService; }

namespace HIKARI {

class InputActionMapEditor {
public:
    void Draw(INPUT::InputService& inputService);
    bool IsDirty() const noexcept { return dirty_; }

private:
    void DrawToolbar(INPUT::InputService& inputService);
    void DrawActionsTab(INPUT::InputService& inputService);
    void DrawContextsTab(INPUT::InputService& inputService);
    void DrawMonitorTab(INPUT::InputService& inputService);
    void DrawActionPopups(INPUT::InputService& inputService);
    void DrawContextPopups(INPUT::InputService& inputService);
    void DrawBindingPopup(INPUT::InputService& inputService);
    void DrawCompositePopup(INPUT::InputService& inputService);
    void DrawProjectPopups(INPUT::InputService& inputService);
    void BeginBindingEdit(
        const INPUT::InputActionMap& map,
        int bindingIndex);
    void BeginBindingCapture(INPUT::InputService& inputService);
    void DrawBindingCapture(INPUT::InputService& inputService);
    std::vector<int> FindBindingConflictIndices(
        const INPUT::InputActionMap& map) const;
    void ApplyBindingDraft(
        INPUT::InputActionMap& map,
        bool replaceConflicts);
    bool DrawBindingConflictResolution(INPUT::InputActionMap& map);
    void BeginCompositeEdit(const INPUT::InputActionMap& map);
    void BeginCompositeCapture(INPUT::InputService& inputService);
    void DrawCompositeCapture(INPUT::InputService& inputService);
    void SetStatus(std::string message, bool isError);

    enum class RebindOwner {
        None,
        Binding,
        Composite,
    };

    int selectedAction_ = 0;
    int selectedContext_ = 0;
    int bindingEditIndex_ = -1;
    int bindingRemoveIndex_ = -1;
    INPUT::InputBinding bindingDraft_{};
    std::vector<int> bindingConflictIndices_{};
    std::string bindingCaptureMessage_{};
    std::string compositeActionId_{};
    std::array<INPUT::InputBinding, 4> compositeBindings_{};
    std::array<bool, 4> compositeCaptured_{};
    std::string compositeCaptureMessage_{};
    std::array<char, 128> actionFilter_{};
    std::array<char, 128> monitorFilter_{};
    std::array<char, 128> newActionId_{};
    std::array<char, 128> newActionName_{};
    std::array<char, 128> newContextId_{};
    std::array<char, 128> newContextName_{};
    int newActionType_ = 0;
    std::string statusMessage_{};
    bool statusIsError_ = false;
    bool dirty_ = false;
    bool bindingPopupRequested_ = false;
    bool bindingApplyPending_ = false;
    bool compositePopupRequested_ = false;
    bool compositeReplaceConflicts_ = true;
    int compositeCaptureStep_ = -1;
    RebindOwner rebindOwner_ = RebindOwner::None;
    bool addActionRequested_ = false;
    bool addContextRequested_ = false;
    bool deleteActionRequested_ = false;
    bool deleteContextRequested_ = false;
    bool reloadRequested_ = false;
    bool restoreRequested_ = false;
    bool showOnlyActiveActions_ = false;
};

} // namespace HIKARI
