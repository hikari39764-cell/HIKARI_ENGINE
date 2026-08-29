#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "Assets/Dialogue/HIKARI_DialogueAsset.h"
#include "Gameplay/Dialogue/HIKARI_DialogueProjectStyle.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {
    struct FrameContext;
    class World;
}

namespace HIKARI::DIALOGUE {

    enum class DialogueRuntimeEventType : uint8_t {
        Started = 0u,
        NodeEntered = 1u,
        ChoiceSelected = 2u,
        EventEmitted = 3u,
        Ended = 4u,
    };

    struct DialogueRuntimeEvent {
        DialogueRuntimeEventType type = DialogueRuntimeEventType::Started;
        RuntimeObjectHandle trigger{};
        RuntimeObjectHandle interactor{};
        std::string assetGuid{};
        std::string nodeId{};
        std::string value{};
    };

    class DialogueSessionService final {
    public:
        bool Start(
            World& world,
            const AssetGuid& assetGuid,
            std::string locale,
            RuntimeObjectHandle trigger,
            RuntimeObjectHandle interactor,
            bool pauseWorld);
        void Update(World& world, const FrameContext& frame);
        void SubmitPresentation(World& world, const FrameContext& frame);
        void Stop(World& world, std::string reason = {});
        void Reset(World* world = nullptr) noexcept;

        bool IsActive() const noexcept { return active_; }
        RuntimeObjectHandle GetTrigger() const noexcept { return trigger_; }
        const std::string& GetCurrentNodeId() const noexcept {
            return currentNodeId_;
        }
        size_t GetSelectedChoice() const noexcept {
            return selectedChoice_;
        }

    private:
        struct PortraitSlotRuntime {
            std::string speakerId{};
            std::string assetGuid{};
            std::string assetPath{};
            float offsetX = 0.0f;
            float offsetY = 0.0f;
            float scale = 1.0f;
            float rotationDegrees = 0.0f;
            bool mirror = false;
            float presence = 0.0f;
            float targetPresence = 0.0f;
            float focus = 0.0f;
            float targetFocus = 0.0f;
        };

        bool EnterNode(World& world, std::string nodeId);
        bool ResolveAutomaticNodes(World& world);
        void ApplyPortraitNode(const DialogueNode& node) noexcept;
        void UpdatePortraits(float unscaledDt) noexcept;
        void ReleaseOwnership(World* world) noexcept;
        float CurrentRevealAmount() const noexcept;

        std::shared_ptr<const DialogueAsset> asset_{};
        std::shared_ptr<const DialogueProjectStyle> style_{};
        std::string locale_{};
        std::string currentNodeId_{};
        RuntimeObjectHandle trigger_{};
        RuntimeObjectHandle interactor_{};
        uint64_t timeDilationToken_ = 0u;
        float sessionElapsed_ = 0.0f;
        float nodeElapsed_ = 0.0f;
        float forcedRevealAmount_ = 0.0f;
        size_t selectedChoice_ = 0u;
        PortraitSlotRuntime leftPortrait_{};
        PortraitSlotRuntime rightPortrait_{};
        bool active_ = false;
        bool inputArmed_ = false;
        bool pushedInputContext_ = false;
    };

} // namespace HIKARI::DIALOGUE
