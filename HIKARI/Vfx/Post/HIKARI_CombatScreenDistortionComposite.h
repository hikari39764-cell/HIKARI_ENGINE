#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <d3d12.h>
#include <wrl.h>

#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "RenderCore/Targets/HIKARI_RenderTarget2D.h"
#include "Vfx/Post/HIKARI_ActionScreenImpulseProvider.h"
#include "Vfx/Post/HIKARI_PostCommon.h"

namespace HIKARI::POST {

// Dedicated two-SRV combat composite.  The general PostQuadDrawer keeps
// a one-SRV root signature, so this owner maintains frame-local SRV heaps
// and CB slices for scene+t1 mask without changing the shared post API.
class CombatScreenDistortionComposite final {
public:
    CombatScreenDistortionComposite();
    ~CombatScreenDistortionComposite();

    void UpdateContext(const GFX::Context& context);
    void Shutdown();

    bool Execute(RenderTarget2D& source, const ActionScreenImpulseProvider::Mask* mask,
                 const ActionScreenImpulseProvider::Mask* accent,
                 const ActionScreenImpulseProvider::CinematicGrade& cinematicGrade, const CommonParams& params,
                 uint64_t frameIndex, RenderTarget2D*& output, bool& maskApplied, bool& accentApplied);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace HIKARI::POST
