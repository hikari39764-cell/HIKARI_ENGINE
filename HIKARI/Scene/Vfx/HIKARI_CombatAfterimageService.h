#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"
#include "Render3D/Vfx/HIKARI_CombatAfterimageFrameSource.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {
    class World;
}

namespace HIKARI::VFX {

    struct VfxProgram;

    // Private presentation request.  Gameplay supplies only an owner and a
    // deduplication identity; the service resolves the current model, pose,
    // and world transform from the World at capture time.
    struct CombatAfterimageRequest {
        RuntimeObjectHandle ownerObject{};
        uint64_t sourceId = 0u;
        uint64_t sequence = 0u;
        uint32_t randomSeed = 0u;
        uint64_t frameIndex = 0u;

        bool silhouette = true;
        float intensity = 1.0f;
        float uniformScale = 1.0f;
        uint32_t silhouetteCount = 4u;
        float lifetimeUnscaledSeconds = 0.28f;
        float opacity = 0.62f;
        float fresnelPower = 3.0f;
        float dissolveStrength = 1.0f;
        float shellOffsetWorld = 0.008f;
        MATH::Vec3 color{ 0.10f, 0.72f, 1.0f };
    };

    class CombatAfterimageService final {
    public:
        CombatAfterimageService();
        ~CombatAfterimageService();

        CombatAfterimageService(const CombatAfterimageService&) = delete;
        CombatAfterimageService& operator=(
            const CombatAfterimageService&) = delete;

        // Captures an immutable copy of the owner's current skinned model
        // draw data.  Invalid owners/assets/poses are rejected without
        // mutating existing active afterimages.
        bool Capture(
            World& world,
            const CombatAfterimageRequest& request) noexcept;
        bool Capture(
            World& world,
            const CombatAfterimageRequest& request,
            const VfxSpecialOutputData& output) noexcept;
        bool Capture(
            World& world,
            const CombatAfterimageRequest& request,
            const VfxSkinnedAfterimageProfile& profile) noexcept;
        bool Capture(
            World& world,
            const CombatAfterimageRequest& request,
            const VfxProgram& program) noexcept;
        bool Capture(
            World& world,
            const CombatAfterimageRequest& request,
            const VfxProgram& program,
            std::string_view outputId) noexcept;

        // Advances unscaled presentation time.  Repeated calls for the same
        // frame are ignored so multiple views cannot age the effect twice.
        void Tick(uint64_t frameIndex, float unscaledDeltaSeconds) noexcept;

        void Reset() noexcept;

        const RENDER3D::COMBAT_VFX::CombatAfterimageFrameSource&
            GetFrameSource() const noexcept {
            return frameSource_;
        }

        size_t GetActiveCount() const noexcept;

    private:
        struct AfterimageState;

        void BuildFrameSource(uint64_t frameIndex) noexcept;

        std::vector<AfterimageState> states_{};
        std::vector<RENDER3D::COMBAT_VFX::CombatAfterimageDraw>
            drawRecords_{};
        RENDER3D::COMBAT_VFX::CombatAfterimageFrameSource frameSource_{};
        uint64_t revision_ = 0u;
        uint64_t lastTickFrame_ = 0u;
        bool hasTicked_ = false;
        bool poseRejectLogged_ = false;
        bool poseRejectedThisFrame_ = false;
    };

} // namespace HIKARI::VFX
