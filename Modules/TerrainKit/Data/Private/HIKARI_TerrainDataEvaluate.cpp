#include <algorithm>
#include <execution>
#include <numeric>

#include <FastNoise/FastNoise.h>

#include "TerrainKit/Data/HIKARI_TerrainData.h"

namespace HIKARI::TERRAIN::DATA {
namespace {

// この標本数を超える矩形は行の帯に分けて並列に噪声を作る。
constexpr std::size_t kParallelSampleThreshold = 1u << 18;
constexpr uint32_t kParallelBandRows = 32u;

FastNoise::SmartNode<FastNoise::ScalableGenerator> MakeBasis(NoiseBasis basis) {
    switch (basis) {
    case NoiseBasis::SuperSimplex: return FastNoise::New<FastNoise::SuperSimplex>();
    case NoiseBasis::Perlin: return FastNoise::New<FastNoise::Perlin>();
    case NoiseBasis::Value: return FastNoise::New<FastNoise::Value>();
    case NoiseBasis::Simplex: break;
    }
    return FastNoise::New<FastNoise::Simplex>();
}

FastNoise::SmartNode<> MakeNoise(const NoiseLayerSettings& settings, std::string& error) {
    if (!settings.encodedNodeTree.empty()) {
        FastNoise::SmartNode<> node = FastNoise::NewFromEncodedNodeTree(settings.encodedNodeTree.c_str());
        if (!node) {
            error = "noise.encodedNodeTree is not a valid FastNoise2 node tree";
        }
        return node;
    }
    FastNoise::SmartNode<FastNoise::ScalableGenerator> basis = MakeBasis(settings.basis);
    basis->SetScale(settings.featureSizeMeters);
    if (settings.fractal == NoiseFractal::None) {
        return basis;
    }
    const auto configure = [&](auto fractal) {
        fractal->SetSource(basis);
        fractal->SetOctaveCount(static_cast<int>(settings.octaves));
        fractal->SetGain(settings.gain);
        fractal->SetLacunarity(settings.lacunarity);
        fractal->SetWeightedStrength(settings.weightedStrength);
        return FastNoise::SmartNode<>(fractal);
    };
    if (settings.fractal == NoiseFractal::Ridged) {
        return configure(FastNoise::New<FastNoise::FractalRidged>());
    }
    return configure(FastNoise::New<FastNoise::FractalFBm>());
}

} // namespace

struct HeightStackEvaluator::Impl {
    struct Layer {
        HeightLayer settings{};
        FastNoise::SmartNode<> noise{};
        // FastNoise2 の分形は八度の振幅を足すだけなので、Σ gain^i で割って ±1 に収める（八度の数で高さが変わらない）。
        float noiseScale = 1.0f;
        const TiledRaster<float>* values = nullptr;
        const TiledRaster<float>* mask = nullptr;
        std::shared_ptr<const HeightImage> image{};
        PreparedSpline spline{};
    };

    TerrainGrid grid{};
    std::vector<Layer> layers{};
    bool valid = true;
    std::string error{};

    void LayerValues(const Layer& layer, const SampleRect& rect, std::vector<float>& values,
                     std::vector<float>& alpha) const {
        const std::size_t count = rect.Count();
        values.assign(count, 0.0f);
        alpha.assign(count, layer.settings.enabled ? layer.settings.opacity : 0.0f);
        if (!layer.settings.enabled) {
            return;
        }
        const double spacing = SampleSpacing(grid);
        switch (layer.settings.kind) {
        case HeightLayerKind::Noise: GenerateNoise(layer, rect, values); break;
        case HeightLayerKind::Image: SampleImage(layer, rect, spacing, values, alpha); break;
        case HeightLayerKind::Sculpt:
            if (layer.values != nullptr) {
                layer.values->ReadRect(grid, rect, values);
            }
            break;
        case HeightLayerKind::Spline: EvaluateSplineHeights(layer.spline, grid, rect, values, alpha); break;
        }
        if (layer.settings.masked && layer.mask != nullptr) {
            std::vector<float> mask;
            layer.mask->ReadRect(grid, rect, mask);
            for (std::size_t i = 0u; i < count; ++i) {
                alpha[i] *= std::clamp(mask[i], 0.0f, 1.0f);
            }
        }
    }

    void GenerateNoise(const Layer& layer, const SampleRect& rect, std::vector<float>& values) const {
        const NoiseLayerSettings& noise = layer.settings.noise;
        const float spacing = static_cast<float>(SampleSpacing(grid));
        const auto band = [&](uint32_t row0, uint32_t rows) {
            const float x0 = static_cast<float>(static_cast<double>(rect.x0) * spacing);
            const float z0 = static_cast<float>(static_cast<double>(rect.z0 + row0) * spacing);
            layer.noise->GenUniformGrid2D(values.data() + static_cast<std::size_t>(row0) * rect.width, x0, z0,
                                          static_cast<int>(rect.width), static_cast<int>(rows), spacing, spacing,
                                          noise.seed);
        };
        if (rect.Count() < kParallelSampleThreshold) {
            band(0u, rect.height);
        } else {
            std::vector<uint32_t> bands((rect.height + kParallelBandRows - 1u) / kParallelBandRows);
            std::iota(bands.begin(), bands.end(), 0u);
            std::for_each(std::execution::par, bands.begin(), bands.end(), [&](uint32_t index) {
                const uint32_t row0 = index * kParallelBandRows;
                band(row0, (std::min)(kParallelBandRows, rect.height - row0));
            });
        }
        const float scale = layer.noiseScale * noise.amplitudeMeters;
        for (float& value : values) {
            value = value * scale + noise.offsetMeters;
        }
    }

