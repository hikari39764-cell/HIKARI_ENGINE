#pragma once
#include "Assets/Material/Authoring/Internal/HIKARI_MaterialEffectGraph.h"

namespace HIKARI::MATERIAL_EFFECT_INTERNAL {
inline bool UsesEffectBlend(EffectGraph& g, const MaterialEffectStep& step) {
    return step.mix != 1 || step.mask != MaterialEffectMask::None || g.Animated(step, "mix");
}
inline Output EffectBlendWeight(EffectGraph& g, const MaterialEffectStep& step, Output uv) {
    auto weight = g.Unary(Kind::Saturate, g.Parameter(step, "mix", step.mix));
    if (step.mask == MaterialEffectMask::None)
        return weight;
    if (step.mask == MaterialEffectMask::Alpha || step.mask == MaterialEffectMask::Luminance) {
        auto field = step.mask == MaterialEffectMask::Alpha ? g.blendAlpha : g.blendLuminance;
        if (field.node.empty())
            field = g.C(1);
        auto mask = g.Unary(Kind::Saturate, field);
        if (step.maskInvert)
            mask = g.Unary(Kind::OneMinus, mask);
        return g.Binary(Kind::Multiply, weight, mask);
    }
    const auto& p = step.maskParameters;
    const auto coordinates = g.Split(uv, Type::Float2);
    auto field = step.mask == MaterialEffectMask::Vertical ? coordinates.y : coordinates.x;
    if (step.mask == MaterialEffectMask::Radial) {
        field = g.Node(Kind::Length, Type::Float1);
        g.Link(g.Binary(Kind::Subtract, uv, g.Constant({p.x, p.y, 0, 0}, Type::Float2)), field, "Value");
    }
    auto mask = g.Unary(Kind::OneMinus, g.Smooth(g.C(p.z), g.C(p.z + (std::max)(p.w, 0.00001f)), field));
    if (step.maskInvert)
        mask = g.Unary(Kind::OneMinus, mask);
    return g.Binary(Kind::Multiply, weight, mask);
}
inline Output BlendEffect(EffectGraph& g, const MaterialEffectStep& step, Output uv, Output before, Output after) {
    return UsesEffectBlend(g, step) ? g.Lerp(before, after, EffectBlendWeight(g, step, uv)) : after;
}
} // namespace HIKARI::MATERIAL_EFFECT_INTERNAL
