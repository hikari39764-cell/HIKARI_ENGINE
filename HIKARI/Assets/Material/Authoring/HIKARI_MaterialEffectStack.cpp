#include "Assets/Material/Authoring/HIKARI_MaterialEffectStackBuilder.h"
#include "Assets/Material/Authoring/Internal/HIKARI_MaterialEffectSampling.h"
#include "Assets/Material/Authoring/Internal/HIKARI_MaterialEffectProcedural.h"
#include "Assets/Material/Authoring/Internal/HIKARI_MaterialEffectStyle.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace HIKARI {
namespace {
using Kind = MaterialNodeKind;
using Type = MaterialValueType;
using namespace MATERIAL_EFFECT_INTERNAL;
} // namespace

MaterialAssetData BuildMaterialEffectStack(const MaterialEffectTextureSet& textures,
                                           std::span<const MaterialEffectStep> effects, bool lit, uint32_t atlasColumns,
                                           uint32_t atlasRows, std::span<const std::string> animatedParameters,
                                           size_t adjustmentStart) {
    EffectGraph g;
    g.animated = animatedParameters;
    g.data.materialName = "VFX Effect Stack";
    g.data.surface.doubleSided = true;
    g.data.surface.unlit = !lit;
    g.data.pbr.unlit = !lit;
    const auto state = g.Node(Kind::ParticleState, Type::Float1, "Age");
    const Output seconds{state.node, "Seconds", Type::Float1};
    auto uv = g.Node(Kind::ParticleUv, Type::Float2, "Value");
    const auto localUv = g.Node(Kind::TextureCoordinate, Type::Float2, "UV1");
    // 追加スロットは先に資源として登録し、参照ノードは必要になった時点で
    // 作る。遮罩は最終 UV、ノイズは歪み/溶解の段の UV を使う。
    std::string maskResourceId{};
    std::string noiseResourceId{};
    std::string detailNoiseResourceId{};
    if (textures.mask.IsValid()) {
        MaterialGraphResourceData resource;
        resource.id = "effect.mask";
        resource.assetGuid = textures.mask;
        resource.semantic = MaterialResourceSemantic::Mask;
        resource.enabled = true;
        g.data.graph.resources.push_back(resource);
        maskResourceId = resource.id;
    }
    if (textures.noise.IsValid()) {
        MaterialGraphResourceData resource;
        resource.id = "effect.noise";
        resource.assetGuid = textures.noise;
        resource.semantic = MaterialResourceSemantic::Noise;
        resource.enabled = true;
        g.data.graph.resources.push_back(resource);
        noiseResourceId = resource.id;
    }
    if (textures.detailNoise.IsValid()) {
        MaterialGraphResourceData resource;
        resource.id = "effect.detailnoise";
        resource.assetGuid = textures.detailNoise;
        resource.semantic = MaterialResourceSemantic::DetailNoise;
        resource.enabled = true;
        g.data.graph.resources.push_back(resource);
        detailNoiseResourceId = resource.id;
    }
    if (textures.flowMap.IsValid()) {
        MaterialGraphResourceData resource;
        resource.id = "effect.flowMap";
        resource.assetGuid = textures.flowMap;
        resource.semantic = MaterialResourceSemantic::FlowMap;
        resource.enabled = true;
        g.data.graph.resources.push_back(resource);
    }
    EffectTextureSampling sampling{g, effects, seconds, textures.flowMap.IsValid()};
    const auto sampleResource = [&](const std::string& id, Output at) {
        const auto target = id == maskResourceId          ? MaterialEffectTextureTarget::Mask
                            : id == detailNoiseResourceId ? MaterialEffectTextureTarget::DetailNoise
                                                          : MaterialEffectTextureTarget::Noise;
        return sampling.Sample(id, at, target);
    };
    const AssetGuid& texture = textures.baseColor;
    const bool hasBaseTexture = texture.IsValid();
    if (hasBaseTexture) {
        MaterialGraphResourceData resource;
        resource.id = "effect.texture";
        resource.assetGuid = texture;
        resource.semantic = MaterialResourceSemantic::BaseColor;
        resource.enabled = true;
        g.data.graph.resources.push_back(resource);
    }
    // 発光のぼかしは基準貼図を複数の UV で引き直して広がりを作る。
    // 効果スタックの後段（Glow）からも使えるよう、ここで持ち上げておく。
    const auto atlasUv = g.Node(Kind::TextureCoordinate, Type::Float2, "UV0");
    const auto sampleBaseAt = [&](Output at) {
        return sampling.SampleBase(at, atlasUv, localUv, atlasColumns, atlasRows);
    };
    // アトラス（フリップブック）ではタップがセル外へ出ないようグリッドで
    // 正規化する。ぼかし系が後段から参照するのでここで定義する。
    const float atlasU = atlasColumns > 1 ? float(atlasColumns) : 1.0f;
    const float atlasV = atlasRows > 1 ? float(atlasRows) : 1.0f;
    if (hasBaseTexture && std::any_of(effects.begin(), effects.end(), [](const auto& step) {
            return step.enabled && GetMaterialEffectStage(step.kind) == MaterialEffectStage::Uv &&
                   step.mask >= MaterialEffectMask::Alpha;
        })) {
        const auto referenceUv = g.Binary(Kind::Add, atlasUv,
                                          g.Binary(Kind::Multiply, g.Binary(Kind::Subtract, uv, localUv),
                                                   g.Constant({1 / atlasU, 1 / atlasV, 0, 0}, Type::Float2)));
        const auto reference = sampleBaseAt(referenceUv);
        g.blendAlpha = g.Split(reference, Type::Float4).w;
        g.blendLuminance = g.Luminance(reference);
    }
    for (const auto& step : effects) {
        if (!step.enabled)
            continue;
        const auto& p = step.parameters;
        const auto originalUv = uv;
        uv = ApplyProceduralUv(g, uv, step);
        if (step.kind == MaterialEffectKind::UvFlow) {
            if (step.textureTarget == MaterialEffectTextureTarget::Shared)
                uv = ApplyUvFlow(g, uv, step, seconds);
        } else if (step.kind == MaterialEffectKind::UvDistortion) {
            auto offset = g.Node(Kind::MakeFloat2, Type::Float2, "Value");
            if (!noiseResourceId.empty()) {
                // 貼図ノイズを使う場合、R と G をそのまま XY にすると
                // グレースケール (R==G) のノイズでは対角線方向にしか
                // ずれない。2 つ目の成分は別スケールで引き直して
                // 去相関させる。これで任意のグレースケールノイズが
                // 2 次元の変位場として使える。
                const auto moving = g.Binary(Kind::Add, g.Binary(Kind::Multiply, uv, g.C((std::max)(p.y, 0.01f))),
                                             g.Binary(Kind::Multiply, seconds, g.C(p.z)));
                const auto decorrelated = g.Binary(
                    Kind::Add, g.Binary(Kind::Multiply, moving, g.Constant({1.37f, 1.37f, 0, 0}, Type::Float2)),
                    g.Constant({0.31f, 0.17f, 0, 0}, Type::Float2));
                const auto sample = g.Split(sampleResource(noiseResourceId, moving), Type::Float4);
                // 2 つ目のノイズ場が指定されていれば、その Y をそのまま
                // 使う。無い場合だけ同じ貼図を別スケールで引き直す。
                const std::string& secondNoise =
                    detailNoiseResourceId.empty() ? noiseResourceId : detailNoiseResourceId;
                const auto sampleB = g.Split(sampleResource(secondNoise, decorrelated), Type::Float4);
                g.Link(sample.x, offset, "X");
                g.Link(sampleB.y, offset, "Y");
            } else {
                const auto moving = g.Binary(Kind::Add, uv, g.Binary(Kind::Multiply, seconds, g.C(p.z)));
                const auto nx = g.Noise(moving, (std::max)(p.y, 0.01f));
                const auto ny = g.Noise(g.Binary(Kind::Add, moving, g.Constant({17.1f, 53.4f, 0, 0}, Type::Float2)),
                                        (std::max)(p.y, 0.01f));
                g.Link(nx, offset, "X");
                g.Link(ny, offset, "Y");
            }
            offset = g.Binary(Kind::Multiply, g.Binary(Kind::Subtract, offset, g.C(0.5f)),
                              g.Parameter(step, "parameters.x", p.x));
            uv = g.Binary(Kind::Add, uv, offset);
        } else if (step.kind == MaterialEffectKind::TurbulentDisplace) {
            // 2 オクターブのノイズで場を作る。UvDistortion より粗く、
            // 進化速度で流れるので「煮立つ」感じが出る。
            const auto base = g.Binary(Kind::Maximum, g.Parameter(step, "parameters.y", p.y), g.C(0.01f));
            const auto moving =
                g.Binary(Kind::Add, g.Binary(Kind::Multiply, uv, base), g.Flow(step, "parameters.z", p.z, seconds));
            Output fieldX{};
            Output fieldY{};
            if (!noiseResourceId.empty()) {
                // 上と同じ理由で 2 成分目は別スケールから取る。
                // グレースケールノイズでも 2 次元の場になる。
                const auto decorrelated = g.Binary(
                    Kind::Add, g.Binary(Kind::Multiply, moving, g.Constant({1.37f, 1.37f, 0, 0}, Type::Float2)),
                    g.Constant({0.31f, 0.17f, 0, 0}, Type::Float2));
                const auto sample = g.Split(sampleResource(noiseResourceId, moving), Type::Float4);
                const auto sampleB = g.Split(sampleResource(noiseResourceId, decorrelated), Type::Float4);
                fieldX = sample.x;
                fieldY = sampleB.y;
            } else {
                fieldX = g.Noise(moving, 1.0f);
                fieldY = g.Noise(g.Binary(Kind::Add, moving, g.Constant({19.3f, 7.7f, 0, 0}, Type::Float2)), 1.0f);
            }
            // 第 2 オクターブを足して細部を作る。比率 0 なら従来の粗さ。
            const auto detailX = g.Noise(g.Binary(Kind::Multiply, moving, g.C(2.7f)), 2.0f);
            const auto detailY = g.Noise(g.Binary(Kind::Add, g.Binary(Kind::Multiply, moving, g.C(2.7f)),
                                                  g.Constant({31.7f, 53.1f, 0, 0}, Type::Float2)),
                                         2.0f);
            fieldX = g.Binary(Kind::Add, fieldX,
                              g.Binary(Kind::Multiply, g.Binary(Kind::Subtract, detailX, g.C(0.5f)),
                                       g.Parameter(step, "parameters.w", (std::max)(p.w, 0.0f))));
            fieldY = g.Binary(Kind::Add, fieldY,
                              g.Binary(Kind::Multiply, g.Binary(Kind::Subtract, detailY, g.C(0.5f)),
                                       g.Parameter(step, "parameters.w", (std::max)(p.w, 0.0f))));
            auto offset = g.Node(Kind::MakeFloat2, Type::Float2, "Value");
            g.Link(fieldX, offset, "X");
            g.Link(fieldY, offset, "Y");
            offset = g.Binary(Kind::Multiply, g.Binary(Kind::Subtract, offset, g.C(0.5f)),
                              g.Parameter(step, "parameters.x", p.x));
            uv = g.Binary(Kind::Add, uv, offset);
        }
        if (GetMaterialEffectStage(step.kind) == MaterialEffectStage::Uv && step.kind != MaterialEffectKind::UvFlow &&
            step.kind != MaterialEffectKind::FlowMap)
            uv = BlendEffect(g, step, localUv, originalUv, uv);
    }
    auto color = g.Constant({1, 1, 1, 1}, Type::Float4);
    auto alpha = g.C(1);
    auto mask = g.C(1);
    auto emissive = g.Constant({}, Type::Float3);
    Output baseUv = uv;
    if (hasBaseTexture) {
        if (atlasColumns > 1 || atlasRows > 1) {
            // 描画側で解決済みのフレームを再利用し、流れ/歪みはセル内で適用。
            const auto inverseGrid =
                g.Constant({1.0f / (std::max)(atlasColumns, 1u), 1.0f / (std::max)(atlasRows, 1u), 0, 0}, Type::Float2);
            const auto bounded =
                g.Binary(Kind::Add, g.Binary(Kind::Multiply, g.Unary(Kind::Saturate, uv), g.C(0.998f)), g.C(0.001f));
            baseUv = g.Binary(Kind::Add, atlasUv,
                              g.Binary(Kind::Multiply, g.Binary(Kind::Subtract, bounded, localUv), inverseGrid));
        }
        color = sampleBaseAt(baseUv);
        // ぼかしは UV 段の確定後に、方向別の複数タップ平均として適用する。
        // 空間方向のサンプリングを所有するのは Blur だけ。
        for (const auto& step : effects) {
            if (!step.enabled || step.kind != MaterialEffectKind::Blur)
                continue;
            g.blendAlpha = g.Split(color, Type::Float4).w;
            g.blendLuminance = g.Luminance(color);
            const auto before = color;
            const auto& blurParameters = step.parameters;
            if (blurParameters.x <= 0.0f && !g.Animated(step, "parameters.x"))
                continue;
            const auto radius = g.Binary(Kind::Maximum, g.Parameter(step, "parameters.x", blurParameters.x), g.C(0));
            // アトラスではタップがセル外へ出ないようグリッドで正規化する。
            const int taps = std::clamp(static_cast<int>(blurParameters.z + 0.5f), 1, 6);
            const float angle = blurParameters.y * 0.01745329252f;
            const float tapU = std::cos(angle) / atlasU;
            const float tapV = std::sin(angle) / atlasV;
            // w>0.5 で直交方向にも伸ばす（十字 2D）。1 パスで 4 方向を
            // 平均するので、分離 2 パスのような中間バッファは要らない。
            // タップ数が 4 倍になるため既定では切っておく。
            const bool crossBlur = blurParameters.w > 0.5f;
            const float crossU = -std::sin(angle) / atlasU;
            const float crossV = std::cos(angle) / atlasV;
            auto sum = color;
            for (int tap = 1; tap <= taps; ++tap) {
                const auto offset =
                    g.Binary(Kind::Multiply, radius, g.Constant({tapU * tap, tapV * tap, 0, 0}, Type::Float2));
                sum = g.Binary(Kind::Add, sum, sampleBaseAt(g.Binary(Kind::Add, baseUv, offset)));
                sum = g.Binary(Kind::Add, sum, sampleBaseAt(g.Binary(Kind::Subtract, baseUv, offset)));
                if (crossBlur) {
                    const auto crossOffset =
                        g.Binary(Kind::Multiply, radius, g.Constant({crossU * tap, crossV * tap, 0, 0}, Type::Float2));
                    sum = g.Binary(Kind::Add, sum, sampleBaseAt(g.Binary(Kind::Add, baseUv, crossOffset)));
                    sum = g.Binary(Kind::Add, sum, sampleBaseAt(g.Binary(Kind::Subtract, baseUv, crossOffset)));
                }
            }
            const float weight = 1.0f / float((crossBlur ? 4 : 2) * taps + 1);
            color = g.Binary(Kind::Multiply, sum, g.Constant({weight, weight, weight, weight}, Type::Float4));
            color = BlendEffect(g, step, localUv, before, color);
        }
        // 拖尾: 片側へ減衰しながらサンプルを伸ばす。対称平均の Blur とは
        // 別物で、進行方向の後ろだけが伸びるので「尾」に見える。
        for (const auto& step : effects) {
            if (!step.enabled || step.kind != MaterialEffectKind::Streak)
                continue;
            g.blendAlpha = g.Split(color, Type::Float4).w;
            g.blendLuminance = g.Luminance(color);
            const auto before = color;
            const auto& streak = step.parameters;
            const auto length = g.Binary(Kind::Maximum, g.Parameter(step, "parameters.x", streak.x), g.C(0));
            const int taps = std::clamp(static_cast<int>(streak.z + 0.5f), 1, 8);
            const auto decay = g.Unary(Kind::Saturate, g.Parameter(step, "parameters.w", streak.w));
            const auto angle =
                g.Binary(Kind::Multiply, g.Parameter(step, "parameters.y", streak.y), g.C(0.01745329252f));
            const auto direction =
                g.Pair(g.Binary(Kind::Multiply, g.Unary(Kind::Cosine, angle), g.C(-1.0f / atlasU / taps)),
                       g.Binary(Kind::Multiply, g.Unary(Kind::Sine, angle), g.C(-1.0f / atlasV / taps)));
            const auto stepOffset = g.Binary(Kind::Multiply, direction, length);
            auto sum = color;
            auto weightTotal = g.C(1);
            auto weight = g.C(1);
            for (int tap = 1; tap <= taps; ++tap) {
                weight = g.Binary(Kind::Multiply, weight, g.Unary(Kind::OneMinus, decay));
                const auto offset = g.Binary(Kind::Multiply, stepOffset, g.C(float(tap)));
                sum = g.Binary(Kind::Add, sum,
                               g.Binary(Kind::Multiply, sampleBaseAt(g.Binary(Kind::Add, baseUv, offset)), weight));
                weightTotal = g.Binary(Kind::Add, weightTotal, weight);
            }
            color = BlendEffect(g, step, localUv, before, g.Binary(Kind::Divide, sum, weightTotal));
        }
        // 放射ブラーは中心から（または中心へ）向かう方向へタップを伸ばす。
        // 方向別 Blur と同じく、UV 確定後の複数タップ平均だけで作る。
        for (const auto& step : effects) {
            if (!step.enabled || step.kind != MaterialEffectKind::RadialBlur)
                continue;
            g.blendAlpha = g.Split(color, Type::Float4).w;
            g.blendLuminance = g.Luminance(color);
            const auto before = color;
            const auto& p = step.parameters;
            const auto radius = g.Binary(Kind::Maximum, g.Parameter(step, "parameters.x", p.x), g.C(0));
            const int taps = std::clamp(static_cast<int>(p.w + 0.5f), 1, 4);
            const auto sign =
                g.Binary(Kind::Subtract,
                         g.Binary(Kind::Multiply,
                                  g.Step(g.C(0), g.Parameter(step, "parameters2.x", step.parameters2.x)), g.C(2)),
                         g.C(1));
            // ズーム式の放射サンプリング: 中心へ向かう線分上を割合で刻む。
            // 正規化ノードに依存しないので Float2 のまま扱える。
            const auto centered =
                g.Binary(Kind::Subtract, baseUv,
                         g.Pair(g.Parameter(step, "parameters.y", p.y), g.Parameter(step, "parameters.z", p.z)));
            auto sum = color;
            for (int tap = 1; tap <= taps; ++tap) {
                const auto scale =
                    g.Binary(Kind::Multiply, radius, g.Binary(Kind::Multiply, sign, g.C(float(tap) / float(taps))));
                const auto offset = g.Binary(
                    Kind::Multiply, centered,
                    g.Binary(Kind::Multiply, scale, g.Constant({1.0f / atlasU, 1.0f / atlasV, 0, 0}, Type::Float2)));
                sum = g.Binary(Kind::Add, sum, sampleBaseAt(g.Binary(Kind::Subtract, baseUv, offset)));
            }
            const float weight = 1.0f / float(taps + 1);
            color = g.Binary(Kind::Multiply, sum, g.Constant({weight, weight, weight, weight}, Type::Float4));
            color = BlendEffect(g, step, localUv, before, color);
        }
        // 拡張した輪郭の alpha もフィルタ結果から取る。
        alpha = g.Split(color, Type::Float4).w;
        mask = g.Luminance(color);
    }
    // 遮罩は不透明度へ掛ける。底色の解決直後に適用し、以降の効果が
    // 発光や階調を自由に足せるようにする。
    if (!maskResourceId.empty()) {
        const auto maskSample = g.Split(sampleResource(maskResourceId, localUv), Type::Float4);
        // 色のにじみ防止用 RGB 拡張は透明画素にも値を持つ。遮罩では
        // 被覆率も掛け、白黒画像 (A=1) と透明縁の画像を同じ契約で扱う。
        alpha = g.Binary(Kind::Multiply, alpha, g.Binary(Kind::Multiply, maskSample.x, maskSample.w));
    }
    Output baseShare{};
    const bool includesParticleColor =
        adjustmentStart < effects.size() &&
        std::any_of(
            effects.begin() + adjustmentStart, effects.end(),
            [](const auto& step) {
                return step.enabled && GetMaterialEffectStage(step.kind) == MaterialEffectStage::Surface;
            });
    for (size_t effectIndex = 0; effectIndex < effects.size(); ++effectIndex) {
        // 調整は下層の色・発光を受け取る。粒子 tint の二重乗算をしない。
        if (includesParticleColor && effectIndex == adjustmentStart) {
            const auto tint = g.Node(Kind::ParticleColor, Type::Float4, "Value");
            color = g.Binary(Kind::Multiply, color, tint);
            alpha = g.Binary(Kind::Multiply, alpha, g.Split(tint, Type::Float4).w);
        }
        const auto& step = effects[effectIndex];
        if (!step.enabled)
            continue;
        const auto& p = step.parameters;
        if (GetMaterialEffectStage(step.kind) != MaterialEffectStage::Surface)
            continue;
        if (effectIndex >= adjustmentStart) {
            MATH::Vec4 literal{};
            if (!g.Literal(emissive, literal) || literal.x != 0 || literal.y != 0 || literal.z != 0) {
                const auto e = g.Split(emissive, Type::Float3);
                auto rgba = g.Node(Kind::MakeFloat4, Type::Float4, "Value");
                g.Link(e.x, rgba, "X");
                g.Link(e.y, rgba, "Y");
                g.Link(e.z, rgba, "Z");
                g.Link(g.C(0), rgba, "W");
                const auto combined = g.Binary(Kind::Add, color, rgba);
                if (lit)
                    baseShare =
                        g.Binary(Kind::Divide,
                                 baseShare.node.empty() ? EffectRgb(g, color)
                                                        : g.Binary(Kind::Multiply, EffectRgb(g, color), baseShare),
                                 g.Binary(Kind::Maximum, EffectRgb(g, combined), g.C(0.0001f)));
                color = combined;
                emissive = g.Constant({}, Type::Float3);
            }
        }
        g.blendAlpha = alpha;
        g.blendLuminance = g.Luminance(color);
        if (effectIndex >= adjustmentStart && step.kind == MaterialEffectKind::Gradient)
            mask = g.blendLuminance;
        const auto beforeColor = color, beforeAlpha = alpha, beforeEmissive = emissive, beforeMask = mask;
        if (!ApplyLocalStyle(g, localUv, step, color) &&
            !ApplyLocalFilter(
                g, localUv, baseUv, step,
                [&](Output at) { return hasBaseTexture ? sampleBaseAt(at) : g.Constant({1, 1, 1, 1}, Type::Float4); },
                color, alpha, atlasU, atlasV) &&
            !ApplyProceduralSurface(g, uv, step, color, alpha, mask))
            switch (step.kind) {
            case MaterialEffectKind::Gradient:
                if (step.colorStops.size() >= 2u) {
                    // 多段ランプは区間ごとの smoothstep を重ねて作る。ノードは
                    // 既存の Lerp/SmoothStep だけを使い、専用の解釈器は持たない。
                    auto stops = step.colorStops;
                    std::sort(stops.begin(), stops.end(),
                              [](const auto& left, const auto& right) { return left.time < right.time; });
                    auto ramp = g.Constant(stops.front().color, Type::Float4);
                    for (size_t index = 1u; index < stops.size(); ++index) {
                        const float span = (std::max)(stops[index].time - stops[index - 1u].time, 1.0e-4f);
                        const auto t = g.Smooth(g.C(stops[index - 1u].time), g.C(stops[index - 1u].time + span), mask);
                        ramp = g.Lerp(ramp, g.Constant(stops[index].color, Type::Float4), t);
                    }
                    color = ramp;
                } else {
                    color = g.Lerp(g.Constant(step.colorA, Type::Float4), g.Constant(step.colorB, Type::Float4), mask);
                }
                break;
            case MaterialEffectKind::Fade: {
                const auto fadeIn = g.Smooth(g.C(0), g.C((std::max)(p.x, 0.0001f)), state);
                const auto fadeOut =
                    g.Unary(Kind::OneMinus, g.Smooth(g.C(1 - (std::max)(p.y, 0.0001f)), g.C(1), state));
                alpha = g.Binary(Kind::Multiply, alpha, g.Binary(Kind::Multiply, fadeIn, fadeOut));
                break;
            }
            case MaterialEffectKind::Wipe:
            case MaterialEffectKind::Dissolve: {
                const auto progressId = step.id + ".progress";
                const auto threshold = std::find(animatedParameters.begin(), animatedParameters.end(), progressId) !=
                                               animatedParameters.end()
                                           ? g.Parameter(step, "progress", p.x)
                                           : g.Lerp(g.C(p.x), g.C(p.y), state);
                Output field;
                const bool dissolve = step.kind == MaterialEffectKind::Dissolve;
                if (dissolve && !noiseResourceId.empty()) {
                    // ノイズ貼図があれば溶解の場もそれに合わせる。
                    field = g.Split(sampleResource(noiseResourceId,
                                                   g.Binary(Kind::Multiply, uv, g.C((std::max)(p.z, 0.01f)))),
                                    Type::Float4)
                                .x;
                } else if (dissolve)
                    field = g.Noise(uv, (std::max)(p.z, 0.01f));
                else {
                    const auto split = g.Split(localUv, Type::Float2);
                    field = p.w < 0.5f ? split.x : split.y;
                }
                const float width = (std::max)(dissolve ? p.w : p.z, 0.0001f);
                const auto edgeEnd = g.Binary(Kind::Add, threshold, g.C(width));
                const auto keep = g.Smooth(threshold, edgeEnd, field);
                if (dissolve) {
                    const auto outer = g.Smooth(edgeEnd, g.Binary(Kind::Add, edgeEnd, g.C(width)), field);
                    emissive = g.Binary(Kind::Add, emissive,
                                        g.Binary(Kind::Multiply, g.Binary(Kind::Subtract, keep, outer),
                                                 g.Constant(step.colorA, Type::Float3)));
                }
                alpha = g.Binary(Kind::Multiply, alpha, keep);
                break;
            }
            case MaterialEffectKind::Emission: {
                auto pulse = g.Unary(Kind::Sine, g.Binary(Kind::Multiply, seconds, g.C(p.z * 6.2831853f)));
                pulse = g.Binary(Kind::Multiply, g.Binary(Kind::Add, g.C(1), g.Binary(Kind::Multiply, pulse, g.C(p.y))),
                                 g.Parameter(step, "parameters.x", p.x));
                emissive = g.Binary(Kind::Add, emissive,
                                    g.Binary(Kind::Multiply, g.Constant(step.colorA, Type::Float3),
                                             g.Binary(Kind::Multiply, mask, pulse)));
                break;
            }
            case MaterialEffectKind::Fresnel: {
                auto rim = g.Node(Kind::Fresnel, Type::Float1);
                g.Link(g.C(0), rim, "F0");
                g.Link(g.C((std::max)(p.x, 0.01f)), rim, "Power");
                emissive = g.Binary(Kind::Add, emissive,
                                    g.Binary(Kind::Multiply, g.Constant(step.colorA, Type::Float3),
                                             g.Binary(Kind::Multiply, rim, g.Parameter(step, "parameters.y", p.y))));
                alpha = g.Binary(Kind::Multiply, alpha, g.Lerp(g.C(1), rim, g.C(std::clamp(p.z, 0.0f, 1.0f))));
                break;
            }
            case MaterialEffectKind::Opacity:
                // スタック内の任意位置で効く統一の不透明度。
                alpha =
                    g.Binary(Kind::Multiply, alpha, g.Unary(Kind::Saturate, g.Parameter(step, "parameters.x", p.x)));
                break;
            case MaterialEffectKind::Glow: {
                // 明部抽出に加えて、基準貼図を半径違いで何度も引き直して
                // ぼかしを作る。UV 空間で広がるので輪郭の外側にも光輪が出る。
                // parameters.w が半径 (UV)。0 なら従来どおり加算のみ。
                const auto softness = g.Binary(Kind::Maximum, g.Parameter(step, "parameters.z", p.z), g.C(0.0001f));
                const auto intensity = g.Parameter(step, "parameters.y", p.y);
                const auto threshold = g.Parameter(step, "parameters.x", p.x);
                const auto thresholdEnd = g.Binary(Kind::Add, threshold, softness);
                const auto brightCore = g.Smooth(threshold, thresholdEnd, g.Luminance(color));
                auto brightTotal = brightCore;
                const auto radius = g.Binary(Kind::Maximum, g.Parameter(step, "parameters.w", p.w), g.C(0));
                // 2 段目 (遠い半径) の多級ぼかし。parameters2.x が半径、
                // parameters2.y が重み (0 なら 0.5)。0 のときは従来動作のまま。
                const float farRadius = (std::max)(step.parameters2.x, 0.0f);
                const float farWeight = step.parameters2.y > 0.0f ? step.parameters2.y : 0.5f;
                if (hasBaseTexture && (p.w > 0.0f || g.Animated(step, "parameters.w"))) {
                    // 十字 4 タップ。タップごとに輝度変換としきい値を計算すると
                    // 式の値スロット (48) を使い切り、コンパイルに失敗する。
                    // 先に平均を作り、輝度変換としきい値は 1 回だけ通す。
                    const auto offsetU =
                        g.Binary(Kind::Multiply, radius, g.Constant({1 / atlasU, 0, 0, 0}, Type::Float2));
                    const auto offsetV =
                        g.Binary(Kind::Multiply, radius, g.Constant({0, 1 / atlasV, 0, 0}, Type::Float2));
                    auto averaged = sampleBaseAt(g.Binary(Kind::Add, baseUv, offsetU));
                    averaged = g.Binary(Kind::Add, averaged, sampleBaseAt(g.Binary(Kind::Subtract, baseUv, offsetU)));
                    averaged = g.Binary(Kind::Add, averaged, sampleBaseAt(g.Binary(Kind::Add, baseUv, offsetV)));
                    averaged = g.Binary(Kind::Add, averaged, sampleBaseAt(g.Binary(Kind::Subtract, baseUv, offsetV)));
                    averaged =
                        g.Binary(Kind::Multiply, averaged, g.Constant({0.25f, 0.25f, 0.25f, 0.25f}, Type::Float4));
                    const auto blurredBright = g.Smooth(threshold, thresholdEnd, g.Luminance(averaged));
                    brightTotal = g.Binary(Kind::Add, brightCore, blurredBright);
                }
                if (hasBaseTexture && farRadius > p.w) {
                    // 遠い段は V 軸 (リングでは帯幅 = 半径方向) の外側 1 タップのみ。
                    // タップを増やすと 128 レジスタ予算を超え、アニメーション付き
                    // レイヤーのマテリアルがコンパイルできなくなる。
                    const auto farOffsetV = g.Constant({0.0f, farRadius / atlasV, 0.0f, 0.0f}, Type::Float2);
                    auto far = sampleBaseAt(g.Binary(Kind::Add, baseUv, farOffsetV));
                    const auto farBright = g.Smooth(threshold, thresholdEnd, g.Luminance(far));
                    brightTotal = g.Binary(Kind::Add, brightTotal, g.Binary(Kind::Multiply, farBright, g.C(farWeight)));
                }
                emissive = g.Binary(Kind::Add, emissive,
                                    g.Binary(Kind::Multiply, g.Constant(step.colorA, Type::Float3),
                                             g.Binary(Kind::Multiply, brightTotal, intensity)));
                break;
            }
            case MaterialEffectKind::ColorCorrect: {
                // x=露出(乗算), y=コントラスト(0.5 中心), z=彩度, w=黒レベル。
                // 追加サンプルを持たない四則演算だけなので、命令/レジスタ
                // 予算をほとんど消費しない。
                const auto exposure = g.Parameter(step, "parameters.x", p.x);
                const auto contrast = g.Parameter(step, "parameters.y", p.y);
                const auto saturation = g.Parameter(step, "parameters.z", p.z);
                const auto black = g.Parameter(step, "parameters.w", p.w);
                auto graded = g.Binary(
                    Kind::Add,
                    g.Binary(Kind::Multiply,
                             g.Binary(Kind::Subtract,
                                      g.Binary(Kind::Multiply, g.Binary(Kind::Subtract, color, black), exposure),
                                      g.C(0.5f)),
                             contrast),
                    g.C(0.5f));
                color = g.Lerp(g.Luminance(graded), graded, saturation);
                const auto rgba = g.Split(color, Type::Float4);
                auto red = rgba.x, green = rgba.y, blue = rgba.z;
                // 灰色軸 (1,1,1) 周りの回転。無彩色と HDR 強度を保ち、
                // 旧 R/B 平面回転のように白が負の赤成分を持たない。
                if (std::abs(step.parameters2.x) > 1.0e-4f || g.Animated(step, "parameters2.x")) {
                    const auto angle = g.Binary(Kind::Multiply, g.Parameter(step, "parameters2.x", step.parameters2.x),
                                                g.C(0.01745329252f));
                    const auto cosine = g.Unary(Kind::Cosine, angle);
                    const auto sine = g.Unary(Kind::Sine, angle);
                    const auto diagonal =
                        g.Binary(Kind::Multiply, g.Binary(Kind::Add, g.C(1), g.Binary(Kind::Multiply, cosine, g.C(2))),
                                 g.C(1.0f / 3.0f));
                    const auto base = g.Binary(Kind::Multiply, g.Unary(Kind::OneMinus, cosine), g.C(1.0f / 3.0f));
                    const auto rotation = g.Binary(Kind::Multiply, sine, g.C(0.57735026919f));
                    const auto forward = g.Binary(Kind::Subtract, base, rotation);
                    const auto backward = g.Binary(Kind::Add, base, rotation);
                    const auto rotate = [&](Output a, Output b, Output c) {
                        return g.Binary(Kind::Maximum, g.C(0),
                                        g.Binary(Kind::Add, g.Binary(Kind::Multiply, a, diagonal),
                                                 g.Binary(Kind::Add, g.Binary(Kind::Multiply, b, forward),
                                                          g.Binary(Kind::Multiply, c, backward))));
                    };
                    const auto oldRed = red;
                    const auto oldGreen = green;
                    red = rotate(oldRed, oldGreen, blue);
                    green = rotate(oldGreen, blue, oldRed);
                    blue = rotate(blue, oldRed, oldGreen);
                }
                if (std::abs(step.parameters2.x) <= 1.0e-4f && !g.Animated(step, "parameters2.x"))
                    break;
                auto node = g.Node(Kind::MakeFloat4, Type::Float4, "Value");
                g.Link(red, node, "X");
                g.Link(green, node, "Y");
                g.Link(blue, node, "Z");
                g.Link(rgba.w, node, "W");
                color = node;
                break;
            }
            case MaterialEffectKind::LuminanceMask: {
                const auto softness = g.Binary(Kind::Maximum, g.Parameter(step, "parameters.y", p.y), g.C(0.0001f));
                const auto threshold = g.Parameter(step, "parameters.x", p.x);
                const auto luminanceMask =
                    g.Smooth(threshold, g.Binary(Kind::Add, threshold, softness), g.Luminance(color));
                alpha = p.z > 0.5f ? luminanceMask : g.Binary(Kind::Multiply, alpha, luminanceMask);
                break;
            }
            case MaterialEffectKind::LightSweep: {
                // 方向に沿った帯を加算する AE のライトスイープ相当。
                const auto sweepAngle =
                    g.Binary(Kind::Multiply, g.Parameter(step, "parameters.z", p.z), g.C(0.01745329252f));
                const auto axis = g.Split(localUv, Type::Float2);
                const auto projection =
                    g.Binary(Kind::Add, g.Binary(Kind::Multiply, axis.x, g.Unary(Kind::Cosine, sweepAngle)),
                             g.Binary(Kind::Multiply, axis.y, g.Unary(Kind::Sine, sweepAngle)));
                const auto center = g.Parameter(step, "parameters.x", p.x);
                const auto halfWidth =
                    g.Binary(Kind::Multiply,
                             g.Binary(Kind::Maximum, g.Parameter(step, "parameters.y", p.y), g.C(0.0001f)), g.C(0.5f));
                const auto leading = g.Smooth(g.Binary(Kind::Subtract, center, halfWidth), center, projection);
                const auto trailing =
                    g.Unary(Kind::OneMinus, g.Smooth(center, g.Binary(Kind::Add, center, halfWidth), projection));
                emissive = g.Binary(Kind::Add, emissive,
                                    g.Binary(Kind::Multiply, g.Constant(step.colorA, Type::Float3),
                                             g.Binary(Kind::Multiply, g.Binary(Kind::Multiply, leading, trailing),
                                                      g.Parameter(step, "parameters.w", p.w))));
                break;
            }
            default: break;
            }
        if (UsesEffectBlend(g, step)) {
            const auto weight = EffectBlendWeight(g, step, localUv);
            const auto blend = [&](Output before, Output after) {
                return before.node == after.node && before.pin == after.pin ? after : g.Lerp(before, after, weight);
            };
            color = blend(beforeColor, color);
            alpha = blend(beforeAlpha, alpha);
            emissive = blend(beforeEmissive, emissive);
            mask = blend(beforeMask, mask);
        }
    }
    if (includesParticleColor && lit && !baseShare.node.empty()) {
        const auto combined = EffectRgb(g, color);
        emissive =
            g.Binary(Kind::Add, emissive, g.Binary(Kind::Multiply, combined, g.Unary(Kind::OneMinus, baseShare)));
        color = g.Binary(Kind::Multiply, color, baseShare);
    }
    const auto output = g.Node(Kind::ParticleOutput, Type::Float4);
    g.Link(color, output, "Color");
    g.Link(alpha, output, "Opacity");
    g.Link(emissive, output, "Emissive");
    g.data.graph.outputNodeId = output.node;
    return std::move(g.data);
}
} // namespace HIKARI
