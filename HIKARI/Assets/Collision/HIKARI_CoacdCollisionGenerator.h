#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/Collision/HIKARI_ModelCollisionMeshExtraction.h"

namespace HIKARI::ASSETS::COLLISION {

    struct CoacdCollisionSettings {
        double threshold = 0.05;
        int maximumConvexHulls = 32;
        int maximumHullVertices = 128;
        bool mergeParts = true;
        uint32_t seed = 0u;
    };

    bool IsCoacdCollisionGeneratorAvailable(std::string& outMessage);

    bool GenerateCoacdCollisionParts(
        const ModelCollisionMeshData& input,
        const CoacdCollisionSettings& settings,
        std::vector<ModelCollisionMeshData>& outParts,
        std::string& outMessage);

} // namespace HIKARI::ASSETS::COLLISION
