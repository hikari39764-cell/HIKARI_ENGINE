#pragma once

#include <cstdint>
#include <memory>
#include <utility>

#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

namespace HIKARI::VFX {

    enum class VfxRenderInputPublishMode : uint8_t {
        RequireAcknowledged,
        SupersedePending,
    };

    // Owns the immutable Scene-to-Renderer handoff for a throttled consumer.
    // The current input remains available after acknowledgement because a
    // renderer still needs emitter presentation state on frames with no new
    // simulation work. Event-like reset/spawn/retire work is never replaced
    // until the consumer acknowledges the exact published sequence, unless
    // the owner explicitly starts a new playback epoch.
    class VfxRenderInputMailbox final {
    public:
        uint64_t Publish(
            RENDER3D::PARTICLES::ParticleRenderInput input,
            VfxRenderInputPublishMode mode =
                VfxRenderInputPublishMode::RequireAcknowledged) {

            if (pending_ && mode ==
                    VfxRenderInputPublishMode::RequireAcknowledged) {
                return 0u;
            }
            ++sequence_;
            if (sequence_ == 0u) {
                sequence_ = 1u;
            }
            current_ = std::make_shared<const
                RENDER3D::PARTICLES::ParticleRenderInput>(
                    std::move(input));
            pending_ = true;
            return sequence_;
        }

        bool Acknowledge(uint64_t sequence) noexcept {
            if (!pending_ || sequence == 0u || sequence != sequence_) {
                return false;
            }
            pending_ = false;
            return true;
        }

        void Clear() noexcept {
            current_.reset();
            pending_ = false;
        }

        bool HasPending() const noexcept {
            return pending_;
        }

        uint64_t GetSequence() const noexcept {
            return sequence_;
        }

        const std::shared_ptr<const
            RENDER3D::PARTICLES::ParticleRenderInput>&
        GetCurrent() const noexcept {
            return current_;
        }

    private:
        std::shared_ptr<const
            RENDER3D::PARTICLES::ParticleRenderInput> current_{};
        uint64_t sequence_ = 0u;
        bool pending_ = false;
    };

} // namespace HIKARI::VFX
