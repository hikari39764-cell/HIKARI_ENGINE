#include "Render3D/Lighting/HIKARI_LocalLightList.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"

namespace HIKARI::RENDER3D::LIGHTING {
    LocalLightGpuData BuildLocalLightGpuData(
        const SceneLightProvider& light) noexcept {

        LocalLightGpuData gpu{};
        gpu.sourceRadius = SanitizeLocalLightSourceRadius(light.sourceRadius);
        gpu.kind = static_cast<uint32_t>(CanonicalLocalLightFalloff(light.distanceFalloff)) << 8u;
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
            gpu.outerConeCos = cone.outerCos;
            gpu.angularFalloff = cone.angularFalloff;
            gpu.kind |= static_cast<uint32_t>(LocalLightType::Spot);
        } else {
            gpu.directionInnerCos = {
                0.0f,
                0.0f,
                1.0f,
                1.0f
            };
        }
        return gpu;
    }

    namespace {

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

        MATH::Vec2 ProjectViewAxisBounds(
            float center, float radius, float inverseNear, float inverseFar, float scale) {
            // 球を含む View 空間 AABB の両深度面を投影する。画面端の遠近差も含める。
            const float lowNear = (center - radius) * inverseNear * scale;
            const float lowFar = (center - radius) * inverseFar * scale;
            const float highNear = (center + radius) * inverseNear * scale;
            const float highFar = (center + radius) * inverseFar * scale;
            return {(std::min)(lowNear, lowFar), (std::max)(highNear, highFar)};
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
            if (!MATH::IsFinite(viewPosition)) return bounds;
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
            const MATH::Mat4& projection = camera.GetProj();
            if (camera.IsOrthographic()) {
                // 正射影の XY 範囲は深度に依存しない。近面を跨ぐ球にも同じ投影を使う。
                const float centerX = viewPosition.x * projection.m[0][0] + projection.m[3][0];
                const float centerY = viewPosition.y * projection.m[1][1] + projection.m[3][1];
                const float radiusX = range * std::abs(projection.m[0][0]);
                const float radiusY = range * std::abs(projection.m[1][1]);
                minimumNdcX = centerX - radiusX;
                maximumNdcX = centerX + radiusX;
                minimumNdcY = centerY - radiusY;
                maximumNdcY = centerY + radiusY;
            } else if (viewDepth > range + nearDistance) {
                const float inverseNearestDepth =
                    1.0f / (std::max)(
                        viewDepth - range,
                        nearDistance);
                const float inverseFarthestDepth = 1.0f / (viewDepth + range);
                const auto xBounds = ProjectViewAxisBounds(viewPosition.x, range,
                    inverseNearestDepth, inverseFarthestDepth, projection.m[0][0]);
                const auto yBounds = ProjectViewAxisBounds(viewPosition.y, range,
                    inverseNearestDepth, inverseFarthestDepth, projection.m[1][1]);
                minimumNdcX = xBounds.x;
                maximumNdcX = xBounds.y;
                minimumNdcY = yBounds.x;
                maximumNdcY = yBounds.y;
            }
            if (maximumNdcX < -1.0f ||
                minimumNdcX > 1.0f ||
                maximumNdcY < -1.0f ||
                minimumNdcY > 1.0f) {
                return bounds;
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

            bounds.minZ = ResolveDepthSlice(
                (std::max)(viewDepth - range, nearDistance),
                nearDistance,
                farDistance,
                depthScale,
                depthBias);
            bounds.maxZ = ResolveDepthSlice(
                (std::min)(viewDepth + range, farDistance),
                nearDistance,
                farDistance,
                depthScale,
                depthBias);
            bounds.visible = true;
            return bounds;
        }

        bool InitializeLocalLightGrid(LocalLightListView& view, const Camera3D& camera,
            uint32_t screenWidth, uint32_t screenHeight, uint32_t clusterCapacity) {
            if (!screenWidth || !screenHeight ||
                screenWidth > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
                screenHeight > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || !MATH::IsFinite(camera.GetView()) ||
                !MATH::IsFinite(camera.GetProj()) || !std::isfinite(camera.GetNearZ()) ||
                !std::isfinite(camera.GetFarZ())) return false;
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
                            clusterCapacity,
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
                    return false;
                }
                view.tileSizePixels *= 2u;
            }

            return true;
        }

        template<class Visitor>
        void VisitVisibleLocalLights(const SceneLightingSet& lighting, const Camera3D& camera,
            uint32_t screenWidth, uint32_t screenHeight, uint32_t lightCapacity,
            LocalLightListView& view, Visitor&& visitor) {
            for (uint32_t sourceIndex = 0; sourceIndex < lighting.lights.size(); ++sourceIndex) {
                const auto& light = lighting.lights[sourceIndex];
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
                        lightCapacity,
                        kMaximumLocalLightCount)) {
                    ++view.rejectedCount;
                    continue;
                }

                const uint32_t lightIndex = view.lightCount++;
                if (light.type == SceneLightType::Spot) ++view.spotLightCount;
                else ++view.pointLightCount;
                visitor(light, sourceIndex, lightIndex, bounds);
            }
        }

    } // namespace

    const SceneLightProvider* SelectDirectionalLight(
        const SceneLightingSet& lighting) noexcept {

        const SceneLightProvider* selected = nullptr;
        for (const SceneLightProvider& light : lighting.lights) {
            if (light.enabled &&
                light.type == SceneLightType::Directional &&
                std::isfinite(light.intensity) && light.intensity > 0.0f &&
                (selected == nullptr || light.priority > selected->priority)) {
                selected = &light;
            }
        }
        return selected;
    }

    DirectionalLight ResolveDirectionalLight(const SceneLightingSet& lighting) noexcept {
        DirectionalLight result{};
        const auto* source = SelectDirectionalLight(lighting);
        result.enabled = source != nullptr;
        result.intensity = source ? source->intensity : 0.0f;
        if (source) {
            result.direction = source->direction;
            result.color = source->color;
        }
        return result;
    }

    LocalLightListView SelectVisibleLocalLights(const SceneLightingSet& lighting,
        const Camera3D& camera, uint32_t screenWidth, uint32_t screenHeight,
        std::span<uint32_t> sourceIndices) {
        LocalLightListView view{};
        view.sourceCount = uint32_t(std::count_if(lighting.lights.begin(), lighting.lights.end(),
            [](const auto& light) { return light.type != SceneLightType::Directional; }));
        if (sourceIndices.empty() || !InitializeLocalLightGrid(view,camera,screenWidth,screenHeight,kMaximumLocalLightClusterCount)) {
            view.rejectedCount = view.sourceCount;
            return view;
        }
        VisitVisibleLocalLights(lighting,camera,screenWidth,screenHeight,
            uint32_t((std::min)(sourceIndices.size(),size_t(kMaximumLocalLightCount))),view,
            [&](const SceneLightProvider&,uint32_t sourceIndex,uint32_t lightIndex,const ClusterBounds&) {
                sourceIndices[lightIndex] = sourceIndex;
            });
        return view;
    }

    LocalLightListView BuildLocalLightList(
        const SceneLightingSet& lighting,
        const Camera3D& camera,
        uint32_t screenWidth,
        uint32_t screenHeight,
        const LocalLightListBuildOutput& output,
        std::span<const LocalLightShadowAssignment> shadows) {

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

        if (!InitializeLocalLightGrid(view, camera, screenWidth, screenHeight, output.clusterHeaderCapacity)) {
            view.rejectedCount = view.sourceCount;
            return view;
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

        VisitVisibleLocalLights(lighting, camera, screenWidth, screenHeight, output.lightCapacity, view,
            [&](const SceneLightProvider& light, uint32_t sourceIndex, uint32_t lightIndex, const ClusterBounds& bounds) {
            output.lights[lightIndex] = BuildLocalLightGpuData(light);
            for (const auto& shadow : shadows) {
                if (shadow.sourceIndex == sourceIndex &&
                    shadow.firstFace < kMaximumLocalShadowFaceCount && light.castsShadows) {
                    output.lights[lightIndex].kind |= (shadow.firstFace + 1u) << 16u;
                    break;
                }
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
        });

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
        view.lightingContentHash = HASH::Fnv1aBytes(HASH::kFnv1a64OffsetBasis,
            output.lights, static_cast<size_t>(view.lightCount) * sizeof(LocalLightGpuData));
        return view;
    }

} // namespace HIKARI::RENDER3D::LIGHTING
