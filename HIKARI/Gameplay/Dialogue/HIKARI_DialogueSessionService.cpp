#include "Gameplay/Dialogue/HIKARI_DialogueSessionService.h"
#include "Gameplay/Dialogue/HIKARI_DialoguePresentation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

#include "Assets/Dialogue/HIKARI_DialogueAssetStore.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Runtime/Services/HIKARI_ActionTimeDilationService.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Core/HIKARI_FrameContext.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"

namespace HIKARI::DIALOGUE {
    namespace {
        constexpr uint64_t kDialogueTimeSource =
            0x4449414C4F475545ull;
        constexpr int32_t kDialogueTimePriority = 20000;
        constexpr std::string_view kDialogueContext = "Dialogue";
        const std::string kAdvanceAction = "Dialogue.Advance";
        const std::string kUpAction = "Dialogue.Up";
        const std::string kDownAction = "Dialogue.Down";
        const std::string kCancelAction = "Dialogue.Cancel";

        size_t CountUtf8Codepoints(std::string_view text) noexcept {
            size_t count = 0u;
            for (unsigned char value : text) {
                if ((value & 0xC0u) != 0x80u) ++count;
            }
            return count;
        }

        uint64_t PresentationSource(
            RuntimeObjectHandle trigger,
            uint64_t salt) noexcept {
            return (trigger.ToValue() ^ salt) | 1u;
        }

    }

    bool DialogueSessionService::Start(
        World& world,
        const AssetGuid& assetGuid,
        std::string locale,
        RuntimeObjectHandle trigger,
        RuntimeObjectHandle interactor,
        bool pauseWorld) {
        if (active_ || !assetGuid.IsValid() || !trigger.IsValid() ||
            !interactor.IsValid()) {
            return false;
        }
        DialogueAssetStore* store = world.Services().Find<
            DialogueAssetStore>();
        if (store == nullptr) {
            return false;
        }
        std::string error{};
        std::shared_ptr<const DialogueAsset> asset = store->Load(
            assetGuid,
            &error);
        if (!asset) {
            return false;
        }
        std::shared_ptr<const DialogueProjectStyle> style =
            store->LoadProjectStyle(&error);
        if (!style) {
            auto fallback = std::make_shared<DialogueProjectStyle>(
                MakeDefaultDialogueProjectStyle());
            fallback->theme = asset->definition.theme;
            NormalizeDialogueProjectStyle(*fallback);
            style = std::move(fallback);
        }

        asset_ = std::move(asset);
        style_ = std::move(style);
        locale_ = locale.empty()
            ? (!style_->fallbackLocale.empty()
                ? style_->fallbackLocale
                : asset_->definition.fallbackLocale)
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
            pushedInputContext_ = input.Contexts().Push(
                std::string(kDialogueContext));
        }
        if (pauseWorld) {
            timeDilationToken_ = SERVICES::GetActionTimeDilationService().Request(
                &world,
                kDialogueTimeSource,
                kDialogueTimePriority,
                0.0f,
                0.0f);
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
        if (!EnterNode(world, asset_->definition.entryNodeId) ||
            !ResolveAutomaticNodes(world)) {
            if (active_) Stop(world, "invalid-entry");
            return false;
        }
        return active_;
    }

