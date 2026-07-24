#pragma once

#include <cstdint>
#include <unordered_map>

#include "Scene/HIKARI_CameraDirector.h"
#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

    struct RuntimePlayStateService;

    class CameraActivationVolumeSystem final : public ISystem {
    public:
        std::string_view GetName() const override {
            return "CameraActivationVolumeSystem";
        }

        void OnWorldAttached(World& world) override;
        void OnWorldDetached(World& world) override;
        void LateUpdate(
            World& world,
            const FrameContext& frame) override;

    private:
        struct ActiveRequest {
            CameraOverrideToken token{};
            CameraActivationRequest request{};
        };

        void ReleaseRequest(uint64_t volumeRuntimeId) noexcept;

        CameraDirector* director_ = nullptr;
        const RuntimePlayStateService* runtimePlayState_ = nullptr;
        std::unordered_map<uint64_t, ActiveRequest> activeRequests_{};
    };

} // namespace HIKARI
