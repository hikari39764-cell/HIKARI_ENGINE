#pragma once
#include "Assets/Material/Authoring/HIKARI_MaterialEffectStack.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Assets/Material/Authoring/Internal/HIKARI_MaterialTemplateGraphBuilder.h"
#include <algorithm>
#include <unordered_map>
#include <cmath>

namespace HIKARI::MATERIAL_EFFECT_INTERNAL {
    using Kind = MaterialNodeKind;
    using Type = MaterialValueType;
        struct Output { std::string node; std::string pin; Type type; };
        struct Split4 { Output x, y, z, w; };
        class EffectGraph {
            std::unordered_map<std::string, Output> expressions;
            static std::string Key(Kind kind, const Output& a, const Output& b = {}) {
                return std::to_string(static_cast<unsigned>(kind)) + ":" + a.node + "." + a.pin + ":" + b.node + "." + b.pin;
            }
        public:
            MaterialAssetData data = ASSETS::MATERIAL::CreateMaterialFromTemplate(MaterialTemplateId::ParticleUnlit);
            std::span<const std::string> animated;
            Output blendAlpha{}, blendLuminance{};
            bool Literal(const Output& output, MATH::Vec4& value) const {
                for (const auto& node : data.graph.nodes)
                    if (node.id == output.node && node.kind == Kind::Constant) {
                        value = node.value.numeric;
                        if (output.type == Type::Float1) value = {value.x,value.x,value.x,value.x};
                        return true;
                    }
                return false;
            }
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
                // 未使用成分を正規化し、折り畳んだスカラーも C() と同じ値スロットを共有する。
                if (type == Type::Float1) value.y = 0;
                if (type == Type::Float1 || type == Type::Float2) value.z = 0;
                if (type != Type::Float4) value.w = 0;
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
                MATH::Vec4 a{}, b{};
                if (Literal(x,a) && Literal(y,b)) return Constant({a.x,b.x,0,0},Type::Float2);
                const auto key = Key(Kind::MakeFloat2,x,y);
                if (const auto it = expressions.find(key); it != expressions.end()) return it->second;
                auto node=Node(Kind::MakeFloat2,Type::Float2,"Value");
                Link(x,node,"X"); Link(y,node,"Y"); expressions.emplace(key,node); return node;
            }
            Split4 Split(Output value, Type type) {
                MATH::Vec4 literal{};
                if (Literal(value,literal)) return {C(literal.x),C(literal.y),C(literal.z),C(literal.w)};
                // TextureSample 自身が公開する成分を使い、追加 Split を実行しない。
                if (value.pin == "Color") for (const auto& source : data.graph.nodes)
                    if (source.id == value.node && source.kind == Kind::TextureSample)
                        return {{value.node,"R",Type::Float1},{value.node,"G",Type::Float1},
                            {value.node,"B",Type::Float1},{value.node,"A",Type::Float1}};
                const auto key = Key(Kind::Split,value);
                auto it = expressions.find(key);
                Output node;
                if (it != expressions.end()) node = it->second;
                else {
                    node=Node(Kind::Split,type,"X"); Link(value,node,"Value"); expressions.emplace(key,node);
                }
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
                const auto weights = Constant({0.299f,0.587f,0.114f,0},Type::Float3);
                const auto key = Key(Kind::Dot,color,weights);
                if (const auto it = expressions.find(key); it != expressions.end()) return it->second;
                if (color.type != Type::Float3) {
                    const auto rgb = Split(color,color.type);
                    color = Node(Kind::MakeFloat3,Type::Float3,"Value");
                    Link(rgb.x,color,"X"); Link(rgb.y,color,"Y"); Link(rgb.z,color,"Z");
                }
                const auto result = Node(Kind::Dot,Type::Float1);
                Link(color,result,"A"); Link(weights,result,"B");
                expressions.emplace(key,result); return result;
            }
            Output C(float x) { return Constant({x,0,0,0}); }
            Output Unary(Kind kind, Output value, std::string pin = "Value") {
                MATH::Vec4 literal{};
                if (Literal(value,literal)) {
                    bool fold = true;
                    for (float* v : {&literal.x,&literal.y,&literal.z,&literal.w}) {
                        switch (kind) {
                        case Kind::Saturate: *v=std::clamp(*v,0.0f,1.0f); break;
                        case Kind::OneMinus: *v=1-*v; break;
                        case Kind::Absolute: *v=std::abs(*v); break;
                        case Kind::Sine: *v=std::sin(*v); break;
                        case Kind::Cosine: *v=std::cos(*v); break;
                        case Kind::Floor: *v=std::floor(*v); break;
                        case Kind::Fraction: *v-=std::floor(*v); break;
                        default: fold=false; break;
                        }
                    }
                    if (fold) return Constant(literal,value.type);
                }
                const auto key = Key(kind,value) + pin;
                if (const auto it = expressions.find(key); it != expressions.end()) return it->second;
                auto result = Node(kind, value.type);
                Link(value, result, std::move(pin));
                expressions.emplace(key,result);
                return result;
            }
            Output Binary(Kind kind, Output a, Output b) {
                const auto type = a.type == Type::Float1 ? b.type : a.type;
                MATH::Vec4 left{},right{},resultValue{};
                const bool leftLiteral = Literal(a,left), rightLiteral = Literal(b,right);
                const auto uniform = [](MATH::Vec4 value, Type valueType, float expected) {
                    return value.x == expected && (valueType == Type::Float1 ||
                        (value.y == expected && (valueType == Type::Float2 ||
                        (value.z == expected && (valueType == Type::Float3 || value.w == expected)))));
                };
                // 型の拡張を失わない恒等演算だけを除去する。動的な値とパラメーターは残す。
                if (rightLiteral && a.type == type &&
                    (((kind == Kind::Add || kind == Kind::Subtract) && uniform(right,b.type,0)) ||
                     ((kind == Kind::Multiply || kind == Kind::Divide) && uniform(right,b.type,1)))) return a;
                if (leftLiteral && b.type == type &&
                    ((kind == Kind::Add && uniform(left,a.type,0)) ||
                     (kind == Kind::Multiply && uniform(left,a.type,1)))) return b;
                if (leftLiteral && rightLiteral) {
                    const float av[]{left.x,left.y,left.z,left.w}, bv[]{right.x,right.y,right.z,right.w};
                    float* values[]{&resultValue.x,&resultValue.y,&resultValue.z,&resultValue.w};
                    bool fold = true;
                    for (unsigned i=0;i<4;++i) {
                        switch (kind) {
                        case Kind::Add: *values[i]=av[i]+bv[i]; break;
                        case Kind::Subtract: *values[i]=av[i]-bv[i]; break;
                        case Kind::Multiply: *values[i]=av[i]*bv[i]; break;
                        case Kind::Divide: if (bv[i]!=0) *values[i]=av[i]/bv[i]; else fold=false; break;
                        case Kind::Minimum: *values[i]=(std::min)(av[i],bv[i]); break;
                        case Kind::Maximum: *values[i]=(std::max)(av[i],bv[i]); break;
                        default: fold=false; break;
                        }
                        fold &= std::isfinite(*values[i]);
                    }
                    if (fold) return Constant(resultValue,type);
                }
                const auto key = Key(kind,a,b);
                if (kind != Kind::Lerp)
                    if (const auto it = expressions.find(key); it != expressions.end()) return it->second;
                auto result = Node(kind, type);
                Link(a, result, "A"); Link(b, result, "B");
                if (kind != Kind::Lerp) expressions.emplace(key,result);
                return result;
            }
            Output Lerp(Output a, Output b, Output t) {
                if (a.node == b.node && a.pin == b.pin) return a;
                MATH::Vec4 weight{};
                if (a.type == b.type && t.type == Type::Float1 && Literal(t,weight)) {
                    if (weight.x==0) return a;
                    if (weight.x==1) return b;
                }
                auto result = Binary(Kind::Lerp, a, b); Link(t, result, "Alpha"); return result;
            }
            Output Step(Output edge, Output value) {
                auto result = Node(Kind::Step, Type::Float1);
                Link(edge,result,"Edge"); Link(value,result,"Value"); return result;
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
