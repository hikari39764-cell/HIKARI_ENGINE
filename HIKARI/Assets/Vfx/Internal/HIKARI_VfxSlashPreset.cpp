#include "Assets/Vfx/Internal/HIKARI_VfxCompositionBuilder.h"
#include <cmath>

namespace HIKARI::VFX::PRESETS::INTERNAL {
    namespace {
        void Sweep(VfxEmitterData& layer, float delay) {
            ParameterKeys(layer, layer.renderShape.id, "ringEndAngleDegrees",
                {{0,-155},{0.12f,-130},{0.4f,-15},{0.68f,76},{1,103}});
            ParameterKeys(layer, layer.renderShape.id, "ringStartAngleDegrees",
                {{0,-158},{0.18f,-150},{0.45f,-133 + delay},{0.75f,-10},{1,85}});
        }
        void BladePath(VfxEmitterData& layer) {
            // 同じキーはワークスペースの位置/回転曲線として編集できる。
            const std::pair<float,float> angles[]{{0,-155},{0.12f,-130},{0.4f,-15},{0.68f,76},{1,103}};
            layer.position.value.mode = VfxAnimatedValueMode::Curve;
            layer.rotation.eulerDegrees.mode = VfxAnimatedValueMode::Curve;
            VfxCurveData* position[]{&layer.position.value.curve.x,&layer.position.value.curve.y,&layer.position.value.curve.z};
            VfxCurveData* rotation[]{&layer.rotation.eulerDegrees.curve.x,&layer.rotation.eulerDegrees.curve.y,&layer.rotation.eulerDegrees.curve.z};
            for (unsigned c=0;c<3;++c) {
                position[c]->keys.clear(); rotation[c]->keys.clear();
                position[c]->interpolation = rotation[c]->interpolation = VfxCurveInterpolation::Linear;
                for (unsigned sample=0;sample<=48;++sample) {
                    const float time=float(sample)/48;
                    unsigned segment=1;
                    while(segment<4 && time>angles[segment].first) ++segment;
                    const auto [leftTime,leftAngle]=angles[segment-1];
                    const auto [rightTime,rightAngle]=angles[segment];
                    const float t=(time-leftTime)/(rightTime-leftTime);
                    const float angle=leftAngle+(rightAngle-leftAngle)*t*t*(3-2*t);
                    const float radians=angle*0.01745329252f;
                    const std::string key=layer.id+".path."+std::to_string(c)+"."+std::to_string(time);
                    position[c]->keys.push_back({key,time,c==0?1.64f*std::cos(radians):c==1?1.64f*std::sin(radians):0});
                    rotation[c]->keys.push_back({key+".rotation",time,c==2?angle:0});
                }
            }
        }
    }
    VfxAssetData Slash() {
        VfxAssetData asset;
        asset.name = "Azure Cleave";
        asset.durationSeconds = 0.85f;
        asset.loop = true;
        auto root = Shape("Swing plane", VfxRenderShapeType::Sprite, 0, 0.82f);
        root.kind = VfxLayerKind::Group;
        root.emitterTransform.transform.position = {0,1.4f,0};
        root.emitterTransform.transform.rotationEulerDegrees = {-18,12,-24};
        const auto parent = root.id;
        std::vector<VfxEmitterData> layers{root};

        for (int i = 0; i < 2; ++i) {
            auto sheet = Shape(i == 0 ? "Flowing blade membrane" : "Refracted undertone",
                VfxRenderShapeType::Ring, 0.06f, 0.40f);
            sheet.parentEmitterId = parent;
            sheet.renderShape.ringOuterRadius = 1.72f;
            sheet.renderShape.ringInnerRadius = i == 0 ? 1.18f : 1.10f;
            sheet.renderShape.taperStart = 0.025f;
            sheet.renderShape.taperEnd = 0.9f;
            if (i != 0) sheet.emitterTransform.transform.rotationEulerDegrees = {4,-3,0};
            Texture(sheet, i == 0 ? "noise_fractal_2" : "noise_fractal_1");
            sheet.renderCommon.maskTextureAssetGuid = AssetGuid{"engine.vfx.texture.slash_blade"};
            sheet.renderCommon.noiseTextureAssetGuid = AssetGuid{"engine.vfx.texture.noise_fractal_1"};
            if (i == 0) Flow(sheet, -0.9f, 0.12f, 0.65f, 1.25f);
            else Flow(sheet, -0.5f, 0.1f, 0.8f, 1.4f);
            Effect(sheet, MaterialEffectKind::UvDistortion, "shear").parameters = {0.04f,1.3f,0.6f,0};
            sheet.renderCommon.flowMapTextureAssetGuid = AssetGuid{"engine.vfx.texture.flow_stream"};
            Effect(sheet, MaterialEffectKind::FlowMap, "directed advection").parameters = {0.07f,2.2f,1,1};
            Effect(sheet, MaterialEffectKind::LuminanceMask, "gaps").parameters =
                i == 0 ? MATH::Vec4{0.1f,0.5f,0,0} : MATH::Vec4{0.05f,0.55f,0,0};
            Sweep(sheet, 0);
            const float opacity = i == 0 ? 1.0f : 0.65f;
            AlphaKeys(sheet, i == 0 ? MATH::Vec3{0.14f,1.8f,4.8f} : MATH::Vec3{0.035f,0.5f,1.65f},
                {{0,0},{0.12f,0.8f*opacity},{0.4f,0.85f*opacity},{0.72f,0.28f*opacity},{1,0}});
            layers.push_back(std::move(sheet));
        }

        auto edge = Shape("Cutting edge", VfxRenderShapeType::Ring, 0.06f, 0.40f);
        edge.parentEmitterId = parent;
        edge.renderShape.ringOuterRadius = 1.64f;
        edge.renderShape.ringInnerRadius = 1.56f;
        edge.renderShape.taperStart = 0.025f;
        edge.renderShape.taperEnd = 1.0f;
        edge.renderShape.uvSwapAxes = true;
        Texture(edge, "soft_line");
        Sweep(edge, 18);
        AlphaKeys(edge, {2.2f,4.2f,5.2f}, {{0,0},{0.1f,1},{0.52f,1},{0.8f,0.4f},{1,0}});
        layers.push_back(std::move(edge));

        auto trail = Shape("Blade motion history", VfxRenderShapeType::Ribbon, 0.06f, 0.40f);
        trail.parentEmitterId = parent;
        trail.history = {VfxHistoryMode::Trail,0.19f,16u,6.0f};
        trail.renderShape.ribbonWidth = 0.35f;
        Texture(trail, "energy_streak");
        trail.renderCommon.maskTextureAssetGuid = AssetGuid{"engine.vfx.texture.gradient_falloff"};
        Flow(trail, -1.8f, 0.08f, 1.2f, 1);
        BladePath(trail);
        AlphaKeys(trail, {0.04f,0.30f,0.90f}, {{0,0.35f},{0.15f,0.55f},{0.85f,0.45f},{1,0.1f}});
        layers.push_back(std::move(trail));

        auto echo = Shape("Detached filament", VfxRenderShapeType::Ring, 0.11f, 0.40f);
        echo.parentEmitterId = parent;
        echo.emitterTransform.transform.rotationEulerDegrees = {8,-10,6};
        echo.emitterTransform.transform.position.z = 0.13f;
        echo.renderShape.ringOuterRadius = 1.82f;
        echo.renderShape.ringInnerRadius = 1.79f;
        echo.renderShape.taperStart = 0.02f;
        echo.renderShape.uvSwapAxes = true;
        Texture(echo, "energy_streak");
        Sweep(echo, 45);
        AlphaKeys(echo, {0.20f,1.0f,2.0f}, {{0,0},{0.12f,0.55f},{0.65f,0.3f},{1,0}});
        layers.push_back(std::move(echo));

        auto tip = Shape("Leading glint", VfxRenderShapeType::Sprite, 0.06f, 0.40f);
        tip.parentEmitterId = parent;
        tip.renderShape.billboardMode = VfxBillboardMode::RotatedBillboard;
        Texture(tip, "star_glint");
        BladePath(tip);
        VectorKeys(tip.scale.value, tip.id + ".size", {{0,{0.10f,0.10f,0.10f}},{0.45f,{0.24f,0.24f,0.24f}},{1,{0.08f,0.08f,0.08f}}});
        AlphaKeys(tip, {2.5f,4.2f,5.0f}, {{0,0},{0.12f,1},{0.82f,0.8f},{1,0}});
        Light(tip, {0.12f,0.48f,1,1}, 1.2f, 3.5f);
        layers.push_back(std::move(tip));

        auto sparks = SparkBurst("Cut fragments", 0.22f, 9, {0.32f,1.3f,2.6f,1}, 3.2f, 0.38f);
        VectorKeys(sparks.scale.value, sparks.id + ".size", {
            {0,{0.014f,0.176f,0.014f}}, {0.65f,{0.007f,0.066f,0.007f}}, {1,{0,0.0055f,0}}});
        sparks.parentEmitterId = parent;
        sparks.emitterTransform.transform.position = {1.58f,-0.42f,0.1f};
        sparks.shape.type = VfxShapeType::Cone;
        sparks.shape.radius = MakeVfxFloatConstant(0.12f);
        sparks.shape.coneAngleDegrees = 58;
        sparks.emitterTransform.transform.rotationEulerDegrees = {-10,0,-15};
        layers.push_back(std::move(sparks));

        auto dust = SparkBurst("Ground wake", 0.17f, 7, {0.4f,0.44f,0.5f,0.18f}, 0.65f, 0.50f);
        dust.emitterTransform.transform.position = {0,0.08f,0};
        dust.shape.type = VfxShapeType::Circle;
        dust.shape.radius = MakeVfxFloatConstant(0.8f);
        dust.renderShape.billboardMode = VfxBillboardMode::RotatedBillboard;
        Texture(dust, "wispy_smoke_flipbook");
        dust.renderCommon.flipbookColumns = 8;
        dust.renderCommon.flipbookRows = 8;
        dust.renderCommon.flipbookFrameCount = 64;
        dust.renderCommon.flipbookFramesPerSecond = 96;
        dust.renderCommon.flipbookLoop = false;
        dust.renderCommon.materialMode = VfxMaterialMode::StandardLit;
        dust.renderCommon.blendMode = VfxBlendMode::Alpha;
        dust.renderCommon.depthMode = VfxParticleDepthMode::SoftIntersection;
        dust.forces.gravity = {0,0.4f,0};
        VectorKeys(dust.scale.value, dust.id + ".spread", {{0,{0.16f,0.16f,0.16f}},{1,{0.65f,0.65f,0.65f}}});
        AlphaKeys(dust, {0.4f,0.44f,0.5f}, {{0,0},{0.12f,0.18f},{1,0}});
        layers.push_back(std::move(dust));
        return MakePresetAssetFromEmitters(std::move(asset), std::move(layers));
    }
}
