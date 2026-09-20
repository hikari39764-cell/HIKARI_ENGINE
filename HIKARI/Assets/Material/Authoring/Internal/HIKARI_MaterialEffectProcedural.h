#pragma once
#include "Assets/Material/Authoring/Internal/HIKARI_MaterialEffectGraph.h"

namespace HIKARI::MATERIAL_EFFECT_INTERNAL {
    inline Output ApplyProceduralUv(EffectGraph& g, Output uv, const MaterialEffectStep& step) {
        const auto P = [&](const char* id, float value) { return g.Parameter(step, id, value); };
        const auto& p = step.parameters;
        const auto& q = step.parameters2;
        const auto mul = [&](Output a, Output b) { return g.Binary(Kind::Multiply, a, b); };
        const auto add = [&](Output a, Output b) { return g.Binary(Kind::Add, a, b); };
        const auto length = [&](Output value) {
            auto node = g.Node(Kind::Length,Type::Float1); g.Link(value,node,"Value"); return node;
        };
        const auto sub = [&](Output a, Output b) { return g.Binary(Kind::Subtract, a, b); };
        if (step.kind == MaterialEffectKind::Pixelate) {
            const auto size = g.Binary(Kind::Maximum,P("parameters.x",p.x),g.C(0.001f));
            const auto cell = g.Unary(Kind::Floor,g.Binary(Kind::Divide,uv,size));
            return g.Lerp(uv,mul(add(cell,g.C(0.5f)),size),P("parameters.y",p.y));
        }
        if (step.kind == MaterialEffectKind::Polar) {
            const auto center = g.Pair(P("parameters.y",p.y), P("parameters.z",p.z));
            const auto scale = g.Binary(Kind::Maximum, P("parameters.w",p.w),g.C(0.001f));
            Output mapped;
            if (q.x > 0.5f) {
                const auto xy = g.Split(uv,Type::Float2);
                const auto angle = mul(sub(xy.x,g.C(0.5f)),g.C(6.2831853f));
                mapped = add(center,mul(g.Pair(g.Unary(Kind::Cosine,angle),g.Unary(Kind::Sine,angle)),
                    g.Binary(Kind::Divide,mul(xy.y,g.C(0.5f)),scale)));
            } else {
                const auto delta = sub(uv,center);
                const auto xy = g.Split(delta,Type::Float2);
                const auto angle = add(mul(g.Binary(Kind::Atan2,xy.y,xy.x),g.C(0.159154943f)),g.C(0.5f));
                const auto radius = mul(length(delta),mul(scale,g.C(2)));
                mapped = g.Pair(angle,radius);
            }
            return g.Lerp(uv,mapped,g.Unary(Kind::Saturate,P("parameters.x",p.x)));
        }
        if (step.kind == MaterialEffectKind::Twirl) {
            const auto center = g.Pair(P("parameters.z",p.z),P("parameters.w",p.w));
            const auto distance = length(sub(uv,center));
            const auto falloff = g.Unary(Kind::Saturate,sub(g.C(1),g.Binary(Kind::Divide,distance,
                g.Binary(Kind::Maximum,P("parameters.y",p.y),g.C(0.001f)))));
            const auto angle = mul(mul(P("parameters.x",p.x),g.C(0.01745329252f)),mul(falloff,falloff));
            return g.UvTransform(uv,center,g.Constant({1,1,0,0},Type::Float2),g.Constant({},Type::Float2),angle);
        }
        if (step.kind == MaterialEffectKind::Wave) {
            const auto center = g.Pair(P("parameters2.y",q.y),P("parameters2.z",q.z));
            const auto delta = sub(uv,center);
            const auto distance = length(delta);
            const auto angle = mul(P("parameters2.w",q.w),g.C(0.01745329252f));
            const auto direction = g.Pair(g.Unary(Kind::Cosine,angle),g.Unary(Kind::Sine,angle));
            const auto xy = g.Split(delta,Type::Float2);
            const auto axis = g.Split(direction,Type::Float2);
            const auto coordinate = q.x > 0.5f ? distance : add(mul(xy.x,axis.x),mul(xy.y,axis.y));
            const auto phase = mul(sub(mul(coordinate,P("parameters.y",p.y)),P("parameters.z",p.z)),g.C(6.2831853f));
            const auto envelope = g.Unary(Kind::Saturate,sub(g.C(1),mul(distance,P("parameters.w",p.w))));
            const auto amplitude = mul(mul(g.Unary(Kind::Sine,phase),P("parameters.x",p.x)),envelope);
            const auto vector = q.x > 0.5f ? g.Binary(Kind::Divide,delta,g.Binary(Kind::Maximum,distance,g.C(0.0001f))) : direction;
            return add(uv,mul(vector,amplitude));
        }
        return uv;
    }

