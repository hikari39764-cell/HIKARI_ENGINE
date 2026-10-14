#include "Render3D/Lighting/HIKARI_LocalLightList.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Decal/HIKARI_LocalDecalRecord.h"
#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"

namespace HIKARI::RENDER3D::LIGHTING {
LocalLightGpuData BuildLocalLightGpuData(const SceneLightProvider& light) noexcept {
    LocalLightGpuData gpu{};
    gpu.sourceRadius = SanitizeLocalLightSourceRadius(light.sourceRadius);
    gpu.positionRange = {light.position.x, light.position.y, light.position.z, light.range};
    gpu.colorIntensity = {light.color.x, light.color.y, light.color.z, (std::max)(0.0f, light.intensity)};
    switch (light.type) {
    case SceneLightType::Spot: {
        const MATH::Vec3 direction = ResolveSpotLightDirection(light);
        const SpotLightCone cone = ResolveSpotLightCone(light);
        gpu.directionInnerCos = {direction.x, direction.y, direction.z, cone.innerCos};
        gpu.outerConeCos = cone.outerCos;
        gpu.angularFalloff = cone.angularFalloff;
        gpu.kind |= static_cast<uint32_t>(LocalLightType::Spot);
        break;
    }
    case SceneLightType::Tube: {
        const MATH::Vec3 axis = ResolveAreaLightTangent(light, {0.0f, 0.0f, 0.0f});
        gpu.directionInnerCos = {axis.x, axis.y, axis.z, 0.5f * light.length};
        gpu.kind |= static_cast<uint32_t>(LocalLightType::Tube);
        break;
    }
    case SceneLightType::Rect: {
        const MATH::Vec3 normal = ResolveSpotLightDirection(light);
        const MATH::Vec3 tangent = ResolveAreaLightTangent(light, normal);
        gpu.directionInnerCos = {normal.x, normal.y, normal.z, 0.5f * light.length};
        gpu.shapeTangent = {tangent.x, tangent.y, tangent.z, 0.5f * light.height};
        // 片面の Lambert 発光面は法線方向に光度 Φ/π を持つ（等方の Φ/4π の 4 倍）。
        gpu.colorIntensity.w *= 4.0f;
        gpu.kind |= static_cast<uint32_t>(LocalLightType::Rect);
        break;
    }
    case SceneLightType::Point: {
        const MATH::Vec3 forward = ResolveSpotLightDirection(light);
        gpu.directionInnerCos = {forward.x, forward.y, forward.z, 1.0f};
        break;
    }
    }
    if (light.type == SceneLightType::Point || light.type == SceneLightType::Spot) {
        // 配光とクッキーの水平の基準（ローカル +X）と、テクスチャの記述子番号 + 1 を整数値のまま float に詰めたもの
        // （配光 + クッキー * 4096、2^24 未満なので正確に表せる）。
        const MATH::Vec3 right = ResolveAreaLightTangent(light, ResolveSpotLightDirection(light));
        const uint32_t textures = light.iesProfileTexture + light.cookieTexture * kLocalLightTextureIndexStride;
        gpu.shapeTangent = {right.x, right.y, right.z, static_cast<float>(textures)};
    }
    return gpu;
}

namespace {

uint32_t ResolveDepthSlice(float distance, float nearDistance, float farDistance, float depthScale, float depthBias) {
    const float clampedDistance = std::clamp(distance, nearDistance, farDistance);
    const float normalized = std::clamp(std::log(clampedDistance) * depthScale + depthBias, 0.0f, 1.0f);
    return (std::min)(static_cast<uint32_t>(normalized * static_cast<float>(kLocalLightClusterDepthSliceCount)),
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

struct UploadedRecordBounds {
    uint32_t recordIndex = 0u;
    ClusterBounds clusters{};
};

// 光錐を含む画面の列だけを残す。霧は同じ列の別の深度を標本にするため、Z は狭めない。
ClusterBounds RefineSpotLightColumns(const LocalLightGpuData& light, ClusterBounds bounds,
                                     const MATH::Mat4& viewProjection, uint32_t width, uint32_t height,
                                     uint32_t tileSize) {
    const MATH::Vec3 axis =
        MATH::Normalize({light.directionInnerCos.x, light.directionInnerCos.y, light.directionInnerCos.z});
    // GPU の正規化・射影の丸めで境界の光を落とさないよう、光錐と分離面を外側へ広げる。
    const double cosine = std::clamp(double(light.outerConeCos) - 1.0e-5, 0.0, 1.0);
    const double sine = std::sqrt(1.0 - cosine * cosine);
    const auto outside = [&](uint32_t coordinate, double edge, double sign) {
        double plane[4]{};
        for (uint32_t i = 0u; i < 4u; ++i)
            plane[i] = sign * (double(viewProjection.m[i][coordinate]) - edge * viewProjection.m[i][3]);
        const double length = std::sqrt(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
        const double axial = plane[0] * axis.x + plane[1] * axis.y + plane[2] * axis.z;
        const double radial = std::sqrt((std::max)(length * length - axial * axial, 0.0));
        // range 球と光錐の共通部分の support。頂点も含み、近面を跨ぐ灯りにも除算なしで使える。
        const double extent = double(light.positionRange.w) *
                              (axial >= length * cosine ? length : (std::max)(0.0, axial * cosine + radial * sine));
        const double x = plane[0] * light.positionRange.x;
        const double y = plane[1] * light.positionRange.y;
        const double z = plane[2] * light.positionRange.z;
        const double tolerance = 1.0e-5 * (std::abs(x) + std::abs(y) + std::abs(z) + std::abs(plane[3]) +
                                           double(light.positionRange.w) * length + 1.0);
        return x + y + z + plane[3] + extent < -tolerance;
    };
    const auto trim = [&](uint32_t& first, uint32_t& last, uint32_t coordinate, uint32_t pixels) {
        const double sign = coordinate == 0u ? 1.0 : -1.0;
        // リストのカメラは非 jitter。画素中心と時域 AA の subpixel 移動を含む余白を残す。
        const auto edge = [&](uint32_t tile, double padding) {
            return sign * (2.0 * (double(tile * tileSize) + padding) / pixels - 1.0);
        };
        while (outside(coordinate, edge(first + 1u, 2.0), -sign)) {
            if (first == last)
                return false;
            ++first;
        }
        while (outside(coordinate, edge(last, -2.0), sign)) {
            if (first == last)
                return false;
            --last;
        }
        return true;
    };
    bounds.visible = trim(bounds.minX, bounds.maxX, 0u, width) && trim(bounds.minY, bounds.maxY, 1u, height);
    return bounds;
}

void CompactSpotLightClusters(LocalLightListView& view, std::vector<UploadedRecordBounds>& bounds,
                              const LocalLightGpuData* records, std::vector<LocalLightClusterHeader>& headers,
                              std::vector<uint32_t>& indices, const Camera3D& camera, uint32_t width, uint32_t height) {
    if (view.spotLightCount == 0u)
        return;
    const MATH::Mat4 viewProjection = camera.GetViewProj();
    for (auto& uploaded : bounds) {
        const auto& light = records[uploaded.recordIndex];
        if ((light.kind & 0xffu) == static_cast<uint32_t>(LocalLightType::Spot))
            uploaded.clusters =
                RefineSpotLightColumns(light, uploaded.clusters, viewProjection, width, height, view.tileSizePixels);
    }
    // 元の名額・index 容量で選んだ後に詰める。除外した灯りの代わりに上限外の灯りを繰り上げない。
    // 光源の順序、影の割当、後続デカールもそのまま保ち、別の GPU リストは増やさない。
    uint32_t cursor = 0u;
    for (uint32_t cluster = 0u; cluster < view.clusterCount; ++cluster) {
        auto& header = headers[cluster];
        const uint32_t previousOffset = header.offset;
        const uint32_t lightCount = header.count & 0xffffu;
        const uint32_t decalCount = header.count >> 16u;
        const uint32_t x = cluster % view.clusterCountX;
        const uint32_t y = (cluster / view.clusterCountX) % view.clusterCountY;
        header.offset = cursor;
        for (uint32_t i = 0u; i < lightCount; ++i) {
            const uint32_t record = indices[previousOffset + i];
            const auto& columns = bounds[record].clusters;
            if (columns.visible && x >= columns.minX && x <= columns.maxX && y >= columns.minY && y <= columns.maxY)
                indices[cursor++] = record;
        }
        header.count = (cursor - header.offset) | (decalCount << 16u);
        for (uint32_t i = 0u; i < decalCount; ++i)
            indices[cursor++] = indices[previousOffset + lightCount + i];
    }
    view.clusterIndexCount = cursor;
}

struct LocalLightListScratch {
    std::vector<uint8_t> clusterCounts{};
    std::vector<uint8_t> clusterWriteCounts{};
    std::vector<uint8_t> clusterDecalCounts{};
    std::vector<uint8_t> clusterDecalWriteCounts{};
    std::vector<UploadedRecordBounds> uploadedBounds{};
    std::vector<UploadedRecordBounds> uploadedDecalBounds{};
    std::vector<LocalLightClusterHeader> clusterHeaders{};
    std::vector<uint32_t> clusterIndices{};
};

LocalLightListScratch& GetLocalLightListScratch() {
    // 一時配列はスレッド単位で再利用し、
    // フレームごとのヒープ確保を避ける。
    thread_local LocalLightListScratch scratch{};
    return scratch;
}

MATH::Vec2 ProjectViewAxisBounds(float center, float radius, float inverseNear, float inverseFar, float scale) {
    // 球を含む View 空間 AABB の両深度面を投影する。画面端の遠近差も含める。
    const float lowNear = (center - radius) * inverseNear * scale;
    const float lowFar = (center - radius) * inverseFar * scale;
    const float highNear = (center + radius) * inverseNear * scale;
    const float highFar = (center + radius) * inverseFar * scale;
    return {(std::min)(lowNear, lowFar), (std::max)(highNear, highFar)};
}

// 球が掛かるクラスタの範囲。
ClusterBounds ProjectSphereBounds(const MATH::Vec3& center, float radius, const Camera3D& camera, uint32_t screenWidth,
                                  uint32_t screenHeight, const LocalLightListView& view) {
    const uint32_t tileSize = view.tileSizePixels;
    const uint32_t clusterCountX = view.clusterCountX;
    const uint32_t clusterCountY = view.clusterCountY;
    const float nearDistance = view.nearDistance;
    const float farDistance = view.farDistance;
    const float depthScale = view.depthScale;
    const float depthBias = view.depthBias;
    ClusterBounds bounds{};
    const MATH::Vec4 viewPosition = camera.GetView().TransformPoint({center.x, center.y, center.z, 1.0f});
    if (!MATH::IsFinite(viewPosition))
        return bounds;
    const float viewDepth = -viewPosition.z;
    const float range = (std::max)(radius, 0.05f);
    if (viewDepth + range < nearDistance || viewDepth - range > farDistance) {
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
        const float inverseNearestDepth = 1.0f / (std::max)(viewDepth - range, nearDistance);
        const float inverseFarthestDepth = 1.0f / (viewDepth + range);
        const auto xBounds =
            ProjectViewAxisBounds(viewPosition.x, range, inverseNearestDepth, inverseFarthestDepth, projection.m[0][0]);
        const auto yBounds =
            ProjectViewAxisBounds(viewPosition.y, range, inverseNearestDepth, inverseFarthestDepth, projection.m[1][1]);
        minimumNdcX = xBounds.x;
        maximumNdcX = xBounds.y;
        minimumNdcY = yBounds.x;
        maximumNdcY = yBounds.y;
    }
    if (maximumNdcX < -1.0f || minimumNdcX > 1.0f || maximumNdcY < -1.0f || minimumNdcY > 1.0f) {
        return bounds;
    }

    const auto pixelX = [screenWidth](float ndc) { return (ndc * 0.5f + 0.5f) * static_cast<float>(screenWidth); };
    const auto pixelY = [screenHeight](float ndc) {
        return (1.0f - (ndc * 0.5f + 0.5f)) * static_cast<float>(screenHeight);
    };
    const float minimumPixelX = std::clamp(pixelX(minimumNdcX), 0.0f, static_cast<float>(screenWidth - 1u));
    const float maximumPixelX = std::clamp(pixelX(maximumNdcX), 0.0f, static_cast<float>(screenWidth - 1u));
    const float minimumPixelY = std::clamp(pixelY(maximumNdcY), 0.0f, static_cast<float>(screenHeight - 1u));
    const float maximumPixelY = std::clamp(pixelY(minimumNdcY), 0.0f, static_cast<float>(screenHeight - 1u));
    bounds.minX = (std::min)(static_cast<uint32_t>(minimumPixelX) / tileSize, clusterCountX - 1u);
    bounds.maxX = (std::min)(static_cast<uint32_t>(maximumPixelX) / tileSize, clusterCountX - 1u);
    bounds.minY = (std::min)(static_cast<uint32_t>(minimumPixelY) / tileSize, clusterCountY - 1u);
    bounds.maxY = (std::min)(static_cast<uint32_t>(maximumPixelY) / tileSize, clusterCountY - 1u);

    bounds.minZ = ResolveDepthSlice((std::max)(viewDepth - range, nearDistance), nearDistance, farDistance, depthScale,
                                    depthBias);
    bounds.maxZ =
        ResolveDepthSlice((std::min)(viewDepth + range, farDistance), nearDistance, farDistance, depthScale, depthBias);
    bounds.visible = true;
    return bounds;
}

bool InitializeLocalLightGrid(LocalLightListView& view, const Camera3D& camera, uint32_t screenWidth,
                              uint32_t screenHeight, uint32_t clusterCapacity) {
    if (!screenWidth || !screenHeight || screenWidth > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        screenHeight > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || !MATH::IsFinite(camera.GetView()) ||
        !MATH::IsFinite(camera.GetProj()) || !std::isfinite(camera.GetNearZ()) || !std::isfinite(camera.GetFarZ()))
        return false;
    view.nearDistance = (std::max)(camera.GetNearZ(), 0.01f);
    view.farDistance = (std::max)(camera.GetFarZ(), view.nearDistance + 0.01f);
    const float depthLogRange = std::log(view.farDistance / view.nearDistance);
    view.depthScale = depthLogRange > 0.0001f ? 1.0f / depthLogRange : 0.0f;
    view.depthBias = -std::log(view.nearDistance) * view.depthScale;
    view.clusterCountZ = kLocalLightClusterDepthSliceCount;
    view.tileSizePixels = 64u;
    for (;;) {
        view.clusterCountX = (screenWidth + view.tileSizePixels - 1u) / view.tileSizePixels;
        view.clusterCountY = (screenHeight + view.tileSizePixels - 1u) / view.tileSizePixels;
        const uint64_t clusterCount = static_cast<uint64_t>(view.clusterCountX) *
                                      static_cast<uint64_t>(view.clusterCountY) *
                                      static_cast<uint64_t>(view.clusterCountZ);
        if (clusterCount <= (std::min)(clusterCapacity, kMaximumLocalLightClusterCount)) {
            view.clusterCount = static_cast<uint32_t>(clusterCount);
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

template <class Visitor>
void VisitVisibleLocalLights(const SceneLightingSet& lighting, const Camera3D& camera, uint32_t screenWidth,
                             uint32_t screenHeight, uint32_t lightCapacity, LocalLightListView& view,
                             Visitor&& visitor) {
    for (uint32_t sourceIndex = 0; sourceIndex < lighting.lights.size(); ++sourceIndex) {
        const auto& light = lighting.lights[sourceIndex];
        if (!IsLocalLightUploadable(light)) {
            ++view.rejectedCount;
            continue;
        }
        ++view.uploadableCount;
        const ClusterBounds bounds = ProjectSphereBounds(light.position, ResolveLocalLightInfluenceRadius(light),
                                                         camera, screenWidth, screenHeight, view);
        if (!bounds.visible) {
            ++view.culledCount;
            continue;
        }
        if (view.lightCount >= (std::min)(lightCapacity, kMaximumLocalLightCount)) {
            ++view.rejectedCount;
            continue;
        }

        const uint32_t lightIndex = view.lightCount++;
        if (light.type == SceneLightType::Spot)
            ++view.spotLightCount;
        else
            ++view.pointLightCount;
        visitor(light, sourceIndex, lightIndex, bounds);
    }
}

} // namespace

LocalLightListView SelectVisibleLocalLights(const SceneLightingSet& lighting, const Camera3D& camera,
                                            uint32_t screenWidth, uint32_t screenHeight,
                                            std::span<uint32_t> sourceIndices, std::span<float> screenCoverage) {
    LocalLightListView view{};
    view.sourceCount = static_cast<uint32_t>(lighting.lights.size());
    if (sourceIndices.empty() ||
        !InitializeLocalLightGrid(view, camera, screenWidth, screenHeight, kMaximumLocalLightClusterCount)) {
        view.rejectedCount = view.sourceCount;
        return view;
    }
    VisitVisibleLocalLights(
        lighting, camera, screenWidth, screenHeight,
        uint32_t((std::min)(sourceIndices.size(), size_t(kMaximumLocalLightCount))), view,
        [&](const SceneLightProvider&, uint32_t sourceIndex, uint32_t lightIndex, const ClusterBounds& bounds) {
            sourceIndices[lightIndex] = sourceIndex;
            if (lightIndex < screenCoverage.size())
                screenCoverage[lightIndex] =
                    float((bounds.maxX - bounds.minX + 1u) * (bounds.maxY - bounds.minY + 1u)) /
                    float(view.clusterCountX * view.clusterCountY);
        });
    return view;
}

uint64_t LocalLightListInputSignature(const SceneLightingSet& lighting, const Camera3D& camera, uint32_t width,
                                      uint32_t height, std::span<const LocalLightShadowAssignment> shadows) noexcept {
    uint64_t hash = HASH::kFnv1a64OffsetBasis;
    const auto append = [&]<class T>(const T& value) { hash = HASH::Fnv1aBytes(hash, &value, sizeof(value)); };
    append(width);
    append(height);
    append(camera.GetView());
    append(camera.GetProj());
    append(camera.GetNearZ());
    append(camera.GetFarZ());
    append(camera.IsOrthographic());
    append(lighting.lights.size());
    for (const auto& light : lighting.lights) {
        append(light.enabled);
        append(light.type);
        append(light.castsShadows);
        append(IsLocalLightUploadable(light));
        // 所有者が公開する GPU 射影でキーを作り、padding/string の生バイトには依存しない。
        append(BuildLocalLightGpuData(light));
    }
    append(lighting.decals.size());
    for (const auto& decal : lighting.decals) {
        append(DECAL::IsDecalUploadable(decal));
        append(DECAL::BuildLocalDecalGpuData(decal));
    }
    append(shadows.size());
    for (const auto& shadow : shadows) {
        append(shadow.sourceIndex);
        append(shadow.firstFace);
        append(shadow.fade);
    }
    return hash;
}

LocalLightListView BuildLocalLightList(const SceneLightingSet& lighting, const Camera3D& camera, uint32_t screenWidth,
                                       uint32_t screenHeight, const LocalLightListBuildOutput& output,
                                       std::span<const LocalLightShadowAssignment> shadows) {
    LocalLightListView view{};
    view.sourceCount = static_cast<uint32_t>(lighting.lights.size());
    if (screenWidth == 0u || screenHeight == 0u || output.records == nullptr || output.clusterHeaders == nullptr ||
        output.clusterIndices == nullptr || output.recordCapacity == 0u || output.clusterHeaderCapacity == 0u ||
        output.clusterIndexCapacity == 0u) {
        view.rejectedCount = view.sourceCount;
        return view;
    }

    if (!InitializeLocalLightGrid(view, camera, screenWidth, screenHeight, output.clusterHeaderCapacity)) {
        view.rejectedCount = view.sourceCount;
        return view;
    }

    LocalLightListScratch& scratch = GetLocalLightListScratch();
    scratch.clusterCounts.assign(view.clusterCount, 0u);
    scratch.clusterWriteCounts.assign(view.clusterCount, 0u);
    scratch.clusterDecalCounts.assign(view.clusterCount, 0u);
    scratch.clusterDecalWriteCounts.assign(view.clusterCount, 0u);
    scratch.uploadedBounds.clear();
    scratch.uploadedDecalBounds.clear();
    scratch.clusterHeaders.assign(view.clusterCount, LocalLightClusterHeader{});
    std::vector<uint8_t>& clusterCounts = scratch.clusterCounts;
    std::vector<uint8_t>& clusterWriteCounts = scratch.clusterWriteCounts;
    std::vector<uint8_t>& clusterDecalCounts = scratch.clusterDecalCounts;
    std::vector<uint8_t>& clusterDecalWriteCounts = scratch.clusterDecalWriteCounts;
    std::vector<UploadedRecordBounds>& uploadedBounds = scratch.uploadedBounds;
    std::vector<UploadedRecordBounds>& uploadedDecalBounds = scratch.uploadedDecalBounds;
    std::vector<LocalLightClusterHeader>& clusterHeaders = scratch.clusterHeaders;
    uploadedBounds.reserve((std::min)(output.recordCapacity, kMaximumLocalLightCount));

    VisitVisibleLocalLights(
        lighting, camera, screenWidth, screenHeight, output.recordCapacity, view,
        [&](const SceneLightProvider& light, uint32_t sourceIndex, uint32_t lightIndex, const ClusterBounds& bounds) {
            output.records[lightIndex] = BuildLocalLightGpuData(light);
            for (const auto& shadow : shadows) {
                if (shadow.sourceIndex == sourceIndex && shadow.firstFace < kMaximumLocalShadowFaceCount &&
                    light.castsShadows) {
                    const uint32_t fade = uint32_t(std::clamp(shadow.fade, 0.0f, 1.0f) * 255.0f + 0.5f);
                    output.records[lightIndex].kind |= fade << 8u | (shadow.firstFace + 1u) << 16u;
                    break;
                }
            }
            uploadedBounds.push_back({lightIndex, bounds});

            for (uint32_t z = bounds.minZ; z <= bounds.maxZ; ++z) {
                for (uint32_t y = bounds.minY; y <= bounds.maxY; ++y) {
                    for (uint32_t x = bounds.minX; x <= bounds.maxX; ++x) {
                        const uint32_t clusterIndex = (z * view.clusterCountY + y) * view.clusterCountX + x;
                        uint8_t& count = clusterCounts[clusterIndex];
                        if (count >= kMaximumLocalLightsPerCluster) {
                            ++view.overflowCount;
                            continue;
                        }
                        ++count;
                    }
                }
            }
        });

    // デカールは光源の後ろの記録に sortOrder の順で並べる（クラスタの中でも後ろのものが上に乗る）。
    for (const DECAL::SceneDecalProvider& decal : lighting.decals) {
        if (!DECAL::IsDecalUploadable(decal)) {
            continue;
        }
        MATH::Vec3 center{};
        float radius = 0.0f;
        DECAL::ResolveDecalBoundingSphere(decal, center, radius);
        const ClusterBounds bounds = ProjectSphereBounds(center, radius, camera, screenWidth, screenHeight, view);
        if (!bounds.visible) {
            continue;
        }
        const uint32_t recordIndex = view.lightCount + view.decalCount;
        if (view.decalCount >= kMaximumLocalDecalCount || recordIndex >= output.recordCapacity) {
            ++view.overflowCount;
            continue;
        }
        const LocalDecalGpuData record = DECAL::BuildLocalDecalGpuData(decal);
        std::memcpy(&output.records[recordIndex], &record, sizeof(record));
        ++view.decalCount;
        uploadedDecalBounds.push_back({recordIndex, bounds});
        for (uint32_t z = bounds.minZ; z <= bounds.maxZ; ++z) {
            for (uint32_t y = bounds.minY; y <= bounds.maxY; ++y) {
                for (uint32_t x = bounds.minX; x <= bounds.maxX; ++x) {
                    uint8_t& count = clusterDecalCounts[(z * view.clusterCountY + y) * view.clusterCountX + x];
                    if (count >= kMaximumLocalDecalsPerCluster) {
                        ++view.overflowCount;
                        continue;
                    }
                    ++count;
                }
            }
        }
    }

    for (uint32_t clusterIndex = 0u; clusterIndex < view.clusterCount; ++clusterIndex) {
        const uint32_t lights = clusterCounts[clusterIndex];
        const uint32_t decals = clusterDecalCounts[clusterIndex];
        LocalLightClusterHeader& header = clusterHeaders[clusterIndex];
        header.offset = view.clusterIndexCount;
        const uint32_t remainingCapacity = view.clusterIndexCount < output.clusterIndexCapacity
                                               ? output.clusterIndexCapacity - view.clusterIndexCount
                                               : 0u;
        const uint32_t storedLights = (std::min)(lights, remainingCapacity);
        const uint32_t storedDecals = (std::min)(decals, remainingCapacity - storedLights);
        header.count = storedLights | storedDecals << 16u;
        view.overflowCount += lights - storedLights + decals - storedDecals;
        view.clusterIndexCount += storedLights + storedDecals;
    }

    scratch.clusterIndices.assign(view.clusterIndexCount, 0u);
    std::vector<uint32_t>& clusterIndices = scratch.clusterIndices;

    for (const UploadedRecordBounds& uploaded : uploadedBounds) {
        const ClusterBounds& bounds = uploaded.clusters;
        for (uint32_t z = bounds.minZ; z <= bounds.maxZ; ++z) {
            for (uint32_t y = bounds.minY; y <= bounds.maxY; ++y) {
                for (uint32_t x = bounds.minX; x <= bounds.maxX; ++x) {
                    const uint32_t clusterIndex = (z * view.clusterCountY + y) * view.clusterCountX + x;
                    const LocalLightClusterHeader& header = clusterHeaders[clusterIndex];
                    uint8_t& writeCount = clusterWriteCounts[clusterIndex];
                    if (writeCount >= (header.count & 0xffffu)) {
                        continue;
                    }
                    clusterIndices[header.offset + writeCount] = uploaded.recordIndex;
                    ++writeCount;
                }
            }
        }
    }
    for (const UploadedRecordBounds& uploaded : uploadedDecalBounds) {
        const ClusterBounds& bounds = uploaded.clusters;
        for (uint32_t z = bounds.minZ; z <= bounds.maxZ; ++z) {
            for (uint32_t y = bounds.minY; y <= bounds.maxY; ++y) {
                for (uint32_t x = bounds.minX; x <= bounds.maxX; ++x) {
                    const uint32_t clusterIndex = (z * view.clusterCountY + y) * view.clusterCountX + x;
                    const LocalLightClusterHeader& header = clusterHeaders[clusterIndex];
                    uint8_t& writeCount = clusterDecalWriteCounts[clusterIndex];
                    if (writeCount >= (header.count >> 16u)) {
                        continue;
                    }
                    clusterIndices[header.offset + (header.count & 0xffffu) + writeCount] = uploaded.recordIndex;
                    ++writeCount;
                }
            }
        }
    }
    CompactSpotLightClusters(view, uploadedBounds, output.records, clusterHeaders, clusterIndices, camera, screenWidth,
                             screenHeight);
    std::memcpy(output.clusterHeaders, clusterHeaders.data(),
                static_cast<size_t>(view.clusterCount) * sizeof(LocalLightClusterHeader));
    if (!clusterIndices.empty()) {
        std::memcpy(output.clusterIndices, clusterIndices.data(),
                    static_cast<size_t>(view.clusterIndexCount) * sizeof(uint32_t));
    }
    return view;
}

} // namespace HIKARI::RENDER3D::LIGHTING
