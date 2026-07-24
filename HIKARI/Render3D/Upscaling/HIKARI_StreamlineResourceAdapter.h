#pragma once

#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

#if defined(HIKARI_WITH_STREAMLINE)
#pragma warning(push, 0)
#include <sl.h>
#pragma warning(pop)
#endif

namespace HIKARI::RENDER3D::UPSCALING::INTERNAL {

#if defined(HIKARI_WITH_STREAMLINE)
    inline sl::Resource MakeStreamlineTextureResource(
        const TEMPORAL::TemporalTextureView& view) {

        sl::Resource resource(
            sl::ResourceType::eTex2d,
            view.resource,
            static_cast<uint32_t>(view.state));
        resource.width = view.width;
        resource.height = view.height;
        resource.nativeFormat =
            static_cast<uint32_t>(view.format);
        resource.mipLevels = 1;
        resource.arrayLayers = 1;
        return resource;
    }
#endif

} // namespace HIKARI::RENDER3D::UPSCALING::INTERNAL
