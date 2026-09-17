#include "Assets/Vfx/HIKARI_VfxPresetRegistry.h"
#include "Assets/Vfx/Internal/HIKARI_VfxPresetBuilder.h"

#include <stdexcept>
#include <utility>
#include <initializer_list>

namespace HIKARI::VFX::PRESETS {
    namespace {
        using namespace INTERNAL;

        VfxEmitterData Shape(std::string id, VfxRenderShapeType geometry) {
            auto layer = MakePresetEmitterBase(id, id);
            layer.kind = VfxLayerKind::Shape;
            layer.renderShape.type = geometry;
            layer.renderShape.billboardMode = VfxBillboardMode::Fixed;
            layer.spawn.ratePerSecond = 0;
            layer.spawn.burstCount = 0;
            layer.spawn.burstTimeline.clear();
            layer.spawn.maximumParticles = 1;
            layer.renderCommon.blendMode = VfxBlendMode::Additive;
            layer.renderCommon.colorTextureAssetGuid = {};
            return layer;
        }

        void Animate(VfxAnimatedVec3Data& value, MATH::Vec3 start, MATH::Vec3 end) {
            value.mode = VfxAnimatedValueMode::Easing;
            value.easing.start = MakeVfxVec3Constant(start);
            value.easing.end = MakeVfxVec3Constant(end);
            value.easing.function = VfxEasingFunction::EaseOut;
        }

        void ParameterKeys(VfxEmitterData& layer, std::string target, std::string property,
            std::initializer_list<MATH::Vec2> values) {
            VfxParameterTrackData track;
            track.id=target+".animation."+property;
            track.targetId=std::move(target);track.propertyId=std::move(property);
            for (auto v:values) track.curve.keys.push_back({track.id+"."+std::to_string(track.curve.keys.size()),v.x,v.y});
            layer.parameterTracks.push_back(std::move(track));
        }
        void VectorKeys(VfxAnimatedVec3Data& value, std::string id,
            std::initializer_list<std::pair<float,MATH::Vec3>> values) {
            value.mode=VfxAnimatedValueMode::Curve;
            VfxCurveData* curves[]{&value.curve.x,&value.curve.y,&value.curve.z};
            for (auto* curve:curves) {curve->keys.clear();curve->interpolation=VfxCurveInterpolation::Smooth;}
            for (auto [time,v]:values) {
                float components[]{v.x,v.y,v.z};
                for(size_t c=0;c<3;++c) curves[c]->keys.push_back({id+"."+std::to_string(c)+"."+std::to_string(curves[c]->keys.size()),time,components[c]});
            }
        }

