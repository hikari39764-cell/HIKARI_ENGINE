#pragma once

#include <cstdint>
#include <unordered_map>

#include "Render3D/HIKARI_Math3D.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {

    // Optional render-presentation override. Gameplay and physics continue to
    // read the authoritative GameObject transform; render systems may consume
    // a smoothed world matrix without feeding it back into simulation.
    class PresentationTransformService {
    public:
        void SetWorldMatrix(
            RuntimeObjectHandle object,
            const MATH::Mat4& worldMatrix);
        bool Remove(RuntimeObjectHandle object) noexcept;
        void Clear() noexcept;

        bool TryGetWorldMatrix(
            RuntimeObjectHandle object,
            MATH::Mat4& outWorldMatrix) const noexcept;
        uint64_t GetRevision() const noexcept;

    private:
        struct Entry {
            MATH::Mat4 worldMatrix = MATH::Mat4::Identity();
        };

        void AdvanceRevision() noexcept;

        std::unordered_map<uint64_t, Entry> entries_{};
        uint64_t revision_ = 1u;
    };

} // namespace HIKARI
