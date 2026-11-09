#include "HIKARI_TerrainKitWater.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "HIKARI_TerrainKitStore.h"

namespace HIKARI::TERRAINKIT {

using namespace TERRAIN::DATA;

void TerrainWaterBodies::Refresh(const TerrainStore& store) {
    const TerrainData& data = store.Data();
    if (data.Revision() == revision_) {
        return;
    }
    revision_ = data.Revision();
    std::vector<SplineLayerSettings> waters{};
    for (const HeightLayer& layer : data.Stack().layers) {
        if (layer.kind == HeightLayerKind::Spline && layer.enabled &&
            (layer.spline.kind == SplineKind::River || layer.spline.kind == SplineKind::Lake)) {
            waters.push_back(layer.spline);
        }
    }
    if (waters == built_) {
        return;
    }
    bodies_.clear();
    for (const SplineLayerSettings& settings : waters) {
        Body body{};
        body.spline = PrepareSpline(settings);
        if (body.spline.sections.size() < 2u) {
            continue;
        }
        body.minimumX = body.maximumX = body.spline.sections.front().x;
        body.minimumZ = body.maximumZ = body.spline.sections.front().z;
        for (const SplineSection& section : body.spline.sections) {
            body.minimumX = (std::min)(body.minimumX, section.x - section.halfWidth);
            body.minimumZ = (std::min)(body.minimumZ, section.z - section.halfWidth);
            body.maximumX = (std::max)(body.maximumX, section.x + section.halfWidth);
            body.maximumZ = (std::max)(body.maximumZ, section.z + section.halfWidth);
        }
        bodies_.push_back(std::move(body));
    }
    built_ = std::move(waters);
}

// 中心線の一番近い区間で、幅の内側なら水。水面の高さと幅は区間の両端から補い、流れは曲線の向き × 流速。
bool TerrainWaterBodies::SampleRiver(const Body& body, float x, float z, GameProjectWaterSourceSample& out) {
    const auto& sections = body.spline.sections;
    float best = 1.0e30f;
    std::optional<size_t> bestIndex{};
    float bestShare = 0.0f;
    for (size_t index = 0u; index + 1u < sections.size(); ++index) {
        const SplineSection& a = sections[index];
        const SplineSection& b = sections[index + 1u];
        const float edgeX = b.x - a.x;
        const float edgeZ = b.z - a.z;
        const float lengthSquared = edgeX * edgeX + edgeZ * edgeZ;
        const float share = lengthSquared > 1.0e-6f
                                ? std::clamp(((x - a.x) * edgeX + (z - a.z) * edgeZ) / lengthSquared, 0.0f, 1.0f)
                                : 0.0f;
        const float dx = a.x + edgeX * share - x;
        const float dz = a.z + edgeZ * share - z;
        const float distanceSquared = dx * dx + dz * dz;
        if (distanceSquared < best) {
            best = distanceSquared;
            bestIndex = index;
            bestShare = share;
        }
    }
    if (!bestIndex.has_value()) {
        return false;
    }
    const SplineSection& a = sections[*bestIndex];
    const SplineSection& b = sections[*bestIndex + 1u];
    const float halfWidth = a.halfWidth + (b.halfWidth - a.halfWidth) * bestShare;
    if (std::sqrt(best) > halfWidth) {
        return false;
    }
    out.surfaceHeight = a.y + (b.y - a.y) * bestShare;
    const float tangentX = a.tangentX + (b.tangentX - a.tangentX) * bestShare;
    const float tangentZ = a.tangentZ + (b.tangentZ - a.tangentZ) * bestShare;
    const float tangentLength = std::sqrt(tangentX * tangentX + tangentZ * tangentZ);
    const float speed = body.spline.settings.flowSpeed / (std::max)(tangentLength, 1.0e-4f);
    out.velocity = {tangentX * speed, 0.0f, tangentZ * speed};
    return true;
}

bool TerrainWaterBodies::Sample(const TerrainStore& store, float x, float z, GameProjectWaterSourceSample& out) {
    out = {};
    if (!store.IsOpen()) {
        return false;
    }
    Refresh(store);
    bool found = false;
    for (const Body& body : bodies_) {
        if (x < body.minimumX || x > body.maximumX || z < body.minimumZ || z > body.maximumZ) {
            continue;
        }
        GameProjectWaterSourceSample sample{};
        if (body.spline.settings.kind == SplineKind::Lake) {
            if (!body.spline.lake.Valid() || body.spline.lake.DistanceAt(x, z) <= 0.0f) {
                continue;
            }
            sample.surfaceHeight = body.spline.lake.level;
        } else if (!SampleRiver(body, x, z, sample)) {
            continue;
        }
        // 重なる水は高い方（下流の湖へ注ぐ河の口など）。
        if (!found || sample.surfaceHeight > out.surfaceHeight) {
            out = sample;
            found = true;
        }
    }
    if (found) {
        if (const std::optional<float> ground = store.HeightAt(x, z)) {
            out.bottomHeight = *ground;
            out.hasBottom = 1u;
        }
    }
    return found;
}

} // namespace HIKARI::TERRAINKIT