        VfxEmitterData Particles(std::string_view id) {
            auto layer = MakePresetEmitterBase(std::string(id), std::string(id));
            layer.kind = VfxLayerKind::Particles;
            layer.spawn.maximumParticles = 128;
            layer.spawn.ratePerSecond = 24;
            layer.basic.lifetime = MakeVfxFloatConstant(0.7f);
            layer.shape.type = VfxShapeType::Cone;
            layer.shape.radius = MakeVfxFloatConstant(0.15f);
            layer.position.radialSpeed = MakeVfxFloatConstant(2);
            layer.renderCommon.colorTextureAssetGuid = AssetGuid{"engine.vfx.texture.energy_streak"};
            layer.renderCommon.blendMode = VfxBlendMode::Additive;
            layer.scale.value.fixed = MakeVfxVec3Constant({0.04f, 0.22f, 0.04f});
            SetPresetColorStops(layer, {3, 1.4f, 0.3f, 1}, {1, 0.2f, 0.05f, 0});
            layer.forces.gravity = {0, -2, 0};
            if (id == "smoke") {
                layer.renderCommon.materialMode = VfxMaterialMode::StandardLit;
                layer.renderCommon.blendMode = VfxBlendMode::Alpha;
                layer.renderCommon.depthMode = VfxParticleDepthMode::SoftIntersection;
                layer.renderCommon.colorTextureAssetGuid = AssetGuid{"engine.vfx.texture.wispy_smoke_flipbook"};
                layer.renderCommon.flipbookColumns = 8;
                layer.renderCommon.flipbookRows = 8;
                layer.renderCommon.flipbookFrameCount = 64;
                layer.renderCommon.flipbookFramesPerSecond = 32;
                layer.spawn.ratePerSecond = 10;
                layer.basic.lifetime = MakeVfxFloatConstant(2);
                layer.position.radialSpeed = MakeVfxFloatConstant(0.4f);
                layer.forces.gravity = {0, 0.2f, 0};
                Animate(layer.scale.value, {0.3f, 0.3f, 0.3f}, {1, 1, 1});
                SetPresetColorStops(layer, {0.65f, 0.7f, 0.75f, 0.5f}, {0.4f, 0.45f, 0.5f, 0});
            } else if (id == "motes") {
                layer.renderCommon.colorTextureAssetGuid = AssetGuid{"engine.vfx.texture.soft_disc"};
                layer.shape.type = VfxShapeType::Sphere;
                layer.shape.radius = MakeVfxFloatConstant(0.7f);
                layer.position.radialSpeed = MakeVfxFloatConstant(0.1f);
                layer.forces.gravity = {};
                layer.scale.value.fixed = MakeVfxVec3Constant({0.07f, 0.07f, 0.07f});
                SetPresetColorStops(layer, {0.2f, 1.5f, 3, 0.8f}, {0.1f, 0.4f, 1, 0});
            } else if (id == "debris") {
                layer.renderShape.type = VfxRenderShapeType::MeshBox;
                layer.renderCommon.colorTextureAssetGuid = {};
                layer.renderCommon.materialMode = VfxMaterialMode::StandardLit;
                layer.renderCommon.blendMode = VfxBlendMode::Alpha;
                layer.scale.value.fixed = MakeVfxVec3Constant({0.08f, 0.12f, 0.08f});
                layer.rotation.eulerDegrees.mode = VfxAnimatedValueMode::Pva;
                layer.rotation.eulerDegrees.pva.velocity = MakeVfxVec3Constant({90, 150, 60});
                layer.forces.gravity = {0, -4, 0};
                layer.forces.collisionPlaneEnabled = true;
                SetPresetColorStops(layer, {0.6f, 0.5f, 0.35f, 1}, {0.4f, 0.3f, 0.2f, 0});
            }
            return layer;
        }

