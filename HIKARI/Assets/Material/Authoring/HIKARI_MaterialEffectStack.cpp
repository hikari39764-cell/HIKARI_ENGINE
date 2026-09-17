#include "Assets/Material/Authoring/HIKARI_MaterialEffectStack.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Assets/Material/Authoring/Internal/HIKARI_MaterialTemplateGraphBuilder.h"
#include <algorithm>
#include <array>

namespace HIKARI {
    namespace {
        using Kind = MaterialNodeKind;
        using Type = MaterialValueType;
        constexpr std::array<std::string_view, 8> names{
            "UvFlow", "UvDistortion", "Gradient", "Fade", "Wipe", "Dissolve", "Emission", "Fresnel"};
        struct Output { std::string node; std::string pin; Type type; };
        class EffectGraph {
        public:
            MaterialAssetData data = ASSETS::MATERIAL::CreateMaterialFromTemplate(MaterialTemplateId::ParticleUnlit);
            std::span<const std::string> animated;
            EffectGraph() { data.graph = {}; }
            Output Node(Kind kind, Type type, std::string pin = "Result") {
                const auto id = "effect.node." + std::to_string(data.graph.nodes.size());
                ASSETS::MATERIAL::TEMPLATE_FACTORY_INTERNAL::AddNode(data, id, id, kind, type,
                    float(data.graph.nodes.size() % 8) * 180, float(data.graph.nodes.size() / 8) * 120);
                return {id, std::move(pin), type};
            }
            void Link(const Output& from, const Output& to, std::string pin) {
                ASSETS::MATERIAL::TEMPLATE_FACTORY_INTERNAL::ConnectNodeInput(data,
                    "effect.edge." + std::to_string(data.graph.edges.size()), from.node, from.pin, to.node, std::move(pin));
            }
            Output Constant(MATH::Vec4 value, Type type = Type::Float1) {
                auto node = Node(Kind::Constant, type, "Value");
                data.graph.nodes.back().value = {type, value};
                return node;
            }
            Output Parameter(const MaterialEffectStep& step, const char* property, float value) {
                const auto id=step.id+"."+property;
                if (std::find(animated.begin(),animated.end(),id)==animated.end()) return C(value);
                const auto existing=std::find_if(data.graph.nodes.begin(),data.graph.nodes.end(),
                    [&](const auto& node){return node.kind==Kind::Parameter && node.referenceId==id;});
                if (existing!=data.graph.nodes.end()) return {existing->id,"Value",Type::Float1};
                data.graph.parameters.push_back({id,id,{Type::Float1,{value,0,0,0}},true});
                auto node=Node(Kind::Parameter,Type::Float1,"Value");
                data.graph.nodes.back().referenceId=id;
                return node;
            }
            Output Pair(Output x,Output y) {
                auto node=Node(Kind::MakeFloat2,Type::Float2,"Value");
                Link(x,node,"X"); Link(y,node,"Y"); return node;
            }
            Output C(float x) { return Constant({x,0,0,0}); }
            Output Unary(Kind kind, Output value, std::string pin = "Value") {
                auto result = Node(kind, value.type);
                Link(value, result, std::move(pin));
                return result;
            }
            Output Binary(Kind kind, Output a, Output b) {
                const auto type = a.type == Type::Float1 ? b.type : a.type;
                auto result = Node(kind, type);
                Link(a, result, "A"); Link(b, result, "B");
                return result;
            }
            Output Lerp(Output a, Output b, Output t) {
                auto result = Binary(Kind::Lerp, a, b); Link(t, result, "Alpha"); return result;
            }
            Output Smooth(Output a, Output b, Output t) {
                auto result = Node(Kind::SmoothStep, Type::Float1);
                Link(a, result, "Edge0"); Link(b, result, "Edge1"); Link(t, result, "Value"); return result;
            }
            Output Noise(Output uv, float scale) {
                auto result = Node(Kind::ValueNoise, Type::Float1);
                Link(uv, result, "UV"); Link(C(scale), result, "Scale"); return result;
            }
        };
    }

