#pragma once
#include "Assets/Vfx/HIKARI_VfxBudgets.h"
#include <limits>
#include <json.hpp>

inline void ValidateVfxFlowContracts() {
    using namespace HIKARI;
    using namespace HIKARI::VFX;
    const auto require = [](bool value, const std::string& message) {
        if (!value)
            throw std::runtime_error(message);
    };
    std::string error, text;
    auto asset = MakeDefaultVfxAssetData();
    auto& layer = asset.emitters.front();
    layer.renderCommon.colorTextureAssetGuid = AssetGuid{"engine.vfx.texture.noise_fractal_1"};
    layer.renderCommon.maskTextureAssetGuid = AssetGuid{"engine.vfx.texture.soft_disc"};
    layer.renderCommon.flowMapTextureAssetGuid = AssetGuid{"engine.vfx.texture.flow_vortex"};
    auto base = MakeMaterialEffect(MaterialEffectKind::UvFlow, "base.uv");
    base.textureTarget = MaterialEffectTextureTarget::BaseColor;
    base.parameters = {1.3f, -0.2f, 2, 1};
    auto mask = MakeMaterialEffect(MaterialEffectKind::UvFlow, "mask.uv");
    mask.textureTarget = MaterialEffectTextureTarget::Mask;
    mask.parameters = {-0.3f, 0.1f, 1, 1};
    auto flow = MakeMaterialEffect(MaterialEffectKind::FlowMap, "flow");
    layer.effects = {base, mask, flow};
    VfxParameterTrackData speed;
    speed.id = "flow.speed";
    speed.targetId = flow.id;
    speed.propertyId = "parameters.y";
    speed.curve.keys = {{"flow.a", 0, 0.2f}, {"flow.b", 1, 2.0f}};
    layer.parameterTracks.push_back(speed);
    require(SERIALIZATION::SerializeVfxSourceJson(asset, text, error), error);
    VfxAssetData parsed;
    require(SERIALIZATION::ParseVfxSourceJson(text, parsed, error), error);
    require(parsed.emitters.front().effects[1].textureTarget == MaterialEffectTextureTarget::Mask,
            "Independent texture target lost on save");
    for (uint32_t atlas : {1u, 4u}) {
        auto candidate = parsed;
        candidate.emitters.front().renderCommon.flipbookColumns = atlas;
        candidate.emitters.front().renderCommon.flipbookRows = atlas;
        candidate.emitters.front().renderCommon.flipbookFrameCount = atlas * atlas;
        auto result = CompileVfxAsset(candidate);
        if (!result.success)
            for (const auto& diagnostic : result.diagnostics)
                std::cerr << diagnostic.message << '\n';
        require(result.success, "Flow + independent UV + atlas material compile failed: " + std::to_string(atlas));
        const auto& curves = result.program.emitters.front().layerCurves;
        require(std::any_of(curves.begin(), curves.end(),
                            [](const auto& curve) { return curve.channel == VfxLayerChannel::MaterialUvOffset; }),
                "Flow speed keys must integrate phase");
        require(SerializeVfxProgram(result.program, text, error), error);
        VfxProgram roundTrip;
        require(DeserializeVfxProgram(text, roundTrip, error), error);
        require(roundTrip.emitters.size() == result.program.emitters.size(), "Flow cooked round-trip failed");
    }
    auto invalid = parsed;
    invalid.emitters.front().renderCommon.flowMapTextureAssetGuid = {};
    require(!CompileVfxAsset(invalid).success, "Missing flow map accepted");
    invalid = parsed;
    flow.id = "duplicate.flow";
    invalid.emitters.front().effects.push_back(flow);
    require(!CompileVfxAsset(invalid).success, "Overlapping flow targets accepted");
    invalid = parsed;
    invalid.emitters.front().effects.front().parameters2.x = std::numeric_limits<float>::quiet_NaN();
    require(!CompileVfxAsset(invalid).success, "Non-finite secondary UV parameters accepted");
    require(SERIALIZATION::SerializeVfxSourceJson(parsed, text, error), error);
    auto json = nlohmann::json::parse(text);
    for (auto& emitter : json["emitters"]) {
        emitter["modules"]["renderCommon"].erase("flowMapTextureAssetGuid");
        emitter["effects"] = nlohmann::json::array({emitter["effects"][0]});
        emitter["effects"][0].erase("textureTarget");
        emitter["parameterTracks"] = nlohmann::json::array();
    }
    require(SERIALIZATION::ParseVfxSourceJson(json.dump(), parsed, error), error);
    require(parsed.emitters.front().effects.front().textureTarget == MaterialEffectTextureTarget::Shared,
            "Omitted UV flow target lost shared mapping default");
    require(CompileVfxAsset(parsed).success, "Source without optional flow fields no longer compiles");
    {
        auto automatic = PRESETS::MakeVfxPresetAsset("slash");
        for (auto& emitter : automatic.emitters) {
            emitter.spawn.capacityMode = VfxParticleCapacityMode::Automatic;
            emitter.spawn.maximumParticles = RENDER3D::PARTICLES::kGpuParticlePoolCapacity;
        }
        auto compiled = CompileVfxAsset(automatic);
        require(compiled.success, "Automatic capacity rejected by summed authoring ceilings");
        auto& rounded = compiled.program;
        const auto reservation =
            std::find_if(rounded.emitters.begin(), rounded.emitters.end(),
                         [](const auto& emitter) { return emitter.kind == VfxLayerKind::Particles; });
        require(reservation != rounded.emitters.end(), "Capacity fixture has no compiled particles");
        uint32_t otherRequests = 0;
        for (const auto& emitter : rounded.emitters)
            if (&emitter != &*reservation && emitter.kind != VfxLayerKind::Group &&
                emitter.kind != VfxLayerKind::Adjustment)
                otherRequests += emitter.simulation.capacity;
        reservation->simulation.capacity = RENDER3D::PARTICLES::kGpuParticlePoolCapacity - otherRequests;
        require(!ValidateVfxProgram(rounded, error) && error.find("aligned particle reservations") != std::string::npos,
                "Raw particle counts fit, but aligned GPU reservation overflow was accepted");
        // 一つの固定プール予約と残りの層だけでも実容量は上限を超える。
        const auto particle = std::find_if(automatic.emitters.begin(), automatic.emitters.end(),
                                           [](const auto& emitter) { return emitter.kind == VfxLayerKind::Particles; });
        require(particle != automatic.emitters.end(), "Capacity fixture has no particles");
        particle->spawn.capacityMode = VfxParticleCapacityMode::Fixed;
        require(!CompileVfxAsset(automatic).success, "Actual compiled particle pool overflow accepted");
    }
    {
        auto extended = MakeDefaultVfxAssetData();
        auto& adjustment = extended.emitters.front();
        adjustment.kind = VfxLayerKind::Adjustment;
        for (uint32_t index = 0; index < kMaximumVfxEffectsPerLayer; ++index)
            adjustment.effects.push_back(
                MakeMaterialEffect(MaterialEffectKind::Opacity, "opacity." + std::to_string(index)));
        const auto compiled = CompileVfxAsset(extended);
        require(compiled.success, "Adjustment chain still has the old eight-effect limit");
        require(SerializeVfxProgram(compiled.program, text, error), error);
        VfxProgram loaded;
        require(DeserializeVfxProgram(text, loaded, error), error);
        require(!loaded.emitters.front().render.effectMaterial && loaded.emitters.front().layerCurves.empty(),
                "An adjustment with no lower layers allocated render work");
        require(SERIALIZATION::SerializeVfxSourceJson(extended, text, error), error);
        VfxAssetData sourceRoundtrip;
        require(SERIALIZATION::ParseVfxSourceJson(text, sourceRoundtrip, error), error);
        require(sourceRoundtrip.emitters.front().effects.size() == kMaximumVfxEffectsPerLayer,
                "Source adjustment effects lost");
        adjustment.effects.push_back(MakeMaterialEffect(MaterialEffectKind::Opacity, "over.limit"));
        require(!CompileVfxAsset(extended).success, "Source effect safety ceiling ignored");
    }
#if defined(HIKARI_WITH_EDITOR)
    EDITOR::VfxWorkspaceController editor;
    editor.NewAsset("Flow target undo");
    const auto id = editor.GetAsset()->emitters.front().id;
    require(editor.AddLayerEffect(id, MaterialEffectKind::UvFlow, error), error);
    const auto effectId = editor.GetAsset()->emitters.front().effects.front().id;
    require(editor.SetProperty(effectId, "textureTarget", "Mask", error), error);
    require(editor.Undo(error), error);
    require(editor.GetAsset()->emitters.front().effects.front().textureTarget == MaterialEffectTextureTarget::Shared,
            "UV target undo did not restore source");
    require(editor.Redo(error), error);
    require(editor.GetAsset()->emitters.front().effects.front().textureTarget == MaterialEffectTextureTarget::Mask,
            "UV target redo failed");
#endif
}