        VfxAssetData Composition(std::string_view id) {
            VfxAssetData asset;
            asset.name = FindVfxPreset(id)->displayName;
            asset.durationSeconds = 0.8f;
            auto body = Shape("body", VfxRenderShapeType::Ring);
            body.renderShape.ringInnerRadius = 0.7f;
            body.renderShape.ringOuterRadius = 1.0f;
            body.renderCommon.colorTextureAssetGuid = AssetGuid{"engine.vfx.texture.slash_strip"};
            SetPresetColorStops(body, {0.2f, 1.3f, 3, 1}, {0.05f, 0.3f, 1, 0});
            if (id == "ribbon_trail") {
                asset.durationSeconds = 1.2f;
                body.history.mode = VfxHistoryMode::Trail;
                body.history.duration = 0.4f;
                body.history.samples = 16;
                body.renderShape.ribbonWidth = 0.5f;
                Animate(body.position.value, {-2,-0.8f,0}, {2,1.2f,0});
                Animate(body.rotation.eulerDegrees, {0,0,-60}, {0,0,50});
            } else if (id == "slash") {
                body.renderShape.ringStartAngleDegrees = -70;
                body.renderShape.ringEndAngleDegrees = 90;
                Animate(body.rotation.eulerDegrees, {-20, 0, -70}, {20, 0, 75});
                body.history.mode = VfxHistoryMode::Afterimage;
                body.history.samples = 6;
                body.history.duration = 0.12f;
                Animate(body.scale.value, {0.65f, 0.65f, 0.65f}, {1.8f, 1.8f, 1.8f});
            } else if (id == "shockwave" || id == "impact") {
                body.renderCommon.colorTextureAssetGuid = AssetGuid{"engine.vfx.texture.shockwave_strip"};
                body.emitterTransform.transform.rotationEulerDegrees = {-90, 0, 0};
                Animate(body.scale.value, {0.1f, 0.1f, 0.1f}, {3, 3, 3});
            } else if (id == "beam" || id == "flame_jet") {
                asset.durationSeconds = 1.6f;
                body.renderShape.type = id == "beam" ? VfxRenderShapeType::MeshCylinder : VfxRenderShapeType::MeshCone;
                body.emitterTransform.transform.rotationEulerDegrees = {90, 0, 0};
                body.scale.value.fixed = MakeVfxVec3Constant(id == "beam" ? MATH::Vec3{0.45f, 4, 0.45f} : MATH::Vec3{1.4f,4,1.4f});
                body.renderCommon.colorTextureAssetGuid = AssetGuid{id == "beam" ?
                    "engine.vfx.texture.shockwave_strip" : "engine.vfx.texture.flame_mask"};
                if (id == "flame_jet") SetPresetColorStops(body, {4, 1.2f, 0.1f, 1}, {1, 0.05f, 0, 0});
            } else if (id == "shield") {
                asset.durationSeconds = 2;
                body.renderShape.type = VfxRenderShapeType::MeshHemisphere;
                body.renderCommon.colorTextureAssetGuid = AssetGuid{"engine.vfx.texture.shield_cells"};
                body.scale.value.fixed = MakeVfxVec3Constant({3, 3, 3});
            } else if (id == "charge") {
                asset.durationSeconds = 1.2f;
                body.renderShape.type = VfxRenderShapeType::MeshSphere;
                body.renderCommon.colorTextureAssetGuid = AssetGuid{"engine.vfx.texture.flow_noise"};
                Animate(body.scale.value, {1.5f, 1.5f, 1.5f}, {0.1f, 0.1f, 0.1f});
            }
            if (id=="slash") {
                VectorKeys(body.rotation.eulerDegrees,"slash.rotation",{{0,{-20,0,-105}},{0.13f,{-20,0,-80}},
                    {0.36f,{5,0,65}},{0.6f,{20,0,105}},{1,{20,0,112}}});
                VectorKeys(body.scale.value,"slash.scale",{{0,{0.5f,0.5f,0.5f}},{0.12f,{0.9f,0.9f,0.9f}},
                    {0.38f,{1.7f,1.7f,1.7f}},{1,{1.9f,1.9f,1.9f}}});
                ParameterKeys(body,body.renderShape.id,"ringEndAngleDegrees",{{0,-35},{0.16f,40},{0.32f,120},{0.7f,125},{1,125}});
                body.history.samples=4;body.history.duration=0.065f;
            }
            if (id=="impact") {
                VectorKeys(body.scale.value,"impact.scale",{{0,{0.08f,0.08f,0.08f}},{0.14f,{1.3f,1.3f,1.3f}},
                    {0.45f,{2.6f,2.6f,2.6f}},{1,{3.2f,3.2f,3.2f}}});
                ParameterKeys(body,body.renderShape.id,"ringInnerRadius",{{0,0.2f},{0.18f,0.65f},{0.6f,0.9f},{1,0.99f}});
            }
            auto glow = Shape("glow", body.renderShape.type);
            body.effects.push_back(MakeMaterialEffect(MaterialEffectKind::UvFlow, "body.flow"));
            body.effects.push_back(MakeMaterialEffect(MaterialEffectKind::Fade, "body.fade"));
            body.effects.push_back(MakeMaterialEffect(MaterialEffectKind::Emission, "body.emission"));
            body.effects.back().colorA = {0.08f,0.5f,1.2f,1};
            if (id == "shield") body.effects.push_back(MakeMaterialEffect(MaterialEffectKind::Fresnel, "body.rim"));
            if (id == "flame_jet") {
                body.effects.back().colorA = {2,0.4f,0.03f,1};
                body.effects.push_back(MakeMaterialEffect(MaterialEffectKind::UvDistortion, "body.distortion"));
                body.effects.push_back(MakeMaterialEffect(MaterialEffectKind::Dissolve, "body.dissolve"));
            }
            if (id=="slash" || id=="impact") {
                ParameterKeys(body,"body.emission","parameters.x",{{0,0.1f},{0.1f,2.6f},{0.22f,1.4f},{0.65f,0.3f},{1,0}});
                ParameterKeys(body,"body.flow","parameters.x",{{0,1.2f},{0.25f,0.8f},{1,0.1f}});
                body.effects.push_back(MakeMaterialEffect(MaterialEffectKind::Dissolve,"body.dissolve"));
                body.effects.back().colorA={0.03f,0.15f,0.4f,1};
                ParameterKeys(body,"body.dissolve","progress",{{0,0},{0.38f,0.02f},{0.68f,0.4f},{1,1}});
            }
            glow.renderShape = body.renderShape;
            glow.renderShape.id += ".glow";
            for (const auto& track:body.parameterTracks) if(track.targetId==body.renderShape.id) {
                auto copy=track;copy.id+=".glow";copy.targetId=glow.renderShape.id;
                for(auto& key:copy.curve.keys) key.id+=".glow";
                glow.parameterTracks.push_back(std::move(copy));
            }
            glow.parentEmitterId = body.id;
            glow.scale.value.fixed = MakeVfxVec3Constant({1.08f, 1.08f, 1.08f});
            glow.renderCommon.colorTextureAssetGuid = body.renderCommon.colorTextureAssetGuid;
            SetPresetColorStops(glow, {0.1f, 0.4f, 1.5f, 0.25f}, {0.05f, 0.1f, 0.3f, 0});
            if (id == "flame_jet") SetPresetColorStops(glow, {2,0.4f,0.03f,0.25f}, {0.5f,0.03f,0,0});
            glow.effects.push_back(MakeMaterialEffect(MaterialEffectKind::Fade, "glow.fade"));
            auto sparks = Particles(id == "charge" || id == "shield" ? "motes" : "sparks");
            sparks.activeDuration = asset.durationSeconds * 0.35f;
            if (id=="slash" || id=="impact") {
                sparks.spawn.ratePerSecond=0;
                sparks.spawn.burstCount=0;
                sparks.startDelay=id=="slash"?0.12f:0.025f;
                sparks.activeDuration=0.15f;
                sparks.spawn.burstTimeline={{"sparks.strike",0,18},{"sparks.follow",0.04f,7}};
                sparks.basic.lifetime={VfxDistributionMode::RandomRange,0.16f,0.38f,0};
                sparks.position.radialSpeed={VfxDistributionMode::RandomRange,3.0f,6.0f,0};
                sparks.shape.radius=MakeVfxFloatConstant(id=="slash"?0.55f:0.15f);
                sparks.shape.coneAngleDegrees=65;
                sparks.basic.localSimulation=false;
                sparks.renderShape.billboardMode=VfxBillboardMode::DirectionalBillboard;
                sparks.forces.drag=2;
                Animate(sparks.scale.value,{0.018f,0.18f,0.018f},{0.002f,0.06f,0.002f});
                SetPresetColorStops(sparks,{0.4f,1.7f,3,1},{0.08f,0.3f,0.7f,0});
            }
            if (id == "ribbon_trail")
                return MakePresetAssetFromEmitters(std::move(asset), {std::move(body), std::move(sparks)});
            return MakePresetAssetFromEmitters(std::move(asset), {std::move(body), std::move(glow), std::move(sparks)});
        }
    }

