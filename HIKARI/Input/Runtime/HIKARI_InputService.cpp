#include "Input/Runtime/HIKARI_InputService.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Core/HIKARI_Logger.h"
#include "Input/Assets/HIKARI_InputActionMapJson.h"
#include "Input/Platform/HIKARI_IInputBackend.h"
#include "Input/Platform/HIKARI_Win32InputBackend.h"

namespace HIKARI::INPUT {
namespace {

std::string PhysicalControlKey(const InputBinding& binding) {
    return std::to_string(static_cast<int>(binding.source)) + ":" +
        binding.control;
}

float ApplyDeadZone(float value, float deadZone) {
    const float magnitude = std::abs(value);
    if (magnitude <= deadZone) return 0.0f;
    if (deadZone <= 0.0f) return value;
    const float normalized =
        (magnitude - deadZone) / (1.0f - deadZone);
    return std::copysign((std::min)(normalized, 1.0f), value);
}

float ActionMagnitude(const InputActionState& state) {
    if (state.valueType == InputActionValueType::Axis2D) {
        return std::sqrt(state.x * state.x + state.y * state.y);
    }
    return std::abs(state.x);
}

} // namespace

InputService::InputService()
    : backend_(std::make_unique<Win32InputBackend>()) {
}

InputService::~InputService() {
    Shutdown();
}

bool InputService::Initialize(const std::filesystem::path& projectRoot) {
    inputDirectory_ = projectRoot / "ProjectSettings" / "Input";
    if (!ReloadProject(&lastError_)) {
        actionMap_ = CreateDefaultInputActionMap();
        contexts_.ResetToDefaults(actionMap_);
        if (!SaveProject(&lastError_)) {
            HIKARI_LOG_ERROR("[Input] " + lastError_);
            return false;
        }
        HIKARI_LOG_WARN(
            "[Input] project maps were missing or invalid; defaults were created.");
    }
    snapshot_ = {};
    rebindOperation_.Reset();
    rebindPreviousDevices_ = {};
    suppressActionsUntilNeutral_ = false;
    if (backend_) backend_->Reset();
    HIKARI_LOG_INFO(
        "[Input] initialized from " + inputDirectory_.generic_string());
    return true;
}

void InputService::Shutdown() {
    StopGamepadVibration();
    if (backend_) backend_->Reset();
    snapshot_ = {};
    rebindOperation_.Reset();
    rebindPreviousDevices_ = {};
    suppressActionsUntilNeutral_ = false;
}

void InputService::SetBackend(std::unique_ptr<IInputBackend> backend) {
    if (backend) backend_ = std::move(backend);
}

void InputService::SetHostWindow(void* nativeWindow) {
    if (backend_) backend_->SetHostWindow(nativeWindow);
}

void InputService::SetMouseCaptureMode(MouseCaptureMode mode) {
    if (backend_) backend_->SetMouseCaptureMode(mode);
}

void InputService::SetMouseCaptureRegion(const MouseCaptureRegion& region) {
    if (backend_) backend_->SetMouseCaptureRegion(region);
}

void InputService::ClearMouseCaptureRegion() {
    if (backend_) backend_->ClearMouseCaptureRegion();
}

MouseCaptureMode InputService::GetMouseCaptureMode() const noexcept {
    return backend_
        ? backend_->GetMouseCaptureMode()
        : MouseCaptureMode::Free;
}

void InputService::SetExternalMouseWheel(float delta) {
    if (backend_) backend_->SetExternalMouseWheel(delta);
}

void InputService::Update(float unscaledDeltaSeconds) {
    if (!backend_) return;
    const InputDeviceKind previousDevice = snapshot_.devices_.lastActiveDevice;
    backend_->Poll(snapshot_.devices_);
    if (snapshot_.devices_.lastActiveDevice == InputDeviceKind::None) {
        snapshot_.devices_.lastActiveDevice = previousDevice;
    }
    ++snapshot_.frameIndex_;
    if (rebindOperation_.IsActive()) {
        std::vector<InputControlActuation> candidates;
        const InputRebindOptions& options = rebindOperation_.GetOptions();
        backend_->EnumerateNewControls(
            rebindPreviousDevices_, snapshot_.devices_,
            options.axisThreshold, options.allowMouseMotion, candidates);
        const bool allControlsReleased = !backend_->IsAnyControlActive(
            snapshot_.devices_, options.releaseThreshold);
        rebindOperation_.Update(
            (std::max)(unscaledDeltaSeconds, 0.0f),
            allControlsReleased, candidates);
        rebindPreviousDevices_ = snapshot_.devices_;
    }
    if (suppressActionsUntilNeutral_ && !rebindOperation_.IsActive() &&
        !backend_->IsAnyControlActive(
            snapshot_.devices_, rebindReleaseThreshold_)) {
        suppressActionsUntilNeutral_ = false;
    }
    if (suppressActionsUntilNeutral_) {
        SuppressActions();
    } else {
        EvaluateActions((std::max)(unscaledDeltaSeconds, 0.0f));
    }

    if (vibrationActive_ && vibrationRemaining_ > 0.0f) {
        vibrationRemaining_ -= (std::max)(unscaledDeltaSeconds, 0.0f);
        if (vibrationRemaining_ <= 0.0f) StopGamepadVibration();
    }
}

bool InputService::SaveProject(std::string* errorMessage) {
    std::string error;
    if (!SaveInputActionMapProject(inputDirectory_, actionMap_, &error)) {
        lastError_ = error;
        if (errorMessage) *errorMessage = error;
        return false;
    }
    lastError_.clear();
    if (errorMessage) errorMessage->clear();
    return true;
}

bool InputService::ReloadProject(std::string* errorMessage) {
    InputActionMap loaded{};
    std::string error;
    if (!LoadInputActionMapProject(inputDirectory_, loaded, &error)) {
        lastError_ = error;
        if (errorMessage) *errorMessage = error;
        return false;
    }
    actionMap_ = std::move(loaded);
    contexts_.ResetToDefaults(actionMap_);
    lastError_.clear();
    if (errorMessage) errorMessage->clear();
    return true;
}

bool InputService::RestoreDefaults(std::string* errorMessage) {
    actionMap_ = CreateDefaultInputActionMap();
    contexts_.ResetToDefaults(actionMap_);
    return SaveProject(errorMessage);
}

bool InputService::BeginRebind(const InputRebindOptions& options) {
    if (!backend_ || rebindOperation_.IsActive()) return false;
    rebindPreviousDevices_ = snapshot_.devices_;
    rebindOperation_.Begin(options);
    rebindReleaseThreshold_ = rebindOperation_.GetOptions().releaseThreshold;
    suppressActionsUntilNeutral_ = options.suppressMappedActions;
    if (suppressActionsUntilNeutral_) SuppressActions();
    return true;
}

void InputService::CancelRebind() {
    rebindOperation_.Cancel();
}

void InputService::ResetRebind() {
    rebindOperation_.Reset();
    rebindPreviousDevices_ = snapshot_.devices_;
}

bool InputService::ConsumeRebindResult(InputRebindResult& outResult) {
    return rebindOperation_.ConsumeResult(outResult);
}

bool InputService::SetGamepadVibration(
    float lowFrequency,
    float highFrequency,
    float durationSeconds) {
    if (!backend_ || !backend_->SetGamepadVibration(
            ResolveGamepadIndex(), lowFrequency, highFrequency)) {
        return false;
    }
    vibrationRemaining_ = (std::max)(durationSeconds, 0.0f);
    vibrationActive_ = lowFrequency > 0.0f || highFrequency > 0.0f;
    return true;
}

void InputService::StopGamepadVibration() {
    if (backend_) {
        (void)backend_->SetGamepadVibration(
            ResolveGamepadIndex(), 0.0f, 0.0f);
    }
    vibrationRemaining_ = 0.0f;
    vibrationActive_ = false;
}

float InputService::ReadBindingValue(const InputBinding& binding) const {
    if (!backend_ || !IsModifierDown(binding.modifierControl)) return 0.0f;
    const float raw = backend_->ReadControl(
        snapshot_.devices_, binding.source, binding.control,
        ResolveGamepadIndex());
    return ApplyDeadZone(raw, binding.deadZone) * binding.scale;
}

bool InputService::IsModifierDown(const std::string& modifier) const {
    if (modifier.empty()) return true;
    InputBindingSource source = InputBindingSource::Keyboard;
    std::string_view control = modifier;
    constexpr std::string_view mousePrefix = "Mouse:";
    constexpr std::string_view gamepadPrefix = "Gamepad:";
    if (control.rfind(mousePrefix, 0) == 0) {
        source = InputBindingSource::MouseButton;
        control.remove_prefix(mousePrefix.size());
    } else if (control.rfind(gamepadPrefix, 0) == 0) {
        source = InputBindingSource::GamepadButton;
        control.remove_prefix(gamepadPrefix.size());
    }
    return backend_ && backend_->ReadControl(
        snapshot_.devices_, source, control, ResolveGamepadIndex()) > 0.5f;
}

uint32_t InputService::ResolveGamepadIndex() const {
    return primaryUser_.GetGamepadIndex().value_or(0u);
}

void InputService::EvaluateActions(float dt) {
    const auto previousActions = snapshot_.actions_;
    snapshot_.actions_.clear();
    for (const InputActionDefinition& action : actionMap_.actions) {
        InputActionState state{};
        state.valueType = action.valueType;
        snapshot_.actions_.emplace(action.actionId, state);
    }

    std::vector<const InputContextDefinition*> activeContexts;
    for (const std::string& contextId : contexts_.GetActiveContexts()) {
        if (const InputContextDefinition* context =
                actionMap_.FindContext(contextId)) {
            activeContexts.push_back(context);
        }
    }
    std::stable_sort(activeContexts.begin(), activeContexts.end(),
        [](const InputContextDefinition* lhs,
           const InputContextDefinition* rhs) {
            return lhs->priority > rhs->priority;
        });

    std::unordered_set<std::string> consumedControls;
    for (const InputContextDefinition* context : activeContexts) {
        for (const InputBinding& binding : context->bindings) {
            const std::string controlKey = PhysicalControlKey(binding);
            if (consumedControls.contains(controlKey)) continue;
            auto stateIt = snapshot_.actions_.find(binding.actionId);
            if (stateIt == snapshot_.actions_.end()) continue;
            const float value = ReadBindingValue(binding);
            if (std::abs(value) <= 1e-6f) continue;

            InputActionState& state = stateIt->second;
            if (state.valueType == InputActionValueType::Button) {
                state.x = (std::max)(state.x, std::abs(value));
            } else if (state.valueType == InputActionValueType::Axis2D &&
                       binding.component == 1) {
                state.y += value;
            } else {
                state.x += value;
            }
            if (context->consumeInput) consumedControls.insert(controlKey);
        }
    }

    for (const InputActionDefinition& action : actionMap_.actions) {
        InputActionState& state = snapshot_.actions_.at(action.actionId);
        if (action.clampValue) {
            if (state.valueType == InputActionValueType::Axis2D) {
                const float magnitude = ActionMagnitude(state);
                if (magnitude > 1.0f) {
                    state.x /= magnitude;
                    state.y /= magnitude;
                }
            } else {
                state.x = (std::clamp)(state.x, -1.0f, 1.0f);
            }
        }
        const bool wasDown = [&] {
            const auto it = previousActions.find(action.actionId);
            return it != previousActions.end() && it->second.down;
        }();
        state.down = ActionMagnitude(state) >
            (state.valueType == InputActionValueType::Button ? 0.5f : 1e-4f);
        state.pressed = state.down && !wasDown;
        state.released = !state.down && wasDown;
        if (state.down) {
            const auto previous = previousActions.find(action.actionId);
            state.heldSeconds = previous != previousActions.end()
                ? previous->second.heldSeconds + dt : dt;
        }
    }
}

void InputService::SuppressActions() {
    const auto previousActions = snapshot_.actions_;
    snapshot_.actions_.clear();
    for (const InputActionDefinition& action : actionMap_.actions) {
        InputActionState state{};
        state.valueType = action.valueType;
        const auto previous = previousActions.find(action.actionId);
        state.released = previous != previousActions.end() &&
            previous->second.down;
        snapshot_.actions_.emplace(action.actionId, state);
    }
}

} // namespace HIKARI::INPUT