    std::string_view MaterialEffectName(MaterialEffectKind kind) noexcept {
        const size_t index = static_cast<size_t>(kind);
        return index < names.size() ? names[index] : "Invalid";
    }
    bool ParseMaterialEffectKind(std::string_view text, MaterialEffectKind& kind) noexcept {
        for (size_t i = 0; i < names.size(); ++i) if (names[i] == text) {
            kind = static_cast<MaterialEffectKind>(i); return true;
        }
        return false;
    }
    MaterialEffectStep MakeMaterialEffect(MaterialEffectKind kind, std::string id) {
        MaterialEffectStep step; step.id = std::move(id); step.kind = kind;
        switch (kind) {
        case MaterialEffectKind::UvFlow: step.parameters = {0.5f,0,1,1}; break;
        case MaterialEffectKind::UvDistortion: step.parameters = {0.06f,5,0.2f,0}; break;
        case MaterialEffectKind::Gradient:
            step.colorA = {0.05f,0.15f,0.8f,1}; step.colorB = {0.4f,1.5f,3,1}; break;
        case MaterialEffectKind::Fade: step.parameters = {0.08f,0.35f,0,0}; break;
        case MaterialEffectKind::Wipe: step.parameters = {0,1,0.06f,0}; break;
        case MaterialEffectKind::Dissolve: step.parameters = {0,1,8,0.08f}; step.colorA = {0.4f,2,4,1}; break;
        case MaterialEffectKind::Emission: step.parameters = {1.5f,0.15f,2,0}; break;
        case MaterialEffectKind::Fresnel: step.parameters = {3,2,0.8f,0}; step.colorA = {0.1f,0.7f,2,1}; break;
        }
        return step;
    }

