#include "Render3D/Lighting/HIKARI_LocalLightList.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"

namespace HIKARI::RENDER3D::LIGHTING {
    namespace {

        LocalLightGpuData ToGpuData(
            const SceneLightProvider& light) {

            LocalLightGpuData gpu{};
            gpu.positionRange = {
                light.position.x,
                light.position.y,
                light.position.z,
                light.range
            };
            gpu.colorIntensity = {
                light.color.x,
                light.color.y,
                light.color.z,
                (std::max)(0.0f, light.intensity)
            };
            if (light.type == SceneLightType::Spot) {
                const MATH::Vec3 direction =
                    ResolveSpotLightDirection(light);
                const SpotLightCone cone = ResolveSpotLightCone(light);
                gpu.directionInnerCos = {
                    direction.x,
                    direction.y,
                    direction.z,
                    cone.innerCos
                };
                gpu.outerCosFalloffTypeFlags = {
                    cone.outerCos,
                    cone.angularFalloff,
                    static_cast<float>(LocalLightType::Spot),
                    static_cast<float>(CanonicalLocalLightFalloff(light.distanceFalloff))
                };
            } else {
                gpu.directionInnerCos = {
                    0.0f,
                    0.0f,
                    1.0f,
                    1.0f
                };
                gpu.outerCosFalloffTypeFlags = {
                    -1.0f,
                    1.0f,
                    static_cast<float>(LocalLightType::Point),
                    static_cast<float>(CanonicalLocalLightFalloff(light.distanceFalloff))
                };
            }
            return gpu;
        }

        uint32_t ResolveDepthSlice(
            float distance,
            float nearDistance,
            float farDistance,
            float depthScale,
            float depthBias) {

            const float clampedDistance = std::clamp(
                distance,
                nearDistance,
                farDistance);
            const float normalized = std::clamp(
                std::log(clampedDistance) * depthScale +
                    depthBias,
                0.0f,
                1.0f);
            return (std::min)(
                static_cast<uint32_t>(
                    normalized *
                    static_cast<float>(
                        kLocalLightClusterDepthSliceCount)),
                kLocalLightClusterDepthSliceCount - 1u);
        }

        struct ClusterBounds {
            uint32_t minX = 0u;
            uint32_t maxX = 0u;
            uint32_t minY = 0u;
            uint32_t maxY = 0u;
            uint32_t minZ = 0u;
            uint32_t maxZ = 0u;
            bool visible = false;
        };

        struct UploadedLightBounds {
            uint32_t lightIndex = 0u;
            ClusterBounds clusters{};
        };

        struct LocalLightListScratch {
            std::vector<uint8_t> clusterCounts{};
            std::vector<uint8_t> clusterWriteCounts{};
            std::vector<UploadedLightBounds> uploadedBounds{};
            std::vector<LocalLightClusterHeader> clusterHeaders{};
            std::vector<uint32_t> clusterIndices{};
        };

        LocalLightListScratch& GetLocalLightListScratch() {
            // 一時配列はスレッド単位で再利用し、
            // フレームごとのヒープ確保を避ける。
            thread_local LocalLightListScratch scratch{};
            return scratch;
        }

