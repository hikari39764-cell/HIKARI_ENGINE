#pragma once
#include "Assets/Material/Authoring/Internal/HIKARI_MaterialEffectBlend.h"

namespace HIKARI::MATERIAL_EFFECT_INTERNAL {
    inline Output ApplyUvFlow(EffectGraph& g, Output uv, const MaterialEffectStep& step, Output seconds) {
        const auto original = uv;
        const auto& p = step.parameters;
        const auto& p2 = step.parameters2;
        if (p2.y > 0.5f) {
            const auto swapped = g.Split(uv, Type::Float2);
            uv = g.Pair(swapped.y, swapped.x);
        }
        // 固定値の組立て・単位変換は作成時に済ませる。アニメーション値だけ
        // 実行時の命令に残し、見た目を変えずに効果を組み合わせる余地を確保する。
        const auto scale = !g.Animated(step,"parameters.z") && !g.Animated(step,"parameters.w")
            ? g.Constant({p.z,p.w,0,0}, Type::Float2)
            : g.Pair(g.Parameter(step,"parameters.z",p.z), g.Parameter(step,"parameters.w",p.w));
        constexpr float radians = 0.01745329252f;
        const auto rotation = g.Animated(step,"parameters2.x")
            ? g.Binary(Kind::Multiply, g.Parameter(step,"parameters2.x",p2.x), g.C(radians))
            : g.C(p2.x * radians);
        const auto mapped = g.Unary(Kind::Fraction, g.UvTransform(uv,
            g.Constant({p2.z,p2.w,0,0}, Type::Float2),
            scale,
            g.Pair(g.Flow(step,"parameters.x",p.x,seconds), g.Flow(step,"parameters.y",p.y,seconds)),
            rotation));
        return BlendEffect(g, step, original, original, mapped);
    }

    // 各スロットの UV と二相フローを所有する。既存グラフ命令へ展開し、
    // サンプラーも GPU インタープリターも追加しない。
    class EffectTextureSampling {
        EffectGraph& g;
        std::span<const MaterialEffectStep> effects;
        Output seconds;
        bool hasFlowMap;
        std::unordered_map<std::string, Output> samples;

        Output Raw(const std::string& id, Output uv) {
            const auto key = id + ":" + uv.node + "." + uv.pin;
            if (const auto found = samples.find(key); found != samples.end()) return found->second;
            auto node = g.Node(Kind::TextureSample, Type::Float4, "Color");
            g.data.graph.nodes.back().referenceId = id;
            g.Link(uv, node, "UV");
            samples.emplace(key,node);
            return node;
        }
        Output SlotUv(Output uv, MaterialEffectTextureTarget target) {
            for (const auto& step : effects)
                if (step.enabled && step.kind == MaterialEffectKind::UvFlow && step.textureTarget == target)
                    uv = ApplyUvFlow(g, uv, step, seconds);
            return uv;
        }
        template<class Sampler>
        Output Flow(Output uv, MaterialEffectTextureTarget target, Sampler sample) {
            if (hasFlowMap && (target == MaterialEffectTextureTarget::BaseColor || target == MaterialEffectTextureTarget::Mask)) {
                for (const auto& step : effects) {
                    if (!step.enabled || step.kind != MaterialEffectKind::FlowMap ||
                        (step.textureTarget != target && step.textureTarget != MaterialEffectTextureTarget::Shared)) continue;
                    const auto original = uv;
        const auto& p = step.parameters;
                    auto fieldUv = SlotUv(g.Binary(Kind::Multiply, uv,
                        g.Parameter(step,"parameters.z",p.z)), MaterialEffectTextureTarget::FlowMap);
                    const auto rg = g.Split(Raw("effect.flowMap", g.Unary(Kind::Fraction, fieldUv)), Type::Float4);
                    auto direction = g.Binary(Kind::Subtract,
                        g.Binary(Kind::Multiply, g.Pair(rg.x,rg.y), g.C(2)), g.C(1));
                    const auto& q = step.parameters2;
                    if (q.x != 0 || g.Animated(step,"parameters2.x")) {
                        direction = g.UvTransform(direction,g.Constant({},Type::Float2),
                            g.Constant({1,1,0,0},Type::Float2),g.Constant({},Type::Float2),
                            g.Binary(Kind::Multiply,g.Parameter(step,"parameters2.x",q.x),g.C(0.01745329252f)));
                    }
                    direction = g.Binary(Kind::Multiply, direction, g.Parameter(step,"parameters.x",p.x));
                    auto clock = g.Flow(step,"parameters.y",p.y,seconds);
                    if (q.z > 0.5f) {
                        const auto variation = g.Parameter(step,"parameters2.y",q.y);
                        clock = g.Binary(Kind::Multiply,clock,g.Lerp(g.C(1),rg.z,variation));
                    }
                    const auto a = g.Unary(Kind::Fraction, clock);
                    const auto b = g.Unary(Kind::Fraction, g.Binary(Kind::Add, clock, g.C(0.5f)));
                    const auto offset = [&](Output phase) {
                        return g.Binary(Kind::Subtract, uv,
                            g.Binary(Kind::Multiply, direction, g.Binary(Kind::Subtract, phase, g.C(0.5f))));
                    };
                    const auto weight = g.Unary(Kind::Absolute,
                        g.Binary(Kind::Subtract, g.Binary(Kind::Multiply, a, g.C(2)), g.C(1)));
                    const auto flowed = g.Lerp(sample(offset(a)), sample(offset(b)), weight);
                    // mix=1 は基準サンプルを省き、通常の二相フローを 2 タップに保つ。
                    if (p.w == 1 && !g.Animated(step,"parameters.w") && !UsesEffectBlend(g,step)) return flowed;
                    auto mix = g.Unary(Kind::Saturate, g.Parameter(step,"parameters.w",p.w));
                    if (UsesEffectBlend(g,step)) mix = g.Binary(Kind::Multiply,mix,EffectBlendWeight(g,step,uv));
                    return g.Lerp(sample(uv), flowed, mix);
                }
            }
            return sample(uv);
        }
    public:
        EffectTextureSampling(EffectGraph& graph, std::span<const MaterialEffectStep> steps, Output time, bool flow)
            : g(graph), effects(steps), seconds(time), hasFlowMap(flow) {}

