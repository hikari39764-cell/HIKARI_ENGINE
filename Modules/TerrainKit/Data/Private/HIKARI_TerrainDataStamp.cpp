#include "TerrainKit/Data/HIKARI_TerrainDataStamp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "HIKARI_TerrainDataNoise.h"

namespace HIKARI::TERRAIN::DATA {
namespace {

constexpr std::array<std::string_view, 6> kBuiltinStampNames{"hill", "crater", "mesa", "volcano", "ridge", "dunes"};

float Smoothstep(float edge0, float edge1, float value) noexcept {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// 値は -1..1 に収まる。
float Fractal(float x, float y, uint32_t seed, int octaves = 5) noexcept {
    float sum = 0.0f;
    float amplitude = 0.5f;
    float total = 0.0f;
    for (int octave = 0; octave < octaves; ++octave) {
        sum += LatticeValueNoise(x, y, seed + static_cast<uint32_t>(octave)) * amplitude;
        total += amplitude;
        amplitude *= 0.5f;
        x *= 2.03f;
        y *= 2.03f;
    }
    return sum / total;
}

// u, v は中心が 0 の -1..1。
float BuiltinValue(std::string_view name, float u, float v) noexcept {
    const float radius = std::sqrt(u * u + v * v);
    const float detail = Fractal(u * 3.0f + 17.0f, v * 3.0f + 5.0f, 11u);
    if (name == "hill") {
        const float dome = 1.0f - Smoothstep(0.0f, 1.0f, radius);
        return dome * (0.9f + 0.1f * detail);
    }
    if (name == "crater") {
        const float rim = std::exp(-std::pow((radius - 0.62f) / 0.16f, 2.0f));
        const float bowl = 1.0f - Smoothstep(0.0f, 0.62f, radius);
        return rim * (0.8f + 0.2f * detail) - bowl * 0.55f;
    }
    if (name == "mesa") {
        const float edge = radius + 0.12f * Fractal(u * 2.5f + 3.0f, v * 2.5f + 9.0f, 23u);
        const float terrace = 0.82f * (1.0f - Smoothstep(0.42f, 0.56f, edge));
        const float skirt = 0.18f * (1.0f - Smoothstep(0.56f, 1.0f, edge));
        return terrace + skirt + 0.03f * detail;
    }
    if (name == "volcano") {
        const float cone = std::pow(std::clamp(1.0f - radius, 0.0f, 1.0f), 1.6f);
        const float vent = 0.55f * (1.0f - Smoothstep(0.03f, 0.16f, radius));
        const float gullies =
            0.08f * std::abs(Fractal(std::atan2(v, u) * 2.5f, radius * 1.5f, 31u, 4)) * Smoothstep(0.1f, 0.5f, radius);
        return std::max(cone - vent - gullies, 0.0f);
    }
    if (name == "ridge") {
        const float along = 1.0f - Smoothstep(0.55f, 1.0f, std::abs(u));
        const float centerline = v + 0.25f * Fractal(u * 2.0f, 2.0f, 41u, 4);
        const float crest = 1.0f - std::abs(Fractal(u * 4.0f, v * 4.0f + 7.0f, 43u)) * 0.45f;
        return along * std::exp(-centerline * centerline / (2.0f * 0.2f * 0.2f)) * crest;
    }
    // dunes：風上が緩く風下が急な波を、ゆらぎのある向きに並べる。
    const float phase = (v + 0.12f * Fractal(u * 1.5f, v * 1.5f, 53u, 3)) * 5.0f;
    const float wave = phase - std::floor(phase);
    const float dune = wave < 0.78f ? wave / 0.78f : (1.0f - wave) / 0.22f;
    return dune * dune * (1.0f - Smoothstep(0.55f, 1.0f, radius));
}

bool FinitePath(std::span<const MATH::Vec2> path) noexcept {
    return std::all_of(path.begin(), path.end(),
                       [](const MATH::Vec2& p) { return std::isfinite(p.x) && std::isfinite(p.y); });
}

SampleRect Union(const SampleRect& a, const SampleRect& b) noexcept {
    if (a.Empty()) {
        return b;
    }
    if (b.Empty()) {
        return a;
    }
    const int64_t x0 = (std::min)(a.x0, b.x0);
    const int64_t z0 = (std::min)(a.z0, b.z0);
    const int64_t x1 = (std::max)(a.X1(), b.X1());
    const int64_t z1 = (std::max)(a.Z1(), b.Z1());
    return SampleRect{x0, z0, static_cast<uint32_t>(x1 - x0 + 1), static_cast<uint32_t>(z1 - z0 + 1)};
}

SampleRect RectOfWorld(const TerrainGrid& grid, MATH::Vec2 origin, MATH::Vec2 size) noexcept {
    const double spacing = SampleSpacing(grid);
    const auto x0 = static_cast<int64_t>(std::ceil(origin.x / spacing));
    const auto z0 = static_cast<int64_t>(std::ceil(origin.y / spacing));
    const auto x1 = static_cast<int64_t>(std::floor((origin.x + size.x) / spacing));
    const auto z1 = static_cast<int64_t>(std::floor((origin.y + size.y) / spacing));
    if (x1 < x0 || z1 < z0) {
        return {};
    }
    return SampleRect{x0, z0, static_cast<uint32_t>(x1 - x0 + 1), static_cast<uint32_t>(z1 - z0 + 1)};
}

} // namespace

std::span<const std::string_view> BuiltinStampNames() noexcept { return kBuiltinStampNames; }

bool MakeBuiltinStamp(std::string_view name, uint32_t size, HeightImage& out) {
    if (size < 2u ||
        std::find(kBuiltinStampNames.begin(), kBuiltinStampNames.end(), name) == kBuiltinStampNames.end()) {
        return false;
    }
    out.width = size;
    out.height = size;
    out.values.resize(static_cast<std::size_t>(size) * size);
    const float scale = 2.0f / static_cast<float>(size - 1u);
    for (uint32_t y = 0u; y < size; ++y) {
        const float v = static_cast<float>(y) * scale - 1.0f;
        for (uint32_t x = 0u; x < size; ++x) {
            const float u = static_cast<float>(x) * scale - 1.0f;
            out.values[static_cast<std::size_t>(y) * size + x] = BuiltinValue(name, u, v);
        }
    }
    return true;
}

bool ApplyStamp(TerrainData& data, uint32_t layerId, const HeightImage& image, const StampSettings& requested,
                TerrainEditRecorder* recorder, BrushEditResult& result, std::string& error) {
    result = {};
    TiledRaster<float>* values = data.LayerValues(layerId);
    const HeightLayer* layer = data.FindLayer(layerId);
    if (values == nullptr || layer == nullptr) {
        error = "stamp needs an existing sculpt layer";
        return false;
    }
    if (!image.Valid()) {
        error = "stamp image is not valid";
        return false;
    }
    if (!std::isfinite(requested.center.x) || !std::isfinite(requested.center.y)) {
        error = "stamp position is not finite";
        return false;
    }
    const StampSettings settings = JSONFIELDS::Sanitize(requested, kStampFields);
    const TerrainGrid& grid = data.Grid();
    // 回した正方形が半径の外へ出ないよう、画像は半径の内接円まで使う。
    const SampleRect rect = SampleRectAround(grid, requested.center.x, requested.center.y, settings.radiusMeters);
    if (rect.Empty() || rect.Count() > kTerrainMaximumRectSamples) {
        error = "stamp covers no samples or too many samples";
        return false;
    }
    const float sign = layer->blend == HeightBlendMode::Subtract ? -1.0f : 1.0f;
    const double spacing = SampleSpacing(grid);
    const float angle = settings.rotationDegrees * std::numbers::pi_v<float> / 180.0f;
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    const float inner = 1.0f - settings.edgeFalloff;
    const float pixelScaleX = 0.5f * static_cast<float>(image.width - 1u);
    const float pixelScaleY = 0.5f * static_cast<float>(image.height - 1u);
    std::vector<float> current{};
    values->ReadRect(grid, rect, current);
    std::vector<uint8_t> keep(current.size(), 0u);
    for (uint32_t z = 0u; z < rect.height; ++z) {
        const float dz =
            static_cast<float>(static_cast<double>(rect.z0 + z) * spacing - requested.center.y) / settings.radiusMeters;
        for (uint32_t x = 0u; x < rect.width; ++x) {
            const float dx = static_cast<float>(static_cast<double>(rect.x0 + x) * spacing - requested.center.x) /
                             settings.radiusMeters;
            const float distance = std::sqrt(dx * dx + dz * dz);
            if (distance >= 1.0f) {
                continue;
            }
            const float u = cosine * dx + sine * dz;
            const float v = -sine * dx + cosine * dz;
            const float value = SampleHeightImage(image, (u + 1.0f) * pixelScaleX, (v + 1.0f) * pixelScaleY);
            const float weight = inner >= 1.0f ? 1.0f : 1.0f - Smoothstep(inner, 1.0f, distance);
            const float height = sign * value * settings.heightMeters * weight;
            const std::size_t index = static_cast<std::size_t>(z) * rect.width + x;
            switch (settings.blend) {
            case StampBlend::Add: current[index] += height; break;
            case StampBlend::Max: current[index] = (std::max)(current[index], height); break;
            case StampBlend::Min: current[index] = (std::min)(current[index], height); break;
            }
            keep[index] = 1u;
        }
    }
    if (recorder != nullptr) {
        recorder->TouchRect(TerrainChannel::LayerValues, layerId, rect);
    }
    values->WriteRect(grid, rect, current, &keep);
    data.MarkChanged();
    result.dabs = 1u;
    result.touched = rect;
    return true;
}

bool ApplyMaskBrush(TerrainData& data, uint32_t layerId, const MaskBrushSettings& requested,
                    std::span<const MATH::Vec2> path, TerrainEditRecorder* recorder, BrushEditResult& result,
                    std::string& error) {
    result = {};
    const HeightLayer* layer = data.FindLayer(layerId);
    TiledRaster<float>* mask = data.LayerMask(layerId);
    if (layer == nullptr || mask == nullptr) {
        error = "mask brush needs an existing layer";
        return false;
    }
    if (path.empty() || !FinitePath(path)) {
        error = "mask brush path is empty or not finite";
        return false;
    }
    if (!layer->masked) {
        HeightLayer masked = *layer;
        masked.masked = true;
        data.SetLayer(masked);
    }
    const MaskBrushSettings settings = JSONFIELDS::Sanitize(requested, kMaskBrushFields);
    const TerrainGrid& grid = data.Grid();
    const double spacing = SampleSpacing(grid);
    const float target = settings.op == MaskBrushOp::Paint ? 1.0f : 0.0f;
    std::vector<float> current{};
    std::vector<uint8_t> keep{};
    for (const MATH::Vec2& dab : PlaceBrushDabs(path, settings.radiusMeters, settings.spacing)) {
        const SampleRect rect = SampleRectAround(grid, dab.x, dab.y, settings.radiusMeters);
        if (rect.Count() > kTerrainMaximumRectSamples) {
            error = "mask brush radius covers too many samples";
            return false;
        }
        if (rect.Empty()) {
            continue;
        }
        if (recorder != nullptr) {
            recorder->TouchRect(TerrainChannel::LayerMask, layerId, rect);
        }
        mask->ReadRect(grid, rect, current);
        keep.assign(current.size(), 0u);
        for (uint32_t z = 0u; z < rect.height; ++z) {
            const double dz = static_cast<double>(rect.z0 + z) * spacing - dab.y;
            for (uint32_t x = 0u; x < rect.width; ++x) {
                const double dx = static_cast<double>(rect.x0 + x) * spacing - dab.x;
                const float weight = BrushFalloff(
                    static_cast<float>(std::sqrt(dx * dx + dz * dz)) / settings.radiusMeters, settings.hardness);
                if (weight <= 0.0f) {
                    continue;
                }
                const std::size_t index = static_cast<std::size_t>(z) * rect.width + x;
                current[index] += (target - current[index]) * settings.strength * weight;
                keep[index] = 1u;
            }
        }
        mask->WriteRect(grid, rect, current, &keep);
        result.touched = Union(result.touched, rect);
        ++result.dabs;
    }
    data.MarkChanged();
    return true;
}

bool ImportLayerMask(TerrainData& data, uint32_t layerId, const HeightImage& image, MATH::Vec2 originMeters,
                     MATH::Vec2 sizeMeters, TerrainEditRecorder* recorder, BrushEditResult& result,
                     std::string& error) {
    result = {};
    const HeightLayer* layer = data.FindLayer(layerId);
    TiledRaster<float>* mask = data.LayerMask(layerId);
    if (layer == nullptr || mask == nullptr) {
        error = "mask import needs an existing layer";
        return false;
    }
    if (!image.Valid() || !(sizeMeters.x > 0.0f) || !(sizeMeters.y > 0.0f)) {
        error = "mask image or its size is not valid";
        return false;
    }
    const TerrainGrid& grid = data.Grid();
    const SampleRect rect = RectOfWorld(grid, originMeters, sizeMeters);
    if (rect.Empty() || rect.Count() > kTerrainMaximumRectSamples) {
        error = "mask covers no samples or too many samples";
        return false;
    }
    if (!layer->masked) {
        HeightLayer masked = *layer;
        masked.masked = true;
        data.SetLayer(masked);
    }
    if (recorder != nullptr) {
        recorder->TouchRect(TerrainChannel::LayerMask, layerId, rect);
    }
    const double spacing = SampleSpacing(grid);
    const double perMeterX = static_cast<double>(image.width - 1u) / sizeMeters.x;
    const double perMeterZ = static_cast<double>(image.height - 1u) / sizeMeters.y;
    std::vector<float> next(rect.Count());
    for (uint32_t z = 0u; z < rect.height; ++z) {
        const double pixelY = (static_cast<double>(rect.z0 + z) * spacing - originMeters.y) * perMeterZ;
        for (uint32_t x = 0u; x < rect.width; ++x) {
            const double pixelX = (static_cast<double>(rect.x0 + x) * spacing - originMeters.x) * perMeterX;
            next[static_cast<std::size_t>(z) * rect.width + x] =
                std::clamp(SampleHeightImage(image, pixelX, pixelY), 0.0f, 1.0f);
        }
    }
    mask->WriteRect(grid, rect, next, nullptr);
    data.MarkChanged();
    result.dabs = 1u;
    result.touched = rect;
    return true;
}

bool ExportLayerMask(const TerrainData& data, uint32_t layerId, const SampleRect& rect, HeightImage& out,
                     std::string& error) {
    const TiledRaster<float>* mask = data.LayerMask(layerId);
    if (mask == nullptr) {
        error = "mask export needs an existing layer";
        return false;
    }
    if (rect.Empty() || rect.width < 2u || rect.height < 2u || rect.Count() > kTerrainMaximumRectSamples) {
        error = "mask export rectangle is not valid";
        return false;
    }
    out.width = rect.width;
    out.height = rect.height;
    mask->ReadRect(data.Grid(), rect, out.values);
    return true;
}

} // namespace HIKARI::TERRAIN::DATA