    void SampleImage(const Layer& layer, const SampleRect& rect, double spacing, std::vector<float>& values,
                     std::vector<float>& alpha) const {
        const ImageLayerSettings& settings = layer.settings.image;
        if (layer.image == nullptr || !layer.image->Valid()) {
            std::fill(alpha.begin(), alpha.end(), 0.0f);
            return;
        }
        const HeightImage& image = *layer.image;
        const double pixelsPerMeterX = static_cast<double>(image.width - 1u) / settings.sizeMeters.x;
        const double pixelsPerMeterZ = static_cast<double>(image.height - 1u) / settings.sizeMeters.y;
        for (uint32_t z = 0u; z < rect.height; ++z) {
            const double worldZ = static_cast<double>(rect.z0 + z) * spacing;
            const double pixelY = (worldZ - settings.originMeters.y) * pixelsPerMeterZ;
            for (uint32_t x = 0u; x < rect.width; ++x) {
                const std::size_t index = static_cast<std::size_t>(z) * rect.width + x;
                const double worldX = static_cast<double>(rect.x0 + x) * spacing;
                const double pixelX = (worldX - settings.originMeters.x) * pixelsPerMeterX;
                // 画像の外は層が無いのと同じ（alpha 0）。
                if (pixelX < -0.5 || pixelY < -0.5 || pixelX > image.width - 0.5 || pixelY > image.height - 0.5) {
                    alpha[index] = 0.0f;
                    continue;
                }
                values[index] =
                    SampleHeightImage(image, pixelX, pixelY) * settings.heightScaleMeters + settings.heightOffsetMeters;
            }
        }
    }
};

HeightStackEvaluator::HeightStackEvaluator(const TerrainData& data) : impl_(std::make_unique<Impl>()) {
    impl_->grid = data.Grid();
    for (const HeightLayer& settings : data.Stack().layers) {
        Impl::Layer layer{};
        layer.settings = settings;
        layer.values = data.LayerValues(settings.id);
        layer.mask = data.LayerMask(settings.id);
        layer.image = data.LayerImage(settings.id);
        if (settings.kind == HeightLayerKind::Spline) {
            layer.spline = PrepareSpline(settings.spline);
        }
        if (settings.kind == HeightLayerKind::Noise) {
            layer.noise = MakeNoise(settings.noise, impl_->error);
            if (settings.noise.encodedNodeTree.empty() && settings.noise.fractal != NoiseFractal::None) {
                float bound = 0.0f;
                float amplitude = 1.0f;
                for (uint32_t octave = 0u; octave < settings.noise.octaves; ++octave) {
                    bound += amplitude;
                    amplitude *= settings.noise.gain;
                }
                layer.noiseScale = 1.0f / bound;
            }
            if (!layer.noise) {
                impl_->valid = false;
                if (impl_->error.empty()) {
                    impl_->error = "noise layer " + std::to_string(settings.id) + " could not be built";
                }
                layer.settings.enabled = false;
            }
        }
        impl_->layers.push_back(std::move(layer));
    }
}

HeightStackEvaluator::~HeightStackEvaluator() = default;

bool HeightStackEvaluator::Valid() const noexcept { return impl_->valid; }
const std::string& HeightStackEvaluator::Error() const noexcept { return impl_->error; }

bool HeightStackEvaluator::EvaluateRect(const SampleRect& rect, std::vector<float>& out, std::string* error,
                                        uint32_t throughLayer) const {
    if (rect.Empty() || rect.Count() > kTerrainMaximumRectSamples) {
        if (error != nullptr) {
            *error = "sample rectangle is empty or larger than the per-operation limit";
        }
        return false;
    }
    out.assign(rect.Count(), 0.0f);
    std::vector<float> values;
    std::vector<float> alpha;
    for (const Impl::Layer& layer : impl_->layers) {
        impl_->LayerValues(layer, rect, values, alpha);
        for (std::size_t i = 0u; i < out.size(); ++i) {
            out[i] = BlendHeight(layer.settings.blend, out[i], values[i], alpha[i]);
        }
        if (throughLayer != 0u && layer.settings.id == throughLayer) {
            break;
        }
    }
    return true;
}

bool HeightStackEvaluator::EvaluateTile(TileCoord tile, std::vector<float>& out, std::string* error) const {
    return EvaluateRect(TileSampleRect(impl_->grid, tile), out, error);
}

bool HeightStackEvaluator::BakeTile(TileCoord tile, TileRaster<uint16_t>& out, std::string* error) const {
    std::vector<float> heights;
    if (!EvaluateTile(tile, heights, error)) {
        return false;
    }
    out = TileRaster<uint16_t>(TileSamples(impl_->grid), 0u);
    for (std::size_t i = 0u; i < heights.size(); ++i) {
        out.values[i] = QuantizeHeight(impl_->grid, heights[i]);
    }
    return true;
}

bool HeightStackEvaluator::EvaluateLayerValues(uint32_t layerId, const SampleRect& rect, std::vector<float>& values,
                                               std::vector<float>& alpha, std::string* error) const {
    const auto it = std::find_if(impl_->layers.begin(), impl_->layers.end(),
                                 [layerId](const Impl::Layer& layer) { return layer.settings.id == layerId; });
    if (it == impl_->layers.end() || rect.Empty() || rect.Count() > kTerrainMaximumRectSamples) {
        if (error != nullptr) {
            *error = "unknown layer or invalid sample rectangle";
        }
        return false;
    }
    impl_->LayerValues(*it, rect, values, alpha);
    return true;
}

} // namespace HIKARI::TERRAIN::DATA