        ClusterBounds ProjectLightBounds(
            const SceneLightProvider& light,
            const Camera3D& camera,
            uint32_t screenWidth,
            uint32_t screenHeight,
            uint32_t tileSize,
            uint32_t clusterCountX,
            uint32_t clusterCountY,
            float nearDistance,
            float farDistance,
            float depthScale,
            float depthBias) {

            ClusterBounds bounds{};
            const MATH::Vec4 viewPosition =
                camera.GetView().TransformPoint({
                    light.position.x,
                    light.position.y,
                    light.position.z,
                    1.0f
                });
            const float viewDepth = -viewPosition.z;
            const float range = (std::max)(light.range, 0.05f);
            if (viewDepth + range < nearDistance ||
                viewDepth - range > farDistance) {
                return bounds;
            }

            float minimumNdcX = -1.0f;
            float maximumNdcX = 1.0f;
            float minimumNdcY = -1.0f;
            float maximumNdcY = 1.0f;
            if (viewDepth > range + nearDistance) {
                const float inverseNearestDepth =
                    1.0f / (std::max)(
                        viewDepth - range,
                        nearDistance);
                const MATH::Mat4& projection = camera.GetProj();
                const float centerNdcX =
                    viewPosition.x * projection.m[0][0] /
                    viewDepth;
                const float centerNdcY =
                    viewPosition.y * projection.m[1][1] /
                    viewDepth;
                const float radiusNdcX =
                    range * projection.m[0][0] *
                    inverseNearestDepth;
                const float radiusNdcY =
                    range * projection.m[1][1] *
                    inverseNearestDepth;
                minimumNdcX = centerNdcX - radiusNdcX;
                maximumNdcX = centerNdcX + radiusNdcX;
                minimumNdcY = centerNdcY - radiusNdcY;
                maximumNdcY = centerNdcY + radiusNdcY;
                if (maximumNdcX < -1.0f ||
                    minimumNdcX > 1.0f ||
                    maximumNdcY < -1.0f ||
                    minimumNdcY > 1.0f) {
                    return bounds;
                }
            }

            const auto pixelX = [screenWidth](float ndc) {
                return (ndc * 0.5f + 0.5f) *
                    static_cast<float>(screenWidth);
            };
            const auto pixelY = [screenHeight](float ndc) {
                return (1.0f - (ndc * 0.5f + 0.5f)) *
                    static_cast<float>(screenHeight);
            };
            const float minimumPixelX = std::clamp(
                pixelX(minimumNdcX),
                0.0f,
                static_cast<float>(screenWidth - 1u));
            const float maximumPixelX = std::clamp(
                pixelX(maximumNdcX),
                0.0f,
                static_cast<float>(screenWidth - 1u));
            const float minimumPixelY = std::clamp(
                pixelY(maximumNdcY),
                0.0f,
                static_cast<float>(screenHeight - 1u));
            const float maximumPixelY = std::clamp(
                pixelY(minimumNdcY),
                0.0f,
                static_cast<float>(screenHeight - 1u));
            bounds.minX = (std::min)(
                static_cast<uint32_t>(minimumPixelX) / tileSize,
                clusterCountX - 1u);
            bounds.maxX = (std::min)(
                static_cast<uint32_t>(maximumPixelX) / tileSize,
                clusterCountX - 1u);
            bounds.minY = (std::min)(
                static_cast<uint32_t>(minimumPixelY) / tileSize,
                clusterCountY - 1u);
            bounds.maxY = (std::min)(
                static_cast<uint32_t>(maximumPixelY) / tileSize,
                clusterCountY - 1u);

            const float cameraDistance =
                MATH::Length(light.position - camera.GetPosition());
            bounds.minZ = ResolveDepthSlice(
                (std::max)(cameraDistance - range, nearDistance),
                nearDistance,
                farDistance,
                depthScale,
                depthBias);
            bounds.maxZ = ResolveDepthSlice(
                (std::min)(cameraDistance + range, farDistance),
                nearDistance,
                farDistance,
                depthScale,
                depthBias);
            bounds.visible = true;
            return bounds;
        }

    } // namespace

    const SceneLightProvider* SelectDirectionalLight(
        const SceneLightingSet& lighting) noexcept {

        for (const SceneLightProvider& light : lighting.lights) {
            if (light.enabled &&
                light.type == SceneLightType::Directional &&
                light.intensity > 0.0f) {
                return &light;
            }
        }
        return nullptr;
    }

