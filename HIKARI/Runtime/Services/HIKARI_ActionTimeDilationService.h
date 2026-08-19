#pragma once

#include <cstdint>
#include <vector>

namespace HIKARI {

    class World;

    namespace SERVICES {

        // Runtime-owned scoped time arbiter for action-game feedback such as
        // hit stop and defensive slow motion.  The World pointer is an owner
        // identity only; no WorldServiceRegistry is exposed by this service.
        class ActionTimeDilationService final {
        public:
            struct RequestDesc {
                uint64_t sourceId = 0u;
                int32_t priority = 0;
                float scale = 1.0f;
                float durationUnscaledSeconds = 0.0f;
            };

            // A zero duration means the request remains until released.  A
            // positive duration is consumed with unscaled host time.
            uint64_t Request(
                World* owner,
                const RequestDesc& request) noexcept;
            uint64_t Request(
                World* owner,
                uint64_t sourceId,
                int32_t priority,
                float scale,
                float durationUnscaledSeconds) noexcept;

            bool Release(
                World* owner,
                uint64_t token) noexcept;
            bool Release(uint64_t token) noexcept;
            void ReleaseOwner(World* owner) noexcept;

            // Apply the state collected since the previous host frame.  This
            // must run before TIME::BeginFrame so the current frame uses the
            // resolved scale.  Requests made during the current host frame
            // therefore become visible on the next host frame.
            void ApplyPending() noexcept;

            // Advance finite requests with unscaled time.  Call this every
            // host frame, including paused frames.
            void Advance(float unscaledDt) noexcept;

            // Clears all owners and restores the global time scale to one.
            void Reset() noexcept;

            float GetEffectiveScale() const noexcept {
                return effectiveScale_;
            }
            float GetEffectiveScale(const World* owner) const noexcept;

            bool HasRequests() const noexcept {
                return !requests_.empty();
            }

        private:
            struct Entry {
                World* owner = nullptr;
                RequestDesc request{};
                uint64_t token = 0u;
                float remainingUnscaledSeconds = 0.0f;
                uint64_t insertionOrder = 0u;
            };

            static bool IsValidRequest(const RequestDesc& request) noexcept;
            static uint64_t EncodeToken(
                uint32_t epoch,
                uint32_t value) noexcept;
            static uint32_t DecodeEpoch(uint64_t token) noexcept;
            static uint32_t DecodeValue(uint64_t token) noexcept;

            const Entry* FindWinner(const World* owner) const noexcept;
            Entry* FindToken(uint64_t token) noexcept;
            bool IsCurrentToken(uint64_t token) const noexcept;
            uint64_t AllocateToken() noexcept;
            void AdvanceEntry(Entry& entry, float unscaledDt) noexcept;

            std::vector<Entry> requests_{};
            uint32_t epoch_ = 1u;
            uint32_t nextTokenValue_ = 1u;
            uint64_t nextInsertionOrder_ = 1u;
            float effectiveScale_ = 1.0f;
            float baselineScale_ = 1.0f;
            bool ownsTimeScale_ = false;
        };

    } // namespace SERVICES
} // namespace HIKARI
