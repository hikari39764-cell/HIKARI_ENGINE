#pragma once

#include <cstdint>

namespace HIKARI::SHADOW {

    enum ShadowCacheMissReasonFlags : uint32_t {
        ShadowCacheMissReasonNone = 0u,
        ShadowCacheMissReasonInvalid = 1u << 0,
        ShadowCacheMissReasonResource = 1u << 1,
        ShadowCacheMissReasonState = 1u << 2,
        ShadowCacheMissReasonResolution = 1u << 3,
        ShadowCacheMissReasonLayout = 1u << 4,
        ShadowCacheMissReasonSource = 1u << 5,
        ShadowCacheMissReasonInstanceCount = 1u << 6,
        ShadowCacheMissReasonMatrix = 1u << 7,
        ShadowCacheMissReasonStaticDirty = 1u << 8,
        ShadowCacheMissReasonNoStaticWork = 1u << 9,
    };

} // namespace HIKARI::SHADOW
