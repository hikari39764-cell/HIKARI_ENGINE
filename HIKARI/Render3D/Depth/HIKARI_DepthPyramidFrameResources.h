#pragma once

#include <cstdint>

#include "Render3D/Depth/HIKARI_DepthPyramidLayer.h"

namespace HIKARI::RENDER3D::DEPTH {

    struct DepthPyramidFrameResources {
        uint32_t frameIndex = 0;
        uint32_t publishCount = 0;
        bool currentValid = false;
        DepthPyramidView current{};

        void BeginFrame(uint32_t nextFrameIndex = 0);
        bool PublishCurrent(const DepthPyramidView& view);
        bool PublishCurrent(DepthPyramidView view, DepthPyramidViewKind viewKind);
        const DepthPyramidView* TryGetCurrent() const;
        const DepthPyramidView* TryGetCurrent(DepthPyramidSourceKind requiredSource) const;
        const DepthPyramidView* TryGetCurrent(
            DepthPyramidSourceKind requiredSource,
            DepthPyramidViewKind requiredViewKind) const;
    };

    void BeginDepthPyramidFrame(uint32_t frameIndex = 0);
    bool PublishFrameDepthPyramid(const DepthPyramidView& view);
    bool PublishFrameDepthPyramid(DepthPyramidView view, DepthPyramidViewKind viewKind);
    const DepthPyramidView* TryGetFrameDepthPyramidView();
    const DepthPyramidView* TryGetFrameDepthPyramidView(DepthPyramidSourceKind requiredSource);
    const DepthPyramidView* TryGetFrameDepthPyramidView(
        DepthPyramidSourceKind requiredSource,
        DepthPyramidViewKind requiredViewKind);

} // namespace HIKARI::RENDER3D::DEPTH