    LocalLightListView BuildLocalLightList(
        const SceneLightingSet& lighting,
        const Camera3D& camera,
        uint32_t screenWidth,
        uint32_t screenHeight,
        const LocalLightListBuildOutput& output) {

        LocalLightListView view{};
        view.sourceCount = static_cast<uint32_t>(
            std::count_if(
                lighting.lights.begin(),
                lighting.lights.end(),
                [](const SceneLightProvider& light) {
                    return light.type !=
                        SceneLightType::Directional;
                }));
        if (screenWidth == 0u || screenHeight == 0u ||
            output.lights == nullptr ||
            output.clusterHeaders == nullptr ||
            output.clusterIndices == nullptr ||
            output.lightCapacity == 0u ||
            output.clusterHeaderCapacity == 0u ||
            output.clusterIndexCapacity == 0u) {
            view.rejectedCount = view.sourceCount;
            return view;
        }

        view.nearDistance = (std::max)(camera.GetNearZ(), 0.01f);
        view.farDistance = (std::max)(
            camera.GetFarZ(),
            view.nearDistance + 0.01f);
        const float depthLogRange = std::log(
            view.farDistance / view.nearDistance);
        view.depthScale = depthLogRange > 0.0001f
            ? 1.0f / depthLogRange
            : 0.0f;
        view.depthBias =
            -std::log(view.nearDistance) * view.depthScale;
        view.clusterCountZ = kLocalLightClusterDepthSliceCount;
        view.tileSizePixels = 64u;
        for (;;) {
            view.clusterCountX =
                (screenWidth + view.tileSizePixels - 1u) /
                view.tileSizePixels;
            view.clusterCountY =
                (screenHeight + view.tileSizePixels - 1u) /
                view.tileSizePixels;
            const uint64_t clusterCount =
                static_cast<uint64_t>(view.clusterCountX) *
                static_cast<uint64_t>(view.clusterCountY) *
                static_cast<uint64_t>(view.clusterCountZ);
            if (clusterCount <=
                    (std::min)(
                        output.clusterHeaderCapacity,
                        kMaximumLocalLightClusterCount)) {
                view.clusterCount =
                    static_cast<uint32_t>(clusterCount);
                break;
            }
            if (view.tileSizePixels >= 1024u) {
                view.clusterCountX = 0u;
                view.clusterCountY = 0u;
                view.clusterCountZ = 0u;
                view.rejectedCount = view.sourceCount;
                return view;
            }
            view.tileSizePixels *= 2u;
        }

        LocalLightListScratch& scratch =
            GetLocalLightListScratch();
        scratch.clusterCounts.assign(
            view.clusterCount,
            0u);
        scratch.clusterWriteCounts.assign(
            view.clusterCount,
            0u);
        scratch.uploadedBounds.clear();
        scratch.clusterHeaders.assign(
            view.clusterCount,
            LocalLightClusterHeader{});
        std::vector<uint8_t>& clusterCounts =
            scratch.clusterCounts;
        std::vector<uint8_t>& clusterWriteCounts =
            scratch.clusterWriteCounts;
        std::vector<UploadedLightBounds>& uploadedBounds =
            scratch.uploadedBounds;
        std::vector<LocalLightClusterHeader>& clusterHeaders =
            scratch.clusterHeaders;
        uploadedBounds.reserve((std::min)(
            output.lightCapacity,
            kMaximumLocalLightCount));

        for (const SceneLightProvider& light : lighting.lights) {
            if (light.type == SceneLightType::Directional) {
                continue;
            }
            if (!IsLocalLightUploadable(light)) {
                ++view.rejectedCount;
                continue;
            }
            ++view.uploadableCount;
            const ClusterBounds bounds = ProjectLightBounds(
                light, camera, screenWidth, screenHeight, view.tileSizePixels,
                view.clusterCountX, view.clusterCountY, view.nearDistance,
                view.farDistance, view.depthScale, view.depthBias);
            if (!bounds.visible) {
                ++view.culledCount;
                continue;
            }
            if (view.lightCount >=
                (std::min)(
                    output.lightCapacity,
                    kMaximumLocalLightCount)) {
                ++view.rejectedCount;
                continue;
            }

            const uint32_t lightIndex = view.lightCount++;
            output.lights[lightIndex] = ToGpuData(light);
            if (light.type == SceneLightType::Spot) {
                ++view.spotLightCount;
            } else {
                ++view.pointLightCount;
            }
            uploadedBounds.push_back({
                lightIndex,
                bounds
            });

            for (uint32_t z = bounds.minZ; z <= bounds.maxZ; ++z) {
                for (uint32_t y = bounds.minY; y <= bounds.maxY; ++y) {
                    for (uint32_t x = bounds.minX; x <= bounds.maxX; ++x) {
                        const uint32_t clusterIndex =
                            (z * view.clusterCountY + y) *
                            view.clusterCountX + x;
                        uint8_t& count =
                            clusterCounts[clusterIndex];
                        if (count >=
                            kMaximumLocalLightsPerCluster) {
                            ++view.overflowCount;
                            continue;
                        }
                        ++count;
                    }
                }
            }
        }

        for (uint32_t clusterIndex = 0u;
             clusterIndex < view.clusterCount;
             ++clusterIndex) {
            const uint32_t count =
                clusterCounts[clusterIndex];
            LocalLightClusterHeader& header =
                clusterHeaders[clusterIndex];
            header.offset = view.clusterIndexCount;
            const uint32_t remainingCapacity =
                view.clusterIndexCount <
                    output.clusterIndexCapacity
                ? output.clusterIndexCapacity -
                    view.clusterIndexCount
                : 0u;
            header.count = (std::min)(
                count,
                remainingCapacity);
            view.overflowCount += count - header.count;
            view.clusterIndexCount += header.count;
        }

        scratch.clusterIndices.assign(
            view.clusterIndexCount,
            0u);
        std::vector<uint32_t>& clusterIndices =
            scratch.clusterIndices;

        for (const UploadedLightBounds& uploaded :
             uploadedBounds) {
            const ClusterBounds& bounds =
                uploaded.clusters;
            for (uint32_t z = bounds.minZ;
                 z <= bounds.maxZ;
                 ++z) {
                for (uint32_t y = bounds.minY;
                     y <= bounds.maxY;
                     ++y) {
                    for (uint32_t x = bounds.minX;
                         x <= bounds.maxX;
                         ++x) {
                        const uint32_t clusterIndex =
                            (z * view.clusterCountY + y) *
                            view.clusterCountX + x;
                        const LocalLightClusterHeader& header =
                            clusterHeaders[clusterIndex];
                        uint8_t& writeCount =
                            clusterWriteCounts[clusterIndex];
                        if (writeCount >= header.count) {
                            continue;
                        }
                        clusterIndices[
                            header.offset + writeCount] =
                            uploaded.lightIndex;
                        ++writeCount;
                    }
                }
            }
        }
        std::memcpy(
            output.clusterHeaders,
            clusterHeaders.data(),
            static_cast<size_t>(view.clusterCount) *
                sizeof(LocalLightClusterHeader));
        if (!clusterIndices.empty()) {
            std::memcpy(
                output.clusterIndices,
                clusterIndices.data(),
                static_cast<size_t>(view.clusterIndexCount) *
                    sizeof(uint32_t));
        }
        return view;
    }

} // namespace HIKARI::RENDER3D::LIGHTING
