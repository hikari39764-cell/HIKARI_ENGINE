#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponent.h"
#include "Scene/Vfx/HIKARI_VfxBindingResolver.h"

namespace HIKARI {

struct RuntimePlayStateService;

namespace BINDING {
class RuntimeBindingStore;
}

namespace VFX {
class VfxPlaybackService;
}

struct VfxPlayerSystemAdmissionStats {
    uint64_t acceptedTriggers = 0u;
    uint64_t parallelLimitRejections = 0u;
    uint64_t playbackRejections = 0u;
};

class VfxPlayerSystem final : public ISystem {
public:
    std::string_view GetName() const override { return "VfxPlayerSystem"; }

    void OnWorldAttached(World& world) override;
    void OnWorldDetached(World& world) override;
    // Slots resolve owner and target transforms after every LateUpdate so a
    // following effect uses this frame's pose. It is registered ahead of
    // ModelRenderSystem (HIKARI_VfxRuntimeFeature.cpp), whose PreRender
    // advances VfxPlaybackService once per frame.
    void PreRender(World& world, const FrameContext& frame) override;
    const VfxPlayerSystemAdmissionStats& GetAdmissionStats() const noexcept;

private:
    struct SlotKey {
        RuntimeObjectHandle owner{};
        VfxSlotId slotId{};

        bool operator==(const SlotKey&) const noexcept = default;
    };

    struct SlotKeyHash {
        size_t operator()(const SlotKey& key) const noexcept;
    };

    struct ActivePlayback {
        VFX::VfxPlaybackHandle handle{};
        VFX::VfxResolvedTarget target{};
    };

    struct SlotRuntimeState {
        bool autoPlayConsumed = false;
        uint64_t lastObservedEventSequence = 0u;
        std::vector<ActivePlayback> active{};
    };

    void StopState(SlotRuntimeState& state);
    void TriggerSlot(const VfxPlayerSlot& slot, const VFX::VfxBindingResolveContext& context, SlotRuntimeState& state,
                     uint64_t eventSequence);
    void RefreshActive(const VfxPlayerSlot& slot, const VFX::VfxBindingResolveContext& context,
                       SlotRuntimeState& state);

    VFX::VfxPlaybackService* playbackService_ = nullptr;
    BINDING::RuntimeBindingStore* bindingStore_ = nullptr;
    const RuntimePlayStateService* runtimePlayState_ = nullptr;
    std::unordered_map<SlotKey, SlotRuntimeState, SlotKeyHash> states_{};
    VfxPlayerSystemAdmissionStats admissionStats_{};
    uint64_t lastFrameIndex_ = UINT64_MAX;
};

} // namespace HIKARI
