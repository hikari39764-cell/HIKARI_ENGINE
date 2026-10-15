#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "HIKARI/Project/GameFlowTypes.h"
#include "Assets/Dialogue/HIKARI_DialogueAsset.h"
#include "Gameplay/Dialogue/HIKARI_DialogueProjectStyle.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {
struct FrameContext;
class World;
} // namespace HIKARI

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
    uint64_t sequence = 0u;
};

class DialogueRuntimeEventQueue final {
public:
    static constexpr uint32_t kCapacity = kGameProjectDialogueEventQueueCapacity;
    static_assert(std::is_nothrow_move_assignable_v<DialogueRuntimeEvent>);

    const DialogueRuntimeEvent& Push(DialogueRuntimeEvent event) noexcept {
        event.sequence = nextSequence_;
        if (nextSequence_ != (std::numeric_limits<uint64_t>::max)()) {
            ++nextSequence_;
        }

        if (count_ == kCapacity) {
            head_ = (head_ + 1u) % kCapacity;
            --count_;
            if (droppedCount_ != (std::numeric_limits<uint64_t>::max)()) {
                ++droppedCount_;
            }
        }
        const uint32_t writeIndex = (head_ + count_) % kCapacity;
        events_[writeIndex] = std::move(event);
        ++count_;
        return events_[writeIndex];
    }

    bool Consume(DialogueRuntimeEvent& outEvent) noexcept {
        outEvent = {};
        if (count_ == 0u)
            return false;
        outEvent = std::move(events_[head_]);
        events_[head_] = {};
        head_ = (head_ + 1u) % kCapacity;
        --count_;
        if (count_ == 0u)
            head_ = 0u;
        return true;
    }

    uint32_t GetCount() const noexcept { return count_; }
    uint64_t GetDroppedCount() const noexcept { return droppedCount_; }

    void Reset() noexcept {
        for (DialogueRuntimeEvent& event : events_)
            event = {};
        head_ = 0u;
        count_ = 0u;
        droppedCount_ = 0u;
        nextSequence_ = 1u;
    }

private:
    std::array<DialogueRuntimeEvent, kCapacity> events_{};
    uint32_t head_ = 0u;
    uint32_t count_ = 0u;
    uint64_t droppedCount_ = 0u;
    uint64_t nextSequence_ = 1u;
};

class DialogueSessionService final {
public:
    bool Start(World& world, const AssetGuid& assetGuid, std::string locale, RuntimeObjectHandle trigger,
               RuntimeObjectHandle interactor, bool pauseWorld);
    void Update(World& world, const FrameContext& frame);
    void SubmitPresentation(World& world, const FrameContext& frame);
    void Stop(World& world, std::string reason = {});
    void Reset(World* world = nullptr) noexcept;

    bool IsActive() const noexcept { return active_; }
    bool ConsumeEvent(DialogueRuntimeEvent& outEvent) noexcept { return eventQueue_.Consume(outEvent); }
    uint32_t GetPendingEventCount() const noexcept { return eventQueue_.GetCount(); }
    uint64_t GetDroppedEventCount() const noexcept { return eventQueue_.GetDroppedCount(); }

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
    void PublishEvent(World& world, DialogueRuntimeEvent event);
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
    DialogueRuntimeEventQueue eventQueue_{};
    bool active_ = false;
    bool inputArmed_ = false;
    bool pushedInputContext_ = false;
};

} // namespace HIKARI::DIALOGUE
