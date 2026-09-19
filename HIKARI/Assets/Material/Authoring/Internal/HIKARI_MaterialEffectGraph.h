#pragma once
#include "Assets/Material/Authoring/HIKARI_MaterialEffectStack.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Assets/Material/Authoring/Internal/HIKARI_MaterialTemplateGraphBuilder.h"
#include <algorithm>

namespace HIKARI::MATERIAL_EFFECT_INTERNAL {
    using Kind = MaterialNodeKind;
    using Type = MaterialValueType;
        struct Output { std::string node; std::string pin; Type type; };
        struct Split4 { Output x, y, z, w; };
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
                // 共通定数を共有する。UV の多重サンプルで同じ定数命令を
                // 増やし続けると、実処理より先に GPU の命令予算を使い切る。
                const auto found = std::find_if(data.graph.nodes.begin(), data.graph.nodes.end(),
                    [&](const auto& node) {
                        const auto& v = node.value.numeric;
                        return node.kind == Kind::Constant && node.value.type == type &&
                            v.x == value.x && v.y == value.y && v.z == value.z && v.w == value.w;
                    });
                if (found != data.graph.nodes.end()) return {found->id, "Value", type};
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
            bool Animated(const MaterialEffectStep& step, const char* property) const {
                return std::find(animated.begin(), animated.end(), step.id + "." + property) != animated.end();
            }
            Output Flow(const MaterialEffectStep& step, const char* property, float speed, Output seconds) {
                return Animated(step, property) ? Parameter(step, property, 0) : Binary(Kind::Multiply, seconds, C(speed));
            }
            Output Pair(Output x,Output y) {
                auto node=Node(Kind::MakeFloat2,Type::Float2,"Value");
                Link(x,node,"X"); Link(y,node,"Y"); return node;
            }
            Split4 Split(Output value, Type type) {
                auto node=Node(Kind::Split,type,"X");
                Link(value,node,"Value");
                return {{node.node,"X",Type::Float1},{node.node,"Y",Type::Float1},
                        {node.node,"Z",Type::Float1},{node.node,"W",Type::Float1}};
            }
            Output UvTransform(Output uv,Output pivot,Output scale,Output offset,Output rotation) {
                auto node=Node(Kind::UvTransform,Type::Float2,"Value");
                Link(uv,node,"UV"); Link(pivot,node,"Pivot"); Link(scale,node,"Scale");
                Link(offset,node,"Offset"); Link(rotation,node,"Rotation"); return node;
            }
            // 色から輝度を取る唯一の実装。Glow と LuminanceMask が共有する。
            Output Luminance(Output color) {
                const auto rgb = Split(color, Type::Float4);
                return Binary(Kind::Add,
                    Binary(Kind::Add,
                        Binary(Kind::Multiply, rgb.x, C(0.299f)),
                        Binary(Kind::Multiply, rgb.y, C(0.587f))),
                    Binary(Kind::Multiply, rgb.z, C(0.114f)));
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
            Output Noise(Output uv, Output scale) {
                auto result = Node(Kind::ValueNoise, Type::Float1);
                Link(uv, result, "UV"); Link(scale, result, "Scale"); return result;
            }
            Output Noise(Output uv, float scale) { return Noise(uv, C(scale)); }
        };
}