    inline bool ApplyProceduralSurface(EffectGraph& g, Output uv, const MaterialEffectStep& step,
        Output& color, Output& alpha, Output& mask) {
        const auto P = [&](const char* id, float value) { return g.Parameter(step,id,value); };
        const auto& p = step.parameters;
        if (step.kind == MaterialEffectKind::Noise) {
            const auto& q = step.parameters2;
            const auto offset = g.Binary(Kind::Add,P("parameters.y",p.y),P("parameters2.y",q.y));
            const auto at = g.Binary(Kind::Add,uv,g.Pair(offset,g.Binary(Kind::Multiply,offset,g.C(0.371f))));
            const auto frequency = g.Binary(Kind::Maximum,P("parameters.x",p.x),g.C(0.01f));
            const auto noise = [&](Output scale) {
                auto node = g.Node(q.x > 0.5f ? Kind::Voronoi : Kind::ValueNoise,Type::Float1);
                g.Link(at,node,"UV"); g.Link(scale,node,"Scale"); return node;
            };
            const auto detail = P("parameters.z",p.z);
            auto field = g.Binary(Kind::Divide,g.Binary(Kind::Add,noise(frequency),
                g.Binary(Kind::Multiply,noise(g.Binary(Kind::Multiply,frequency,g.C(2.7f))),detail)),
                g.Binary(Kind::Add,g.C(1),detail));
            field = g.Unary(Kind::Saturate,g.Binary(Kind::Add,g.C(0.5f),g.Binary(Kind::Multiply,
                g.Binary(Kind::Subtract,field,g.C(0.5f)),P("parameters.w",p.w))));
            alpha = g.Binary(Kind::Multiply,alpha,g.Lerp(g.C(1),field,P("parameters2.z",q.z)));
            color = g.Binary(Kind::Multiply,color,g.Lerp(g.C(1),field,P("parameters2.w",q.w)));
            mask = field;
            return true;
        }
        if (step.kind == MaterialEffectKind::Levels) {
            const auto rgba = g.Split(color,Type::Float4);
            const auto black = P("parameters.x",p.x);
            const auto span = g.Binary(Kind::Maximum,g.Binary(Kind::Subtract,P("parameters.y",p.y),black),g.C(0.0001f));
            const auto power = g.Binary(Kind::Divide,g.C(1),g.Binary(Kind::Maximum,P("parameters.z",p.z),g.C(0.05f)));
            const auto grade = [&](Output value) {
                const auto normalized = g.Binary(Kind::Maximum,g.C(0),
                    g.Binary(Kind::Divide,g.Binary(Kind::Subtract,value,black),span));
                auto corrected = g.Node(Kind::Power,Type::Float1);
                g.Link(normalized,corrected,"Base"); g.Link(power,corrected,"Exponent");
                return g.Lerp(value,corrected,P("parameters.w",p.w));
            };
            auto result = g.Node(Kind::MakeFloat4,Type::Float4,"Value");
            g.Link(grade(rgba.x),result,"X"); g.Link(grade(rgba.y),result,"Y");
            g.Link(grade(rgba.z),result,"Z"); g.Link(rgba.w,result,"W"); color = result;
            return true;
        }
        if (step.kind == MaterialEffectKind::ChannelMixer) {
            const auto rgb = g.Split(color,Type::Float4);
            mask = g.Unary(Kind::Saturate,g.Binary(Kind::Add,
                g.Binary(Kind::Add,g.Binary(Kind::Multiply,rgb.x,P("parameters.x",p.x)),
                    g.Binary(Kind::Multiply,rgb.y,P("parameters.y",p.y))),
                g.Binary(Kind::Multiply,rgb.z,P("parameters.z",p.z))));
            alpha = g.Binary(Kind::Multiply,alpha,g.Lerp(g.C(1),mask,P("parameters.w",p.w)));
            return true;
        }
        return false;
    }
}
