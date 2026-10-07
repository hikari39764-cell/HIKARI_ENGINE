#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {
class World;
namespace RENDER3D::RUNTIME {
class SceneRenderCache;
}
} // namespace HIKARI

namespace HIKARI::VFX {

// Private presentation request.  Gameplay supplies only an owner and a
// deduplication identity; the service resolves the current model, pose,
// and world transform from the World at capture time.
struct CombatAfterimageRequest {
    RuntimeObjectHandle ownerObject{};
    uint64_t sourceId = 0u;
    uint64_t sequence = 0u;
    uint32_t randomSeed = 0u;
    uint64_t frameIndex = 0u;

    float intensity = 1.0f;
    float uniformScale = 1.0f;
    uint32_t silhouetteCount = 4u;
    float lifetimeUnscaledSeconds = 0.28f;
    float opacity = 0.62f;
    MATH::Vec3 color{0.10f, 0.72f, 1.0f};
};

class CombatAfterimageService final {
public:
    CombatAfterimageService();
    ~CombatAfterimageService();

    CombatAfterimageService(const CombatAfterimageService&) = delete;
    CombatAfterimageService& operator=(const CombatAfterimageService&) = delete;

    // Admission queues a sample after final animation/appearance sync.
    // Missing final poses fail closed during publication.
    bool Capture(World& world, const CombatAfterimageRequest& request) noexcept;
    // SkinnedAfterimage 出力の見た目で取り込む。要求の intensity/opacity は倍率として掛ける。
    bool Capture(World& world, const CombatAfterimageRequest& request, const VfxSpecialOutputData& output) noexcept;

    // Advances unscaled presentation time.  Repeated calls for the same
    // frame are ignored so multiple views cannot age the effect twice.
    void Tick(uint64_t frameIndex, float unscaledDeltaSeconds) noexcept;
    bool SetPaused(RuntimeObjectHandle owner, uint64_t sourceId, bool paused) noexcept;
    bool Clear(RuntimeObjectHandle owner, uint64_t sourceId) noexcept;
    void Publish(World& world, RENDER3D::RUNTIME::SceneRenderCache& scene, uint64_t frameIndex) noexcept;

    void Reset() noexcept;

    size_t GetActiveCount() const noexcept;
    size_t GetSampleCount(RuntimeObjectHandle owner, uint64_t sourceId) const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace HIKARI::VFX
