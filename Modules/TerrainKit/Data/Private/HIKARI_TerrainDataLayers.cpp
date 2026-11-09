#include "TerrainKit/Data/HIKARI_TerrainDataLayers.h"

#include <cmath>
#include <exception>
#include <set>

#include "Core/Math/HIKARI_ScalarMath.h"

namespace HIKARI::TERRAIN::DATA {
namespace {

bool Finite(float value) noexcept { return std::isfinite(value); }

bool FiniteSpline(const SplineLayerSettings& s) noexcept {
    for (const SplinePoint& point : s.points) {
        if (!Finite(point.position.x) || !Finite(point.position.y) || !Finite(point.position.z) ||
            !Finite(point.widthMeters)) {
            return false;
        }
    }
    return Finite(s.falloffMeters) && Finite(s.edgeMeters) && Finite(s.depthMeters) && Finite(s.wetMeters) &&
           Finite(s.flowSpeed) && Finite(s.clarityMeters) && Finite(s.waterColor.x) && Finite(s.waterColor.y) &&
           Finite(s.waterColor.z);
}

bool FiniteLayer(const HeightLayer& layer) noexcept {
    const NoiseLayerSettings& n = layer.noise;
    const ImageLayerSettings& i = layer.image;
    return Finite(layer.opacity) && Finite(n.featureSizeMeters) && Finite(n.gain) && Finite(n.lacunarity) &&
           Finite(n.weightedStrength) && Finite(n.amplitudeMeters) && Finite(n.offsetMeters) &&
           Finite(i.originMeters.x) && Finite(i.originMeters.y) && Finite(i.sizeMeters.x) && Finite(i.sizeMeters.y) &&
           Finite(i.heightScaleMeters) && Finite(i.heightOffsetMeters) && FiniteSpline(layer.spline);
}

} // namespace

bool ValidateHeightLayerStack(const HeightLayerStack& stack, std::string* error) {
    const auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    if (!ValidateTerrainGrid(stack.grid, error)) {
        return false;
    }
    std::set<uint32_t> ids;
    for (const HeightLayer& layer : stack.layers) {
        if (layer.id == 0u || layer.id >= stack.nextLayerId) {
            return fail("layer id " + std::to_string(layer.id) + " must be within 1..nextLayerId-1");
        }
        if (!ids.insert(layer.id).second) {
            return fail("layer id " + std::to_string(layer.id) + " is used twice");
        }
        if (!FiniteLayer(layer)) {
            return fail("layer " + std::to_string(layer.id) + " has a non-finite value");
        }
        if (layer.kind == HeightLayerKind::Image &&
            (layer.image.sizeMeters.x <= 0.0f || layer.image.sizeMeters.y <= 0.0f)) {
            return fail("image layer " + std::to_string(layer.id) + " needs a positive size");
        }
        if (layer.spline.points.size() > kSplineMaximumPoints) {
            return fail("spline layer " + std::to_string(layer.id) + " has too many points");
        }
    }
    return true;
}

nlohmann::json WriteHeightLayerStackJson(const HeightLayerStack& stack) {
    nlohmann::json out = nlohmann::json::object();
    JSONFIELDS::Write(out, stack, kHeightLayerStackFields);
    return out;
}

bool ReadHeightLayerStackJson(const nlohmann::json& in, HeightLayerStack& out, std::string& error) {
    if (!in.is_object()) {
        error = "terrain layer stack must be a JSON object";
        return false;
    }
    HeightLayerStack stack{};
    try {
        JSONFIELDS::Read(in, stack, kHeightLayerStackFields);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    if (!ValidateHeightLayerStack(stack, &error)) {
        return false;
    }
    out = std::move(stack);
    return true;
}

float BlendHeight(HeightBlendMode mode, float below, float value, float alpha) noexcept {
    switch (mode) {
    case HeightBlendMode::Add: return below + alpha * value;
    case HeightBlendMode::Subtract: return below - alpha * value;
    case HeightBlendMode::Max: return MATH::Lerp(below, (std::max)(below, value), alpha);
    case HeightBlendMode::Min: return MATH::Lerp(below, (std::min)(below, value), alpha);
    case HeightBlendMode::Replace: return MATH::Lerp(below, value, alpha);
    case HeightBlendMode::Multiply: return MATH::Lerp(below, below * value, alpha);
    }
    return below;
}

} // namespace HIKARI::TERRAIN::DATA
