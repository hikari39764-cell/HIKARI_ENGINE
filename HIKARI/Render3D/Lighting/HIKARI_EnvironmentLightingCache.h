#pragma once

#include <cstdint>
#include <memory>
#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereParameters.h"
#include "Gfx/HIKARI_GfxContext.h"

namespace HIKARI::IBL {

enum class EnvironmentLightingCacheFailure : uint8_t { None, MissingInput, Pipeline, Allocation, Upload };

struct EnvironmentLightingCacheStatus {
    bool valid = false;
    bool building = false;
    bool current = false;
    uint32_t dispatchCount = 0;
    uint32_t remainingDispatches = 0;
    uint64_t sourceEpoch = 0;
    uint64_t ageFrames = 0;
    uint64_t workingSetBytes = 0;
    EnvironmentLightingCacheFailure failure = EnvironmentLightingCacheFailure::None;
};

// IBL の GPU 生成と二重バッファだけを所有する。Scene/asset/露出は所有しない。
// 同じ source の最後の完全な結果を維持し、部分的な mip/face を公開しない。
class EnvironmentLightingCache final {
public:
    EnvironmentLightingCache();
    ~EnvironmentLightingCache();
    EnvironmentLightingCache(const EnvironmentLightingCache&) = delete;
    EnvironmentLightingCache& operator=(const EnvironmentLightingCache&) = delete;

    bool Update(const GFX::Context& graphics, const SkySettings& sky,
                const ATMOSPHERE::AtmosphereLutConstants& atmosphere, RENDER3D::TextureResourceHandle source,
                D3D12_GPU_DESCRIPTOR_HANDLE atmosphereSkyView, uint64_t frameIndex);
    void Reset(const GFX::Context& graphics);
    void Invalidate();
    IblEnvironmentSource GetSource() const;
    const EnvironmentLightingCacheStatus& GetStatus() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace HIKARI::IBL
