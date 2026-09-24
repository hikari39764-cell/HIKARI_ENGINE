#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace HIKARI {
struct MeshPrimitive;
}

namespace HIKARI::ASSETS::LIGHTING::INTERNAL {

struct XatlasInputAnalysis {
    uint32_t outOfRangeTriangleCount = 0u;
    uint32_t nonFinitePositionCount = 0u;
    uint32_t degenerateTriangleCount = 0u;
    uint32_t validTriangleCount = 0u;
    bool useVertexNormals = true;
};

struct XatlasInputGeometry {
    XatlasInputAnalysis analysis{};
    std::vector<uint32_t> indices{};

    bool CanGenerate() const noexcept {
        return analysis.nonFinitePositionCount == 0u && analysis.validTriangleCount > 0u && !indices.empty();
    }
};

XatlasInputGeometry PrepareXatlasInputGeometry(const MeshPrimitive& primitive);

std::string DescribeXatlasInputRejection(const XatlasInputAnalysis& analysis);

std::string DescribeXatlasInputRepair(const XatlasInputAnalysis& analysis);

} // namespace HIKARI::ASSETS::LIGHTING::INTERNAL
