#pragma once
#include "Assets/Material/Authoring/Internal/HIKARI_MaterialEffectGraph.h"

namespace HIKARI::MATERIAL_EFFECT_INTERNAL {
inline Output EffectLength(EffectGraph& g, Output value) {
    auto result = g.Node(Kind::Length, Type::Float1);
    g.Link(value, result, "Value");
    return result;
}
inline Output EffectRgb(EffectGraph& g, Output value) {
    const auto channels = g.Split(value, value.type);
    auto result = g.Node(Kind::MakeFloat3, Type::Float3, "Value");
    g.Link(channels.x, result, "X");
    g.Link(channels.y, result, "Y");
    g.Link(channels.z, result, "Z");
    return result;
}
// 全て UV 単位。画面サイズ、投影座標、フレーム番号を参照しない。
inline bool ApplyLocalStyle(EffectGraph& g, Output uv, const MaterialEffectStep& step, Output& color) {
    const auto& p = step.parameters;
    const auto P = [&](const char* id, float value) { return g.Parameter(step, id, value); };
    const auto add = [&](Output a, Output b) { return g.Binary(Kind::Add, a, b); };
    const auto sub = [&](Output a, Output b) { return g.Binary(Kind::Subtract, a, b); };
    const auto mul = [&](Output a, Output b) { return g.Binary(Kind::Multiply, a, b); };
    const auto div = [&](Output a, Output b) { return g.Binary(Kind::Divide, a, b); };
    const auto maximum = [&](Output a, float b) { return g.Binary(Kind::Maximum, a, g.C(b)); };
    const auto sat = [&](Output a) { return g.Unary(Kind::Saturate, a); };
    const auto rotated = [&](Output at, Output angle) {
        return g.UvTransform(at, g.Constant({}, Type::Float2), g.Constant({1, 1, 0, 0}, Type::Float2),
                             g.Constant({}, Type::Float2), mul(angle, g.C(0.01745329252f)));
    };
    if (step.kind == MaterialEffectKind::Posterize || step.kind == MaterialEffectKind::Dither) {
        const auto levels = maximum(P("parameters.x", p.x), 2);
        const auto positive = maximum(color, 0);
        auto bounded = div(positive, add(g.C(1), positive));
        if (step.kind == MaterialEffectKind::Dither) {
            // 4x4 Bayer。整数セル上のパターンなので停止時にちらつかない。
            const auto cell = g.Unary(Kind::Floor, div(uv, maximum(P("parameters.y", p.y), 0.001f)));
            const auto xy = g.Split(cell, Type::Float2);
            const auto bayer2 = [&](Output x, Output y) {
                const auto xBit = g.Binary(Kind::Modulo, x, g.C(2));
                const auto yBit = g.Binary(Kind::Modulo, y, g.C(2));
                return add(mul(g.Unary(Kind::Absolute, sub(xBit, yBit)), g.C(2)), yBit);
            };
            const auto bayer = div(
                add(mul(bayer2(xy.x, xy.y), g.C(4)),
                    add(bayer2(g.Unary(Kind::Floor, mul(xy.x, g.C(0.5f))), g.Unary(Kind::Floor, mul(xy.y, g.C(0.5f)))),
                        g.C(0.5f))),
                g.C(16));
            bounded = sat(add(bounded, div(mul(sub(bayer, g.C(0.5f)), P("parameters.z", p.z)), levels)));
        }
        auto bucket = g.Binary(Kind::Minimum, g.Unary(Kind::Floor, mul(bounded, levels)), sub(levels, g.C(1)));
        color = g.Lerp(color, div(bucket, maximum(sub(levels, bucket), 1)),
                       P(step.kind == MaterialEffectKind::Dither ? "parameters.w" : "parameters.y",
                         step.kind == MaterialEffectKind::Dither ? p.w : p.y));
        return true;
    }
    if (step.kind == MaterialEffectKind::Duotone || step.kind == MaterialEffectKind::Threshold ||
        step.kind == MaterialEffectKind::Halftone) {
        const auto luma = maximum(g.Luminance(color), 0);
        const auto bounded = div(luma, add(g.C(1), luma));
        Output tone;
        if (step.kind == MaterialEffectKind::Duotone)
            tone =
                sat(add(add(mul(sub(bounded, g.C(0.5f)), P("parameters.y", p.y)), g.C(0.5f)), P("parameters.x", p.x)));
        else if (step.kind == MaterialEffectKind::Threshold) {
            const auto threshold = P("parameters.x", p.x), softness = maximum(P("parameters.y", p.y), 0.001f);
            tone = g.Smooth(sub(threshold, softness), add(threshold, softness), luma);
        } else {
            const auto at = rotated(uv, P("parameters.y", p.y));
            const auto cell = sub(g.Unary(Kind::Fraction, div(at, maximum(P("parameters.x", p.x), 0.001f))), g.C(0.5f));
            const auto radius = mul(g.Unary(Kind::SquareRoot, sat(mul(bounded, P("parameters.z", p.z)))), g.C(0.7f));
            tone = g.Unary(Kind::OneMinus, g.Smooth(radius, add(radius, g.C(0.04f)), EffectLength(g, cell)));
        }
        auto styled = g.Lerp(g.Constant(step.colorA, Type::Float4), g.Constant(step.colorB, Type::Float4), tone);
        const auto retain = step.kind == MaterialEffectKind::Halftone ? g.C(1) : P("parameters.z", p.z);
        styled = mul(styled, g.Lerp(g.C(1), maximum(luma, 1), retain));
        color = g.Lerp(color, styled, P("parameters.w", p.w));
        return true;
    }
    if (step.kind == MaterialEffectKind::Vignette) {
        auto delta = sub(uv, g.Constant({0.5f, 0.5f, 0, 0}, Type::Float2));
        delta = mul(delta, g.Pair(P("parameters.w", p.w), g.C(1)));
        const auto radius = P("parameters.y", p.y);
        const auto edge =
            g.Smooth(radius, add(radius, maximum(P("parameters.z", p.z), 0.001f)), mul(EffectLength(g, delta), g.C(2)));
        color = mul(color, sub(g.C(1), mul(edge, P("parameters.x", p.x))));
        return true;
    }
    if (step.kind == MaterialEffectKind::Grain) {
        const auto cell = g.Unary(Kind::Floor, div(uv, maximum(P("parameters.y", p.y), 0.001f)));
        const auto noise = g.Noise(add(cell, P("parameters.z", p.z)), 1.0f);
        color = maximum(mul(color, add(g.C(1), mul(sub(mul(noise, g.C(2)), g.C(1)),
                                                   mul(P("parameters.x", p.x), P("parameters.w", p.w))))),
                        0);
        return true;
    }
    if (step.kind == MaterialEffectKind::Scanlines) {
        const auto xy = g.Split(rotated(uv, P("parameters.w", p.w)), Type::Float2);
        const auto phase =
            mul(add(div(xy.y, maximum(P("parameters.x", p.x), 0.001f)), P("parameters.z", p.z)), g.C(6.2831853f));
        const auto wave = add(g.C(0.5f), mul(g.Unary(Kind::Cosine, phase), g.C(0.5f)));
        color = mul(color, sub(g.C(1), mul(wave, P("parameters.y", p.y))));
        return true;
    }
    return false;
}

template <class Sample>
bool ApplyLocalFilter(EffectGraph& g, Output uv, Output sampleUv, const MaterialEffectStep& step, Sample sample,
                      Output& color, Output& alpha, float atlasU, float atlasV) {
    const auto& p = step.parameters;
    const auto& q = step.parameters2;
    const auto P = [&](const char* id, float value) { return g.Parameter(step, id, value); };
    const auto add = [&](Output a, Output b) { return g.Binary(Kind::Add, a, b); };
    const auto sub = [&](Output a, Output b) { return g.Binary(Kind::Subtract, a, b); };
    const auto mul = [&](Output a, Output b) { return g.Binary(Kind::Multiply, a, b); };
    const auto tap = [&](Output offset) {
        return sample(add(sampleUv, mul(offset, g.Constant({1 / atlasU, 1 / atlasV, 0, 0}, Type::Float2))));
    };
    if (step.kind == MaterialEffectKind::Chromatic) {
        const auto angle = mul(P("parameters.y", p.y), g.C(0.01745329252f));
        const auto axis = g.Pair(g.Unary(Kind::Cosine, angle), g.Unary(Kind::Sine, angle));
        const auto delta = sub(uv, g.Constant({0.5f, 0.5f, 0, 0}, Type::Float2));
        const auto offset = mul(g.Lerp(axis, delta, P("parameters.z", p.z)), P("parameters.x", p.x));
        const auto red = g.Split(tap(offset), Type::Float4), blue = g.Split(tap(mul(offset, g.C(-1))), Type::Float4);
        const auto current = g.Split(color, Type::Float4);
        const auto source = g.Split(sample(sampleUv), Type::Float4);
        // 貼図のずれを下層の最終色へ反映する。調整前の tint/色補正を生の RGB で上書きしない。
        const auto shifted = [&](Output value, Output original, Output displaced) {
            return mul(value, g.Binary(Kind::Divide, displaced, g.Binary(Kind::Maximum, original, g.C(0.0001f))));
        };
        auto result = g.Node(Kind::MakeFloat4, Type::Float4, "Value");
        g.Link(shifted(current.x, source.x, red.x), result, "X");
        g.Link(current.y, result, "Y");
        g.Link(shifted(current.z, source.z, blue.z), result, "Z");
        g.Link(current.w, result, "W");
        color = g.Lerp(color, result, P("parameters.w", p.w));
        return true;
    }
    if (step.kind != MaterialEffectKind::AlphaMorph && step.kind != MaterialEffectKind::Outline &&
        step.kind != MaterialEffectKind::Ink)
        return false;
    const auto radius = g.Unary(Kind::Absolute, P("parameters.x", p.x));
    const auto center = g.Split(sample(sampleUv), Type::Float4).w;
    auto lo = center, hi = center;
    for (const MATH::Vec2 direction : {MATH::Vec2{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
        const auto coverage =
            g.Split(tap(mul(radius, g.Constant({direction.x, direction.y, 0, 0}, Type::Float2))), Type::Float4).w;
        lo = g.Binary(Kind::Minimum, lo, coverage);
        hi = g.Binary(Kind::Maximum, hi, coverage);
    }
    if (step.kind == MaterialEffectKind::AlphaMorph) {
        const auto positive = g.Step(g.C(0), P("parameters.x", p.x));
        auto coverage = g.Lerp(lo, hi, positive);
        const auto feather = P("parameters.y", p.y);
        const auto soften = g.Unary(Kind::Saturate, g.Binary(Kind::Divide, feather, add(radius, g.C(0.0001f))));
        coverage = g.Lerp(coverage, mul(add(lo, hi), g.C(0.5f)), soften);
        alpha = g.Lerp(alpha, g.Unary(Kind::Saturate, add(alpha, sub(coverage, center))), P("parameters.z", p.z));
    } else if (step.kind == MaterialEffectKind::Outline) {
        const auto edge = p.y > 0.5f ? sub(center, lo) : sub(hi, center);
        const auto coverage = mul(edge, mul(P("parameters.z", p.z), P("parameters.w", p.w)));
        const auto finalAlpha = g.Unary(Kind::Saturate, add(alpha, coverage));
        color = g.Lerp(color, g.Constant(step.colorA, Type::Float4),
                       g.Binary(Kind::Divide, coverage, g.Binary(Kind::Maximum, finalAlpha, g.C(0.0001f))));
        alpha = finalAlpha;
    } else {
        const auto paper = g.Noise(uv, mul(P("parameters2.y", q.y), g.C(96)));
        const auto edge = g.Unary(Kind::Saturate, mul(sub(hi, lo), P("parameters.y", p.y)));
        const auto luma = g.Binary(Kind::Maximum, g.Luminance(color), g.C(0));
        const auto wash = g.Binary(Kind::Divide, luma, add(g.C(1), luma));
        const auto pigment = g.Unary(Kind::Saturate, add(add(g.C(0.3f), mul(sub(g.C(1), wash), g.C(0.5f))), edge));
        const auto inkColor = g.Constant(step.colorA, Type::Float4);
        const auto ink = g.Lerp(add(inkColor, g.C(0.22f)), inkColor, pigment);
        const auto grain = sub(g.C(1), mul(paper, mul(P("parameters.z", p.z), P("parameters2.w", q.w))));
        color = g.Lerp(color, mul(ink, grain), P("parameters.w", p.w));
        const auto bleed =
            g.Unary(Kind::Saturate, g.Binary(Kind::Divide, P("parameters2.x", q.x), add(radius, g.C(0.0001f))));
        const auto coverage = g.Lerp(alpha, hi, bleed);
        alpha = g.Lerp(alpha, mul(coverage, sub(g.C(1), mul(paper, P("parameters2.z", q.z)))), P("parameters.w", p.w));
    }
    return true;
}
} // namespace HIKARI::MATERIAL_EFFECT_INTERNAL
