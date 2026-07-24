#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/Runtime/HIKARI_RenderSurfaceContract.h"

namespace HIKARI::RENDER3D::RUNTIME {

    constexpr uint32_t kInvalidRenderModelIndex = kInvalidRenderSurfaceIndex;

    struct RenderModelNodeRecord {
        uint32_t nodeIndex = kInvalidRenderModelIndex;
        int parentIndex = -1;
        MATH::Mat4 localMatrix{};
        Bounds localBounds{};
        bool hasMesh = false;
        int meshIndex = -1;
        int skinIndex = -1;
    };

    struct RenderModelAsset {
        const ModelAsset* source = nullptr;
        std::string sourceName{};
        std::string sourcePath{};

        Bounds localBounds{};

        bool valid = false;
        bool hasSkinnedSurfaces = false;
        bool hasStaticSurfaces = false;

        std::vector<RenderModelNodeRecord> nodes{};
        // Surface は material 単位で切った描画契約。
        std::vector<RenderSurfaceRecord> surfaces{};
    };

    bool BuildRenderModelAsset(
        const ModelAsset& source,
        RenderModelAsset& out,
        std::string* outMessage = nullptr);

} // namespace HIKARI::RENDER3D::RUNTIME