    bool DialogueSessionService::EnterNode(
        World& world,
        std::string nodeId) {
        if (!asset_ || nodeId.empty() ||
            FindDialogueNode(asset_->definition, nodeId) == nullptr) {
            return false;
        }
        currentNodeId_ = std::move(nodeId);
        nodeElapsed_ = 0.0f;
        forcedRevealAmount_ = 0.0f;
        selectedChoice_ = 0u;
        if (const DialogueNode* node = FindDialogueNode(
                asset_->definition,
                currentNodeId_)) {
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

    void DialogueSessionService::ApplyPortraitNode(
        const DialogueNode& node) noexcept {
        if (!style_) return;

        auto findSpeakerSlot = [&](std::string_view speakerId)
            -> PortraitSlotRuntime* {
            if (!speakerId.empty() &&
                leftPortrait_.speakerId == speakerId) {
                return &leftPortrait_;
            }
            if (!speakerId.empty() &&
                rightPortrait_.speakerId == speakerId) {
                return &rightPortrait_;
            }
            return nullptr;
        };
        auto findVisualSlot = [&](const DialogueSpeakerProfile& speaker)
            -> PortraitSlotRuntime* {
            const DialoguePortraitVisualIdentity identity{
                speaker.portraitAssetGuid,
                speaker.resolvedPortraitAssetPath,
                speaker.offsetX,
                speaker.offsetY,
                speaker.scale,
                speaker.rotationDegrees,
                speaker.mirror,
            };
            const std::array<PortraitSlotRuntime*, 2u> slots{
                &leftPortrait_, &rightPortrait_ };
            for (PortraitSlotRuntime* slot : slots) {
                if (slot->speakerId.empty() &&
                    slot->assetGuid.empty() &&
                    slot->assetPath.empty()) {
                    continue;
                }
                const DialoguePortraitVisualIdentity existing{
                    slot->assetGuid,
                    slot->assetPath,
                    slot->offsetX,
                    slot->offsetY,
                    slot->scale,
                    slot->rotationDegrees,
                    slot->mirror,
                };
                if (SameDialoguePortraitVisualIdentity(
                        existing, identity)) {
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
                if (const DialogueSpeakerProfile* speaker =
                        FindDialogueSpeaker(*style_, node.speakerId)) {
                    target = findVisualSlot(*speaker);
                }
            }
            if (target == nullptr) {
                if (node.portraitSlot == DialoguePortraitSlot::Left) {
                    target = &leftPortrait_;
                }
                else if (node.portraitSlot == DialoguePortraitSlot::Right) {
                    target = &rightPortrait_;
                }
            }
            if (target != nullptr) {
                target->targetPresence = 0.0f;
                target->targetFocus = 0.0f;
            }
            return;
        }
        if (node.type != DialogueNodeType::Line ||
            node.speakerId.empty()) {
            return;
        }

        const DialogueSpeakerProfile* speaker = FindDialogueSpeaker(
            *style_,
            node.speakerId);
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
            if (target != nullptr) target->targetFocus = 1.0f;
            return;
        }
        if (speaker == nullptr ||
            speaker->resolvedPortraitAssetPath.empty()) {
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
            }
            else if (desired == DialoguePortraitSlot::Right) {
                target = &rightPortrait_;
            }
            else if (leftPortrait_.speakerId.empty() ||
                     leftPortrait_.targetPresence <= 0.0f) {
                target = &leftPortrait_;
            }
            else if (rightPortrait_.speakerId.empty() ||
                     rightPortrait_.targetPresence <= 0.0f) {
                target = &rightPortrait_;
            }
            else {
                target = leftPortrait_.focus <= rightPortrait_.focus
                    ? &leftPortrait_
                    : &rightPortrait_;
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
        }
        else if (target != nullptr && speaker != nullptr &&
            !reusedVisualSlot) {
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

    void DialogueSessionService::UpdatePortraits(
        float unscaledDt) noexcept {
        if (!style_) return;
        const auto approach = [unscaledDt](
            float current,
            float target,
            float seconds) noexcept {
            if (unscaledDt <= 0.0f) return current;
            const float amount = std::clamp(
                unscaledDt / (std::max)(seconds, 0.001f),
                0.0f,
                1.0f);
            return current + (target - current) * amount;
        };
        const auto update = [&](PortraitSlotRuntime& slot) {
            const float presenceSeconds =
                slot.targetPresence >= slot.presence
                    ? style_->portraitTransition.enterSeconds
                    : style_->portraitTransition.exitSeconds;
            slot.presence = approach(
                slot.presence,
                slot.targetPresence,
                presenceSeconds);
            slot.focus = approach(
                slot.focus,
                slot.targetFocus,
                style_->portraitTransition.focusSeconds);
            if (slot.targetPresence <= 0.0f &&
                slot.presence <= 0.001f) {
                slot = {};
            }
        };
        update(leftPortrait_);
        update(rightPortrait_);
    }

    bool DialogueSessionService::ResolveAutomaticNodes(World& world) {
        for (size_t guard = 0u; guard < 64u && active_; ++guard) {
            const DialogueNode* node = FindDialogueNode(
                asset_->definition,
                currentNodeId_);
            if (node == nullptr) return false;
            if (node->type == DialogueNodeType::Event) {
                PublishEvent(world, DialogueRuntimeEvent{
                    DialogueRuntimeEventType::EventEmitted,
                    trigger_, interactor_, asset_->guid.value,
                    currentNodeId_, node->eventId });
                if (!EnterNode(world, node->nextNodeId)) return false;
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
        if (!asset_ || !active_) return 0.0f;
        const DialogueNode* node = FindDialogueNode(
            asset_->definition,
            currentNodeId_);
        if (node == nullptr) return 0.0f;
        const std::string_view text = ResolveLocalizedText(
            node->text,
            locale_,
            asset_->definition.fallbackLocale);
        const size_t count = (std::max)(CountUtf8Codepoints(text), size_t{ 1u });
        const DialogueTheme& theme = style_
            ? style_->theme
            : asset_->definition.theme;
        const float authored = nodeElapsed_ *
            theme.charactersPerSecond /
            static_cast<float>(count);
        return std::clamp(
            (std::max)(authored, forcedRevealAmount_),
            0.0f,
            1.0f);
    }

    void DialogueSessionService::Update(
        World& world,
        const FrameContext& frame) {
        if (!active_ || !asset_) return;
        const float dt = std::isfinite(frame.unscaledDt)
            ? (std::max)(frame.unscaledDt, 0.0f)
            : 0.0f;
        sessionElapsed_ += dt;
        nodeElapsed_ += dt;
        UpdatePortraits(dt);

        const INPUT::InputSnapshot& snapshot =
            SERVICES::GetInputService().GetSnapshot();
        const bool anyDown = snapshot.IsDown(kAdvanceAction) ||
            snapshot.IsDown(kUpAction) ||
            snapshot.IsDown(kDownAction) ||
            snapshot.IsDown(kCancelAction);
        if (!inputArmed_) {
            inputArmed_ = !anyDown;
            return;
        }
        if (snapshot.IsPressed(kCancelAction)) {
            Stop(world, "cancelled");
            return;
        }

        const DialogueNode* node = FindDialogueNode(
            asset_->definition,
            currentNodeId_);
        if (node == nullptr) {
            Stop(world, "missing-node");
            return;
        }
        if (node->type == DialogueNodeType::Choice) {
            if (!node->choices.empty()) {
                if (snapshot.IsPressed(kUpAction)) {
                    selectedChoice_ = selectedChoice_ == 0u
                        ? node->choices.size() - 1u
                        : selectedChoice_ - 1u;
                }
                if (snapshot.IsPressed(kDownAction)) {
                    selectedChoice_ =
                        (selectedChoice_ + 1u) % node->choices.size();
                }
                if (snapshot.IsPressed(kAdvanceAction)) {
                    const DialogueChoice& choice = node->choices[
                        selectedChoice_];
                    PublishEvent(world, DialogueRuntimeEvent{
                        DialogueRuntimeEventType::ChoiceSelected,
                        trigger_, interactor_, asset_->guid.value,
                        currentNodeId_, choice.id });
                    if (!EnterNode(world, choice.nextNodeId) ||
                        !ResolveAutomaticNodes(world)) {
                        if (active_) Stop(world, "invalid-choice-target");
                    }
                }
            }
            return;
        }
        if (node->type != DialogueNodeType::Line) return;
        const float reveal = CurrentRevealAmount();
        const bool advance = snapshot.IsPressed(kAdvanceAction);
        if (advance && reveal < 1.0f && node->skippable) {
            forcedRevealAmount_ = 1.0f;
            return;
        }
        const bool autoAdvance = node->autoAdvanceSeconds > 0.0f &&
            reveal >= 1.0f &&
            nodeElapsed_ >= node->autoAdvanceSeconds;
        if ((advance && reveal >= 1.0f) || autoAdvance) {
            if (!EnterNode(world, node->nextNodeId) ||
                !ResolveAutomaticNodes(world)) {
                if (active_) Stop(world, "invalid-next-target");
            }
        }
    }

    void DialogueSessionService::SubmitPresentation(
        World& world,
        const FrameContext&) {
        if (!active_ || !asset_) return;
        GAME_UI::GameUiPresentationService* ui = world.Services().Find<
            GAME_UI::GameUiPresentationService>();
        const DialogueNode* node = FindDialogueNode(
            asset_->definition,
            currentNodeId_);
        if (ui == nullptr || node == nullptr) return;

        DialoguePresentationState state{};
        state.definition = &asset_->definition;
        state.style = style_.get();
        state.node = node;
        state.locale = locale_;
        state.sourceIdBase = PresentationSource(
            trigger_,
            0xD1A1060Eull);
        state.selectedChoice = selectedChoice_;
        state.sessionElapsed = sessionElapsed_;
        state.revealAmount = CurrentRevealAmount();
        state.useGamepadAdvancePrompt =
            SERVICES::GetInputService().GetSnapshot().GetLastActiveDevice() ==
            INPUT::InputDeviceKind::Gamepad;
        state.leftPortrait = DialoguePortraitPresentationState{
            leftPortrait_.speakerId,
            leftPortrait_.presence,
            leftPortrait_.focus };
        state.rightPortrait = DialoguePortraitPresentationState{
            rightPortrait_.speakerId,
            rightPortrait_.presence,
            rightPortrait_.focus };
        (void)SubmitDialoguePresentation(*ui, state);
    }
    void DialogueSessionService::ReleaseOwnership(World* world) noexcept {
        if (timeDilationToken_ != 0u) {
            if (world != nullptr) {
                (void)SERVICES::GetActionTimeDilationService().Release(
                    world,
                    timeDilationToken_);
            }
            else {
                (void)SERVICES::GetActionTimeDilationService().Release(
                    timeDilationToken_);
            }
            timeDilationToken_ = 0u;
        }
        if (pushedInputContext_) {
            (void)SERVICES::GetInputService().Contexts().Pop(
                kDialogueContext);
            pushedInputContext_ = false;
        }
    }

    void DialogueSessionService::Stop(
        World& world,
        std::string reason) {
        if (!active_) return;
        PublishEvent(world, DialogueRuntimeEvent{
            DialogueRuntimeEventType::Ended,
            trigger_, interactor_, asset_ ? asset_->guid.value : std::string{},
            currentNodeId_, std::move(reason) });
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

    void DialogueSessionService::PublishEvent(
        World& world,
        DialogueRuntimeEvent event) {
        const DialogueRuntimeEvent& queued = eventQueue_.Push(
            std::move(event));
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

    namespace {
        GameProjectUiColor White(float alpha = 1.0f) noexcept {
            return GameProjectUiColor{ 1.0f, 1.0f, 1.0f, alpha };
        }

        GameProjectUiColor InkBlue(float alpha = 1.0f) noexcept {
            return GameProjectUiColor{
                0.0156863f, 0.1686275f, 0.5058824f, alpha };
        }

        GameProjectUiColor ToUiColor(
            const DialogueColor& color) noexcept {
            return GameProjectUiColor{
                color.r, color.g, color.b, color.a };
        }

        GameProjectUiColor LerpUiColor(
            const DialogueColor& from,
            const DialogueColor& to,
            float amount) noexcept {
            const float t = std::clamp(amount, 0.0f, 1.0f);
            return GameProjectUiColor{
                from.r + (to.r - from.r) * t,
                from.g + (to.g - from.g) * t,
                from.b + (to.b - from.b) * t,
                from.a + (to.a - from.a) * t,
            };
        }

        GAME_UI::GameUiTextEffectStyle ApplyTextStyle(
            GameProjectUiTextRequest& request,
            const DialogueTextStyle& style) noexcept {
            request.textColor = ToUiColor(style.textColor);
            request.outlineColor = ToUiColor(style.outlineColor);
            request.shadowColor = ToUiColor(style.shadowColor);
            request.outlineWidth = style.outlineWidth;
            request.shadowOffsetX = style.shadowOffsetX;
            request.shadowOffsetY = style.shadowOffsetY;
            request.letterSpacing = style.letterSpacing;
            request.lineHeightMultiplier = style.lineHeightMultiplier;
            GAME_UI::GameUiTextEffectStyle result{};
            result.glowColor = ToUiColor(style.glowColor);
            result.glowWidth = style.glowWidth;
            result.glowIntensity = style.glowIntensity;
            return result;
        }
    }

    bool SubmitDialoguePresentation(
        GAME_UI::GameUiPresentationService& service,
        const DialoguePresentationState& state) noexcept {
        try {
            if (state.definition == nullptr || state.node == nullptr ||
                state.sourceIdBase == 0u) {
                return false;
            }
            const DialogueTheme& theme = state.style != nullptr
                ? state.style->theme
                : state.definition->theme;
            const std::string_view fallbackLocale =
                state.style != nullptr &&
                    !state.style->fallbackLocale.empty()
                    ? std::string_view(state.style->fallbackLocale)
                    : std::string_view(state.definition->fallbackLocale);
            const float opening = std::clamp(
                state.sessionElapsed /
                    (std::max)(theme.panelRevealSeconds, 0.01f),
                0.0f,
                1.0f);
            const float easedOpening = 1.0f -
                std::pow(1.0f - opening, 3.0f);
            const uint64_t base = state.sourceIdBase;

            GameProjectUiBackdropRequest backdrop{};
            backdrop.sourceId = base;
            backdrop.priority = 2000;
            backdrop.flags = kGameProjectUiSuppressNonModal;
            backdrop.dimAmount = theme.backdropDim;
            backdrop.blurRadiusPixels = theme.backdropBlur * 12.0f;
            backdrop.desaturation = theme.backdropDesaturation;
            backdrop.vignette = 0.24f;
            backdrop.progress = easedOpening;
            backdrop.tint = GameProjectUiColor{
                0.0156863f, 0.0352941f, 0.1019608f, 1.0f };
            (void)service.SubmitBackdrop(backdrop);

            if (state.style != nullptr) {
                const auto submitPortrait = [&](
                    const DialoguePortraitPresentationState& portrait,
                    const DialoguePortraitSlotStyle& slot,
                    bool left,
                    uint64_t sourceOffset,
                    int32_t order) {
                    if (portrait.speakerId.empty() ||
                        portrait.presence <= 0.001f) {
                        return;
                    }
                    const DialogueSpeakerProfile* speaker =
                        FindDialogueSpeaker(
                            *state.style,
                            portrait.speakerId);
                    if (speaker == nullptr ||
                        speaker->resolvedPortraitAssetPath.empty()) {
                        return;
                    }
                    const float focus = std::clamp(
                        portrait.focus, 0.0f, 1.0f);
                    const float presence = std::clamp(
                        portrait.presence, 0.0f, 1.0f);
                    const auto& transition =
                        state.style->portraitTransition;
                    const float focusScale =
                        transition.listeningScale +
                        (transition.speakingScale -
                            transition.listeningScale) * focus;
                    const float scale = speaker->scale * focusScale;
                    const float width = slot.width * scale;
                    const float height = slot.height * scale;
                    const float slideDirection = left ? -1.0f : 1.0f;

                    GameProjectUiSpriteRequest request{};
                    request.sourceId = base + sourceOffset;
                    request.order = order;
                    request.flags = kGameProjectUiSubmissionModal;
                    request.referenceWidth = theme.referenceWidth;
                    request.referenceHeight = theme.referenceHeight;
                    request.x = slot.x + (slot.width - width) * 0.5f +
                        speaker->offsetX + slideDirection *
                            transition.slideDistance * (1.0f - presence);
                    request.y = slot.y + (slot.height - height) +
                        speaker->offsetY;
                    request.width = width;
                    request.height = height;
                    request.pivotX = 0.5f;
                    request.pivotY = 1.0f;
                    request.rotationRadians =
                        (slot.rotationDegrees + speaker->rotationDegrees) *
                        0.01745329252f;
                    request.fillAmount = focus;
                    request.effectProgress = presence;
                    request.effectIntensity =
                        transition.edgeLightIntensity;
                    request.timeSeconds = state.sessionElapsed;
                    request.directionX = speaker->mirror ? -1.0f : 1.0f;
                    request.tint = LerpUiColor(
                        transition.listeningTint,
                        transition.speakingTint,
                        focus);
                    request.tint.a *= presence * easedOpening;
                    request.effectColor = ToUiColor(
                        transition.edgeLightColor);
                    request.highlightColor = White();
                    request.effect =
                        GameProjectUiEffect::DialoguePortraitFocus;
                    (void)service.SubmitSprite(
                        speaker->resolvedPortraitAssetPath,
                        request);
                };
                submitPortrait(
                    state.leftPortrait,
                    state.style->leftPortrait,
                    true,
                    10u,
                    760);
                submitPortrait(
                    state.rightPortrait,
                    state.style->rightPortrait,
                    false,
                    11u,
                    770);
            }

            GameProjectUiSpriteRequest panel{};
            panel.sourceId = base + 1u;
            panel.order = 900;
            panel.flags = kGameProjectUiSubmissionModal;
            panel.referenceWidth = theme.referenceWidth;
            panel.referenceHeight = theme.referenceHeight;
            panel.x = theme.panelX;
            panel.y = theme.panelY + (1.0f - easedOpening) * 26.0f;
            panel.width = theme.panelWidth;
            panel.height = theme.panelHeight;
            panel.pivotX = 0.5f;
            panel.pivotY = 0.5f;
            panel.effectProgress = opening;
            panel.effectIntensity = 1.15f;
            panel.timeSeconds = state.sessionElapsed;
            panel.tint = White(easedOpening);
            panel.effectColor = InkBlue();
            panel.highlightColor = White();
            panel.effect = GameProjectUiEffect::DialoguePanelReveal;
            (void)service.SubmitSprite(theme.dialoguePanelAssetPath, panel);

            const DialogueSpeakerProfile* speakerProfile =
                state.style != nullptr
                    ? FindDialogueSpeaker(
                        *state.style,
                        state.node->speakerId)
                    : nullptr;
            const std::string_view speaker = speakerProfile != nullptr
                ? ResolveLocalizedText(
                    speakerProfile->displayName,
                    state.locale,
                    fallbackLocale)
                : ResolveLocalizedText(
                    state.node->speaker,
                    state.locale,
                    fallbackLocale);
            if (!speaker.empty() &&
                !theme.speakerPlateAssetPath.empty()) {
                GameProjectUiSpriteRequest plate = panel;
                plate.sourceId = base + 2u;
                plate.order = 910;
                plate.x = theme.speakerPlateX;
                plate.y = theme.speakerPlateY;
                plate.width = theme.speakerPlateWidth;
                plate.height = theme.speakerPlateHeight;
                plate.effect = GameProjectUiEffect::DialoguePanelReveal;
                (void)service.SubmitSprite(
                    theme.speakerPlateAssetPath,
                    plate);

                GameProjectUiTextRequest speakerText{};
                speakerText.sourceId = base + 3u;
                speakerText.order = 920;
                speakerText.flags = kGameProjectUiSubmissionModal;
                speakerText.referenceWidth = theme.referenceWidth;
                speakerText.referenceHeight = theme.referenceHeight;
                speakerText.x = theme.speakerTextX;
                speakerText.y = theme.speakerTextY;
                speakerText.maxWidth = theme.speakerTextWidth;
                speakerText.maxHeight = theme.speakerTextHeight;
                speakerText.pixelHeight = theme.speakerPixelHeight *
                    theme.speakerTextScale;
                speakerText.rotationRadians =
                    theme.speakerTextRotationDegrees * 0.01745329252f;
                speakerText.revealAmount = opening;
                const GAME_UI::GameUiTextEffectStyle speakerStyle =
                    ApplyTextStyle(
                        speakerText,
                        theme.speakerTextStyle);
                (void)service.SubmitStyledText(
                    theme.fontAssetPath,
                    speaker,
                    speakerText,
                    speakerStyle);
            }

            const std::string_view body = ResolveLocalizedText(
                state.node->text,
                state.locale,
                fallbackLocale);
            if (!body.empty()) {
                GameProjectUiTextRequest bodyText{};
                bodyText.sourceId = base + 4u;
                bodyText.order = 930;
                bodyText.flags = kGameProjectUiSubmissionModal;
                bodyText.referenceWidth = theme.referenceWidth;
                bodyText.referenceHeight = theme.referenceHeight;
                bodyText.x = theme.bodyTextX;
                bodyText.y = theme.bodyTextY;
                bodyText.maxWidth = theme.bodyTextWidth;
                bodyText.maxHeight = theme.bodyTextHeight;
                bodyText.pixelHeight = theme.bodyPixelHeight *
                    theme.bodyTextScale;
                bodyText.rotationRadians =
                    theme.bodyTextRotationDegrees * 0.01745329252f;
                bodyText.revealAmount = std::clamp(
                    state.revealAmount, 0.0f, 1.0f);
                bodyText.timeSeconds = state.sessionElapsed;
                const GAME_UI::GameUiTextEffectStyle bodyStyle =
                    ApplyTextStyle(bodyText, theme.bodyTextStyle);
                (void)service.SubmitStyledText(
                    theme.fontAssetPath,
                    body,
                    bodyText,
                    bodyStyle);
            }

            if (state.node->type == DialogueNodeType::Line) {
                std::string_view promptAsset =
                    state.useGamepadAdvancePrompt
                    ? std::string_view(theme.advanceGamepadAssetPath)
                    : std::string_view(theme.advanceMouseAssetPath);
                if (promptAsset.empty()) {
                    promptAsset = state.useGamepadAdvancePrompt
                        ? std::string_view(theme.advanceMouseAssetPath)
                        : std::string_view(theme.advanceGamepadAssetPath);
                }
                const float revealInput = std::clamp(
                    (state.revealAmount - 0.92f) / 0.08f,
                    0.0f,
                    1.0f);
                const float promptReveal = revealInput * revealInput *
                    (3.0f - 2.0f * revealInput) * easedOpening;
                if (!promptAsset.empty() && promptReveal > 0.001f) {
                    constexpr float kGamepadWidth = 59.0f;
                    constexpr float kGamepadHeight = 104.0f;
                    constexpr float kMouseWidth = 55.0f;
                    constexpr float kMouseHeight = 112.0f;
                    const float phase = state.sessionElapsed /
                        (std::max)(theme.advancePromptPulseSeconds, 0.20f) *
                        6.28318530718f;
                    const float wave = 0.5f + 0.5f * std::sin(phase);
                    const float animatedScale = theme.advancePromptScale *
                        (1.0f + theme.advancePromptScaleAmount * wave);
                    const float naturalWidth =
                        state.useGamepadAdvancePrompt
                        ? kGamepadWidth
                        : kMouseWidth;
                    const float naturalHeight =
                        state.useGamepadAdvancePrompt
                        ? kGamepadHeight
                        : kMouseHeight;
                    const float width = naturalWidth * animatedScale;
                    const float height = naturalHeight * animatedScale;

                    GameProjectUiSpriteRequest prompt{};
                    prompt.sourceId = base + 5u;
                    prompt.order = 935;
                    prompt.flags = kGameProjectUiSubmissionModal;
                    prompt.referenceWidth = theme.referenceWidth;
                    prompt.referenceHeight = theme.referenceHeight;
                    prompt.x = theme.advancePromptX - width * 0.5f;
                    prompt.y = theme.advancePromptY - height +
                        std::sin(phase) * theme.advancePromptBobDistance;
                    prompt.width = width;
                    prompt.height = height;
                    prompt.pivotX = 0.5f;
                    prompt.pivotY = 1.0f;
                    prompt.effectProgress = promptReveal;
                    prompt.effectIntensity = 0.32f + wave * 0.18f;
                    prompt.timeSeconds = state.sessionElapsed;
                    prompt.tint = White(
                        promptReveal * (0.78f + wave * 0.22f));
                    prompt.effectColor = InkBlue();
                    prompt.highlightColor = White();
                    prompt.effect = GameProjectUiEffect::DialoguePanelReveal;
                    (void)service.SubmitSprite(promptAsset, prompt);
                }
            }

            if (state.node->type != DialogueNodeType::Choice) {
                return true;
            }
            for (size_t index = 0u;
                 index < state.node->choices.size();
                 ++index) {
                const bool selected = index == state.selectedChoice;
                const float y = theme.choiceY +
                    static_cast<float>(index) *
                        (theme.choiceHeight + theme.choiceGap);
                GameProjectUiSpriteRequest choicePanel{};
                choicePanel.sourceId = base + 100u + index * 2u;
                choicePanel.order = 940 + static_cast<int32_t>(index) * 2;
                choicePanel.flags = kGameProjectUiSubmissionModal;
                choicePanel.referenceWidth = theme.referenceWidth;
                choicePanel.referenceHeight = theme.referenceHeight;
                choicePanel.x = theme.choiceX + (selected ? -12.0f : 0.0f);
                choicePanel.y = y;
                choicePanel.width = theme.choiceWidth;
                choicePanel.height = theme.choiceHeight;
                choicePanel.effectProgress = opening;
                choicePanel.effectIntensity = selected ? 1.25f : 0.75f;
                choicePanel.timeSeconds = state.sessionElapsed;
                choicePanel.tint = White(opening);
                choicePanel.effectColor = InkBlue();
                choicePanel.highlightColor = White();
                choicePanel.effect = selected
                    ? GameProjectUiEffect::DialogueChoiceSelected
                    : GameProjectUiEffect::DialogueChoiceIdle;
                if (!theme.choiceAssetPath.empty()) {
                    (void)service.SubmitSprite(
                        theme.choiceAssetPath,
                        choicePanel);
                }

                const std::string_view label = ResolveLocalizedText(
                    state.node->choices[index].text,
                    state.locale,
                    fallbackLocale);
                GameProjectUiTextRequest choiceText{};
                choiceText.sourceId = choicePanel.sourceId + 1u;
                choiceText.order = choicePanel.order + 1;
                choiceText.flags = kGameProjectUiSubmissionModal;
                choiceText.referenceWidth = theme.referenceWidth;
                choiceText.referenceHeight = theme.referenceHeight;
                choiceText.x = choicePanel.x + theme.choiceTextOffsetX;
                choiceText.y = y + theme.choiceTextOffsetY;
                choiceText.maxWidth = theme.choiceTextWidth;
                choiceText.maxHeight = theme.choiceTextHeight;
                choiceText.pixelHeight = theme.choicePixelHeight *
                    theme.choiceTextScale;
                choiceText.rotationRadians =
                    theme.choiceTextRotationDegrees * 0.01745329252f;
                choiceText.horizontalAlign = GameProjectUiTextAlign::Center;
                choiceText.verticalAlign =
                    GameProjectUiVerticalAlign::Center;
                choiceText.revealAmount = opening;
                GAME_UI::GameUiTextEffectStyle choiceStyle =
                    ApplyTextStyle(
                        choiceText,
                        theme.choiceTextStyle);
                if (selected) choiceStyle.glowIntensity *= 1.35f;
                (void)service.SubmitStyledText(
                    theme.fontAssetPath,
                    label,
                    choiceText,
                    choiceStyle);
            }
            return true;
        }
        catch (...) {
            return false;
        }
    }

} // namespace HIKARI::DIALOGUE