        Output Sample(const std::string& id, Output uv, MaterialEffectTextureTarget target) {
            uv = SlotUv(uv, target);
            return Flow(uv, target, [&](Output at) {
                if (target == MaterialEffectTextureTarget::Noise || target == MaterialEffectTextureTarget::DetailNoise)
                    at = g.Unary(Kind::Fraction, at);
                return Raw(id, at);
            });
        }
        Output SampleBase(Output uv, Output atlasUv, Output localUv, uint32_t columns, uint32_t rows) {
            const bool mapped = std::any_of(effects.begin(), effects.end(), [](const auto& step) {
                return step.enabled &&
                    ((step.kind == MaterialEffectKind::UvFlow && step.textureTarget == MaterialEffectTextureTarget::BaseColor) ||
                     (step.kind == MaterialEffectKind::FlowMap && (step.textureTarget == MaterialEffectTextureTarget::BaseColor ||
                        step.textureTarget == MaterialEffectTextureTarget::Shared)));
            });
            if (!mapped && columns <= 1 && rows <= 1) return Raw("effect.texture", uv);
            const bool atlas = columns > 1 || rows > 1;
            Output origin{}, grid{}, inverse{};
            if (atlas) {
                grid = g.Constant({float((std::max)(columns,1u)),float((std::max)(rows,1u)),0,0}, Type::Float2);
                inverse = g.Constant({1.0f/(std::max)(columns,1u),1.0f/(std::max)(rows,1u),0,0}, Type::Float2);
                origin = g.Binary(Kind::Subtract, atlasUv, g.Binary(Kind::Multiply, localUv, inverse));
                uv = g.Binary(Kind::Multiply, g.Binary(Kind::Subtract, uv, origin), grid);
            }
            uv = SlotUv(uv, MaterialEffectTextureTarget::BaseColor);
            return Flow(uv, MaterialEffectTextureTarget::BaseColor, [&](Output at) {
                if (atlas) {
                    // フローとブラーのタップを現在のセル内に限定する。
                    at = g.Binary(Kind::Minimum, g.Binary(Kind::Maximum, at, g.C(0.001f)), g.C(0.999f));
                    at = g.Binary(Kind::Add, origin, g.Binary(Kind::Multiply, at, inverse));
                }
                return Raw("effect.texture", at);
            });
        }
    };
}
