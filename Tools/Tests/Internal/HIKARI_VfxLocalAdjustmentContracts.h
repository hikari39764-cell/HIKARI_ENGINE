#pragma once
#include <algorithm>
#include <cmath>
#include <json.hpp>
#include "Assets/Vfx/HIKARI_VfxLayerOrder.h"
#include "Assets/Vfx/HIKARI_VfxLayerAnimation.h"
#include "Assets/Vfx/Compiler/HIKARI_VfxCompiler.h"
#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJson.h"
#include "Assets/Formats/HIKARI_HvfxcFormat.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"
#include "HIKARI_ContractAssert.h"
#include "Render3D/Vfx/HIKARI_ParticleSpatialReference.h"
#include "Render3D/Vfx/HIKARI_ParticleGeometryLayout.h"

namespace HIKARI::TESTS {
inline void AssertVfxLocalAdjustments() {
    using namespace VFX;
    const auto makeLayer = [](const char* prefix, VfxLayerKind kind, uint32_t order) {
        auto source = MakeDefaultVfxAssetData();
        std::string text, error;
        assert(SERIALIZATION::SerializeVfxSourceJson(source, text, error));
        auto json = nlohmann::json::parse(text);
        const auto rename = [&](auto&& self, nlohmann::json& node) -> void {
            if (node.is_object())
                for (auto it = node.begin(); it != node.end(); ++it) {
                    if (it.key() == "id" && it.value().is_string())
                        it.value() = std::string(prefix) + "." + it.value().get<std::string>();
                    else
                        self(self, it.value());
                }
            else if (node.is_array())
                for (auto& value : node)
                    self(self, value);
        };
        rename(rename, json);
        assert(SERIALIZATION::ParseVfxSourceJson(json.dump(), source, error));
        auto layer = source.emitters.front();
        layer.kind = kind;
        layer.order = order;
        layer.effects.clear();
        layer.parameterTracks.clear();
        layer.renderCommon.colorTextureAssetGuid = {"engine.vfx.texture.soft_disc"};
        return layer;
    };
    auto upper = makeLayer("upper", VfxLayerKind::Shape, 0);
    auto outer = makeLayer("outer", VfxLayerKind::Adjustment, 1);
    auto group = makeLayer("group", VfxLayerKind::Group, 2);
    auto inner = makeLayer("inner", VfxLayerKind::Adjustment, 0);
    auto lower = makeLayer("lower", VfxLayerKind::Shape, 1);
    auto adjacent = makeLayer("adjacent", VfxLayerKind::Shape, 3);
    inner.parentEmitterId = group.id;
    lower.parentEmitterId = group.id;
    VfxAssetData hierarchy = MakeDefaultVfxAssetData();
    hierarchy.emitters = {upper, outer, group, inner, lower, adjacent};
    assert(CollectVfxAdjustments(hierarchy.emitters, hierarchy.emitters[0]).empty());
    const auto inherited = CollectVfxAdjustments(hierarchy.emitters, hierarchy.emitters[4]);
    assert(inherited.size() == 2 && inherited[0]->id == inner.id && inherited[1]->id == outer.id);
    const auto sibling = CollectVfxAdjustments(hierarchy.emitters, hierarchy.emitters[5]);
    assert(sibling.size() == 1 && sibling[0]->id == outer.id);

    auto source = MakeDefaultVfxAssetData();
    source.durationSeconds = 2;
    source.loop = false;
    outer.startDelay = 0.2f;
    outer.activeDuration = 0.8f;
    outer.effects = {MakeMaterialEffect(MaterialEffectKind::Twirl, "warp")};
    VfxParameterTrackData track;
    track.id = "angle";
    track.targetId = "warp";
    track.propertyId = "parameters.x";
    track.curve.keys = {{"angle.begin", 0, 0}, {"angle.end", 1, 100}};
    outer.parameterTracks = {track};
    lower.parentEmitterId.clear();
    lower.order = 2;
    source.emitters = {outer, lower};
    const auto compiled = CompileVfxAsset(source);
    assert(compiled.success);
    std::string data, error;
    assert(SerializeVfxProgram(compiled.program, data, error));
    VfxProgram program;
    assert(DeserializeVfxProgram(data, program, error));
    assert(!program.emitters[1].render.effectMaterialIncludesParticleColor);
    uint32_t angleSlot = UINT32_MAX, mixSlot = UINT32_MAX;
    for (const auto& curve : program.emitters[1].layerCurves) {
        assert(curve.ownerEmitterIndex == 0);
        if (curve.channel == VfxLayerChannel::MaterialParameter)
            angleSlot = curve.materialSlot;
        if (curve.channel == VfxLayerChannel::MaterialEffectMix)
            mixSlot = curve.materialSlot;
    }
    assert(angleSlot != UINT32_MAX && mixSlot != UINT32_MAX);
    auto invalid = program;
    invalid.emitters[1].layerCurves.front().ownerEmitterIndex = 1;
    assert(!ValidateVfxProgram(invalid, error));
    const auto parameter = [](const auto& input, uint32_t slot) {
        assert(input.compositions.empty() && !input.emitters.empty());
        const auto& parameters = input.emitters.back().render.animatedParameters;
        const auto found =
            std::find_if(parameters.begin(), parameters.end(), [&](const auto& p) { return p.slot == slot; });
        assert(found != parameters.end());
        return found->value;
    };
    VfxPlaybackService playback;
    VfxPlayRequest request;
    request.assetGuid = GenerateAssetGuid();
    request.program = std::make_shared<VfxProgram>(program);
    const auto playing = playback.PlayDetailed(request);
    assert(playing.IsAccepted());
    assert(playback.Seek(playing.handle, 1, 0.4f).complete);
    assert(std::abs(parameter(playback.GetRenderInput(), angleSlot) - 25) < 0.01f);
    assert(parameter(playback.GetRenderInput(), mixSlot) == 1);
    assert(playback.SetTimeScale(playing.handle, 0));
    playback.Update(2, 0.5f);
    assert(std::abs(parameter(playback.GetRenderInput(), angleSlot) - 25) < 0.01f);
    assert(playback.Seek(playing.handle, 3, 1.2f).complete);
    assert(parameter(playback.GetRenderInput(), mixSlot) == 0);
    program.emitters[0].authoredOverrides.enabled = 0;
    VfxPlaybackService disabled;
    request.program = std::make_shared<VfxProgram>(program);
    const auto hidden = disabled.PlayDetailed(request);
    assert(hidden.IsAccepted());
    assert(disabled.Seek(hidden.handle, 1, 0.4f).complete);
    assert(parameter(disabled.GetRenderInput(), mixSlot) == 0);

    // 三次元の変形と下層合成のスタイルは別の実行経路を持つ。
    auto spatialSource = source;
    spatialSource.emitters[0].effects[0].space = MaterialEffectSpace::Group3D;
    spatialSource.emitters[0].effects.push_back(MakeMaterialEffect(MaterialEffectKind::Ink, "ink"));
    auto spatialCompiled = CompileVfxAsset(spatialSource);
    assert(spatialCompiled.success);
    assert(spatialCompiled.program.emitters[0].runtimeEffects.size() == 2);
    assert(spatialCompiled.program.emitters[1].spatialOwners == std::vector<uint32_t>{0});
    assert(spatialCompiled.program.emitters[1].layerCurves.empty());
    assert(SerializeVfxProgram(spatialCompiled.program, data, error));
    VfxProgram spatialProgram;
    assert(DeserializeVfxProgram(data, spatialProgram, error));
    VfxPlaybackService spatialPlayback;
    request.program = std::make_shared<VfxProgram>(spatialProgram);
    const auto spatialHandle = spatialPlayback.PlayDetailed(request);
    assert(spatialHandle.IsAccepted());
    assert(spatialPlayback.Seek(spatialHandle.handle, 1, .4f).complete);
    const auto& spatialInput = spatialPlayback.GetRenderInput();
    assert(spatialInput.compositions.size() == 1);
    assert(spatialInput.compositions[0].layers[0].effects.size() == 1);
    assert(spatialInput.compositions[0].layers[0].effects[0].kind == MaterialEffectKind::Ink);
    const auto spatialSample = spatialInput.emitters.back().render.spatialEffects;
    assert(spatialSample.size() == 1 && std::abs(spatialSample[0].parameters.x - 25) < .01f);
    assert(spatialPlayback.SetTimeScale(spatialHandle.handle, 0));
    spatialPlayback.Update(2, .5f);
    assert(spatialPlayback.GetRenderInput().emitters.back().render.spatialEffects[0].parameters.x ==
           spatialSample[0].parameters.x);
    assert(spatialPlayback.Seek(spatialHandle.handle, 3, 1.2f).complete);
    assert(spatialPlayback.GetRenderInput().emitters.back().render.spatialEffects.empty());
    assert(spatialPlayback.GetRenderInput().compositions.empty());
    auto invalidSpatial = spatialProgram;
    invalidSpatial.emitters[0].runtimeEffects[0].mask = MaterialEffectMask::Alpha;
    assert(!ValidateVfxProgram(invalidSpatial, error));
    invalidSpatial = spatialProgram;
    invalidSpatial.emitters[1].spatialOwners = {1};
    assert(!ValidateVfxProgram(invalidSpatial, error));

    using namespace RENDER3D::PARTICLES;
    ParticleSpatialEffect bend;
    bend.kind = MaterialEffectKind::Polar;
    bend.parameters = {1, .5f, .5f, 1};
    const auto front = DeformParticlePoint({.25f, 0, .2f}, bend);
    const auto back = DeformParticlePoint({.25f, 0, -.2f}, bend);
    assert(std::abs(front.x - 1 / (2 * 3.14159265f)) < .0001f);
    assert(std::abs(front.y + 1 / (2 * 3.14159265f)) < .0001f);
    assert(std::abs(front.z - back.z - .4f) < .0001f);
    bend.mix = 0;
    assert(MATH::Length(DeformParticlePoint({.25f, 0, .2f}, bend) - MATH::Vec3{.25f, 0, .2f}) < .0001f);
    ParticleEmitterRenderParameters geometry;
    geometry.spatialEffects = {bend};
    assert(ParticleGeometryTriangleCount(geometry) == 128);
    geometry.shape = ParticleRenderShape::MeshBox;
    assert(ParticleGeometryTriangleCount(geometry) == 192);
    geometry.shape = ParticleRenderShape::CrossPlane;
    assert(ParticleGeometryTriangleCount(geometry) == 256);

    // 旧版の source は変換せずに拒否する。
    assert(SERIALIZATION::SerializeVfxSourceJson(source, data, error));
    auto outdated = nlohmann::json::parse(data);
    outdated["version"] = 13;
    VfxAssetData rejected;
    assert(!SERIALIZATION::ParseVfxSourceJson(outdated.dump(), rejected, error));
}
} // namespace HIKARI::TESTS
