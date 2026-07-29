#include "Assets/Lighting/HIKARI_LightmapSurfaceAnalysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace HIKARI::ASSETS::LIGHTING {

    namespace {

        void AddIssue(
            LightmapSurfaceAnalysis& analysis,
            LightmapSurfaceIssue issue) {
            analysis.issueMask |=
                static_cast<uint32_t>(issue);
        }

        bool IsFiniteUv(const MATH::Vec2& uv) {
            return
                std::isfinite(uv.x) &&
                std::isfinite(uv.y);
        }

        bool IsInsideUnitRange(const MATH::Vec2& uv) {
            constexpr float kTolerance = 0.0001f;
            return
                uv.x >= -kTolerance &&
                uv.y >= -kTolerance &&
                uv.x <= 1.0f + kTolerance &&
                uv.y <= 1.0f + kTolerance;
        }

        bool IsDegenerateUvTriangle(
            const MATH::Vec2& uv0,
            const MATH::Vec2& uv1,
            const MATH::Vec2& uv2) {
            const MATH::Vec2 edge0 = uv1 - uv0;
            const MATH::Vec2 edge1 = uv2 - uv0;
            const float twiceArea =
                edge0.x * edge1.y -
                edge0.y * edge1.x;
            return std::abs(twiceArea) <= 1.0e-8f;
        }

        float Edge(
            const MATH::Vec2& first,
            const MATH::Vec2& second,
            const MATH::Vec2& point) {
            return
                (point.x - first.x) *
                    (second.y - first.y) -
                (point.y - first.y) *
                    (second.x - first.x);
        }

        constexpr uint32_t kOverlapGridResolution = 32u;

        void RasterizeUvOverlapSamples(
            const MATH::Vec2 (&uv)[3],
            std::array<
                uint8_t,
                kOverlapGridResolution *
                    kOverlapGridResolution>& occupancy,
            LightmapSurfaceAnalysis& analysis) {

            MATH::Vec2 pixelUv[3]{};
            for (uint32_t corner = 0u;
                corner < 3u;
                ++corner) {
                pixelUv[corner] = uv[corner] *
                    static_cast<float>(
                        kOverlapGridResolution);
            }
            const float area =
                Edge(
                    pixelUv[0],
                    pixelUv[1],
                    pixelUv[2]);
            if (std::abs(area) <= 1.0e-8f) {
                return;
            }
            const float inverseArea = 1.0f / area;
            const int minX = std::clamp(
                static_cast<int>(
                    std::floor((std::min)({
                        pixelUv[0].x,
                        pixelUv[1].x,
                        pixelUv[2].x }))),
                0,
                static_cast<int>(
                    kOverlapGridResolution) - 1);
            const int maxX = std::clamp(
                static_cast<int>(
                    std::ceil((std::max)({
                        pixelUv[0].x,
                        pixelUv[1].x,
                        pixelUv[2].x }))),
                0,
                static_cast<int>(
                    kOverlapGridResolution) - 1);
            const int minY = std::clamp(
                static_cast<int>(
                    std::floor((std::min)({
                        pixelUv[0].y,
                        pixelUv[1].y,
                        pixelUv[2].y }))),
                0,
                static_cast<int>(
                    kOverlapGridResolution) - 1);
            const int maxY = std::clamp(
                static_cast<int>(
                    std::ceil((std::max)({
                        pixelUv[0].y,
                        pixelUv[1].y,
                        pixelUv[2].y }))),
                0,
                static_cast<int>(
                    kOverlapGridResolution) - 1);

            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) {
                    const MATH::Vec2 sample{
                        static_cast<float>(x) + 0.5f,
                        static_cast<float>(y) + 0.5f
                    };
                    const float weights[3]{
                        Edge(
                            pixelUv[1],
                            pixelUv[2],
                            sample) * inverseArea,
                        Edge(
                            pixelUv[2],
                            pixelUv[0],
                            sample) * inverseArea,
                        Edge(
                            pixelUv[0],
                            pixelUv[1],
                            sample) * inverseArea
                    };
                    constexpr float kInteriorEpsilon =
                        1.0e-5f;
                    if (weights[0] <= kInteriorEpsilon ||
                        weights[1] <= kInteriorEpsilon ||
                        weights[2] <= kInteriorEpsilon) {
                        continue;
                    }
                    const size_t sampleIndex =
                        static_cast<size_t>(y) *
                            kOverlapGridResolution +
                        static_cast<size_t>(x);
                    if (occupancy[sampleIndex] != 0u) {
                        ++analysis.uvOverlapSampleCount;
                    } else {
                        occupancy[sampleIndex] = 1u;
                        ++analysis.uvCoveredSampleCount;
                    }
                }
            }
        }

        void AppendIssue(
            std::vector<std::string>& issues,
            bool present,
            const char* text) {
            if (present) {
                issues.emplace_back(text);
            }
        }

    } // namespace

    LightmapSurfaceAnalysis AnalyzeLightmapSurface(
        const MeshPrimitive& primitive,
        AlphaMode alphaMode) {

        LightmapSurfaceAnalysis analysis{};
        if (primitive.layout !=
            VertexLayoutKind::StaticPNTT) {
            AddIssue(
                analysis,
                LightmapSurfaceIssue::
                    UnsupportedVertexLayout);
        }
        if (primitive.hasMorphTargets) {
            AddIssue(
                analysis,
                LightmapSurfaceIssue::
                    MorphTargetsUnsupported);
        }
        if (primitive.staticVertices.empty() ||
            primitive.indices.size() < 3u) {
            AddIssue(
                analysis,
                LightmapSurfaceIssue::MissingGeometry);
        }
        if (alphaMode == AlphaMode::Mask) {
            AddIssue(
                analysis,
                LightmapSurfaceIssue::
                    AlphaMaskedSurface);
        } else if (alphaMode == AlphaMode::Blend) {
            AddIssue(
                analysis,
                LightmapSurfaceIssue::BlendedSurface);
        }

        if (analysis.HasIssue(
                LightmapSurfaceIssue::
                    UnsupportedVertexLayout) ||
            analysis.HasIssue(
                LightmapSurfaceIssue::
                    MissingGeometry) ||
            analysis.HasIssue(
                LightmapSurfaceIssue::
                    AlphaMaskedSurface) ||
            analysis.HasIssue(
                LightmapSurfaceIssue::
                    BlendedSurface)) {
            return analysis;
        }

        std::array<
            uint8_t,
            kOverlapGridResolution *
                kOverlapGridResolution> uvOccupancy{};
        for (size_t index = 0u;
            index + 2u < primitive.indices.size();
            index += 3u) {
            const uint32_t indices[3]{
                primitive.indices[index],
                primitive.indices[index + 1u],
                primitive.indices[index + 2u]
            };
            if (indices[0] >=
                    primitive.staticVertices.size() ||
                indices[1] >=
                    primitive.staticVertices.size() ||
                indices[2] >=
                    primitive.staticVertices.size()) {
                AddIssue(
                    analysis,
                    LightmapSurfaceIssue::
                        IndexOutOfRange);
                continue;
            }

            const MATH::Vec2 uv[3]{
                primitive.staticVertices[
                    indices[0]].uv1,
                primitive.staticVertices[
                    indices[1]].uv1,
                primitive.staticVertices[
                    indices[2]].uv1
            };
            bool finite = true;
            for (const MATH::Vec2& corner : uv) {
                if (!IsFiniteUv(corner)) {
                    AddIssue(
                        analysis,
                        LightmapSurfaceIssue::
                            UvNonFinite);
                    finite = false;
                } else if (!IsInsideUnitRange(corner)) {
                    AddIssue(
                        analysis,
                        LightmapSurfaceIssue::
                            UvOutsideUnitRange);
                }
            }
            if (finite &&
                IsDegenerateUvTriangle(
                    uv[0],
                    uv[1],
                    uv[2])) {
                AddIssue(
                    analysis,
                    LightmapSurfaceIssue::
                        UvDegenerate);
            } else if (finite &&
                !analysis.HasIssue(
                    LightmapSurfaceIssue::
                        UvOutsideUnitRange)) {
                RasterizeUvOverlapSamples(
                    uv,
                    uvOccupancy,
                    analysis);
            }
            ++analysis.triangleCount;
        }
        if (analysis.uvOverlapSampleCount >
            analysis.uvCoveredSampleCount / 100u + 2u) {
            AddIssue(
                analysis,
                LightmapSurfaceIssue::UvOverlap);
        }
        return analysis;
    }

    std::string DescribeLightmapSurfaceAnalysis(
        const LightmapSurfaceAnalysis& analysis) {

        if (analysis.IsEligible()) {
            return "Ready";
        }

        std::vector<std::string> issues{};
        AppendIssue(
            issues,
            analysis.HasIssue(
                LightmapSurfaceIssue::
                    UnsupportedVertexLayout),
            "unsupported vertex layout");
        AppendIssue(
            issues,
            analysis.HasIssue(
                LightmapSurfaceIssue::
                    MorphTargetsUnsupported),
            "morph targets are not bakeable");
        AppendIssue(
            issues,
            analysis.HasIssue(
                LightmapSurfaceIssue::MissingGeometry),
            "missing triangle geometry");
        AppendIssue(
            issues,
            analysis.HasIssue(
                LightmapSurfaceIssue::
                    IndexOutOfRange),
            "index out of range");
        AppendIssue(
            issues,
            analysis.HasIssue(
                LightmapSurfaceIssue::UvNonFinite),
            "UV1 is not finite");
        AppendIssue(
            issues,
            analysis.HasIssue(
                LightmapSurfaceIssue::
                    UvOutsideUnitRange),
            "UV1 is outside [0, 1]");
        AppendIssue(
            issues,
            analysis.HasIssue(
                LightmapSurfaceIssue::UvDegenerate),
            "UV1 contains degenerate triangles");
        AppendIssue(
            issues,
            analysis.HasIssue(
                LightmapSurfaceIssue::UvOverlap),
            "UV1 contains overlapping islands");
        AppendIssue(
            issues,
            analysis.HasIssue(
                LightmapSurfaceIssue::
                    AlphaMaskedSurface),
            "alpha-masked surface needs DXR any-hit");
        AppendIssue(
            issues,
            analysis.HasIssue(
                LightmapSurfaceIssue::BlendedSurface),
            "blended surface is excluded");

        std::string description{};
        for (const std::string& issue : issues) {
            if (!description.empty()) {
                description += "; ";
            }
            description += issue;
        }
        return description.empty()
            ? "No bakeable triangles"
            : description;
    }

} // namespace HIKARI::ASSETS::LIGHTING