    MaterialAssetData BuildMaterialEffectStack(const AssetGuid& texture,
        std::span<const MaterialEffectStep> effects, bool lit,
        uint32_t atlasColumns, uint32_t atlasRows, std::span<const std::string> animatedParameters) {
        EffectGraph g;
        g.animated=animatedParameters;
        g.data.materialName = "VFX Effect Stack";
        g.data.surface.doubleSided = true;
        g.data.surface.unlit = !lit;
        g.data.pbr.unlit = !lit;
        const auto state = g.Node(Kind::ParticleState, Type::Float1, "Age");
        const Output seconds{state.node, "Seconds", Type::Float1};
        auto uv = g.Node(Kind::ParticleUv, Type::Float2, "Value");
        const auto localUv = uv;
        for (const auto& step : effects) {
            if (!step.enabled) continue;
            const auto& p = step.parameters;
            if (step.kind == MaterialEffectKind::UvFlow) {
                uv = g.Binary(Kind::Multiply, uv, g.Constant({p.z,p.w,0,0}, Type::Float2));
                const auto flow=[&](const char* property,float speed) {
                    const auto id=step.id+"."+property;
                    // キー付き速度はCPUで積分済みのUVオフセット。速度変更で位相を飛ばさない。
                    return std::find(animatedParameters.begin(),animatedParameters.end(),id)!=animatedParameters.end() ?
                        g.Parameter(step,property,0) : g.Binary(Kind::Multiply,seconds,g.C(speed));
                };
                uv = g.Binary(Kind::Add,uv,g.Pair(flow("parameters.x",p.x),flow("parameters.y",p.y)));
                uv = g.Unary(Kind::Fraction, uv);
            } else if (step.kind == MaterialEffectKind::UvDistortion) {
                const auto moving = g.Binary(Kind::Add, uv, g.Binary(Kind::Multiply, seconds, g.C(p.z)));
                const auto nx = g.Noise(moving, (std::max)(p.y, 0.01f));
                const auto ny = g.Noise(g.Binary(Kind::Add, moving, g.Constant({17.1f,53.4f,0,0}, Type::Float2)), (std::max)(p.y, 0.01f));
                auto offset = g.Node(Kind::MakeFloat2, Type::Float2, "Value");
                g.Link(nx, offset, "X"); g.Link(ny, offset, "Y");
                offset = g.Binary(Kind::Multiply, g.Binary(Kind::Subtract, offset, g.C(0.5f)), g.Parameter(step,"parameters.x",p.x));
                uv = g.Binary(Kind::Add, uv, offset);
            }
        }
        auto color = g.Constant({1,1,1,1}, Type::Float4);
        auto alpha = g.C(1);
        auto mask = g.C(1);
        auto emissive = g.Constant({}, Type::Float3);
        if (texture.IsValid()) {
            MaterialGraphResourceData resource;
            resource.id = "effect.texture"; resource.assetGuid = texture;
            resource.semantic = MaterialResourceSemantic::BaseColor; resource.enabled = true;
            g.data.graph.resources.push_back(resource);
            color = g.Node(Kind::TextureSample, Type::Float4, "Color");
            g.data.graph.nodes.back().referenceId = resource.id;
            auto sampleUv = uv;
            if (atlasColumns > 1 || atlasRows > 1) {
                // 描画側で解決済みのフレームを再利用し、流れ/歪みはセル内で適用。
                const auto inverseGrid = g.Constant({1.0f / (std::max)(atlasColumns, 1u),
                    1.0f / (std::max)(atlasRows, 1u), 0, 0}, Type::Float2);
                const auto atlasUv = g.Node(Kind::TextureCoordinate, Type::Float2, "UV0");
                const auto bounded = g.Binary(Kind::Add,
                    g.Binary(Kind::Multiply, g.Unary(Kind::Saturate, uv), g.C(0.998f)), g.C(0.001f));
                sampleUv = g.Binary(Kind::Add, atlasUv,
                    g.Binary(Kind::Multiply, g.Binary(Kind::Subtract, bounded, localUv), inverseGrid));
            }
            g.Link(sampleUv, color, "UV");
            alpha = {color.node, "Alpha", Type::Float1}; mask = {color.node, "R", Type::Float1};
        }
        for (const auto& step : effects) {
            if (!step.enabled) continue;
            const auto& p = step.parameters;
            switch (step.kind) {
            case MaterialEffectKind::Gradient:
                color = g.Lerp(g.Constant(step.colorA, Type::Float4), g.Constant(step.colorB, Type::Float4), mask);
                break;
            case MaterialEffectKind::Fade: {
                const auto fadeIn = g.Smooth(g.C(0), g.C((std::max)(p.x,0.0001f)), state);
                const auto fadeOut = g.Unary(Kind::OneMinus, g.Smooth(g.C(1-(std::max)(p.y,0.0001f)), g.C(1), state));
                alpha = g.Binary(Kind::Multiply, alpha, g.Binary(Kind::Multiply, fadeIn, fadeOut));
                break;
            }
            case MaterialEffectKind::Wipe:
            case MaterialEffectKind::Dissolve: {
                const auto progressId=step.id+".progress";
                const auto threshold=std::find(animatedParameters.begin(),animatedParameters.end(),progressId)!=animatedParameters.end() ?
                    g.Parameter(step,"progress",p.x) : g.Lerp(g.C(p.x),g.C(p.y),state);
                Output field;
                const bool dissolve = step.kind == MaterialEffectKind::Dissolve;
                if (dissolve) field = g.Noise(uv, (std::max)(p.z,0.01f));
                else {
                    const auto split = g.Node(Kind::Split, Type::Float2, p.w < 0.5f ? "X" : "Y");
                    g.Link(uv, split, "Value"); field = {split.node, split.pin, Type::Float1};
                }
                const float width = (std::max)(dissolve ? p.w : p.z, 0.0001f);
                const auto edgeEnd = g.Binary(Kind::Add, threshold, g.C(width));
                const auto keep = g.Smooth(threshold, edgeEnd, field);
                if (dissolve) {
                    const auto outer = g.Smooth(edgeEnd, g.Binary(Kind::Add, edgeEnd, g.C(width)), field);
                    emissive = g.Binary(Kind::Add, emissive, g.Binary(Kind::Multiply,
                        g.Binary(Kind::Subtract, keep, outer), g.Constant(step.colorA, Type::Float3)));
                }
                alpha = g.Binary(Kind::Multiply, alpha, keep);
                break;
            }
            case MaterialEffectKind::Emission: {
                auto pulse = g.Unary(Kind::Sine, g.Binary(Kind::Multiply, seconds, g.C(p.z * 6.2831853f)));
                pulse = g.Binary(Kind::Multiply, g.Binary(Kind::Add, g.C(1),
                    g.Binary(Kind::Multiply, pulse, g.C(p.y))), g.Parameter(step,"parameters.x",p.x));
                emissive = g.Binary(Kind::Add, emissive, g.Binary(Kind::Multiply,
                    g.Constant(step.colorA, Type::Float3), g.Binary(Kind::Multiply, mask, pulse)));
                break;
            }
            case MaterialEffectKind::Fresnel: {
                auto rim = g.Node(Kind::Fresnel, Type::Float1);
                g.Link(g.C(0), rim, "F0"); g.Link(g.C((std::max)(p.x,0.01f)), rim, "Power");
                emissive = g.Binary(Kind::Add, emissive, g.Binary(Kind::Multiply,
                    g.Constant(step.colorA, Type::Float3), g.Binary(Kind::Multiply, rim, g.Parameter(step,"parameters.y",p.y))));
                alpha = g.Binary(Kind::Multiply, alpha, g.Lerp(g.C(1), rim, g.C(std::clamp(p.z,0.0f,1.0f))));
                break;
            }
            default: break;
            }
        }
        const auto output = g.Node(Kind::ParticleOutput, Type::Float4);
        g.Link(color, output, "Color"); g.Link(alpha, output, "Opacity"); g.Link(emissive, output, "Emissive");
        g.data.graph.outputNodeId = output.node;
        return std::move(g.data);
    }
}
