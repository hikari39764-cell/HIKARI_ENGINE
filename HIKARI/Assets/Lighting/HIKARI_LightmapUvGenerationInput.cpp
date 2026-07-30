#include "Assets/Lighting/HIKARI_LightmapUvGenerationInput.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>

#include "Assets/Models/HIKARI_ModelAssetTypes.h"
#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI::ASSETS::LIGHTING::INTERNAL {

    namespace {

        float LengthSquared(
            const MATH::Vec3& value) noexcept {

            return
                value.x * value.x +
                value.y * value.y +
                value.z * value.z;
        }

        bool AreXatlasColocal(
            const MATH::Vec3& lhs,
            const MATH::Vec3& rhs) noexcept {

            constexpr float kXatlasPositionEpsilon =
                std::numeric_limits<float>::epsilon();
            return
                std::abs(lhs.x - rhs.x) <=
                    kXatlasPositionEpsilon &&
                std::abs(lhs.y - rhs.y) <=
                    kXatlasPositionEpsilon &&
                std::abs(lhs.z - rhs.z) <=
                    kXatlasPositionEpsilon;
        }

    } // namespace

    XatlasInputGeometry PrepareXatlasInputGeometry(
        const MeshPrimitive& primitive) {

        XatlasInputGeometry input{};
        XatlasInputAnalysis& analysis =
            input.analysis;
        input.indices.reserve(
            primitive.indices.size());
        for (const Vertex3D& vertex :
            primitive.staticVertices) {
            if (!MATH::IsFinite(vertex.position)) {
                ++analysis.nonFinitePositionCount;
            }

            const float normalLengthSquared =
                LengthSquared(vertex.normal);
            if (!MATH::IsFinite(vertex.normal) ||
                !MATH::IsFinite(normalLengthSquared) ||
                normalLengthSquared <=
                    std::numeric_limits<float>::
                        epsilon()) {
                analysis.useVertexNormals = false;
            }
        }

        constexpr float kMinimumTriangleArea =
            std::numeric_limits<float>::epsilon();
        for (size_t index = 0u;
            index + 2u < primitive.indices.size();
            index += 3u) {
            const uint32_t index0 =
                primitive.indices[index];
            const uint32_t index1 =
                primitive.indices[index + 1u];
            const uint32_t index2 =
                primitive.indices[index + 2u];
            if (index0 >=
                    primitive.staticVertices.size() ||
                index1 >=
                    primitive.staticVertices.size() ||
                index2 >=
                    primitive.staticVertices.size()) {
                ++analysis.outOfRangeTriangleCount;
                continue;
            }

            const MATH::Vec3& position0 =
                primitive.staticVertices[index0].
                    position;
            const MATH::Vec3& position1 =
                primitive.staticVertices[index1].
                    position;
            const MATH::Vec3& position2 =
                primitive.staticVertices[index2].
                    position;
            if (!MATH::IsFinite(position0) ||
                !MATH::IsFinite(position1) ||
                !MATH::IsFinite(position2)) {
                continue;
            }

            const MATH::Vec3 edge01{
                position1.x - position0.x,
                position1.y - position0.y,
                position1.z - position0.z
            };
            const MATH::Vec3 edge02{
                position2.x - position0.x,
                position2.y - position0.y,
                position2.z - position0.z
            };
            const MATH::Vec3 edge12{
                position2.x - position1.x,
                position2.y - position1.y,
                position2.z - position1.z
            };
            const float edge01LengthSquared =
                LengthSquared(edge01);
            const float edge02LengthSquared =
                LengthSquared(edge02);
            const float edge12LengthSquared =
                LengthSquared(edge12);
            const MATH::Vec3 doubledAreaVector{
                edge01.y * edge02.z -
                    edge01.z * edge02.y,
                edge01.z * edge02.x -
                    edge01.x * edge02.z,
                edge01.x * edge02.y -
                    edge01.y * edge02.x
            };
            const float doubledAreaSquared =
                LengthSquared(doubledAreaVector);
            const float area =
                std::sqrt(doubledAreaSquared) *
                0.5f;
            if (!MATH::IsFinite(
                    edge01LengthSquared) ||
                !MATH::IsFinite(
                    edge02LengthSquared) ||
                !MATH::IsFinite(
                    edge12LengthSquared) ||
                edge01LengthSquared <= 0.0f ||
                edge02LengthSquared <= 0.0f ||
                edge12LengthSquared <= 0.0f ||
                AreXatlasColocal(
                    position0,
                    position1) ||
                AreXatlasColocal(
                    position0,
                    position2) ||
                AreXatlasColocal(
                    position1,
                    position2) ||
                !MATH::IsFinite(area) ||
                area <= kMinimumTriangleArea) {
                ++analysis.degenerateTriangleCount;
                continue;
            }

            input.indices.push_back(index0);
            input.indices.push_back(index1);
            input.indices.push_back(index2);
            ++analysis.validTriangleCount;
        }
        return input;
    }

    std::string DescribeXatlasInputRejection(
        const XatlasInputAnalysis& analysis) {

        std::string message =
            "xatlas input rejected";
        if (analysis.outOfRangeTriangleCount > 0u) {
            message +=
                ": out-of-range triangle indices=" +
                std::to_string(
                    analysis.
                        outOfRangeTriangleCount);
        }
        if (analysis.nonFinitePositionCount > 0u) {
            message +=
                ": non-finite positions=" +
                std::to_string(
                    analysis.
                        nonFinitePositionCount);
        }
        if (analysis.degenerateTriangleCount > 0u) {
            message +=
                ": degenerate triangles=" +
                std::to_string(
                    analysis.
                        degenerateTriangleCount);
        }
        if (analysis.validTriangleCount == 0u) {
            message += ": no usable triangles";
        }
        return message;
    }

    std::string DescribeXatlasInputRepair(
        const XatlasInputAnalysis& analysis) {

        if (analysis.outOfRangeTriangleCount == 0u &&
            analysis.degenerateTriangleCount == 0u) {
            return {};
        }

        std::string message =
            "removed non-renderable source triangles";
        if (analysis.outOfRangeTriangleCount > 0u) {
            message +=
                ": out-of-range indices=" +
                std::to_string(
                    analysis.
                        outOfRangeTriangleCount);
        }
        if (analysis.degenerateTriangleCount > 0u) {
            message +=
                ": degenerate=" +
                std::to_string(
                    analysis.
                        degenerateTriangleCount);
        }
        return message;
    }

} // namespace HIKARI::ASSETS::LIGHTING::INTERNAL
