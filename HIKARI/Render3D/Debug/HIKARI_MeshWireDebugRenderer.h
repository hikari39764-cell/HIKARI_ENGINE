#pragma once

#include <cstddef>
#include <cstdint>

#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI {
    class ModelAsset;
}

namespace HIKARI::MESHWIREDEBUG {

    struct MeshWireDebugStats {
        size_t submittedModelCount = 0;
        size_t submittedLineCount = 0;
        size_t truncatedModelCount = 0;
        size_t cacheHitCount = 0;
        size_t cacheMissCount = 0;
    };

    void BeginFrame();
    void SubmitModelWire(const ModelAsset& asset, const Transform3D& world, uint32_t color, uint32_t maxLines, bool perPrimitiveColor);
    void SubmitModelBounds(const ModelAsset& asset, const Transform3D& world, uint32_t color);
    const MeshWireDebugStats& GetDebugStats();
    void ClearCache();

} // namespace HIKARI::MESHWIREDEBUG
