#pragma once

#include <string_view>

#include "Scene/Vfx/HIKARI_VfxPlaybackTypes.h"

namespace HIKARI {
    class World;
}

namespace HIKARI::VFX {

    class VfxPlaybackService;

    bool ResolveVfxAnimationAttachment(
        World& world,
        RuntimeObjectHandle owner,
        std::string_view socketName,
        VfxWorldTransform& outTransform);

    // Install the resolver at the World composition root. Playback no longer
    // depends on VfxPlayerComponent being present, so public VfxView users
    // get the same owner/socket semantics as component-driven slots.
    void ConfigureVfxWorldAttachmentResolver(
        World& world,
        VfxPlaybackService& playback);

} // namespace HIKARI::VFX
