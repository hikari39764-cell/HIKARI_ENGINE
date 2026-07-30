#include "Tools/Baking/Backends/Internal/HIKARI_DxrLightmapBakeUtilities.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <utility>

#include <DirectXTex.h>

#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI::TOOLS::BAKING::INTERNAL {

    namespace {

        constexpr float kMinimumNormalLengthSquared =
            1.0e-12f;
        constexpr float kMaximumHalfFloat = 65504.0f;

        float Edge(
            const MATH::Vec2& a,
            const MATH::Vec2& b,
            const MATH::Vec2& p) {
            return (p.x - a.x) * (b.y - a.y) -
                (p.y - a.y) * (b.x - a.x);
        }

        void SetMessage(
            std::string* outMessage,
            std::string message) {
            if (outMessage != nullptr) {
                *outMessage = std::move(message);
            }
        }

        bool TryNormalize(
            const MATH::Vec3& value,
            MATH::Vec3& outNormal) {

            const float lengthSquared =
                MATH::Dot(value, value);
            if (!MATH::IsFinite(value) ||
                !MATH::IsFinite(lengthSquared) ||
                lengthSquared <=
                    kMinimumNormalLengthSquared) {
                outNormal = {};
                return false;
            }

            const float inverseLength =
                1.0f / std::sqrt(lengthSquared);
            outNormal = value * inverseLength;
            return MATH::IsFinite(outNormal);
        }

        bool DilateLightmapAllocations(
            std::vector<MATH::Vec4>& pixels,
            std::vector<uint8_t>& valid,
            std::vector<uint32_t>& ownerIndices,
            const std::vector<ASSETS::LIGHTING::
                LightmapAtlasAllocation>& allocations,
            uint32_t width,
            uint32_t height) {

            constexpr uint32_t kInvalidOwner =
                ASSETS::LIGHTING::
                    kInvalidLightmapSurfaceIndex;
            for (uint32_t allocationIndex = 0u;
                 allocationIndex < allocations.size();
                 ++allocationIndex) {
                const ASSETS::LIGHTING::LightmapAtlasRect&
                    rect =
                        allocations[allocationIndex].outerRect;
                if (rect.width == 0u ||
                    rect.height == 0u ||
                    rect.x + rect.width > width ||
                    rect.y + rect.height > height) {
                    return false;
                }

                std::vector<size_t> frontier{};
                frontier.reserve(
                    static_cast<size_t>(rect.width) *
                    rect.height);
                for (uint32_t y = rect.y;
                     y < rect.y + rect.height;
                     ++y) {
                    for (uint32_t x = rect.x;
                         x < rect.x + rect.width;
                         ++x) {
                        const size_t index =
                            static_cast<size_t>(y) * width + x;
                        if (valid[index] != 0u &&
                            ownerIndices[index] ==
                                allocationIndex) {
                            frontier.push_back(index);
                        }
                    }
                }
                if (frontier.empty()) {
                    return false;
                }

                for (size_t read = 0u;
                     read < frontier.size();
                     ++read) {
                    const size_t sourceIndex =
                        frontier[read];
                    const uint32_t sourceX =
                        static_cast<uint32_t>(
                            sourceIndex % width);
                    const uint32_t sourceY =
                        static_cast<uint32_t>(
                            sourceIndex / width);
                    constexpr int neighbors[4][2]{
                        { -1, 0 },
                        { 1, 0 },
                        { 0, -1 },
                        { 0, 1 }
                    };
                    for (const auto& neighbor : neighbors) {
                        const int x =
                            static_cast<int>(sourceX) +
                            neighbor[0];
                        const int y =
                            static_cast<int>(sourceY) +
                            neighbor[1];
                        if (x < static_cast<int>(rect.x) ||
                            y < static_cast<int>(rect.y) ||
                            x >= static_cast<int>(
                                rect.x + rect.width) ||
                            y >= static_cast<int>(
                                rect.y + rect.height)) {
                            continue;
                        }
                        const size_t destinationIndex =
                            static_cast<size_t>(y) * width +
                            static_cast<size_t>(x);
                        if (ownerIndices[destinationIndex] !=
                            kInvalidOwner) {
                            continue;
                        }
                        pixels[destinationIndex] =
                            pixels[sourceIndex];
                        pixels[destinationIndex].w = 1.0f;
                        valid[destinationIndex] = 1u;
                        ownerIndices[destinationIndex] =
                            allocationIndex;
                        frontier.push_back(
                            destinationIndex);
                    }
                }
            }
            return true;
        }

    } // namespace

    DxrLightmapUvRasterization RasterizeLightmapTarget(
        const RENDER3D::LIGHTING::TRANSPORT::
            LightingTransportScene& scene,
        const RENDER3D::LIGHTING::TRANSPORT::
            LightingTransportSurfaceTarget& target,
        uint32_t width,
        uint32_t height) {

        DxrLightmapUvRasterization result{};
        result.texels.resize(
            static_cast<size_t>(width) * height);
        result.validTexels.assign(
            result.texels.size(),
            0u);
        result.ownerIndices.assign(
            result.texels.size(),
            ASSETS::LIGHTING::
                kInvalidLightmapSurfaceIndex);
        std::vector<uint32_t> allocationCoverage(
            target.atlasAllocations.size(),
            0u);

        for (const uint32_t triangleIndex :
             target.triangleIndices) {
            if (triangleIndex >= scene.triangles.size()) {
                continue;
            }
            const RENDER3D::LIGHTING::TRANSPORT::
                LightingTransportTriangle& triangle =
                    scene.triangles[triangleIndex];
            if (triangle.lightmapAllocationIndex >=
                target.atlasAllocations.size()) {
                continue;
            }
            if (!MATH::IsFinite(triangle.position[0]) ||
                !MATH::IsFinite(triangle.position[1]) ||
                !MATH::IsFinite(triangle.position[2])) {
                continue;
            }

            MATH::Vec3 geometricNormal{};
            if (!TryNormalize(
                    MATH::Cross(
                        triangle.position[1] -
                            triangle.position[0],
                        triangle.position[2] -
                            triangle.position[0]),
                    geometricNormal)) {
                continue;
            }
            const MATH::Vec3 summedVertexNormal =
                triangle.normal[0] +
                triangle.normal[1] +
                triangle.normal[2];
            if (MATH::IsFinite(summedVertexNormal) &&
                MATH::Dot(
                    geometricNormal,
                    summedVertexNormal) < 0.0f) {
                geometricNormal =
                    geometricNormal * -1.0f;
            }

            MATH::Vec2 pixelUv[3]{};
            for (uint32_t corner = 0u; corner < 3u; ++corner) {
                pixelUv[corner] = {
                    triangle.lightmapUv[corner].x *
                        static_cast<float>(width),
                    triangle.lightmapUv[corner].y *
                        static_cast<float>(height)
                };
            }

            const float area =
                Edge(pixelUv[0], pixelUv[1], pixelUv[2]);
            if (std::abs(area) <= 1.0e-8f) {
                continue;
            }
            const float inverseArea = 1.0f / area;
            const int minX = std::clamp(
                static_cast<int>(std::floor((std::min)({
                    pixelUv[0].x,
                    pixelUv[1].x,
                    pixelUv[2].x }))),
                0,
                static_cast<int>(width) - 1);
            const int maxX = std::clamp(
                static_cast<int>(std::ceil((std::max)({
                    pixelUv[0].x,
                    pixelUv[1].x,
                    pixelUv[2].x }))),
                0,
                static_cast<int>(width) - 1);
            const int minY = std::clamp(
                static_cast<int>(std::floor((std::min)({
                    pixelUv[0].y,
                    pixelUv[1].y,
                    pixelUv[2].y }))),
                0,
                static_cast<int>(height) - 1);
            const int maxY = std::clamp(
                static_cast<int>(std::ceil((std::max)({
                    pixelUv[0].y,
                    pixelUv[1].y,
                    pixelUv[2].y }))),
                0,
                static_cast<int>(height) - 1);

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
                    if (weights[0] < -1.0e-5f ||
                        weights[1] < -1.0e-5f ||
                        weights[2] < -1.0e-5f) {
                        continue;
                    }

                    const size_t texelIndex =
                        static_cast<size_t>(y) * width +
                        static_cast<size_t>(x);
                    if (result.validTexels[texelIndex] != 0u) {
                        if (result.ownerIndices[texelIndex] !=
                            triangle.
                                lightmapAllocationIndex) {
                            ++result.overlapTexelCount;
                        }
                        continue;
                    }

                    MATH::Vec3 position{};
                    MATH::Vec3 normal{};
                    for (uint32_t corner = 0u;
                        corner < 3u;
                        ++corner) {
                        position = position +
                            triangle.position[corner] *
                                weights[corner];
                        normal = normal +
                            triangle.normal[corner] *
                                weights[corner];
                    }
                    if (!MATH::IsFinite(position)) {
                        continue;
                    }
                    MATH::Vec3 normalizedNormal{};
                    if (!TryNormalize(
                            normal,
                            normalizedNormal)) {
                        normalizedNormal =
                            geometricNormal;
                    }
                    if (!MATH::IsFinite(normalizedNormal)) {
                        continue;
                    }

                    result.validTexels[texelIndex] = 1u;
                    result.ownerIndices[texelIndex] =
                        triangle.lightmapAllocationIndex;
                    ++result.coveredTexelCount;
                    ++allocationCoverage[
                        triangle.lightmapAllocationIndex];
                    result.texels[texelIndex].position = {
                        position.x,
                        position.y,
                        position.z,
                        1.0f
                    };
                    result.texels[texelIndex].normal = {
                        normalizedNormal.x,
                        normalizedNormal.y,
                        normalizedNormal.z,
                        1.0f
                    };
                }
            }
        }
        result.uncoveredAllocationCount =
            static_cast<uint32_t>(std::count(
                allocationCoverage.begin(),
                allocationCoverage.end(),
                0u));
        return result;
    }

    bool WriteProgressiveLightmapDds(
        const std::filesystem::path& outputPath,
        uint32_t width,
        uint32_t height,
        uint32_t expectedSampleCount,
        const MATH::Vec4* accumulation,
        std::vector<uint8_t> validTexels,
        std::vector<uint32_t> ownerIndices,
        const std::vector<ASSETS::LIGHTING::
            LightmapAtlasAllocation>& allocations,
        std::string* outMessage) {

        if (accumulation == nullptr ||
            expectedSampleCount == 0u ||
            validTexels.size() !=
                static_cast<size_t>(width) * height ||
            ownerIndices.size() != validTexels.size() ||
            allocations.empty()) {
            SetMessage(
                outMessage,
                "Lightmap readback data does not match its extent.");
            return false;
        }

        const size_t pixelCount =
            static_cast<size_t>(width) * height;
        std::vector<MATH::Vec4> pixels(pixelCount);
        uint32_t incompleteSampleTexelCount = 0u;
        uint32_t nonFiniteTexelCount = 0u;
        uint32_t negativeTexelCount = 0u;
        uint32_t halfFloatOverflowTexelCount = 0u;
        float maximumRadiance = 0.0f;
        for (size_t i = 0u; i < pixelCount; ++i) {
            if (validTexels[i] == 0u) {
                continue;
            }

            const MATH::Vec4& accumulated =
                accumulation[i];
            if (!MATH::IsFinite(accumulated)) {
                ++nonFiniteTexelCount;
                continue;
            }
            const float expectedSamples =
                static_cast<float>(expectedSampleCount);
            if (std::abs(
                    accumulated.w -
                    expectedSamples) > 0.25f) {
                ++incompleteSampleTexelCount;
                continue;
            }

            pixels[i] = {
                accumulated.x / expectedSamples,
                accumulated.y / expectedSamples,
                accumulated.z / expectedSamples,
                1.0f
            };
            const MATH::Vec4& pixel = pixels[i];
            if (!MATH::IsFinite(pixel)) {
                ++nonFiniteTexelCount;
                continue;
            }
            if (pixel.x < -1.0e-5f ||
                pixel.y < -1.0e-5f ||
                pixel.z < -1.0e-5f) {
                ++negativeTexelCount;
                continue;
            }
            if (pixel.x >= kMaximumHalfFloat ||
                pixel.y >= kMaximumHalfFloat ||
                pixel.z >= kMaximumHalfFloat) {
                ++halfFloatOverflowTexelCount;
                continue;
            }
            pixels[i] = {
                (std::max)(0.0f, pixel.x),
                (std::max)(0.0f, pixel.y),
                (std::max)(0.0f, pixel.z),
                1.0f
            };
            maximumRadiance = (std::max)(
                maximumRadiance,
                (std::max)(
                    pixels[i].x,
                    (std::max)(
                        pixels[i].y,
                        pixels[i].z)));
        }

        if (incompleteSampleTexelCount > 0u ||
            nonFiniteTexelCount > 0u ||
            negativeTexelCount > 0u ||
            halfFloatOverflowTexelCount > 0u) {
            std::ostringstream stream{};
            stream
                << "DXR lightmap rejected invalid transport output:"
                << " incomplete-sample texels="
                << incompleteSampleTexelCount
                << ", non-finite texels="
                << nonFiniteTexelCount
                << ", negative texels="
                << negativeTexelCount
                << ", RGB16F overflow texels="
                << halfFloatOverflowTexelCount
                << ". No artifact was written.";
            SetMessage(outMessage, stream.str());
            return false;
        }
        if (!DilateLightmapAllocations(
                pixels,
                validTexels,
                ownerIndices,
                allocations,
                width,
                height)) {
            SetMessage(
                outMessage,
                "Lightmap owner-aware dilation failed.");
            return false;
        }

        DirectX::ScratchImage source{};
        HRESULT result = source.Initialize2D(
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            width,
            height,
            1u,
            1u);
        if (FAILED(result)) {
            SetMessage(
                outMessage,
                "Failed to allocate lightmap image.");
            return false;
        }
        const DirectX::Image* image =
            source.GetImage(0u, 0u, 0u);
        if (image == nullptr || image->pixels == nullptr) {
            SetMessage(
                outMessage,
                "Lightmap image storage is missing.");
            return false;
        }
        for (uint32_t y = 0u; y < height; ++y) {
            std::memcpy(
                image->pixels + image->rowPitch * y,
                pixels.data() +
                    static_cast<size_t>(y) * width,
                sizeof(MATH::Vec4) * width);
        }

        DirectX::ScratchImage mipChain{};
        result = DirectX::GenerateMipMaps(
            source.GetImages(),
            source.GetImageCount(),
            source.GetMetadata(),
            DirectX::TEX_FILTER_DEFAULT,
            ASSETS::LIGHTING::
                kSurfaceLightmapSafeMipCount,
            mipChain);
        const DirectX::ScratchImage* mipSource =
            SUCCEEDED(result) ? &mipChain : &source;

        DirectX::ScratchImage converted{};
        result = DirectX::Convert(
            mipSource->GetImages(),
            mipSource->GetImageCount(),
            mipSource->GetMetadata(),
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            DirectX::TEX_FILTER_DEFAULT,
            0.0f,
            converted);
        if (FAILED(result)) {
            SetMessage(
                outMessage,
                "Failed to convert lightmap to RGB16F.");
            return false;
        }

        std::error_code error{};
        std::filesystem::create_directories(
            outputPath.parent_path(),
            error);
        if (error) {
            SetMessage(
                outMessage,
                "Failed to create lightmap output directory: " +
                    error.message());
            return false;
        }
        result = DirectX::SaveToDDSFile(
            converted.GetImages(),
            converted.GetImageCount(),
            converted.GetMetadata(),
            DirectX::DDS_FLAGS_NONE,
            outputPath.wstring().c_str());
        if (FAILED(result)) {
            std::ostringstream stream{};
            stream << "Failed to save lightmap DDS, hr=0x"
                << std::hex
                << static_cast<unsigned long>(result);
            SetMessage(outMessage, stream.str());
            return false;
        }
        SetMessage(
            outMessage,
            "DXR indirect lightmap saved: " +
                outputPath.generic_string() +
                " (maximum radiance=" +
                std::to_string(maximumRadiance) +
                ").");
        return true;
    }

} // namespace HIKARI::TOOLS::BAKING::INTERNAL
