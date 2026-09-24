#include "Gameplay/Dialogue/HIKARI_DialogueSessionService.h"
#include "Gameplay/Dialogue/HIKARI_DialoguePresentation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

#include "Assets/HIKARI_DocumentAssetStores.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Runtime/Services/HIKARI_ActionTimeDilationService.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "HIKARI/Project/FrameContext.h"
#include "Core/HIKARI_Logger.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"

namespace HIKARI::DIALOGUE {
namespace {
constexpr uint64_t kDialogueTimeSource = 0x4449414C4F475545ull;
constexpr int32_t kDialogueTimePriority = 20000;
constexpr std::string_view kDialogueContext = "Dialogue";
const std::string kAdvanceAction = "Dialogue.Advance";
const std::string kUpAction = "Dialogue.Up";
const std::string kDownAction = "Dialogue.Down";
const std::string kCancelAction = "Dialogue.Cancel";

size_t CountUtf8Codepoints(std::string_view text) noexcept {
    size_t count = 0u;
    for (unsigned char value : text) {
        if ((value & 0xC0u) != 0x80u)
            ++count;
    }
    return count;
}

uint64_t PresentationSource(RuntimeObjectHandle trigger, uint64_t salt) noexcept {
    return (trigger.ToValue() ^ salt) | 1u;
}

} // namespace

bool DialogueSessionService::Start(World& world, const AssetGuid& assetGuid, std::string locale,
                                   RuntimeObjectHandle trigger, RuntimeObjectHandle interactor, bool pauseWorld) {
    if (active_ || !assetGuid.IsValid() || !trigger.IsValid() || !interactor.IsValid()) {
        return false;
    }
    DialogueAssetStore* store = world.Services().Find<DialogueAssetStore>();
    if (store == nullptr) {
        return false;
    }
    std::string error{};
    std::shared_ptr<const DialogueAsset> asset = store->Load(assetGuid, &error);
    if (!asset) {
        CORE::LogMessage(CORE::LogLevel::Warn, "Dialogue", error, assetGuid.value);
        return false;
    }
    std::shared_ptr<const DialogueProjectStyle> style = store->LoadProjectStyle(&error);
    if (!style) {
        CORE::LogMessage(CORE::LogLevel::Warn, "Dialogue", error, assetGuid.value);
        return false;
    }

    asset_ = std::move(asset);
    style_ = std::move(style);
    locale_ = locale.empty()
                  ? (!style_->fallbackLocale.empty() ? style_->fallbackLocale : asset_->definition.fallbackLocale)
                  : std::move(locale);
    trigger_ = trigger;
    interactor_ = interactor;
    currentNodeId_.clear();
    sessionElapsed_ = 0.0f;
    nodeElapsed_ = 0.0f;
    forcedRevealAmount_ = 0.0f;
    selectedChoice_ = 0u;
    leftPortrait_ = {};
    rightPortrait_ = {};
    inputArmed_ = false;
    active_ = true;

    INPUT::InputService& input = SERVICES::GetInputService();
    if (!input.Contexts().IsActive(kDialogueContext)) {
        pushedInputContext_ = input.Contexts().Push(std::string(kDialogueContext));
    }
    if (pauseWorld) {
        timeDilationToken_ = SERVICES::GetActionTimeDilationService().Request(&world, kDialogueTimeSource,
                                                                              kDialogueTimePriority, 0.0f, 0.0f);
        if (timeDilationToken_ == 0u) {
            ReleaseOwnership(&world);
            asset_.reset();
            style_.reset();
            active_ = false;
            return false;
        }
    }
    PublishEvent(world, DialogueRuntimeEvent{
                            DialogueRuntimeEventType::Started,
                            trigger_,
                            interactor_,
                            assetGuid.value,
                            asset_->definition.entryNodeId,
                            {},
                        });
    if (!EnterNode(world, asset_->definition.entryNodeId) || !ResolveAutomaticNodes(world)) {
        if (active_)
            Stop(world, "invalid-entry");
        return false;
    }
    return active_;
}

bool DialogueSessionService::EnterNode(World& world, std::string nodeId) {
    if (!asset_ || nodeId.empty() || FindDialogueNode(asset_->definition, nodeId) == nullptr) {
        return false;
    }
    currentNodeId_ = std::move(nodeId);
    nodeElapsed_ = 0.0f;
    forcedRevealAmount_ = 0.0f;
    selectedChoice_ = 0u;
    if (const DialogueNode* node = FindDialogueNode(asset_->definition, currentNodeId_)) {
        ApplyPortraitNode(*node);
    }
    PublishEvent(world, DialogueRuntimeEvent{
                            DialogueRuntimeEventType::NodeEntered,
                            trigger_,
                            interactor_,
                            asset_->guid.value,
                            currentNodeId_,
                            {},
                        });
    return true;
}

void DialogueSessionService::ApplyPortraitNode(const DialogueNode& node) noexcept {
    if (!style_)
        return;

    auto findSpeakerSlot = [&](std::string_view speakerId) -> PortraitSlotRuntime* {
        if (!speakerId.empty() && leftPortrait_.speakerId == speakerId) {
            return &leftPortrait_;
        }
        if (!speakerId.empty() && rightPortrait_.speakerId == speakerId) {
            return &rightPortrait_;
        }
        return nullptr;
    };
    auto findVisualSlot = [&](const DialogueSpeakerProfile& speaker) -> PortraitSlotRuntime* {
        const DialoguePortraitVisualIdentity identity{
            speaker.portraitAssetGuid,
            speaker.resolvedPortraitAssetPath,
            speaker.offsetX,
            speaker.offsetY,
            speaker.scale,
            speaker.rotationDegrees,
            speaker.mirror,
        };
        const std::array<PortraitSlotRuntime*, 2u> slots{&leftPortrait_, &rightPortrait_};
        for (PortraitSlotRuntime* slot : slots) {
            if (slot->speakerId.empty() && slot->assetGuid.empty() && slot->assetPath.empty()) {
                continue;
            }
            const DialoguePortraitVisualIdentity existing{
                slot->assetGuid, slot->assetPath,       slot->offsetX, slot->offsetY,
                slot->scale,     slot->rotationDegrees, slot->mirror,
            };
            if (SameDialoguePortraitVisualIdentity(existing, identity)) {
                return slot;
            }
        }
        return nullptr;
    };
    const auto dimBoth = [&]() {
        leftPortrait_.targetFocus = 0.0f;
        rightPortrait_.targetFocus = 0.0f;
    };

    if (node.portraitAction == DialoguePortraitAction::Hide) {
        PortraitSlotRuntime* target = findSpeakerSlot(node.speakerId);
        if (target == nullptr) {
            if (const DialogueSpeakerProfile* speaker = FindDialogueSpeaker(*style_, node.speakerId)) {
                target = findVisualSlot(*speaker);
            }
        }
        if (target == nullptr) {
            if (node.portraitSlot == DialoguePortraitSlot::Left) {
                target = &leftPortrait_;
            } else if (node.portraitSlot == DialoguePortraitSlot::Right) {
                target = &rightPortrait_;
            }
        }
        if (target != nullptr) {
            target->targetPresence = 0.0f;
            target->targetFocus = 0.0f;
        }
        return;
    }
    if (node.type != DialogueNodeType::Line || node.speakerId.empty()) {
        return;
    }

    const DialogueSpeakerProfile* speaker = FindDialogueSpeaker(*style_, node.speakerId);
    PortraitSlotRuntime* target = findSpeakerSlot(node.speakerId);
    bool reusedVisualSlot = false;
    // A localized/display-name change can use a different speaker id
    // while retaining the same visual source. Reuse that slot in place;
    // the current node still owns the speaker label presented below.
    if (target == nullptr && speaker != nullptr) {
        target = findVisualSlot(*speaker);
        reusedVisualSlot = target != nullptr;
    }
    if (node.portraitAction == DialoguePortraitAction::Keep) {
        dimBoth();
        if (target != nullptr)
            target->targetFocus = 1.0f;
        return;
    }
    if (speaker == nullptr || speaker->resolvedPortraitAssetPath.empty()) {
        dimBoth();
        return;
    }

    DialoguePortraitSlot desired = node.portraitSlot;
    if (desired == DialoguePortraitSlot::Auto) {
        desired = speaker->preferredSlot;
    }
    if (target == nullptr) {
        if (desired == DialoguePortraitSlot::Left) {
            target = &leftPortrait_;
        } else if (desired == DialoguePortraitSlot::Right) {
            target = &rightPortrait_;
        } else if (leftPortrait_.speakerId.empty() || leftPortrait_.targetPresence <= 0.0f) {
            target = &leftPortrait_;
        } else if (rightPortrait_.speakerId.empty() || rightPortrait_.targetPresence <= 0.0f) {
            target = &rightPortrait_;
        } else {
            target = leftPortrait_.focus <= rightPortrait_.focus ? &leftPortrait_ : &rightPortrait_;
        }
        target->speakerId = node.speakerId;
        target->assetGuid = speaker->portraitAssetGuid;
        target->assetPath = speaker->resolvedPortraitAssetPath;
        target->offsetX = speaker->offsetX;
        target->offsetY = speaker->offsetY;
        target->scale = speaker->scale;
        target->rotationDegrees = speaker->rotationDegrees;
        target->mirror = speaker->mirror;
        target->presence = 0.0f;
        target->focus = 0.0f;
    } else if (target != nullptr && speaker != nullptr && !reusedVisualSlot) {
        // The same speaker id may intentionally switch to another
        // portrait asset; keep the existing slot but refresh its visual
        // identity so a later alias can still be matched correctly.
        target->assetGuid = speaker->portraitAssetGuid;
        target->assetPath = speaker->resolvedPortraitAssetPath;
        target->offsetX = speaker->offsetX;
        target->offsetY = speaker->offsetY;
        target->scale = speaker->scale;
        target->rotationDegrees = speaker->rotationDegrees;
        target->mirror = speaker->mirror;
    }
    target->targetPresence = 1.0f;
    dimBoth();
    target->targetFocus = 1.0f;
}

void DialogueSessionService::UpdatePortraits(float unscaledDt) noexcept {
    if (!style_)
        return;
    const auto approach = [unscaledDt](float current, float target, float seconds) noexcept {
        if (unscaledDt <= 0.0f)
            return current;
        const float amount = std::clamp(unscaledDt / (std::max)(seconds, 0.001f), 0.0f, 1.0f);
        return current + (target - current) * amount;
    };
    const auto update = [&](PortraitSlotRuntime& slot) {
        const float presenceSeconds = slot.targetPresence >= slot.presence ? style_->portraitTransition.enterSeconds
                                                                           : style_->portraitTransition.exitSeconds;
        slot.presence = approach(slot.presence, slot.targetPresence, presenceSeconds);
        slot.focus = approach(slot.focus, slot.targetFocus, style_->portraitTransition.focusSeconds);
        if (slot.targetPresence <= 0.0f && slot.presence <= 0.001f) {
            slot = {};
        }
    };
    update(leftPortrait_);
    update(rightPortrait_);
}

bool DialogueSessionService::ResolveAutomaticNodes(World& world) {
    for (size_t guard = 0u; guard < 64u && active_; ++guard) {
        const DialogueNode* node = FindDialogueNode(asset_->definition, currentNodeId_);
        if (node == nullptr)
            return false;
        if (node->type == DialogueNodeType::Event) {
            PublishEvent(world, DialogueRuntimeEvent{DialogueRuntimeEventType::EventEmitted, trigger_, interactor_,
                                                     asset_->guid.value, currentNodeId_, node->eventId});
            if (!EnterNode(world, node->nextNodeId))
                return false;
            continue;
        }
        if (node->type == DialogueNodeType::End) {
            Stop(world, "completed");
        }
        return true;
    }
    return !active_;
}

float DialogueSessionService::CurrentRevealAmount() const noexcept {
    if (!asset_ || !active_)
        return 0.0f;
    const DialogueNode* node = FindDialogueNode(asset_->definition, currentNodeId_);
    if (node == nullptr)
        return 0.0f;
    const std::string_view text = ResolveLocalizedText(node->text, locale_, asset_->definition.fallbackLocale);
    const size_t count = (std::max)(CountUtf8Codepoints(text), size_t{1u});
    const DialogueTheme& theme = style_->theme;
    const float authored = nodeElapsed_ * theme.charactersPerSecond / static_cast<float>(count);
    return std::clamp((std::max)(authored, forcedRevealAmount_), 0.0f, 1.0f);
}

void DialogueSessionService::Update(World& world, const FrameContext& frame) {
    if (!active_ || !asset_)
        return;
    const float dt = std::isfinite(frame.unscaledDt) ? (std::max)(frame.unscaledDt, 0.0f) : 0.0f;
    sessionElapsed_ += dt;
    nodeElapsed_ += dt;
    UpdatePortraits(dt);

    const INPUT::InputSnapshot& snapshot = SERVICES::GetInputService().GetSnapshot();
    const bool anyDown = snapshot.IsDown(kAdvanceAction) || snapshot.IsDown(kUpAction) ||
                         snapshot.IsDown(kDownAction) || snapshot.IsDown(kCancelAction);
    if (!inputArmed_) {
        inputArmed_ = !anyDown;
        return;
    }
    if (snapshot.IsPressed(kCancelAction)) {
        Stop(world, "cancelled");
        return;
    }

    const DialogueNode* node = FindDialogueNode(asset_->definition, currentNodeId_);
    if (node == nullptr) {
        Stop(world, "missing-node");
        return;
    }
    if (node->type == DialogueNodeType::Choice) {
        if (!node->choices.empty()) {
            if (snapshot.IsPressed(kUpAction)) {
                selectedChoice_ = selectedChoice_ == 0u ? node->choices.size() - 1u : selectedChoice_ - 1u;
            }
            if (snapshot.IsPressed(kDownAction)) {
                selectedChoice_ = (selectedChoice_ + 1u) % node->choices.size();
            }
            if (snapshot.IsPressed(kAdvanceAction)) {
                const DialogueChoice& choice = node->choices[selectedChoice_];
                PublishEvent(world, DialogueRuntimeEvent{DialogueRuntimeEventType::ChoiceSelected, trigger_,
                                                         interactor_, asset_->guid.value, currentNodeId_, choice.id});
                if (!EnterNode(world, choice.nextNodeId) || !ResolveAutomaticNodes(world)) {
                    if (active_)
                        Stop(world, "invalid-choice-target");
                }
            }
        }
        return;
    }
    if (node->type != DialogueNodeType::Line)
        return;
    const float reveal = CurrentRevealAmount();
    const bool advance = snapshot.IsPressed(kAdvanceAction);
    if (advance && reveal < 1.0f && node->skippable) {
        forcedRevealAmount_ = 1.0f;
        return;
    }
    const bool autoAdvance =
        node->autoAdvanceSeconds > 0.0f && reveal >= 1.0f && nodeElapsed_ >= node->autoAdvanceSeconds;
    if ((advance && reveal >= 1.0f) || autoAdvance) {
        if (!EnterNode(world, node->nextNodeId) || !ResolveAutomaticNodes(world)) {
            if (active_)
                Stop(world, "invalid-next-target");
        }
    }
}

void DialogueSessionService::SubmitPresentation(World& world, const FrameContext&) {
    if (!active_ || !asset_)
        return;
    GAME_UI::GameUiPresentationService* ui = world.Services().Find<GAME_UI::GameUiPresentationService>();
    const DialogueNode* node = FindDialogueNode(asset_->definition, currentNodeId_);
    if (ui == nullptr || node == nullptr)
        return;

    DialoguePresentationState state{};
    state.definition = &asset_->definition;
    state.style = style_.get();
    state.node = node;
    state.locale = locale_;
    state.sourceIdBase = PresentationSource(trigger_, 0xD1A1060Eull);
    state.selectedChoice = selectedChoice_;
    state.sessionElapsed = sessionElapsed_;
    state.revealAmount = CurrentRevealAmount();
    state.useGamepadAdvancePrompt =
        SERVICES::GetInputService().GetSnapshot().GetLastActiveDevice() == INPUT::InputDeviceKind::Gamepad;
    state.leftPortrait =
        DialoguePortraitPresentationState{leftPortrait_.speakerId, leftPortrait_.presence, leftPortrait_.focus};
    state.rightPortrait =
        DialoguePortraitPresentationState{rightPortrait_.speakerId, rightPortrait_.presence, rightPortrait_.focus};
    (void)SubmitDialoguePresentation(*ui, state);
}
void DialogueSessionService::ReleaseOwnership(World* world) noexcept {
    if (timeDilationToken_ != 0u) {
        if (world != nullptr) {
            (void)SERVICES::GetActionTimeDilationService().Release(world, timeDilationToken_);
        } else {
            (void)SERVICES::GetActionTimeDilationService().Release(timeDilationToken_);
        }
        timeDilationToken_ = 0u;
    }
    if (pushedInputContext_) {
        (void)SERVICES::GetInputService().Contexts().Pop(kDialogueContext);
        pushedInputContext_ = false;
    }
}

void DialogueSessionService::Stop(World& world, std::string reason) {
    if (!active_)
        return;
    PublishEvent(world,
                 DialogueRuntimeEvent{DialogueRuntimeEventType::Ended, trigger_, interactor_,
                                      asset_ ? asset_->guid.value : std::string{}, currentNodeId_, std::move(reason)});
    ReleaseOwnership(&world);
    asset_.reset();
    style_.reset();
    locale_.clear();
    currentNodeId_.clear();
    trigger_ = {};
    interactor_ = {};
    sessionElapsed_ = 0.0f;
    nodeElapsed_ = 0.0f;
    forcedRevealAmount_ = 0.0f;
    selectedChoice_ = 0u;
    leftPortrait_ = {};
    rightPortrait_ = {};
    inputArmed_ = false;
    active_ = false;
}

void DialogueSessionService::PublishEvent(World& world, DialogueRuntimeEvent event) {
    const DialogueRuntimeEvent& queued = eventQueue_.Push(std::move(event));
    world.FrameEvents().Publish(queued);
}

void DialogueSessionService::Reset(World* world) noexcept {
    ReleaseOwnership(world);
    asset_.reset();
    style_.reset();
    locale_.clear();
    currentNodeId_.clear();
    trigger_ = {};
    interactor_ = {};
    sessionElapsed_ = 0.0f;
    nodeElapsed_ = 0.0f;
    forcedRevealAmount_ = 0.0f;
    selectedChoice_ = 0u;
    leftPortrait_ = {};
    rightPortrait_ = {};
    eventQueue_.Reset();
    inputArmed_ = false;
    active_ = false;
}

} // namespace HIKARI::DIALOGUE
