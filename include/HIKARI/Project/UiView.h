#pragma once

#include <cstddef>
#include <string_view>

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/UiTypes.h"
#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

struct UiAnimationStatus {
    float seconds = 0, duration = 0;
    bool playing = false, closing = false;
};

struct GameProjectSdkAccess;

// A frame-local, non-owning command view. Paths are consumed
// synchronously and must resolve below the active project's Assets root.
class UiView {
public:
    UiView() = default;

    bool IsValid() const noexcept { return world_ != nullptr; }

    HIKARI_GAME_PROJECT_SDK_API bool SubmitSprite(std::string_view projectRelativeAssetPath,
                                                  const GameProjectUiSpriteRequest& request) const noexcept;

    HIKARI_GAME_PROJECT_SDK_API bool SubmitText(std::string_view projectRelativeFontAssetPath,
                                                std::string_view utf8Text,
                                                const GameProjectUiTextRequest& request) const noexcept;

    HIKARI_GAME_PROJECT_SDK_API bool SubmitBackdrop(const GameProjectUiBackdropRequest& request) const noexcept;
    // Returns true only during the frame in which the Scene-authored
    // button on this object was activated. The object must belong to this
    // World and callers must remain on the runtime owner thread.
    HIKARI_GAME_PROJECT_SDK_API bool WasActivated(GameProjectObjectHandle object) const noexcept;
    // Runtime-only bindings for Scene-authored controls. SetText is a no-op
    // when the resolved UTF-8 text is unchanged; projects own localization.
    HIKARI_GAME_PROJECT_SDK_API bool SetText(GameProjectObjectHandle object, std::string_view utf8Text) const noexcept;
    // Copies the editable text value without exposing engine-owned storage. Required bytes excludes the terminator.
    HIKARI_GAME_PROJECT_SDK_API bool CopyTextFieldText(GameProjectObjectHandle object, char* destination,
                                                       size_t capacity, size_t* requiredBytes) const noexcept;
    // Binds an explicit scoped UI source to another object in this World. Pass an invalid target to clear it.
    HIKARI_GAME_PROJECT_SDK_API bool SetBindingSource(GameProjectObjectHandle source,
                                                      GameProjectObjectHandle target) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool SetCanvasEnabled(GameProjectObjectHandle object, bool enabled) const noexcept;
    // Menu canvases receive input automatically while visible. This runtime-only override can suspend or resume it.
    HIKARI_GAME_PROJECT_SDK_API bool SetCanvasInputActive(GameProjectObjectHandle object, bool active) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool SetButtonInteractable(GameProjectObjectHandle object,
                                                           bool interactable) const noexcept;
    // Requests focus for a focusable control in an active Menu Canvas. The control handle must belong to this World.
    HIKARI_GAME_PROJECT_SDK_API bool RequestFocus(GameProjectObjectHandle control) const noexcept;
    // Returns an invalid handle when no Menu control currently owns focus.
    HIKARI_GAME_PROJECT_SDK_API GameProjectObjectHandle GetFocusedControl() const noexcept;
    // Owner-thread runtime changes; never modify the saved scene. Missing
    // components, foreign/stale handles and nonfinite numbers return false.
    // Opacity and progress values are clamped by the authored range.
    HIKARI_GAME_PROJECT_SDK_API bool SetGroupVisible(GameProjectObjectHandle object, bool visible) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool SetGroupOpacity(GameProjectObjectHandle object, float opacity) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool SetProgressValue(GameProjectObjectHandle object, float value) const noexcept;
    // Uses the current resolved viewport size and clamps against the known content extent.
    HIKARI_GAME_PROJECT_SDK_API bool SetScrollOffset(GameProjectObjectHandle object, float offset) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool GetScrollOffset(GameProjectObjectHandle object, float& offset) const noexcept;
    // Plays a named clip from the object's authored UiAnimator asset.
    // One playback per animator. Child animators are independent instances.
    // Non-looping clips hold their final pose until StopAnimation or replacement.
    HIKARI_GAME_PROJECT_SDK_API bool PlayAnimation(GameProjectObjectHandle object,
                                                   std::string_view clip) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool SeekAnimation(GameProjectObjectHandle object, float seconds) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool PauseAnimation(GameProjectObjectHandle object, bool paused) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool StopAnimation(GameProjectObjectHandle object) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool GetAnimationStatus(GameProjectObjectHandle object,
                                                        UiAnimationStatus& status) const noexcept;

private:
    friend struct GameProjectSdkAccess;

    explicit UiView(void* world) noexcept : world_(world) {}

    void* world_ = nullptr;
};

} // namespace HIKARI
