#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

#include <DirectXMath.h>

#include "Scene/Components/Rendering/Vfx/HIKARI_GhostPossessionComponent.h"
#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {

    class MaterialFxComponent;
    class VfxPlayerComponent;
    struct RuntimePlayStateService;

    class GhostPossessionSystem final : public ISystem {
    public:
        std::string_view GetName() const override {
            return "GhostPossessionSystem";
        }

        void OnWorldAttached(World& world) override;
        void OnWorldDetached(World& world) override;
        void Update(World& world, const FrameContext& frame) override;

    private:
        struct RuntimeHandleHash {
            size_t operator()(RuntimeObjectHandle handle) const noexcept;
        };

        struct RuntimeState {
            std::string previousProfileId{};
            DirectX::XMFLOAT4
                previousValues[VFX::kMaterialFxUserCount]{};
            bool previousValuesInitialized = false;
            float blendWeight = 0.0f;
            bool vfxActive = false;
            VfxSlotId enterSlotId{};
            VfxSlotId loopSlotId{};
            VfxSlotId exitSlotId{};
            bool finished = false;
        };

        static void CaptureSurface(
            const MaterialFxComponent& materialFx,
            RuntimeState& state);
        static void ApplySurface(
            MaterialFxComponent& materialFx,
            const GhostPossessionSettings& settings,
            RuntimeState& state);
        static void RestoreSurface(
            MaterialFxComponent& materialFx,
            RuntimeState& state);
        static void UpdateVfx(
            VfxPlayerComponent* player,
            const GhostPossessionSettings& settings,
            bool desiredActive,
            bool runtimeActive,
            RuntimeState& state);
        static void StopRemovedVfx(
            VfxPlayerComponent* player,
            bool runtimeActive,
            RuntimeState& state);

        const RuntimePlayStateService* runtimePlayState_ = nullptr;
        std::unordered_map<
            RuntimeObjectHandle,
            RuntimeState,
            RuntimeHandleHash> states_{};
    };

} // namespace HIKARI