    const std::vector<VfxPresetDescriptor>& GetVfxPresetCatalog() {
        static const std::vector<VfxPresetDescriptor> presets{
            {"slash", "Arc Slash", "Compositions", "Animated arc mesh, mapped texture, glow and sparks"},
            {"shockwave", "Shockwave", "Compositions", "Expanding ground ring with layered glow"},
            {"beam", "Energy Beam", "Compositions", "Textured cylindrical beam in world space"},
            {"shield", "Shield", "Compositions", "Mesh dome with glow and drifting motes"},
            {"charge", "Charge", "Compositions", "Collapsing energy sphere and motes"},
            {"flame_jet", "Flame Jet", "Compositions", "Textured cone and supporting sparks"},
            {"impact", "Impact", "Compositions", "Expanding impact mesh and short sparks"},
            {"ribbon_trail", "Motion Trail", "Compositions", "Continuous textured ribbon following animated world-space edges"},
            {"sparks", "Sparks", "Particles", "Short streaks under gravity"},
            {"smoke", "Smoke / Dust", "Particles", "Lit soft smoke flipbook"},
            {"motes", "Glow Motes", "Particles", "Slow drifting glow particles"},
            {"debris", "Debris", "Particles", "Rotating fragments with ground bounce"}
        };
        return presets;
    }

    const VfxPresetDescriptor* FindVfxPreset(std::string_view id) noexcept {
        for (const auto& preset : GetVfxPresetCatalog()) if (preset.id == id) return &preset;
        return nullptr;
    }

    VfxAssetData MakeVfxPresetAsset(std::string_view id) {
        const auto* preset = FindVfxPreset(id);
        if (!preset) throw std::invalid_argument("Unknown VFX preset: " + std::string(id));
        if (preset->category == "Compositions") return Composition(id);
        VfxAssetData asset;
        asset.name = preset->displayName;
        asset.durationSeconds = 2;
        asset.loop = true;
        return MakePresetAssetFromEmitters(std::move(asset), {Particles(id)});
    }

    VfxPresetPackage MakeVfxPresetPackage(std::string_view id) {
        return {MakeVfxPresetAsset(id)};
    }
}
