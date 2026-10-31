#include "Editor/Documents/HIKARI_VfxEditorDocument.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxPreviewSession.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/Formats/HIKARI_HvfxcFormat.h"
#include "Assets/Vfx/Compiler/HIKARI_VfxCompiler.h"
#include "Assets/Vfx/HIKARI_VfxBuiltinTextureCatalog.h"
#include "Assets/Vfx/HIKARI_VfxPropertySchema.h"
#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJson.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Render3D/Vfx/HIKARI_ParticleCpuSimulation.h"
#include "Render3D/Vfx/HIKARI_GpuParticleParameterPacking.h"
#include "Render3D/Vfx/HIKARI_ParticleGeometryLayout.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Render3D/Runtime/HIKARI_RenderModelCache.h"
#include "Render3D/Models/HIKARI_ModelManager.h"
#include "Scene/Vfx/HIKARI_VfxMeshOutput.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponent.h"
#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponentAuthoring.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"
#include "Scene/Vfx/HIKARI_VfxBindingResolver.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"
#include "Internal/HIKARI_ContractAssert.h"
#include "Internal/HIKARI_VfxLocalAdjustmentContracts.h"

namespace {
void PostProcessModeContracts() {
    using namespace HIKARI;
    using VFX::VfxPostProcessMode;
    auto source = VFX::MakeDefaultVfxAssetData();
    assert(source.emitters.front().renderCommon.postProcessMode == VfxPostProcessMode::Scene);
    std::string json, error, bytes;
    assert(VFX::SERIALIZATION::SerializeVfxSourceJson(source, json, error));
    auto document = nlohmann::json::parse(json);
    document["emitters"][0]["modules"]["renderCommon"].erase("postProcessMode");
    VFX::VfxAssetData parsed;
    assert(VFX::SERIALIZATION::ParseVfxSourceJson(document.dump(), parsed, error));
    assert(parsed.emitters[0].renderCommon.postProcessMode == VfxPostProcessMode::Scene);
    document["emitters"][0]["modules"]["renderCommon"]["postProcessMode"] = "Unknown";
    assert(!VFX::SERIALIZATION::ParseVfxSourceJson(document.dump(), parsed, error));
    for (const auto mode : {VfxPostProcessMode::Scene, VfxPostProcessMode::PreserveColor}) {
        source.emitters[0].renderCommon.postProcessMode = mode;
        assert(VFX::SERIALIZATION::SerializeVfxSourceJson(source, json, error));
        assert(VFX::SERIALIZATION::ParseVfxSourceJson(json, parsed, error));
        assert(parsed.emitters[0].renderCommon.postProcessMode == mode);
        const auto compiled = VFX::CompileVfxAsset(parsed);
        assert(compiled.success && compiled.program.emitters[0].render.postProcessMode == mode);
        assert(VFX::SerializeVfxProgram(compiled.program, bytes, error));
        VFX::VfxProgram loaded;
        assert(VFX::DeserializeVfxProgram(bytes, loaded, error));
        assert(loaded.emitters[0].render.postProcessMode == mode);
        loaded.emitters[0].render.postProcessMode = static_cast<VfxPostProcessMode>(255);
        assert(!VFX::ValidateVfxProgram(loaded, error));
    }
    // Independent layers may choose different routes; matte dependencies cannot cross Global Post.
    document = nlohmann::json::parse(json);
    auto child = document["emitters"][0];
    const auto uniqueIds = [&](auto&& self, nlohmann::json& node) -> void {
        if (node.is_object()) {
            for (auto& [key, value] : node.items()) {
                if (key == "id" && value.is_string())
                    value = value.get<std::string>() + ".copy";
                else
                    self(self, value);
            }
        } else if (node.is_array())
            for (auto& value : node)
                self(self, value);
    };
    uniqueIds(uniqueIds, child);
    child["modules"]["renderCommon"]["postProcessMode"] = "Scene";
    document["emitters"].push_back(child);
    assert(VFX::SERIALIZATION::ParseVfxSourceJson(document.dump(), parsed, error));
    assert(VFX::CompileVfxAsset(parsed).success);
    parsed.emitters[1].matte.sourceLayerId = parsed.emitters[0].id;
    assert(!VFX::CompileVfxAsset(parsed).success);
    parsed.emitters[1].renderCommon.postProcessMode = VfxPostProcessMode::PreserveColor;
    assert(VFX::CompileVfxAsset(parsed).success);
}

void RingRuntimeBindings() {
    using namespace HIKARI;
    auto source = VFX::MakeDefaultVfxAssetData();
    source.loop = true;
    auto& layer = source.emitters.front();
    layer.kind = VFX::VfxLayerKind::Shape;
    layer.renderShape.type = VFX::VfxRenderShapeType::Ring;
    const std::array<const char*, 4> names{"ringInnerRadius", "ringOuterRadius", "ringStartAngleDegrees",
                                           "ringEndAngleDegrees"};
    const std::array<float, 4> values{9.2f, 10.f, 120.f, 420.f};
    for (size_t i = 0; i < names.size(); ++i) {
        auto value = VFX::MakeDefaultVfxValue(VFX::VfxValueType::Float);
        value.numeric.x = values[i];
        source.inputs.push_back({names[i], names[i], VFX::VfxValueType::Float, value});
        layer.inputLinks.push_back(
            {std::string("link.") + names[i], names[i], {layer.id, layer.renderShape.id, names[i]}, 1.f, 0.f});
    }
    const auto compiled = VFX::CompileVfxAsset(source);
    assert(compiled.success);
    std::string bytes;
    std::string error;
    assert(VFX::SerializeVfxProgram(compiled.program, bytes, error));
    VFX::VfxProgram restored;
    assert(VFX::DeserializeVfxProgram(bytes, restored, error));
    VFX::VfxPlaybackService service;
    VFX::VfxPlayRequest request;
    request.program = std::make_shared<const VFX::VfxProgram>(std::move(restored));
    const auto result = service.PlayDetailed(request);
    assert(result.IsAccepted());
    service.Update(1u, .125f);
    const auto first = service.GetRenderInput().emitters.front();
    assert(first.directShape && std::abs(first.render.ringInnerRadius - 9.2f) < 1e-5f);
    assert(std::abs(first.render.ringOuterRadius - 10.f) < 1e-5f);
    assert(std::abs((first.render.ringEndAngleRadians - first.render.ringStartAngleRadians) -
                    300.f * 0.01745329251994329577f) < 1e-5f);
    assert(service.SetTimeScale(result.handle, 0.f));
    std::vector<VFX::VfxParameterOverride> update;
    for (size_t i = 0u; i < 2u; ++i) {
        auto value = VFX::MakeDefaultVfxValue(VFX::VfxValueType::Float);
        value.numeric.x = values[i] + 17.f;
        update.push_back({names[i], value});
    }
    assert(service.SetParameters(result.handle, update));
    service.Update(2u, .5f);
    const auto paused = service.GetRenderInput().emitters.front();
    assert(paused.key == first.key && paused.deltaSeconds == 0.f);
    assert(paused.emitterAgeSeconds == first.emitterAgeSeconds && paused.shapeAgeSeconds == first.shapeAgeSeconds);
    assert(std::abs(paused.render.ringOuterRadius - 27.f) < 1e-5f);
    assert(std::abs(paused.render.ringOuterRadius - paused.render.ringInnerRadius - .8f) < 1e-5f);
    assert(!service.SetTimeScale(result.handle, -1.f));
    assert(!service.SetTimeScale(result.handle, std::numeric_limits<float>::quiet_NaN()));
    assert(service.SetTimeScale(result.handle, 1.f));
    service.Update(3u, .125f);
    assert(service.GetRenderInput().emitters.front().emitterAgeSeconds > paused.emitterAgeSeconds);
    assert(service.Stop(result.handle));
    assert(!service.SetTimeScale(result.handle, 0.f));
}

void PreviewOwnership() {
    HIKARI::EDITOR::VfxEditorDocument document;
    document.NewAsset();
    // 同じ効果契約が編集時とコンパイル済みデータの両方を守る。
    using namespace HIKARI;
    auto effect = MakeMaterialEffect(MaterialEffectKind::Twirl, "contract.warp");
    effect.space = MaterialEffectSpace::Group3D;
    assert(ValidateMaterialEffect(effect).empty());
    effect.mask = MaterialEffectMask::Alpha;
    assert(!ValidateMaterialEffect(effect).empty());
    effect.space = MaterialEffectSpace::LayerUv;
    assert(ValidateMaterialEffect(effect).empty());
    effect.colorStops.push_back({"bad", 2.0f, {1, 1, 1, 1}});
    assert(!ValidateMaterialEffect(effect).empty());
    effect = MakeMaterialEffect(MaterialEffectKind::FlowMap, "contract.flow");
    effect.textureTarget = MaterialEffectTextureTarget::Noise;
    assert(!ValidateMaterialEffect(effect).empty());
    effect.parameters2.z = 0;
    assert(!GetMaterialEffectParameters(effect, true)[1].animatable);
    effect.parameters2.z = 1;
    assert(GetMaterialEffectParameters(effect, true)[1].animatable);
    const auto original = document.Asset();
    auto edited = original;
    std::string message;
    edited.durationSeconds += 1;
    assert(document.ApplyAsset(edited, message, 9));
    edited.name = "Metadata in runtime gesture";
    assert(document.ApplyAsset(edited, message, 9, HIKARI::EDITOR::VfxDocumentImpact::Metadata));
    assert(document.Undo() && document.GetLastImpact() == HIKARI::EDITOR::VfxDocumentImpact::Runtime);
    assert(document.Asset().durationSeconds == original.durationSeconds);
    assert(document.Redo() && document.Asset().name == edited.name);
    assert(!document.CanRedo());

    HIKARI::EDITOR::VfxPreviewSession visible, capture;
    visible.BindSource(document.Asset(), {}, true);
    assert(capture.CopyFrom(visible, message));
    const auto capturedDuration = capture.GetPreviewTimelineDuration();
    edited.durationSeconds += 10;
    assert(document.ApplyAsset(edited, message));
    visible.BindSource(document.Asset(), {}, true);
    assert(capture.GetPreviewTimelineDuration() == capturedDuration);
    assert(visible.GetPreviewTimelineDuration() > capturedDuration);
    assert(visible.PlayPreview(message));
    visible.StopPreview();
    edited.durationSeconds = -1;
    visible.BindSource(edited, {}, true);
    edited.name = "Metadata must not acknowledge an uncompiled runtime edit";
    visible.BindSource(edited, {}, false);
    assert(!visible.PlayPreview(message));
    assert(capture.PlayPreview(message));
    capture.StopPreview();
}

bool HasDiagnostic(const HIKARI::VFX::VfxCompileResult& result, const char* code) {
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

const HIKARI::ComponentAuthoringCommandDefinition* FindCommand(
    const std::vector<HIKARI::ComponentAuthoringCommandDefinition>& commands, const char* id) {
    for (const auto& command : commands) {
        if (command.id == id)
            return &command;
    }
    return nullptr;
}

void AssertVfxComponentDecodeRejected(HIKARI::VfxPlayerComponent& component, const nlohmann::json& malformed,
                                      const char* diagnosticFragment) {
    nlohmann::json before{};
    component.Serialize(before);
    std::string diagnostic{};
    assert(!component.TryDeserialize(malformed, diagnostic));
    assert(!diagnostic.empty());
    assert(diagnostic.find(diagnosticFragment) != std::string::npos);
    nlohmann::json after{};
    component.Serialize(after);
    assert(after == before);
}
} // namespace

int main() {
    PostProcessModeContracts();
    RingRuntimeBindings();
    PreviewOwnership();
    HIKARI::TESTS::AssertVfxLocalAdjustments();
    using namespace HIKARI;

    const MATH::Vec3 cameraRight{1.0f, 0.0f, 0.0f};
    const MATH::Vec3 cameraUp{0.0f, 1.0f, 0.0f};
    const MATH::Vec3 toCamera{0.0f, 0.0f, -1.0f};
    const MATH::Vec3 fixedYAxisRight = MATH::Normalize(MATH::Cross(toCamera, cameraUp));
    assert(MATH::Dot(fixedYAxisRight, cameraRight) > 0.999f);
    const MATH::Vec3 particleFront = MATH::Normalize(MATH::Cross(fixedYAxisRight, cameraUp) * -1.0f);
    assert(MATH::Dot(particleFront, toCamera) > 0.999f);
    const MATH::Vec3 rotatedRight = MATH::Normalize(cameraRight * 0.0f + cameraUp * 1.0f);
    const MATH::Vec3 rotatedUp = MATH::Normalize(cameraRight * -1.0f + cameraUp * 0.0f);
    assert(MATH::Dot(rotatedRight, cameraUp) > 0.999f);
    const MATH::Vec3 rotatedFront = MATH::Normalize(MATH::Cross(rotatedRight, rotatedUp) * -1.0f);
    assert(MATH::Dot(rotatedFront, particleFront) > 0.999f);

    const RENDER3D::PARTICLES::INTERNAL::GpuParticleEmitterUploadAddress emitter257 =
        RENDER3D::PARTICLES::INTERNAL::ResolveGpuParticleEmitterUploadAddress(
            256u, RENDER3D::PARTICLES::kDefaultParticleEmitterUploadPageCapacity);
    assert(emitter257.valid);
    assert(emitter257.pageIndex == 1u);
    assert(emitter257.elementIndex == 0u);
    assert(!RENDER3D::PARTICLES::INTERNAL::ResolveGpuParticleEmitterUploadAddress(
                RENDER3D::PARTICLES::kMaximumParticleEmitterSubmissionsPerFrame,
                RENDER3D::PARTICLES::kDefaultParticleEmitterUploadPageCapacity)
                .valid);

    AssetRegistry assetRegistry{};
    AssetRegistry secondAssetRegistry{};
    assert(assetRegistry.GetInstanceId() != secondAssetRegistry.GetInstanceId());
    const uint64_t initialAssetRevision = assetRegistry.GetRevision();
    auto descriptor = std::make_unique<AssetDescriptor>();
    descriptor->id.value = "asset.registry.revision";
    assert(assetRegistry.RegisterDescriptor(std::move(descriptor)));
    assert(assetRegistry.GetRevision() != initialAssetRevision);
    const uint64_t registeredAssetRevision = assetRegistry.GetRevision();
    assetRegistry.Clear();
    assert(assetRegistry.GetRevision() != registeredAssetRevision);
    const uint64_t liveRegistryId = assetRegistry.GetInstanceId();
    const uint64_t liveRegistryRevision = assetRegistry.GetRevision();
    AssetRegistry stagedAssetRegistry{};
    auto stagedDescriptor = std::make_unique<AssetDescriptor>();
    stagedDescriptor->id.value = "asset.registry.staged";
    assert(stagedAssetRegistry.RegisterDescriptor(std::move(stagedDescriptor)));
    assetRegistry.SwapContents(stagedAssetRegistry);
    assert(assetRegistry.GetInstanceId() == liveRegistryId);
    assert(assetRegistry.GetRevision() != liveRegistryRevision);
    assert(assetRegistry.FindDescriptor("asset.registry.staged") != nullptr);

    VFX::VfxAssetData source = VFX::MakeDefaultVfxAssetData();
    assert(source.emitters.front().renderShape.billboardMode == VFX::VfxBillboardMode::RotatedBillboard);
    source.name = "Native Smoke";
    source.durationSeconds = 1.0f;
    source.inputs.push_back(
        {"input.speed", "Speed", VFX::VfxValueType::Float, VFX::MakeDefaultVfxValue(VFX::VfxValueType::Float)});
    source.inputs.front().defaultValue.numeric.x = 1.0f;
    VFX::VfxEmitterData& emitter = source.emitters.front();
    emitter.spawn.ratePerSecond = 4.0f;
    emitter.spawn.burstTimeline = {
        {emitter.spawn.id + ".burst.zero", 0.0f, 2u},
        {emitter.spawn.id + ".burst.followup", 0.5f, 3u},
    };
    emitter.attachment.mode = VFX::VfxAttachmentMode::OwnerSocket;
    emitter.attachment.follow = VFX::VfxFollowMode::Transform;
    emitter.attachment.socketName = "weapon_tip";
    emitter.attachment.offset.position = {0.1f, 0.2f, 0.3f};
    emitter.attachment.offset.rotationEulerDegrees = {2.0f, 3.0f, 4.0f};
    emitter.attachment.offset.scale = {1.1f, 0.9f, 1.0f};
    emitter.shape.type = VFX::VfxShapeType::Circle;
    emitter.shape.radius = VFX::MakeVfxFloatConstant(2.0f);
    emitter.shape.boxHalfExtents = {1.25f, 2.5f, 3.75f};
    emitter.shape.cylinderHeight = 4.5f;
    emitter.shape.circleInnerRadius = 0.25f;
    emitter.shape.circleEmitOnEdge = true;
    emitter.shape.edgeLength = 7.5f;
    VFX::VfxSpecialOutputData weaponOutput{};
    weaponOutput.id = emitter.id + ".output.weapon";
    weaponOutput.type = VFX::VfxSpecialOutputType::WeaponPathDistortion;
    weaponOutput.weaponPathDistortion.distortion = 2.0f;
    weaponOutput.weaponPathDistortion.blur = 0.25f;
    weaponOutput.weaponPathDistortion.opacity = 0.75f;
    weaponOutput.weaponPathDistortion.emissive = 3.0f;
    weaponOutput.weaponPathDistortion.flow = 1.5f;
    weaponOutput.weaponPathDistortion.width = 0.4f;
    weaponOutput.weaponPathDistortion.lifetimeSeconds = 0.3f;
    weaponOutput.weaponPathDistortion.bodyColor = {1.0f, 0.2f, 0.1f, 1.0f};
    weaponOutput.weaponPathDistortion.edgeColor = {1.0f, 0.8f, 0.3f, 0.5f};
    VFX::VfxSpecialOutputData impulseOutput{};
    impulseOutput.id = emitter.id + ".output.impulse";
    impulseOutput.type = VFX::VfxSpecialOutputType::ScreenImpulse;
    impulseOutput.screenImpulse.durationSeconds = 0.4f;
    impulseOutput.screenImpulse.maxRadius = 1.25f;
    impulseOutput.screenImpulse.ringThickness = 0.3f;
    impulseOutput.screenImpulse.distortion = 0.2f;
    impulseOutput.screenImpulse.chromaticAberration = 0.03f;
    impulseOutput.screenImpulse.flash = 0.6f;
    impulseOutput.screenImpulse.intensity = 2.0f;
    impulseOutput.screenImpulse.color = {0.2f, 0.4f, 1.0f, 1.0f};
    impulseOutput.screenImpulse.waveFrequency = 2.0f;
    impulseOutput.screenImpulse.waveAmplitude = 0.8f;
    impulseOutput.screenImpulse.waveDamping = 0.9f;
    impulseOutput.screenImpulse.waveCount = 3u;
    VFX::VfxSpecialOutputData afterimageOutput{};
    afterimageOutput.id = emitter.id + ".output.afterimage";
    afterimageOutput.type = VFX::VfxSpecialOutputType::SkinnedAfterimage;
    afterimageOutput.skinnedAfterimage.sampleCount = 5u;
    afterimageOutput.skinnedAfterimage.lifetimeSeconds = 0.5f;
    afterimageOutput.skinnedAfterimage.opacity = 0.65f;
    afterimageOutput.skinnedAfterimage.fresnel = 1.4f;
    afterimageOutput.skinnedAfterimage.dissolve = 0.2f;
    afterimageOutput.skinnedAfterimage.shellOffset = 0.03f;
    afterimageOutput.skinnedAfterimage.color = {0.4f, 0.8f, 1.0f, 1.0f};
    afterimageOutput.skinnedAfterimage.intensity = 1.8f;
    emitter.specialOutputs = {
        weaponOutput,
        impulseOutput,
        afterimageOutput,
    };
    emitter.basic.lifetime = VFX::MakeVfxFloatConstant(3.0f);
    emitter.renderCommon.materialMode = VFX::VfxMaterialMode::CustomMaterial;
    emitter.renderCommon.materialAssetGuid.value = "00112233445566778899aabbccddeeff";
    emitter.renderCommon.depthMode = VFX::VfxParticleDepthMode::SoftIntersection;
    emitter.renderCommon.softIntersectionDistance = 0.42f;
    emitter.renderCommon.depthBias = 0.0015f;
    emitter.renderCommon.flipbookColumns = 8u;
    emitter.renderCommon.flipbookRows = 8u;
    emitter.renderCommon.flipbookFrameCount = 64u;
    emitter.renderCommon.flipbookFramesPerSecond = 24.0f;
    emitter.renderCommon.flipbookLoop = true;
    emitter.renderCommon.flipbookRandomStart = true;
    emitter.renderShape.type = VFX::VfxRenderShapeType::Ring;
    emitter.renderShape.ringOuterRadius = 2.0f;
    emitter.renderShape.ringInnerRadius = 1.25f;
    emitter.renderShape.ringSegmentCount = 48u;
    emitter.renderShape.ringStartAngleDegrees = 15.0f;
    emitter.renderShape.ringEndAngleDegrees = 315.0f;
    emitter.position.value.mode = VFX::VfxAnimatedValueMode::Pva;
    emitter.position.value.pva.initial = {
        VFX::VfxDistributionMode::RandomRange, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 17u};
    emitter.position.value.pva.velocity = VFX::MakeVfxVec3Constant({0.0f, 2.0f, 0.0f});
    emitter.position.value.pva.acceleration = VFX::MakeVfxVec3Constant({0.0f, -1.0f, 0.0f});
    emitter.rotation.eulerDegrees.mode = VFX::VfxAnimatedValueMode::Easing;
    emitter.rotation.eulerDegrees.easing.start = VFX::MakeVfxVec3Constant({0.0f, 0.0f, 0.0f});
    emitter.rotation.eulerDegrees.easing.end = VFX::MakeVfxVec3Constant({0.0f, 180.0f, 0.0f});
    emitter.rotation.eulerDegrees.easing.function = VFX::VfxEasingFunction::EaseInOut;
    emitter.scale.value.mode = VFX::VfxAnimatedValueMode::Curve;
    emitter.scale.value.curve.x = VFX::MakeVfxConstantCurve("scale.x", 1.0f);
    emitter.scale.value.curve.x.keys.back().value = 2.0f;
    emitter.scale.value.curve.x.randomOffset = {VFX::VfxDistributionMode::RandomRange, 0.1f, 0.2f, 23u};
    emitter.scale.value.curve.y = VFX::MakeVfxConstantCurve("scale.y", 1.0f);
    emitter.scale.value.curve.y.keys.back().value = 0.5f;
    emitter.scale.value.curve.z = VFX::MakeVfxConstantCurve("scale.z", 1.0f);
    emitter.inputLinks.push_back(
        {"link.speed.spawn", "input.speed", {emitter.id, emitter.spawn.id, "rateMultiplier"}, 1.0f, 0.0f});

    const VFX::VfxPropertySchema* spawnRateSchema =
        VFX::ResolveVfxPropertySchema(emitter, emitter.inputLinks.front().target);
    assert(spawnRateSchema != nullptr);
    assert(spawnRateSchema->bindable && spawnRateSchema->runtimeMutable);
    assert(spawnRateSchema->valueType == VFX::VfxValueType::Float);
    assert(VFX::IsVfxInputTypeCompatible(VFX::VfxValueType::Int, *spawnRateSchema));
    assert(VFX::IsVfxInputTypeCompatible(VFX::VfxValueType::Float4, VFX::VfxValueType::Color));
    assert(!VFX::IsVfxInputTypeCompatible(VFX::VfxValueType::Bool, *spawnRateSchema));

    std::string sourceJsonA{};
    std::string sourceJsonB{};
    std::string error{};
    assert(VFX::SERIALIZATION::SerializeVfxSourceJson(source, sourceJsonA, error));
    assert(VFX::SERIALIZATION::SerializeVfxSourceJson(source, sourceJsonB, error));
    assert(sourceJsonA == sourceJsonB);

    const nlohmann::json canonicalSourceDocument = nlohmann::json::parse(sourceJsonA);
    assert(canonicalSourceDocument["emitters"][0]["modules"]["renderShape"]["billboardMode"] == "RotatedBillboard");
    assert(canonicalSourceDocument["emitters"][0]["modules"]["renderCommon"]["flipbookFrameCount"] == 64u);
    assert(canonicalSourceDocument["emitters"][0]["modules"]["renderCommon"]["materialMode"] == "CustomMaterial");
    assert(canonicalSourceDocument["emitters"][0]["modules"]["spawn"]["capacityMode"] == "Automatic");

    // Source import canonicalizes authored burst order so cooked playback's
    // monotonic next-key cursor cannot skip a key after an older hand-edited
    // document is loaded.
    nlohmann::json unsortedBurstDocument = canonicalSourceDocument;
    auto& unsortedBurstTimeline = unsortedBurstDocument["emitters"][0]["modules"]["spawn"]["burstTimeline"];
    std::swap(unsortedBurstTimeline[0], unsortedBurstTimeline[1]);
    VFX::VfxAssetData sortedBurstSource{};
    assert(VFX::SERIALIZATION::ParseVfxSourceJson(unsortedBurstDocument.dump(), sortedBurstSource, error));
    const auto& sortedBurstKeys = sortedBurstSource.emitters.front().spawn.burstTimeline;
    assert(sortedBurstKeys.size() == 2u);
    assert(sortedBurstKeys[0].time <= sortedBurstKeys[1].time);
    assert(VFX::CompileVfxAsset(sortedBurstSource).success);

    constexpr std::array<VFX::VfxBillboardMode, 4u> spriteOrientations{
        VFX::VfxBillboardMode::RotatedBillboard,
        VFX::VfxBillboardMode::FixedYAxis,
        VFX::VfxBillboardMode::DirectionalBillboard,
        VFX::VfxBillboardMode::Fixed,
    };
    for (size_t orientationIndex = 0u; orientationIndex < spriteOrientations.size(); ++orientationIndex) {
        VFX::VfxAssetData orientationSource = source;
        orientationSource.emitters.front().renderShape.billboardMode = spriteOrientations[orientationIndex];
        std::string orientationJson{};
        assert(VFX::SERIALIZATION::SerializeVfxSourceJson(orientationSource, orientationJson, error));
        VFX::VfxAssetData orientationRoundTrip{};
        assert(VFX::SERIALIZATION::ParseVfxSourceJson(orientationJson, orientationRoundTrip, error));
        assert(orientationRoundTrip.emitters.front().renderShape.billboardMode == spriteOrientations[orientationIndex]);

        const VFX::VfxCompileResult orientationCompiled = VFX::CompileVfxAsset(orientationRoundTrip);
        assert(orientationCompiled.success);
        assert(static_cast<uint32_t>(orientationCompiled.program.emitters.front().render.billboardMode) ==
               orientationIndex);
        std::string orientationCooked{};
        assert(VFX::SerializeVfxProgram(orientationCompiled.program, orientationCooked, error));
        VFX::VfxProgram orientationCookedRoundTrip{};
        assert(VFX::DeserializeVfxProgram(orientationCooked, orientationCookedRoundTrip, error));
        assert(orientationCookedRoundTrip.emitters.front().render.billboardMode ==
               orientationCompiled.program.emitters.front().render.billboardMode);
    }

    // 旧版の名前・形式は変換せずに拒否する。
    for (const char* retiredName : {"CameraFacing", "Vertical"}) {
        nlohmann::json retiredDocument = canonicalSourceDocument;
        retiredDocument["emitters"][0]["modules"]["renderShape"]["billboardMode"] = retiredName;
        VFX::VfxAssetData rejected{};
        assert(!VFX::SERIALIZATION::ParseVfxSourceJson(retiredDocument.dump(), rejected, error));
    }
    nlohmann::json outdatedDocument = canonicalSourceDocument;
    outdatedDocument["version"] = 2u;
    VFX::VfxAssetData rejectedOutdated{};
    assert(!VFX::SERIALIZATION::ParseVfxSourceJson(outdatedDocument.dump(), rejectedOutdated, error));

    // Activation and burst keys use emitter-local time. A zero duration
    // means "until asset end" after startDelay, including the exact-end
    // boundary; overflowing windows are rejected before compile/cook.
    VFX::VfxAssetData activation = VFX::MakeDefaultVfxAssetData();
    activation.durationSeconds = 1.0f;
    VFX::VfxEmitterData& activationEmitter = activation.emitters.front();
    activationEmitter.startDelay = 0.25f;
    activationEmitter.activeDuration = 0.0f;
    activationEmitter.spawn.burstTimeline = {
        {activationEmitter.spawn.id + ".burst.end", 0.75f, 2u},
    };
    const auto activationCompiled = VFX::CompileVfxAsset(activation);
    assert(activationCompiled.success);
    std::string activationJson{};
    assert(VFX::SERIALIZATION::SerializeVfxSourceJson(activation, activationJson, error));
    VFX::VfxAssetData activationRoundTrip{};
    assert(VFX::SERIALIZATION::ParseVfxSourceJson(activationJson, activationRoundTrip, error));
    assert(activationRoundTrip.emitters.front().startDelay == 0.25f);
    assert(activationRoundTrip.emitters.front().activeDuration == 0.0f);
    assert(activationRoundTrip.emitters.front().spawn.burstTimeline.front().time == 0.75f);
    VFX::VfxAssetData activationExactEnd = activation;
    activationExactEnd.emitters.front().startDelay = 1.0f;
    activationExactEnd.emitters.front().spawn.burstTimeline.clear();
    assert(VFX::CompileVfxAsset(activationExactEnd).success);
    VFX::VfxAssetData activationOverflow = activation;
    activationOverflow.emitters.front().startDelay = 1.01f;
    const auto activationOverflowResult = VFX::CompileVfxAsset(activationOverflow);
    assert(!activationOverflowResult.success);
    assert(HasDiagnostic(activationOverflowResult, "Vfx.EmitterActivationOutsideAsset"));
    VFX::VfxAssetData explicitOverflow = activation;
    explicitOverflow.emitters.front().activeDuration = 0.76f;
    const auto explicitOverflowResult = VFX::CompileVfxAsset(explicitOverflow);
    assert(!explicitOverflowResult.success);
    assert(HasDiagnostic(explicitOverflowResult, "Vfx.EmitterActivationOutsideAsset"));

    // Capacity is enforced per key, not as a sum across the whole timeline;
    // particles from an earlier burst may have expired before a later one.
    VFX::VfxAssetData sequentialBursts = VFX::MakeDefaultVfxAssetData();
    sequentialBursts.durationSeconds = 2.0f;
    sequentialBursts.emitters.front().spawn.maximumParticles = 32u;
    sequentialBursts.emitters.front().spawn.burstTimeline = {
        {"burst.first", 0.0f, 32u},
        {"burst.second", 1.0f, 32u},
    };
    const VFX::VfxCompileResult sequentialBurstsResult = VFX::CompileVfxAsset(sequentialBursts);
    assert(sequentialBurstsResult.success);
    assert(sequentialBurstsResult.program.emitters.front().simulation.capacity == 32u);

    // Two emitters with unique module IDs, shared by the special-output checks below.
    VFX::VfxAssetData multiEmitter = VFX::MakeDefaultVfxAssetData();
    multiEmitter.durationSeconds = 1.0f;
    VFX::VfxEmitterData secondEmitter = multiEmitter.emitters.front();
    const auto suffixIds = [](VFX::VfxEmitterData& value, std::string_view suffix) {
        const auto append = [suffix](std::string& id) {
            id += ".";
            id += suffix;
        };
        append(value.id);
        append(value.basic.id);
        append(value.emitterTransform.id);
        append(value.attachment.id);
        append(value.spawn.id);
        append(value.shape.id);
        append(value.position.id);
        append(value.rotation.id);
        append(value.scale.id);
        append(value.forces.id);
        append(value.color.id);
        append(value.renderCommon.id);
        append(value.renderShape.id);
        for (VFX::VfxColorStopData& stop : value.color.value.gradient.stops) {
            append(stop.id);
        }
    };
    suffixIds(multiEmitter.emitters.front(), "first");
    suffixIds(secondEmitter, "second");
    multiEmitter.emitters.front().spawn.burstTimeline.clear();
    secondEmitter.spawn.burstTimeline.clear();
    secondEmitter.order = 1u;
    multiEmitter.emitters.push_back(std::move(secondEmitter));

    VFX::VfxAssetData duplicateOutputAsset = multiEmitter;
    duplicateOutputAsset.emitters.front().specialOutputs = {weaponOutput};
    duplicateOutputAsset.emitters[1].specialOutputs = {weaponOutput};
    const VFX::VfxCompileResult duplicateOutputAssetResult = VFX::CompileVfxAsset(duplicateOutputAsset);
    assert(!duplicateOutputAssetResult.success);
    assert(HasDiagnostic(duplicateOutputAssetResult, "Vfx.DuplicateStableId"));

    VFX::VfxAssetData roundTripSource{};
    assert(VFX::SERIALIZATION::ParseVfxSourceJson(sourceJsonA, roundTripSource, error));
    assert(roundTripSource.emitters.front().attachment.mode == VFX::VfxAttachmentMode::OwnerSocket);
    assert(roundTripSource.emitters.front().attachment.follow == VFX::VfxFollowMode::Transform);
    assert(roundTripSource.emitters.front().attachment.socketName == "weapon_tip");
    assert(roundTripSource.emitters.front().spawn.burstTimeline.size() == 2u);
    const auto roundTripWeapon = std::find_if(roundTripSource.emitters.front().specialOutputs.begin(),
                                              roundTripSource.emitters.front().specialOutputs.end(),
                                              [](const VFX::VfxSpecialOutputData& output) {
                                                  return output.type == VFX::VfxSpecialOutputType::WeaponPathDistortion;
                                              });
    const auto roundTripImpulse = std::find_if(roundTripSource.emitters.front().specialOutputs.begin(),
                                               roundTripSource.emitters.front().specialOutputs.end(),
                                               [](const VFX::VfxSpecialOutputData& output) {
                                                   return output.type == VFX::VfxSpecialOutputType::ScreenImpulse;
                                               });
    const auto roundTripAfterimage = std::find_if(
        roundTripSource.emitters.front().specialOutputs.begin(), roundTripSource.emitters.front().specialOutputs.end(),
        [](const VFX::VfxSpecialOutputData& output) {
            return output.type == VFX::VfxSpecialOutputType::SkinnedAfterimage;
        });
    assert(roundTripWeapon != roundTripSource.emitters.front().specialOutputs.end());
    assert(roundTripImpulse != roundTripSource.emitters.front().specialOutputs.end());
    assert(roundTripAfterimage != roundTripSource.emitters.front().specialOutputs.end());
    assert(roundTripWeapon->weaponPathDistortion.blur == 0.25f);
    assert(roundTripImpulse->screenImpulse.waveCount == 3u);
    assert(roundTripAfterimage->skinnedAfterimage.sampleCount == 5u);
    nlohmann::json damagedSourceDocument = canonicalSourceDocument;
    damagedSourceDocument["version"] = 1u;
    const std::string damagedSource = damagedSourceDocument.dump();
    VFX::VfxAssetData unchangedSource = VFX::MakeDefaultVfxAssetData();
    unchangedSource.name = "transaction sentinel";
    assert(!VFX::SERIALIZATION::ParseVfxSourceJson(damagedSource, unchangedSource, error));
    assert(unchangedSource.name == "transaction sentinel");
    nlohmann::json negativeBurstDocument = canonicalSourceDocument;
    negativeBurstDocument["emitters"][0]["modules"]["spawn"]["burstTimeline"][0]["count"] = -1;
    VFX::VfxAssetData negativeBurstSentinel = VFX::MakeDefaultVfxAssetData();
    assert(!VFX::SERIALIZATION::ParseVfxSourceJson(negativeBurstDocument.dump(), negativeBurstSentinel, error));
    nlohmann::json negativeWaveDocument = canonicalSourceDocument;
    negativeWaveDocument["emitters"][0]["specialOutputs"][1]["screenImpulse"]["waveCount"] = -1;
    assert(!VFX::SERIALIZATION::ParseVfxSourceJson(negativeWaveDocument.dump(), negativeBurstSentinel, error));
    nlohmann::json negativeSampleDocument = canonicalSourceDocument;
    negativeSampleDocument["emitters"][0]["specialOutputs"][0]["skinnedAfterimage"]["sampleCount"] = -1;
    assert(!VFX::SERIALIZATION::ParseVfxSourceJson(negativeSampleDocument.dump(), negativeBurstSentinel, error));
    nlohmann::json negativeWeaponProfile = canonicalSourceDocument;
    negativeWeaponProfile["emitters"][0]["specialOutputs"][2]["weaponPathDistortion"]["blur"] = -0.01f;
    assert(!VFX::SERIALIZATION::ParseVfxSourceJson(negativeWeaponProfile.dump(), negativeBurstSentinel, error));
    nlohmann::json negativeScreenProfile = canonicalSourceDocument;
    negativeScreenProfile["emitters"][0]["specialOutputs"][1]["screenImpulse"]["distortion"] = -0.01f;
    assert(!VFX::SERIALIZATION::ParseVfxSourceJson(negativeScreenProfile.dump(), negativeBurstSentinel, error));
    nlohmann::json negativeAfterimageProfile = canonicalSourceDocument;
    negativeAfterimageProfile["emitters"][0]["specialOutputs"][0]["skinnedAfterimage"]["fresnel"] = -0.01f;
    assert(!VFX::SERIALIZATION::ParseVfxSourceJson(negativeAfterimageProfile.dump(), negativeBurstSentinel, error));
    nlohmann::json excessiveWaveDocument = canonicalSourceDocument;
    excessiveWaveDocument["emitters"][0]["specialOutputs"][1]["screenImpulse"]["waveCount"] =
        VFX::kVfxScreenImpulseWaveCountMax + 1u;
    assert(!VFX::SERIALIZATION::ParseVfxSourceJson(excessiveWaveDocument.dump(), negativeBurstSentinel, error));
    nlohmann::json excessiveSampleDocument = canonicalSourceDocument;
    excessiveSampleDocument["emitters"][0]["specialOutputs"][0]["skinnedAfterimage"]["sampleCount"] =
        VFX::kVfxAfterimageSampleCountMax + 1u;
    assert(!VFX::SERIALIZATION::ParseVfxSourceJson(excessiveSampleDocument.dump(), negativeBurstSentinel, error));
    const VFX::VfxCompileResult compiled = VFX::CompileVfxAsset(roundTripSource);
    assert(compiled.success);
    assert(compiled.program.maximumParticleLifetimeSeconds == 3.0f);
    assert(compiled.program.dependencyGuids.size() == 1u);
    assert(compiled.program.emitters.front().simulation.capacity < emitter.spawn.maximumParticles);
    assert(compiled.program.emitters.front().simulation.capacity >= emitter.spawn.burstTimeline.front().count);
    assert(compiled.program.emitters.front().inputLinks.size() == 1u);
    const VFX::VfxProgramInputLink& compiledLink = compiled.program.emitters.front().inputLinks.front();
    assert(compiledLink.semantic == VFX::VfxRuntimeBindingSemantic::SpawnRateMultiplier);
    const VFX::VfxRuntimePropertyLayout spawnRateLayout = VFX::ResolveVfxRuntimePropertyLayout(compiledLink.semantic);
    assert(compiledLink.byteOffset == spawnRateLayout.byteOffset);
    assert(compiledLink.storageType == spawnRateLayout.storageType);
    assert(compiled.program.emitters.front().simulation.position.mode == RENDER3D::PARTICLES::ParticleValueMode::Pva);
    assert(compiled.program.emitters.front().simulation.rotationRadians.mode ==
           RENDER3D::PARTICLES::ParticleValueMode::Easing);
    assert(compiled.program.emitters.front().simulation.scale.mode == RENDER3D::PARTICLES::ParticleValueMode::Curve);
    assert(compiled.program.emitters.front().simulation.scale.curves[0].randomOffset.mode ==
           RENDER3D::PARTICLES::ParticleDistributionMode::RandomRange);
    assert(compiled.program.emitters.front().simulation.scale.curves[0].randomOffset.randomGroup == 23u);
    const auto& compiledRender = compiled.program.emitters.front().render;
    assert(compiledRender.materialMode == RENDER3D::PARTICLES::ParticleMaterialMode::CustomMaterial);
    assert(compiledRender.materialAssetGuid == emitter.renderCommon.materialAssetGuid.value);
    assert(compiledRender.depthMode == RENDER3D::PARTICLES::ParticleDepthMode::SoftIntersection);
    assert(compiledRender.softIntersectionDistance == 0.42f);
    assert(compiledRender.depthBias == 0.0015f);
    assert(compiledRender.flipbookColumns == 8u);
    assert(compiledRender.flipbookRows == 8u);
    assert(compiledRender.flipbookFrameCount == 64u);
    assert(compiledRender.flipbookFramesPerSecond == 24.0f);
    assert(compiledRender.flipbookLoop);
    assert(compiledRender.flipbookRandomStart);
    assert(compiledRender.shape == RENDER3D::PARTICLES::ParticleRenderShape::Ring);
    assert(compiledRender.ringSegmentCount == 48u);
    assert(compiledRender.ringOuterRadius == 2.0f);
    assert(compiledRender.ringInnerRadius == 1.25f);

    VFX::VfxProgram invalidCookedProfile = compiled.program;
    auto invalidCookedImpulse = std::find_if(invalidCookedProfile.emitters.front().specialOutputs.begin(),
                                             invalidCookedProfile.emitters.front().specialOutputs.end(),
                                             [](const VFX::VfxSpecialOutputData& output) {
                                                 return output.type == VFX::VfxSpecialOutputType::ScreenImpulse;
                                             });
    assert(invalidCookedImpulse != invalidCookedProfile.emitters.front().specialOutputs.end());
    invalidCookedImpulse->screenImpulse.waveCount = VFX::kVfxScreenImpulseWaveCountMax + 1u;
    std::string invalidCookedPayload{};
    assert(!VFX::SerializeVfxProgram(invalidCookedProfile, invalidCookedPayload, error));

    std::string cookedA{};
    std::string cookedB{};
    assert(VFX::SerializeVfxProgram(compiled.program, cookedA, error));
    assert(VFX::SerializeVfxProgram(compiled.program, cookedB, error));
    assert(cookedA == cookedB);
    VFX::VfxProgram cookedRoundTrip{};
    assert(VFX::DeserializeVfxProgram(cookedA, cookedRoundTrip, error));
    assert(cookedRoundTrip.sourceHash == compiled.program.sourceHash);
    assert(cookedRoundTrip.emitters.front().attachment.mode == VFX::VfxAttachmentMode::OwnerSocket);
    assert(cookedRoundTrip.emitters.front().attachment.socketName == "weapon_tip");
    assert(cookedRoundTrip.emitters.front().burstTimeline.size() == 2u);
    assert(cookedRoundTrip.emitters.front().specialOutputs.size() == 3u);
    assert(cookedRoundTrip.emitters.front().simulation.lifetime.minimum == 3.0f);
    const auto& cookedSimulation = cookedRoundTrip.emitters.front().simulation;
    assert(cookedSimulation.shape == RENDER3D::PARTICLES::ParticleShape::Circle);
    assert(cookedSimulation.shapeBoxHalfExtents.x == 1.25f);
    assert(cookedSimulation.shapeBoxHalfExtents.y == 2.5f);
    assert(cookedSimulation.shapeBoxHalfExtents.z == 3.75f);
    assert(cookedSimulation.shapeCylinderHeight == 4.5f);
    assert(cookedSimulation.shapeCircleInnerRadius == 0.25f);
    assert(cookedSimulation.shapeCircleEmitOnEdge == 1u);
    assert(cookedSimulation.shapeEdgeLength == 7.5f);
    const auto& cookedRender = cookedRoundTrip.emitters.front().render;
    assert(cookedRender.materialMode == compiledRender.materialMode);
    assert(cookedRender.materialAssetGuid == compiledRender.materialAssetGuid);
    assert(cookedRender.depthMode == compiledRender.depthMode);
    assert(cookedRender.softIntersectionDistance == compiledRender.softIntersectionDistance);
    assert(cookedRender.depthBias == compiledRender.depthBias);
    assert(cookedRender.flipbookColumns == compiledRender.flipbookColumns);
    assert(cookedRender.flipbookRows == compiledRender.flipbookRows);
    assert(cookedRender.flipbookFrameCount == compiledRender.flipbookFrameCount);
    assert(cookedRender.flipbookFramesPerSecond == compiledRender.flipbookFramesPerSecond);
    assert(cookedRender.flipbookLoop == compiledRender.flipbookLoop);
    assert(cookedRender.flipbookRandomStart == compiledRender.flipbookRandomStart);
    assert(cookedRender.ringSegmentCount == compiledRender.ringSegmentCount);
    assert(cookedRender.ringStartAngleRadians == compiledRender.ringStartAngleRadians);
    assert(cookedRender.ringEndAngleRadians == compiledRender.ringEndAngleRadians);

    // Special output order is a program identity detail. Reordering source
    // entries must not change the canonical cooked bytes or source hash.
    VFX::VfxAssetData reorderedOutputSource = roundTripSource;
    std::reverse(reorderedOutputSource.emitters.front().specialOutputs.begin(),
                 reorderedOutputSource.emitters.front().specialOutputs.end());
    const VFX::VfxCompileResult reorderedOutputCompiled = VFX::CompileVfxAsset(reorderedOutputSource);
    assert(reorderedOutputCompiled.success);
    std::string reorderedOutputCooked{};
    assert(VFX::SerializeVfxProgram(reorderedOutputCompiled.program, reorderedOutputCooked, error));
    assert(reorderedOutputCompiled.program.sourceHash == compiled.program.sourceHash);
    assert(reorderedOutputCooked == cookedA);

    // Only the profile selected by the output type is active. Hidden profile
    // values must not make an otherwise usable output impossible to compile.
    VFX::VfxAssetData hiddenSourceProfile = roundTripSource;
    auto hiddenSourceWeapon = std::find_if(hiddenSourceProfile.emitters.front().specialOutputs.begin(),
                                           hiddenSourceProfile.emitters.front().specialOutputs.end(),
                                           [](const VFX::VfxSpecialOutputData& output) {
                                               return output.type == VFX::VfxSpecialOutputType::WeaponPathDistortion;
                                           });
    assert(hiddenSourceWeapon != hiddenSourceProfile.emitters.front().specialOutputs.end());
    hiddenSourceWeapon->screenImpulse.waveCount = VFX::kVfxScreenImpulseWaveCountMax + 1u;
    hiddenSourceWeapon->skinnedAfterimage.sampleCount = VFX::kVfxAfterimageSampleCountMax + 1u;
    assert(VFX::CompileVfxAsset(hiddenSourceProfile).success);

    VFX::VfxProgram hiddenProgramProfile = compiled.program;
    auto hiddenProgramWeapon = std::find_if(hiddenProgramProfile.emitters.front().specialOutputs.begin(),
                                            hiddenProgramProfile.emitters.front().specialOutputs.end(),
                                            [](const VFX::VfxSpecialOutputData& output) {
                                                return output.type == VFX::VfxSpecialOutputType::WeaponPathDistortion;
                                            });
    assert(hiddenProgramWeapon != hiddenProgramProfile.emitters.front().specialOutputs.end());
    hiddenProgramWeapon->screenImpulse.waveCount = VFX::kVfxScreenImpulseWaveCountMax + 1u;
    hiddenProgramWeapon->skinnedAfterimage.sampleCount = VFX::kVfxAfterimageSampleCountMax + 1u;
    assert(VFX::ValidateVfxProgram(hiddenProgramProfile, error));

    // Inactive depth fields stay finite serialized state, but their
    // hidden ranges must not reject an otherwise valid emitter.
    VFX::VfxAssetData hiddenParticlePresentation = roundTripSource;
    auto& hiddenParticle = hiddenParticlePresentation.emitters.front().renderCommon;
    hiddenParticle.depthMode = VFX::VfxParticleDepthMode::WorldOccluded;
    hiddenParticle.softIntersectionDistance = -2.0f;
    hiddenParticle.depthBias = -1.0f;
    assert(VFX::CompileVfxAsset(hiddenParticlePresentation).success);
    std::string hiddenParticleJson{};
    assert(VFX::SERIALIZATION::SerializeVfxSourceJson(hiddenParticlePresentation, hiddenParticleJson, error));
    VFX::VfxAssetData hiddenParticleRoundTrip{};
    assert(VFX::SERIALIZATION::ParseVfxSourceJson(hiddenParticleJson, hiddenParticleRoundTrip, error));
    assert(VFX::CompileVfxAsset(hiddenParticleRoundTrip).success);

    VFX::VfxAssetData invalidSoftPresentation = hiddenParticlePresentation;
    invalidSoftPresentation.emitters.front().renderCommon.depthMode = VFX::VfxParticleDepthMode::SoftIntersection;
    assert(!VFX::CompileVfxAsset(invalidSoftPresentation).success);
    VFX::VfxAssetData invalidContactPresentation = hiddenParticlePresentation;
    invalidContactPresentation.emitters.front().renderCommon.depthMode = VFX::VfxParticleDepthMode::ContactSurface;
    assert(!VFX::CompileVfxAsset(invalidContactPresentation).success);
    VFX::VfxAssetData zeroBlurSource = roundTripSource;
    auto zeroBlurWeapon =
        std::find_if(zeroBlurSource.emitters.front().specialOutputs.begin(),
                     zeroBlurSource.emitters.front().specialOutputs.end(), [](const VFX::VfxSpecialOutputData& output) {
                         return output.type == VFX::VfxSpecialOutputType::WeaponPathDistortion;
                     });
    assert(zeroBlurWeapon != zeroBlurSource.emitters.front().specialOutputs.end());
    zeroBlurWeapon->weaponPathDistortion.blur = 0.0f;
    const VFX::VfxCompileResult zeroBlurCompiled = VFX::CompileVfxAsset(zeroBlurSource);
    assert(zeroBlurCompiled.success);
    std::string zeroBlurPayload{};
    assert(VFX::SerializeVfxProgram(zeroBlurCompiled.program, zeroBlurPayload, error));
    VFX::VfxProgram zeroBlurRoundTrip{};
    assert(VFX::DeserializeVfxProgram(zeroBlurPayload, zeroBlurRoundTrip, error));
    const auto zeroBlurProgramWeapon = std::find_if(
        zeroBlurRoundTrip.emitters.front().specialOutputs.begin(),
        zeroBlurRoundTrip.emitters.front().specialOutputs.end(), [](const VFX::VfxSpecialOutputData& output) {
            return output.type == VFX::VfxSpecialOutputType::WeaponPathDistortion;
        });
    assert(zeroBlurProgramWeapon != zeroBlurRoundTrip.emitters.front().specialOutputs.end());
    assert(zeroBlurProgramWeapon->weaponPathDistortion.blur == 0.0f);

    constexpr std::array<VFX::VfxShapeType, 4u> shapeTypes{
        VFX::VfxShapeType::Box,
        VFX::VfxShapeType::Cylinder,
        VFX::VfxShapeType::Circle,
        VFX::VfxShapeType::Edge,
    };
    for (const VFX::VfxShapeType shapeType : shapeTypes) {
        VFX::VfxAssetData shapeSource = roundTripSource;
        shapeSource.emitters.front().shape.type = shapeType;
        shapeSource.emitters.front().shape.radius = VFX::MakeVfxFloatConstant(2.0f);
        const VFX::VfxCompileResult shapeCompiled = VFX::CompileVfxAsset(shapeSource);
        assert(shapeCompiled.success);
        std::string shapePayload{};
        assert(VFX::SerializeVfxProgram(shapeCompiled.program, shapePayload, error));
        VFX::VfxProgram shapeRoundTrip{};
        assert(VFX::DeserializeVfxProgram(shapePayload, shapeRoundTrip, error));
        const auto& shapeSimulation = shapeRoundTrip.emitters.front().simulation;
        assert(shapeSimulation.shape == static_cast<RENDER3D::PARTICLES::ParticleShape>(shapeType));
        assert(shapeSimulation.shapeBoxHalfExtents.x == 1.25f);
        assert(shapeSimulation.shapeCylinderHeight == 4.5f);
        assert(shapeSimulation.shapeCircleInnerRadius == 0.25f);
        assert(shapeSimulation.shapeCircleEmitOnEdge == 1u);
        assert(shapeSimulation.shapeEdgeLength == 7.5f);
    }

    VFX::VfxAssetData invalidShapeSource = roundTripSource;
    invalidShapeSource.emitters.front().shape.boxHalfExtents.x = -0.1f;
    assert(!VFX::CompileVfxAsset(invalidShapeSource).success);
    VFX::VfxAssetData invalidCircleSource = roundTripSource;
    invalidCircleSource.emitters.front().shape.type = VFX::VfxShapeType::Circle;
    invalidCircleSource.emitters.front().shape.radius = VFX::MakeVfxFloatConstant(0.2f);
    invalidCircleSource.emitters.front().shape.circleInnerRadius = 0.3f;
    assert(!VFX::CompileVfxAsset(invalidCircleSource).success);

    VFX::VfxProgram invalidShapeProgram = compiled.program;
    invalidShapeProgram.emitters.front().simulation.shapeBoxHalfExtents.x = -0.1f;
    assert(!VFX::ValidateVfxProgram(invalidShapeProgram, error));
    VFX::VfxProgram invalidCircleProgram = compiled.program;
    invalidCircleProgram.emitters.front().simulation.shape = RENDER3D::PARTICLES::ParticleShape::Circle;
    invalidCircleProgram.emitters.front().simulation.shapeRadius.minimum = 0.2f;
    invalidCircleProgram.emitters.front().simulation.shapeCircleInnerRadius = 0.3f;
    assert(!VFX::ValidateVfxProgram(invalidCircleProgram, error));

    VFX::VfxProgram staleProgram = compiled.program;
    staleProgram.version = 6u;
    assert(!VFX::ValidateVfxProgram(staleProgram, error));

    VFX::VfxProgram unorderedBurstProgram = compiled.program;
    assert(unorderedBurstProgram.emitters.front().burstTimeline.size() >= 2u);
    std::swap(unorderedBurstProgram.emitters.front().burstTimeline[0],
              unorderedBurstProgram.emitters.front().burstTimeline[1]);
    assert(!VFX::ValidateVfxProgram(unorderedBurstProgram, error));
    std::string unorderedBurstPayload{};
    assert(!VFX::SerializeVfxProgram(unorderedBurstProgram, unorderedBurstPayload, error));

    VFX::VfxAssetData directTextureSource = VFX::MakeDefaultVfxAssetData();
    directTextureSource.name = "Direct Texture";
    directTextureSource.emitters.front().renderCommon.colorTextureAssetGuid =
        AssetGuid{"ffeeddccbbaa99887766554433221100"};
    VFX::VfxCompileResult directTextureCompiled = VFX::CompileVfxAsset(directTextureSource);
    assert(directTextureCompiled.success);
    assert(directTextureCompiled.program.dependencyGuids.size() == 1u);
    directTextureSource.emitters.front().renderCommon.colorTextureAssetGuid = AssetGuid{"engine.vfx.texture.soft_disc"};
    directTextureCompiled = VFX::CompileVfxAsset(directTextureSource);
    assert(directTextureCompiled.success);
    assert(directTextureCompiled.program.dependencyGuids.empty());
    assert(directTextureCompiled.program.emitters.front().render.colorTextureAssetGuid ==
           "engine.vfx.texture.soft_disc");

    VFX::VfxAssetData fixedCapacitySource = VFX::MakeDefaultVfxAssetData();
    fixedCapacitySource.name = "Fixed Capacity";
    fixedCapacitySource.emitters.front().spawn.capacityMode = VFX::VfxParticleCapacityMode::Fixed;
    fixedCapacitySource.emitters.front().spawn.maximumParticles = 777u;
    const VFX::VfxCompileResult fixedCapacityCompiled = VFX::CompileVfxAsset(fixedCapacitySource);
    assert(fixedCapacityCompiled.success);
    assert(fixedCapacityCompiled.program.emitters.front().simulation.capacity == 777u);

    std::string damagedCooked = cookedA;
    damagedCooked.front() ^= static_cast<char>(0x01);
    cookedRoundTrip.programId = "transaction sentinel";
    assert(!VFX::DeserializeVfxProgram(damagedCooked, cookedRoundTrip, error));
    assert(cookedRoundTrip.programId == "transaction sentinel");
    VFX::VfxProgram cyclicProgram = compiled.program;
    cyclicProgram.emitters.front().parentEmitterIndex = 0u;
    assert(!VFX::SerializeVfxProgram(cyclicProgram, damagedCooked, error));

    const size_t inputCountOffset = sizeof(uint32_t) * 3u + sizeof(uint64_t) + sizeof(uint32_t) +
                                    compiled.program.programId.size() + sizeof(uint32_t) +
                                    compiled.program.name.size() + sizeof(float) * 2u + sizeof(uint8_t);
    assert(inputCountOffset + sizeof(uint32_t) <= cookedA.size());
    std::string hugeCountPayload = cookedA;
    const uint32_t hugeCount = (std::numeric_limits<uint32_t>::max)();
    std::memcpy(hugeCountPayload.data() + inputCountOffset, &hugeCount, sizeof(hugeCount));
    cookedRoundTrip.programId = "count transaction sentinel";
    assert(!VFX::DeserializeVfxProgram(hugeCountPayload, cookedRoundTrip, error));
    assert(cookedRoundTrip.programId == "count transaction sentinel");

    const std::filesystem::path cookedPath = std::filesystem::temp_directory_path() / "hikari-native-vfx-smoke.hvfxc";
    std::error_code ignored{};
    std::filesystem::remove(cookedPath, ignored);
    assert(VFX::WriteHvfxcFile(cookedPath, compiled.program, error));
    VFX::VfxProgram fileRoundTrip{};
    assert(VFX::ReadHvfxcFile(cookedPath, fileRoundTrip, error));
    assert(fileRoundTrip.programId == compiled.program.programId);

    const std::filesystem::path staleVersionPath =
        std::filesystem::temp_directory_path() / "hikari-native-vfx-stale-v5.hvfxc";
    std::filesystem::remove(staleVersionPath, ignored);
    assert(std::filesystem::copy_file(cookedPath, staleVersionPath, std::filesystem::copy_options::overwrite_existing));
    {
        std::fstream staleFile(staleVersionPath, std::ios::in | std::ios::out | std::ios::binary);
        assert(staleFile.is_open());
        const uint32_t staleVersion = 6u;
        staleFile.seekp(4);
        staleFile.write(reinterpret_cast<const char*>(&staleVersion), sizeof(staleVersion));
        assert(static_cast<bool>(staleFile));
    }
    fileRoundTrip.programId = "stale v6 sentinel";
    assert(!VFX::ReadHvfxcFile(staleVersionPath, fileRoundTrip, error));
    assert(fileRoundTrip.programId == "stale v6 sentinel");
    std::filesystem::remove(staleVersionPath, ignored);
    std::filesystem::remove(cookedPath, ignored);

    const std::filesystem::path hugeHeaderPath =
        std::filesystem::temp_directory_path() / "hikari-native-vfx-huge-header.hvfxc";
    std::filesystem::remove(hugeHeaderPath, ignored);
    std::array<std::byte, 24u> hugeHeader{};
    const uint32_t hvfxcMagic = 0x58465648u;
    const uint32_t hvfxcVersion = VFX::kHvfxcFormatVersion;
    const uint32_t hvfxcHeaderSize = 24u;
    std::memcpy(hugeHeader.data(), &hvfxcMagic, sizeof(hvfxcMagic));
    std::memcpy(hugeHeader.data() + 4u, &hvfxcVersion, sizeof(hvfxcVersion));
    std::memcpy(hugeHeader.data() + 8u, &hvfxcHeaderSize, sizeof(hvfxcHeaderSize));
    std::memcpy(hugeHeader.data() + 12u, &hugeCount, sizeof(hugeCount));
    {
        std::ofstream hugeHeaderFile(hugeHeaderPath, std::ios::binary | std::ios::trunc);
        assert(hugeHeaderFile.is_open());
        hugeHeaderFile.write(reinterpret_cast<const char*>(hugeHeader.data()),
                             static_cast<std::streamsize>(hugeHeader.size()));
        assert(static_cast<bool>(hugeHeaderFile));
    }
    fileRoundTrip.programId = "file transaction sentinel";
    assert(!VFX::ReadHvfxcFile(hugeHeaderPath, fileRoundTrip, error));
    assert(fileRoundTrip.programId == "file transaction sentinel");
    std::filesystem::remove(hugeHeaderPath, ignored);

    VFX::VfxAssetData invalid = source;
    invalid.emitters.front().scale.value.curve.x.keys.resize(RENDER3D::PARTICLES::kMaxParticleCurveKeys + 1u,
                                                             {"duplicate", 0.5f, 1.0f, 0.0f, 0.0f});
    const VFX::VfxCompileResult invalidResult = VFX::CompileVfxAsset(invalid);
    assert(!invalidResult.success);
    assert(HasDiagnostic(invalidResult, "Vfx.CurveKeyLimit"));

    // 向きモードは P5 の Look At Line / FaceCameraPosition 追加で 0..5 が有効。
    // 範囲外の値だけが拒否される。
    VFX::VfxAssetData invalidOrientation = source;
    invalidOrientation.emitters.front().renderShape.billboardMode = static_cast<VFX::VfxBillboardMode>(9u);
    const VFX::VfxCompileResult invalidOrientationResult = VFX::CompileVfxAsset(invalidOrientation);
    assert(!invalidOrientationResult.success);
    assert(HasDiagnostic(invalidOrientationResult, "Vfx.InvalidBillboardMode"));

    VFX::VfxProgram invalidProgramOrientation = compiled.program;
    invalidProgramOrientation.emitters.front().render.billboardMode =
        static_cast<RENDER3D::PARTICLES::ParticleBillboardMode>(9u);
    std::string invalidProgramOrientationError{};
    assert(!VFX::ValidateVfxProgram(invalidProgramOrientation, invalidProgramOrientationError));
    assert(!invalidProgramOrientationError.empty());

    VFX::VfxAssetData invalidFlipbook = source;
    invalidFlipbook.emitters.front().renderCommon.flipbookFrameCount = 65u;
    const VFX::VfxCompileResult invalidFlipbookResult = VFX::CompileVfxAsset(invalidFlipbook);
    assert(!invalidFlipbookResult.success);
    assert(HasDiagnostic(invalidFlipbookResult, "Vfx.InvalidFlipbook"));

    VFX::VfxAssetData tooManyColorStops = source;
    auto& excessiveStops = tooManyColorStops.emitters.front().color.value.gradient.stops;
    excessiveStops.clear();
    for (uint32_t index = 0u; index <= RENDER3D::PARTICLES::kMaxParticleColorStops; ++index) {
        excessiveStops.push_back(
            {"color.limit." + std::to_string(index),
             static_cast<float>(index) / static_cast<float>(RENDER3D::PARTICLES::kMaxParticleColorStops),
             {1.0f, 1.0f, 1.0f, 1.0f}});
    }
    const VFX::VfxCompileResult excessiveColorResult = VFX::CompileVfxAsset(tooManyColorStops);
    assert(!excessiveColorResult.success);
    assert(HasDiagnostic(excessiveColorResult, "Vfx.ColorStopLimit"));

    VFX::VfxAssetData invalidModuleAddress = source;
    invalidModuleAddress.emitters.front().inputLinks.front().target.moduleId = "module.missing";
    const VFX::VfxCompileResult invalidModuleResult = VFX::CompileVfxAsset(invalidModuleAddress);
    assert(!invalidModuleResult.success);
    assert(HasDiagnostic(invalidModuleResult, "Vfx.UnknownPropertyAddress"));

    VFX::VfxAssetData invalidPropertyAddress = source;
    invalidPropertyAddress.emitters.front().inputLinks.front().target.propertyId = "property.missing";
    const VFX::VfxCompileResult invalidPropertyResult = VFX::CompileVfxAsset(invalidPropertyAddress);
    assert(!invalidPropertyResult.success);
    assert(HasDiagnostic(invalidPropertyResult, "Vfx.UnknownPropertyAddress"));

    VFX::VfxAssetData duplicateInputTarget = source;
    VFX::VfxInputLinkData duplicateTargetLink = duplicateInputTarget.emitters.front().inputLinks.front();
    duplicateTargetLink.id += ".duplicate-target";
    duplicateInputTarget.emitters.front().inputLinks.push_back(std::move(duplicateTargetLink));
    const VFX::VfxCompileResult duplicateInputTargetResult = VFX::CompileVfxAsset(duplicateInputTarget);
    assert(!duplicateInputTargetResult.success);
    assert(HasDiagnostic(duplicateInputTargetResult, "Vfx.DuplicateInputTarget"));

    VFX::VfxAssetData invalidInputType = source;
    invalidInputType.inputs.front().type = VFX::VfxValueType::Bool;
    invalidInputType.inputs.front().defaultValue = VFX::MakeDefaultVfxValue(VFX::VfxValueType::Bool);
    const VFX::VfxCompileResult invalidInputTypeResult = VFX::CompileVfxAsset(invalidInputType);
    assert(!invalidInputTypeResult.success);
    assert(HasDiagnostic(invalidInputTypeResult, "Vfx.InputTypeMismatch"));

    VFX::VfxAssetData integerScalarInput = source;
    integerScalarInput.inputs.front().type = VFX::VfxValueType::Int;
    integerScalarInput.inputs.front().defaultValue = VFX::MakeDefaultVfxValue(VFX::VfxValueType::Int);
    integerScalarInput.inputs.front().defaultValue.integer = 2;
    const VFX::VfxCompileResult integerScalarResult = VFX::CompileVfxAsset(integerScalarInput);
    assert(integerScalarResult.success);
    assert(integerScalarResult.program.emitters.front().inputLinks.front().inputType == VFX::VfxValueType::Int);
    std::string compatibilityError{};
    assert(VFX::ValidateVfxProgram(integerScalarResult.program, compatibilityError));

    VFX::VfxAssetData float4ColorInput = source;
    float4ColorInput.inputs.front().type = VFX::VfxValueType::Float4;
    float4ColorInput.inputs.front().defaultValue = VFX::MakeDefaultVfxValue(VFX::VfxValueType::Float4);
    float4ColorInput.inputs.front().defaultValue.numeric = {0.25f, 0.5f, 0.75f, 1.0f};
    VFX::VfxEmitterData& float4ColorEmitter = float4ColorInput.emitters.front();
    float4ColorEmitter.inputLinks.front().target.moduleId = float4ColorEmitter.color.id;
    float4ColorEmitter.inputLinks.front().target.propertyId = "valueMultiplier";
    const VFX::VfxCompileResult float4ColorResult = VFX::CompileVfxAsset(float4ColorInput);
    assert(float4ColorResult.success);
    const VFX::VfxProgramInputLink& float4ColorLink = float4ColorResult.program.emitters.front().inputLinks.front();
    assert(float4ColorLink.inputType == VFX::VfxValueType::Float4);
    assert(float4ColorLink.semantic == VFX::VfxRuntimeBindingSemantic::ColorMultiplier);
    compatibilityError.clear();
    assert(VFX::ValidateVfxProgram(float4ColorResult.program, compatibilityError));

    const auto verifyColorModeRoundTrip = [&source](VFX::VfxAnimatedColorMode sourceMode,
                                                    RENDER3D::PARTICLES::ParticleColorMode programMode) {
        VFX::VfxAssetData modeAsset = source;
        VFX::VfxAnimatedColorData& color = modeAsset.emitters.front().color.value;
        color.mode = sourceMode;
        switch (sourceMode) {
        case VFX::VfxAnimatedColorMode::Fixed: color.fixed = VFX::MakeVfxColorConstant({0.2f, 0.3f, 0.4f, 1.0f}); break;
        case VFX::VfxAnimatedColorMode::RandomRange:
            color.fixed = {
                VFX::VfxDistributionMode::RandomRange, {0.1f, 0.2f, 0.3f, 0.4f}, {0.5f, 0.6f, 0.7f, 0.8f}, 31u};
            break;
        case VFX::VfxAnimatedColorMode::Easing:
            color.easing.start = VFX::MakeVfxColorConstant({1.0f, 0.0f, 0.0f, 1.0f});
            color.easing.middle = VFX::MakeVfxColorConstant({0.0f, 1.0f, 0.0f, 1.0f});
            color.easing.end = VFX::MakeVfxColorConstant({0.0f, 0.0f, 1.0f, 0.0f});
            color.easing.useMiddle = true;
            color.easing.function = VFX::VfxEasingFunction::EaseInOut;
            break;
        case VFX::VfxAnimatedColorMode::Curve:
            color.curve.r = VFX::MakeVfxConstantCurve("mode.color.r", 1.0f);
            color.curve.g = VFX::MakeVfxConstantCurve("mode.color.g", 0.5f);
            color.curve.b = VFX::MakeVfxConstantCurve("mode.color.b", 0.25f);
            color.curve.a = VFX::MakeVfxConstantCurve("mode.color.a", 1.0f);
            color.curve.a.keys.back().value = 0.0f;
            color.curve.a.randomOffset = {VFX::VfxDistributionMode::RandomRange, -0.05f, 0.05f, 41u};
            break;
        case VFX::VfxAnimatedColorMode::Gradient: break;
        }
        std::string json{};
        std::string localError{};
        assert(VFX::SERIALIZATION::SerializeVfxSourceJson(modeAsset, json, localError));
        VFX::VfxAssetData sourceRoundTrip{};
        assert(VFX::SERIALIZATION::ParseVfxSourceJson(json, sourceRoundTrip, localError));
        const VFX::VfxCompileResult modeCompiled = VFX::CompileVfxAsset(sourceRoundTrip);
        assert(modeCompiled.success);
        assert(modeCompiled.program.emitters.front().simulation.color.mode == programMode);
        std::string payload{};
        assert(VFX::SerializeVfxProgram(modeCompiled.program, payload, localError));
        VFX::VfxProgram programRoundTrip{};
        assert(VFX::DeserializeVfxProgram(payload, programRoundTrip, localError));
        assert(programRoundTrip.emitters.front().simulation.color.mode == programMode);
    };
    verifyColorModeRoundTrip(VFX::VfxAnimatedColorMode::Fixed, RENDER3D::PARTICLES::ParticleColorMode::Fixed);
    verifyColorModeRoundTrip(VFX::VfxAnimatedColorMode::RandomRange,
                             RENDER3D::PARTICLES::ParticleColorMode::RandomRange);
    verifyColorModeRoundTrip(VFX::VfxAnimatedColorMode::Easing, RENDER3D::PARTICLES::ParticleColorMode::Easing);
    verifyColorModeRoundTrip(VFX::VfxAnimatedColorMode::Curve, RENDER3D::PARTICLES::ParticleColorMode::Curve);
    verifyColorModeRoundTrip(VFX::VfxAnimatedColorMode::Gradient, RENDER3D::PARTICLES::ParticleColorMode::Gradient);

    VFX::VfxAssetData overBudget = source;
    overBudget.emitters.front().spawn.maximumParticles = RENDER3D::PARTICLES::kGpuParticlePoolCapacity + 1u;
    const VFX::VfxCompileResult overBudgetResult = VFX::CompileVfxAsset(overBudget);
    assert(!overBudgetResult.success);
    assert(HasDiagnostic(overBudgetResult, "Vfx.InvalidSpawnModule"));

    VfxPlayerComponent component{};
    VfxPlayerSlot& firstSlot = component.AddSlot("Primary");
    firstSlot.effectAssetGuid.value = "ffeeddccbbaa99887766554433221100";
    firstSlot.variableBindings.push_back({"input.speed", "variable.speed"});
    const VfxSlotId firstId = firstSlot.id;
    VfxPlayerSlot* duplicate = component.DuplicateSlot(firstId);
    assert(duplicate != nullptr && duplicate->id != firstId);
    nlohmann::json componentJson{};
    component.Serialize(componentJson);
    componentJson["slots"][1]["id"] = firstId.value;
    VfxPlayerComponent componentRoundTrip{};
    componentRoundTrip.Deserialize(componentJson);
    assert(componentRoundTrip.GetSlots().size() == 2u);
    assert(componentRoundTrip.GetSlots()[0].id != componentRoundTrip.GetSlots()[1].id);

    nlohmann::json stableComponentJson{};
    componentRoundTrip.Serialize(stableComponentJson);
    nlohmann::json wrongSlotId = stableComponentJson;
    wrongSlotId["slots"][0]["id"] = "not-an-id";
    AssertVfxComponentDecodeRejected(componentRoundTrip, wrongSlotId, ".id must be a non-negative integer");
    nlohmann::json wrongEffectGuid = stableComponentJson;
    wrongEffectGuid["slots"][0]["effectAssetGuid"] = 17;
    AssertVfxComponentDecodeRejected(componentRoundTrip, wrongEffectGuid, ".effectAssetGuid must be a string");
    nlohmann::json wrongOffset = stableComponentJson;
    wrongOffset["slots"][0]["offset"] = "not-a-transform";
    AssertVfxComponentDecodeRejected(componentRoundTrip, wrongOffset, ".offset must be an object");
    nlohmann::json wrongPosition = stableComponentJson;
    wrongPosition["slots"][0]["offset"]["position"] = {0.0f, "not-a-number", 0.0f};
    AssertVfxComponentDecodeRejected(componentRoundTrip, wrongPosition,
                                     ".offset.position must be an array of three finite numbers");
    nlohmann::json wrongBindings = stableComponentJson;
    wrongBindings["slots"][0]["variableBindings"] = {
        {"inputId", "input.speed"},
        {"variableId", "variable.speed"},
    };
    AssertVfxComponentDecodeRejected(componentRoundTrip, wrongBindings, ".variableBindings must be an array");
    nlohmann::json wrongBindingId = stableComponentJson;
    wrongBindingId["slots"][0]["variableBindings"][0]["inputId"] = 5;
    AssertVfxComponentDecodeRejected(componentRoundTrip, wrongBindingId, ".inputId must be a string");
    assert(componentRoundTrip.RequestPlay(componentRoundTrip.GetSlots().front().id));
    nlohmann::json wrongBindingVariable = stableComponentJson;
    wrongBindingVariable["slots"][0]["variableBindings"][0]["variableId"] = false;
    componentRoundTrip.Deserialize(wrongBindingVariable);
    nlohmann::json afterVoidDeserialize{};
    componentRoundTrip.Serialize(afterVoidDeserialize);
    assert(afterVoidDeserialize == stableComponentJson);
    const auto preservedCommands = componentRoundTrip.ConsumePendingCommands();
    assert(preservedCommands.size() == 1u);

    const auto authoringCommands = BuildVfxPlayerComponentAuthoringCommands();
    const auto* configureCommand = FindCommand(authoringCommands, "slot.configure");
    const auto* bindCommand = FindCommand(authoringCommands, "slot.bind_variable");
    const auto* unbindCommand = FindCommand(authoringCommands, "slot.unbind_variable");
    assert(configureCommand != nullptr && configureCommand->execute);
    assert(bindCommand != nullptr && bindCommand->execute);
    assert(unbindCommand != nullptr && unbindCommand->execute);
    VfxPlayerSlot* authoringSlotPointer = componentRoundTrip.FindSlot(componentRoundTrip.GetSlots().front().id);
    assert(authoringSlotPointer != nullptr);
    VfxPlayerSlot& authoringSlot = *authoringSlotPointer;
    const std::string originalName = authoringSlot.name;
    const AuthoringCommandResult invalidConfigure =
        configureCommand->execute(componentRoundTrip, {
                                                          {"slotId", authoringSlot.id.value},
                                                          {"values",
                                                           {
                                                               {"name", "Must Not Commit"},
                                                               {"spawnTarget", "Unknown"},
                                                           }},
                                                      });
    assert(!invalidConfigure.ok);
    assert(authoringSlot.name == originalName);
    const AuthoringCommandResult wrongTypeConfigure =
        configureCommand->execute(componentRoundTrip, {
                                                          {"slotId", authoringSlot.id.value},
                                                          {"values",
                                                           {
                                                               {"name", 42},
                                                           }},
                                                      });
    assert(!wrongTypeConfigure.ok);
    assert(authoringSlot.name == originalName);
    const AuthoringCommandResult unknownFieldConfigure =
        configureCommand->execute(componentRoundTrip, {
                                                          {"slotId", authoringSlot.id.value},
                                                          {"values",
                                                           {
                                                               {"unknownField", true},
                                                           }},
                                                      });
    assert(!unknownFieldConfigure.ok);
    const AuthoringCommandResult unknownOffsetConfigure =
        configureCommand->execute(componentRoundTrip, {
                                                          {"slotId", authoringSlot.id.value},
                                                          {"values",
                                                           {
                                                               {"offset",
                                                                {
                                                                    {"unknownOffsetField", {0.0f, 0.0f, 0.0f}},
                                                                }},
                                                           }},
                                                      });
    assert(!unknownOffsetConfigure.ok);
    const AuthoringCommandResult nonFiniteOffsetConfigure = configureCommand->execute(
        componentRoundTrip, {
                                {"slotId", authoringSlot.id.value},
                                {"values",
                                 {
                                     {"offset",
                                      {
                                          {"position", {(std::numeric_limits<float>::infinity)(), 0.0f, 0.0f}},
                                      }},
                                 }},
                            });
    assert(!nonFiniteOffsetConfigure.ok);
    const AuthoringCommandResult bindResult =
        bindCommand->execute(componentRoundTrip, {
                                                     {"slotId", authoringSlot.id.value},
                                                     {"inputId", "input.tint"},
                                                     {"variableId", "variable.tint"},
                                                 });
    assert(bindResult.ok);
    assert(authoringSlot.variableBindings.size() == 2u);
    const AuthoringCommandResult unbindResult =
        unbindCommand->execute(componentRoundTrip, {
                                                       {"slotId", authoringSlot.id.value},
                                                       {"inputId", "input.tint"},
                                                   });
    assert(unbindResult.ok);
    assert(authoringSlot.variableBindings.size() == 1u);

    BINDING::Catalog bindingCatalog{};
    BINDING::VariableDefinition speed{};
    speed.id = "variable.speed";
    speed.name = "Speed";
    speed.defaultValue = BINDING::MakeDefaultValue(BINDING::ValueType::Float);
    speed.defaultValue.numeric.x = 2.0f;
    bindingCatalog.variables.push_back(speed);
    BINDING::VariableDefinition enabledVariable{};
    enabledVariable.id = "variable.enabled";
    enabledVariable.name = "Enabled";
    enabledVariable.defaultValue = BINDING::MakeDefaultValue(BINDING::ValueType::Bool);
    bindingCatalog.variables.push_back(enabledVariable);
    BINDING::EventDefinition impact{};
    impact.id = "event.impact";
    impact.name = "Impact";
    impact.hasPayload = true;
    impact.payloadType = BINDING::ValueType::Float3;
    bindingCatalog.events.push_back(impact);
    BINDING::EventDefinition scalarEvent{};
    scalarEvent.id = "event.scalar";
    scalarEvent.name = "Scalar";
    scalarEvent.hasPayload = true;
    scalarEvent.payloadType = BINDING::ValueType::Float;
    bindingCatalog.events.push_back(scalarEvent);
    BINDING::RuntimeBindingStore bindings{};
    assert(bindings.Initialize(bindingCatalog));

    auto authoringDescriptor = std::make_unique<VfxAssetDescriptor>();
    authoringDescriptor->id.value = "ffeeddccbbaa99887766554433221100";
    authoringDescriptor->type = AssetType::VfxEffect;
    authoringDescriptor->program = std::make_shared<VFX::VfxProgram>(compiled.program);
    assert(assetRegistry.RegisterDescriptor(std::move(authoringDescriptor)));
    auto uncompiledDescriptor = std::make_unique<VfxAssetDescriptor>();
    uncompiledDescriptor->id.value = "0123456789abcdef0123456789abcdef";
    uncompiledDescriptor->type = AssetType::VfxEffect;
    assert(assetRegistry.RegisterDescriptor(std::move(uncompiledDescriptor)));
    SceneDocument authoringDocument{};
    SceneObjectData fixedObject{};
    fixedObject.id.value = 42u;
    fixedObject.name = "Fixed Target";
    authoringDocument.objects.push_back(std::move(fixedObject));
    WorldServiceRegistry worldServices{};
    assert(worldServices.Register(bindings));
    const InspectorContext authoringContext{&assetRegistry, nullptr, &authoringDocument,
                                            &worldServices, nullptr, nullptr};
    AuthoringCommandResult authoringValidation =
        ValidateVfxPlayerComponentAuthoring(componentRoundTrip, authoringContext);
    assert(authoringValidation.ok);
    VfxPlayerSlot* validatedSlot = componentRoundTrip.FindSlot(componentRoundTrip.GetSlots().front().id);
    assert(validatedSlot != nullptr);
    const VfxPlayerSlot validSlot = *validatedSlot;

    validatedSlot->offset.position.x = (std::numeric_limits<float>::infinity)();
    authoringValidation = ValidateVfxPlayerComponentAuthoring(componentRoundTrip, authoringContext);
    assert(!authoringValidation.ok && authoringValidation.code == "NON_FINITE_VFX_OFFSET");
    *validatedSlot = validSlot;

    validatedSlot->effectAssetGuid.value = "0123456789abcdef0123456789abcdef";
    authoringValidation = ValidateVfxPlayerComponentAuthoring(componentRoundTrip, authoringContext);
    assert(!authoringValidation.ok && authoringValidation.code == "VFX_PROGRAM_UNAVAILABLE");
    *validatedSlot = validSlot;

    validatedSlot->variableBindings.front().variableId = "variable.enabled";
    authoringValidation = ValidateVfxPlayerComponentAuthoring(componentRoundTrip, authoringContext);
    assert(!authoringValidation.ok && authoringValidation.code == "RUNTIME_VARIABLE_TYPE_MISMATCH");
    *validatedSlot = validSlot;

    validatedSlot->spawnTarget = VfxSpawnTarget::EventPayloadPosition;
    validatedSlot->triggerEventId = scalarEvent.id;
    authoringValidation = ValidateVfxPlayerComponentAuthoring(componentRoundTrip, authoringContext);
    assert(!authoringValidation.ok && authoringValidation.code == "RUNTIME_EVENT_PAYLOAD_MISMATCH");
    validatedSlot->triggerEventId = impact.id;
    authoringValidation = ValidateVfxPlayerComponentAuthoring(componentRoundTrip, authoringContext);
    assert(authoringValidation.ok);
    *validatedSlot = validSlot;

    validatedSlot->spawnTarget = VfxSpawnTarget::FixedObject;
    validatedSlot->fixedObjectId.value = 999u;
    authoringValidation = ValidateVfxPlayerComponentAuthoring(componentRoundTrip, authoringContext);
    assert(!authoringValidation.ok && authoringValidation.code == "SCENE_OBJECT_NOT_FOUND");
    validatedSlot->fixedObjectId.value = 42u;
    authoringValidation = ValidateVfxPlayerComponentAuthoring(componentRoundTrip, authoringContext);
    assert(authoringValidation.ok);
    *validatedSlot = validSlot;

    authoringValidation = ValidateVfxPlayerComponentAuthoring(componentRoundTrip, InspectorContext{});
    assert(!authoringValidation.ok && authoringValidation.code == "VFX_ASSET_CONTEXT_UNAVAILABLE");

    bindings.BeginFrame(7u);
    BINDING::Value position = BINDING::MakeDefaultValue(BINDING::ValueType::Float3);
    position.numeric = {4.0f, 5.0f, 6.0f, 0.0f};
    assert(bindings.Emit(impact.id, {}, &position));

    BINDING::RuntimeBindingStore eventBudgetStore{};
    assert(eventBudgetStore.Initialize(bindingCatalog));
    eventBudgetStore.BeginFrame(100u);
    for (size_t index = 0u; index < BINDING::kMaximumRuntimeBindingEventsPerFrame; ++index) {
        assert(eventBudgetStore.Emit(impact.id, {}, &position));
    }
    assert(!eventBudgetStore.Emit(impact.id, {}, &position));
    assert(eventBudgetStore.GetEventAdmissionStats().acceptedThisFrame ==
           BINDING::kMaximumRuntimeBindingEventsPerFrame);
    assert(eventBudgetStore.GetEventAdmissionStats().budgetRejectionsThisFrame == 1u);
    assert(eventBudgetStore.GetEventAdmissionStats().budgetRejectionsTotal == 1u);
    eventBudgetStore.BeginFrame(100u);
    assert(!eventBudgetStore.Emit(impact.id, {}, &position));
    assert(eventBudgetStore.GetEventAdmissionStats().budgetRejectionsThisFrame == 2u);
    eventBudgetStore.BeginFrame(101u);
    for (size_t index = 0u; index < BINDING::kMaximumRuntimeBindingEventsPerFrame; ++index) {
        assert(eventBudgetStore.Emit(impact.id, {}, &position));
    }
    assert(eventBudgetStore.GetEvents().size() == BINDING::kMaximumRuntimeBindingEventsPerFrame * 2u);
    eventBudgetStore.BeginFrame(102u);
    assert(eventBudgetStore.GetEvents().size() == BINDING::kMaximumRuntimeBindingEventsPerFrame);

    VfxPlayerSlot bindingSlot{};
    bindingSlot.id = {1u};
    bindingSlot.effectAssetGuid.value = "ffeeddccbbaa99887766554433221100";
    bindingSlot.spawnTarget = VfxSpawnTarget::EventPayloadPosition;
    bindingSlot.offset.position = {1.0f, 0.0f, 0.0f};
    bindingSlot.variableBindings.push_back({"input.speed", "variable.speed"});
    VFX::VfxBindingResolveContext resolveContext{};
    resolveContext.ownerObject = {3u, 2u};
    resolveContext.event = &bindings.GetEvents().front();
    resolveContext.variables = bindings.PublishSnapshot();
    VFX::VfxPlayRequest resolvedRequest{};
    VFX::VfxResolvedTarget resolvedTarget{};
    std::vector<VFX::VfxPlaybackDiagnostic> diagnostics{};
    assert(VFX::BuildVfxPlayRequest(bindingSlot, resolveContext, resolvedRequest, resolvedTarget, diagnostics));
    assert(resolvedRequest.transform.position.x == 5.0f);
    assert(resolvedRequest.transform.position.y == 5.0f);
    assert(resolvedRequest.parameters.size() == 1u);
    assert(resolvedRequest.parameters.front().value.numeric.x == 2.0f);

    auto program = std::make_shared<VFX::VfxProgram>(compiled.program);
    VFX::VfxPlaybackService serviceA{};
    VFX::VfxPlaybackService serviceB{};
    assert(serviceA.GetPlaybackNamespace() != 0u);
    assert(serviceB.GetPlaybackNamespace() != 0u);
    assert(serviceA.GetPlaybackNamespace() != serviceB.GetPlaybackNamespace());
    VFX::VfxPlayRequest play{};
    play.assetGuid.value = "ffeeddccbbaa99887766554433221100";
    play.program = program;
    play.parameters = resolvedRequest.parameters;

    VFX::VfxPlaybackService invalidParameterService{};
    VFX::VfxPlayRequest invalidParameterPlay = play;
    invalidParameterPlay.parameters.front().inputId = "input.missing";
    const VFX::VfxPlayResult invalidParameterResult = invalidParameterService.PlayDetailed(invalidParameterPlay);
    assert(!invalidParameterResult.IsAccepted());
    assert(invalidParameterService.GetAdmissionStats().activeInstances == 0u);
    assert(std::any_of(invalidParameterResult.diagnostics.begin(), invalidParameterResult.diagnostics.end(),
                       [](const VFX::VfxPlaybackDiagnostic& diagnostic) {
                           return diagnostic.code == "Vfx.InvalidParameterOverrides";
                       }));

    assert(program->emitters.size() == 1u);
    VFX::VfxPlaybackService admissionService{};
    VFX::VfxPlaybackHandle firstAdmissionHandle{};
    for (size_t index = 0u; index < VFX::kMaximumActiveVfxPlaybackInstances; ++index) {
        const VFX::VfxPlayResult admitted = admissionService.PlayDetailed(play);
        assert(admitted.IsAccepted());
        if (index == 0u) {
            firstAdmissionHandle = admitted.handle;
        }
    }
    const VFX::VfxPlayResult budgetRejected = admissionService.PlayDetailed(play);
    assert(!budgetRejected.IsAccepted());
    assert(std::any_of(
        budgetRejected.diagnostics.begin(), budgetRejected.diagnostics.end(),
        [](const VFX::VfxPlaybackDiagnostic& diagnostic) { return diagnostic.code == "Vfx.PlaybackBudgetExceeded"; }));
    assert(admissionService.GetAdmissionStats().activeInstances == VFX::kMaximumActiveVfxPlaybackInstances);
    assert(admissionService.GetAdmissionStats().activeEmitterInstances == VFX::kMaximumActiveVfxPlaybackEmitters);
    assert(admissionService.GetAdmissionStats().budgetRejections == 1u);
    admissionService.Update(19u, 0.0f);
    assert(admissionService.GetRenderInput().emitters.size() == VFX::kMaximumActiveVfxPlaybackEmitters);
    assert(admissionService.Stop(firstAdmissionHandle));
    assert(admissionService.GetAdmissionStats().activeInstances == VFX::kMaximumActiveVfxPlaybackInstances - 1u);
    assert(!admissionService.PlayDetailed(play).IsAccepted());
    admissionService.Update(20u, 0.0f);
    assert(admissionService.PlayDetailed(play).IsAccepted());

    const auto assertDirectProgramRejected = [&play](VFX::VfxProgram malformedProgram) {
        std::string validationError{};
        assert(!VFX::ValidateVfxProgram(malformedProgram, validationError));
        assert(!validationError.empty());
        VFX::VfxPlaybackService malformedService{};
        VFX::VfxPlayRequest malformedPlay = play;
        malformedPlay.program = std::make_shared<VFX::VfxProgram>(std::move(malformedProgram));
        const VFX::VfxPlayResult result = malformedService.PlayDetailed(malformedPlay);
        assert(!result.IsAccepted());
        assert(!result.diagnostics.empty());
    };

    VFX::VfxProgram invalidParentProgram = compiled.program;
    invalidParentProgram.emitters.front().parentEmitterIndex =
        static_cast<uint32_t>(invalidParentProgram.emitters.size());
    assertDirectProgramRejected(std::move(invalidParentProgram));

    VFX::VfxProgram cyclicDirectProgram = compiled.program;
    cyclicDirectProgram.emitters.push_back(cyclicDirectProgram.emitters.front());
    cyclicDirectProgram.emitters[0].sourceEmitterId += ".cycle-a";
    cyclicDirectProgram.emitters[0].parentEmitterIndex = 1u;
    cyclicDirectProgram.emitters[1].sourceEmitterId += ".cycle-b";
    cyclicDirectProgram.emitters[1].emitterIndex = 1u;
    cyclicDirectProgram.emitters[1].parentEmitterIndex = 0u;
    assertDirectProgramRejected(std::move(cyclicDirectProgram));

    VFX::VfxProgram invalidInputIndexProgram = compiled.program;
    invalidInputIndexProgram.emitters.front().inputLinks.front().inputIndex =
        static_cast<uint32_t>(invalidInputIndexProgram.inputs.size());
    assertDirectProgramRejected(std::move(invalidInputIndexProgram));

    VFX::VfxProgram invalidByteOffsetProgram = compiled.program;
    ++invalidByteOffsetProgram.emitters.front().inputLinks.front().byteOffset;
    assertDirectProgramRejected(std::move(invalidByteOffsetProgram));

    VFX::VfxProgram invalidStorageProgram = compiled.program;
    invalidStorageProgram.emitters.front().inputLinks.front().storageType =
        RENDER3D::PARTICLES::ParticleRuntimePropertyType::Float4;
    assertDirectProgramRejected(std::move(invalidStorageProgram));

    VFX::VfxProgram duplicateSemanticProgram = compiled.program;
    duplicateSemanticProgram.emitters.front().inputLinks.push_back(
        duplicateSemanticProgram.emitters.front().inputLinks.front());
    assertDirectProgramRejected(std::move(duplicateSemanticProgram));

    VFX::VfxProgram duplicateOutputProgram = compiled.program;
    VFX::VfxProgramEmitter duplicateOutputEmitter = duplicateOutputProgram.emitters.front();
    duplicateOutputEmitter.emitterIndex = 1u;
    duplicateOutputEmitter.sourceEmitterId += ".duplicate-output";
    duplicateOutputEmitter.parentEmitterIndex = VFX::kInvalidVfxProgramIndex;
    duplicateOutputProgram.emitters.push_back(std::move(duplicateOutputEmitter));
    VFX::VfxPlayRequest duplicateOutputPlay = play;
    duplicateOutputPlay.program = std::make_shared<VFX::VfxProgram>(std::move(duplicateOutputProgram));
    const VFX::VfxPlayResult duplicateOutputPlayResult = serviceA.PlayDetailed(duplicateOutputPlay);
    assert(!duplicateOutputPlayResult.IsAccepted());
    assert(std::any_of(duplicateOutputPlayResult.diagnostics.begin(), duplicateOutputPlayResult.diagnostics.end(),
                       [](const VFX::VfxPlaybackDiagnostic& diagnostic) {
                           return diagnostic.code == "Vfx.DuplicateSpecialOutputId";
                       }));

    const VFX::VfxPlayResult playA = serviceA.PlayDetailed(play);
    const VFX::VfxPlayResult playB = serviceB.PlayDetailed(play);
    assert(playA.IsAccepted() && playB.IsAccepted());
    assert(playA.handle.slot == playB.handle.slot);
    assert(playA.handle.generation == playB.handle.generation);
    serviceA.Update(1u, 0.25f);
    const auto firstServiceAFrame = serviceA.GetRenderInput().emitters.front();
    serviceA.Update(1u, 0.25f);
    const auto duplicateServiceAFrame = serviceA.GetRenderInput().emitters.front();
    assert(duplicateServiceAFrame.spawnCount == firstServiceAFrame.spawnCount);
    assert(duplicateServiceAFrame.emitterAgeSeconds == firstServiceAFrame.emitterAgeSeconds);
    serviceB.Update(1u, 0.25f);
    const auto keyA = serviceA.GetRenderInput().emitters.front().key;
    const auto keyB = serviceB.GetRenderInput().emitters.front().key;
    assert(!(keyA == keyB));

    VFX::VfxPlaybackService seekServiceA{};
    VFX::VfxPlaybackService seekServiceB{};
    const VFX::VfxPlayResult seekPlayA = seekServiceA.PlayDetailed(play);
    const VFX::VfxPlayResult seekPlayB = seekServiceB.PlayDetailed(play);
    assert(seekPlayA.IsAccepted() && seekPlayB.IsAccepted());
    VFX::VfxSeekResult seekA = seekServiceA.Seek(seekPlayA.handle, 8u, 0.5f, 1.0f / 60.0f, 5u);
    assert(seekA.accepted && !seekA.complete);
    const size_t firstSeekBatchCount = seekServiceA.GetRenderInput().emitters.size();
    while (!seekA.complete) {
        const float previousTime = seekA.currentSeconds;
        seekA = seekServiceA.Seek(seekPlayA.handle, 8u, 0.5f, 1.0f / 60.0f, 5u);
        assert(seekA.accepted);
        assert(seekA.currentSeconds > previousTime || seekA.complete);
    }
    assert(seekA.currentSeconds == 0.5f);
    assert(seekServiceA.GetRenderInput().emitters.size() > firstSeekBatchCount);
    uint32_t previousOrdinal = 0u;
    bool firstOrdinal = true;
    for (const auto& submission : seekServiceA.GetRenderInput().emitters) {
        if (!firstOrdinal) {
            assert(submission.simulationOrdinal > previousOrdinal);
        }
        previousOrdinal = submission.simulationOrdinal;
        firstOrdinal = false;
    }
    const VFX::VfxSeekResult seekB = seekServiceB.Seek(seekPlayB.handle, 8u, 0.5f, 1.0f / 60.0f);
    assert(seekB.accepted && seekB.complete);
    RENDER3D::PARTICLES::ParticleCpuSimulation seekSimulationA{};
    RENDER3D::PARTICLES::ParticleCpuSimulation seekSimulationB{};
    seekSimulationA.Apply(seekServiceA.GetRenderInput());
    seekSimulationB.Apply(seekServiceB.GetRenderInput());
    const auto seekKeyA = seekServiceA.GetRenderInput().emitters.front().key;
    const auto seekKeyB = seekServiceB.GetRenderInput().emitters.front().key;
    const auto* seekEmitterA = seekSimulationA.Find(seekKeyA);
    const auto* seekEmitterB = seekSimulationB.Find(seekKeyB);
    assert(seekEmitterA != nullptr && seekEmitterB != nullptr);
    assert(seekSimulationA.GetAliveParticleCount(seekKeyA) == seekSimulationB.GetAliveParticleCount(seekKeyB));
    const auto findFirstAlive = [](const auto& emitterState) {
        return std::find_if(emitterState.particles.begin(), emitterState.particles.end(),
                            [](const auto& particle) { return particle.alive; });
    };
    const auto seekParticleA = findFirstAlive(*seekEmitterA);
    const auto seekParticleB = findFirstAlive(*seekEmitterB);
    assert(seekParticleA != seekEmitterA->particles.end());
    assert(seekParticleB != seekEmitterB->particles.end());
    assert(std::abs(seekParticleA->position.x - seekParticleB->position.x) < 1.0e-5f);
    assert(std::abs(seekParticleA->position.y - seekParticleB->position.y) < 1.0e-5f);
    assert(seekParticleA->rotationRadians.y > 0.0f);
    assert(seekParticleA->scale.x > 1.0f);
    assert(seekParticleA->position.y > 0.0f);
    assert(seekParticleA->color.w < 1.0f);

    VFX::VfxSeekResult seekZero = seekServiceA.Seek(seekPlayA.handle, 9u, 0.0f);
    assert(seekZero.accepted && seekZero.complete);
    assert(seekZero.currentSeconds == 0.0f);
    assert(!seekServiceA.GetRenderInput().emitters.empty());
    assert(seekServiceA.GetRenderInput().emitters.front().resetSimulation);
    assert(seekServiceA.GetRenderInput().emitters.front().deltaSeconds == 0.0f);

    VFX::VfxPlaybackService restartService{};
    RENDER3D::PARTICLES::ParticleCpuSimulation restartSimulation{};
    VFX::VfxPlayResult restartPlay = restartService.PlayDetailed(play);
    assert(restartPlay.IsAccepted());
    restartService.Update(30u, 0.25f);
    assert(restartService.GetRenderInput().emitters.size() == 1u);
    restartSimulation.Apply(restartService.GetRenderInput());
    RENDER3D::PARTICLES::ParticleEmitterInstanceKey restartKey = restartService.GetRenderInput().emitters.front().key;
    VFX::VfxPlaybackHandle restartHandle = restartPlay.handle;
    assert(restartSimulation.GetEmitterCount() == 1u);

    for (uint64_t frameIndex = 31u; frameIndex < 35u; ++frameIndex) {
        const auto retiredKey = restartKey;
        const auto retiredHandle = restartHandle;
        assert(restartService.Stop(retiredHandle));
        restartService.Update(frameIndex, 0.0f);
        assert(restartService.GetRenderInput().emitters.empty());
        assert(std::find(restartService.GetRenderInput().retiredEmitters.begin(),
                         restartService.GetRenderInput().retiredEmitters.end(),
                         retiredKey) != restartService.GetRenderInput().retiredEmitters.end());

        restartPlay = restartService.PlayDetailed(play);
        assert(restartPlay.IsAccepted());
        restartHandle = restartPlay.handle;
        assert(restartHandle.slot == retiredHandle.slot);
        assert(restartHandle.generation != retiredHandle.generation);
        const VFX::VfxSeekResult restartSeek = restartService.Seek(restartHandle, frameIndex, 0.0f);
        assert(restartSeek.accepted && restartSeek.complete);

        const auto& restartInput = restartService.GetRenderInput();
        assert(restartInput.emitters.size() == 1u);
        assert(std::find(restartInput.retiredEmitters.begin(), restartInput.retiredEmitters.end(), retiredKey) !=
               restartInput.retiredEmitters.end());
        restartKey = restartInput.emitters.front().key;
        assert(restartKey.playbackSlot == restartHandle.slot);
        assert(restartKey.playbackGeneration == restartHandle.generation);

        restartSimulation.Apply(restartInput);
        assert(restartSimulation.GetEmitterCount() == 1u);
        assert(restartSimulation.Find(retiredKey) == nullptr);
        assert(restartSimulation.Find(restartKey) != nullptr);
    }

    auto seekBudgetProgram = std::make_shared<VFX::VfxProgram>(compiled.program);
    seekBudgetProgram->emitters.resize(64u, seekBudgetProgram->emitters.front());
    for (uint32_t emitterIndex = 0u; emitterIndex < seekBudgetProgram->emitters.size(); ++emitterIndex) {
        auto& budgetEmitter = seekBudgetProgram->emitters[emitterIndex];
        budgetEmitter.sourceEmitterId += ".seek-budget." + std::to_string(emitterIndex);
        budgetEmitter.emitterIndex = emitterIndex;
        budgetEmitter.parentEmitterIndex = VFX::kInvalidVfxProgramIndex;
        for (VFX::VfxSpecialOutputData& output : budgetEmitter.specialOutputs) {
            output.id += ".seek-budget." + std::to_string(emitterIndex);
        }
        budgetEmitter.simulation.capacity = 1u;
        budgetEmitter.burstTimeline.clear();
        budgetEmitter.specialOutputs.clear();
    }
    VFX::VfxPlaybackService seekBudgetService{};
    VFX::VfxPlayRequest seekBudgetPlay = play;
    seekBudgetPlay.program = seekBudgetProgram;
    const VFX::VfxPlayResult seekBudgetHandle = seekBudgetService.PlayDetailed(seekBudgetPlay);
    assert(seekBudgetHandle.IsAccepted());
    const VFX::VfxSeekCapacity initialSeekCapacity = seekBudgetService.GetSeekCapacity(seekBudgetHandle.handle, 10u);
    assert(initialSeekCapacity.handleActive);
    assert(initialSeekCapacity.emitterCount == 64u);
    assert(initialSeekCapacity.maximumSteps == RENDER3D::PARTICLES::kMaximumParticleEmitterSubmissionsPerFrame / 64u);
    const VFX::VfxSeekResult boundedSeek = seekBudgetService.Seek(seekBudgetHandle.handle, 10u, 0.2f, 1.0f / 1000.0f);
    assert(boundedSeek.accepted && !boundedSeek.complete);
    assert(seekBudgetService.GetRenderInput().emitters.size() ==
           RENDER3D::PARTICLES::kMaximumParticleEmitterSubmissionsPerFrame);
    assert(std::any_of(boundedSeek.diagnostics.begin(), boundedSeek.diagnostics.end(),
                       [](const VFX::VfxPlaybackDiagnostic& diagnostic) {
                           return diagnostic.code == "Vfx.SeekSubmissionBudgetExhausted";
                       }));
    const size_t exhaustedSubmissionCount = seekBudgetService.GetRenderInput().emitters.size();
    const float exhaustedSeekTime = boundedSeek.currentSeconds;
    const VFX::VfxSeekResult sameFrameContinuation =
        seekBudgetService.Seek(seekBudgetHandle.handle, 10u, 0.2f, 1.0f / 1000.0f);
    assert(sameFrameContinuation.accepted && !sameFrameContinuation.complete);
    assert(sameFrameContinuation.currentSeconds == exhaustedSeekTime);
    assert(seekBudgetService.GetRenderInput().emitters.size() == exhaustedSubmissionCount);
    assert(seekBudgetService.GetSeekCapacity(seekBudgetHandle.handle, 10u).maximumSteps == 0u);

    auto loopProgram = std::make_shared<VFX::VfxProgram>(compiled.program);
    loopProgram->loop = true;
    loopProgram->durationSeconds = 0.25f;
    VFX::VfxPlaybackService loopService{};
    VFX::VfxPlayRequest loopPlay = play;
    loopPlay.program = loopProgram;
    // The program validator treats duration and emitter-local burst keys as
    // one contract. Keep the copied test program valid after shortening the
    // loop window by moving its follow-up key inside that window.
    loopProgram->emitters.front().burstTimeline[1].time = 0.2f;
    const VFX::VfxPlayResult loopResult = loopService.PlayDetailed(loopPlay);
    assert(loopResult.IsAccepted());
    loopService.Update(1u, 0.25f);
    const uint32_t firstLoopSpawn = loopService.GetRenderInput().emitters.front().spawnCount;
    loopService.Update(2u, 0.0f);
    assert(firstLoopSpawn >= emitter.spawn.burstTimeline.front().count);
    assert(loopService.GetRenderInput().emitters.front().spawnCount == emitter.spawn.burstTimeline.front().count);

    auto ownerExpiryProgram = std::make_shared<VFX::VfxProgram>(*loopProgram);
    ownerExpiryProgram->emitters.front().attachment.mode = VFX::VfxAttachmentMode::OwnerSocket;
    ownerExpiryProgram->emitters.front().attachment.follow = VFX::VfxFollowMode::Transform;
    ownerExpiryProgram->emitters.front().attachment.socketName = "weapon_tip";
    VFX::VfxPlaybackService ownerExpiryService{};
    bool ownerAlive = true;
    ownerExpiryService.SetAttachmentResolver(
        [&ownerAlive](RuntimeObjectHandle owner, std::string_view, VFX::VfxWorldTransform& outTransform) {
            if (!ownerAlive || !owner.IsValid()) {
                return false;
            }
            outTransform.position = {2.0f, 3.0f, 4.0f};
            return true;
        });
    VFX::VfxPlayRequest ownerExpiryPlay = play;
    ownerExpiryPlay.program = ownerExpiryProgram;
    ownerExpiryPlay.ownerObject = {41u, 7u};
    const VFX::VfxPlayResult ownerExpiryResult = ownerExpiryService.PlayDetailed(ownerExpiryPlay);
    assert(ownerExpiryResult.IsAccepted());
    ownerExpiryService.Update(1u, 1.0f / 60.0f);
    assert(ownerExpiryService.IsActive(ownerExpiryResult.handle));
    assert(!ownerExpiryService.GetRenderInput().emitters.empty());
    const auto ownerExpiryEmitterKey = ownerExpiryService.GetRenderInput().emitters.front().key;
    ownerAlive = false;
    ownerExpiryService.Update(2u, 1.0f / 60.0f);
    assert(!ownerExpiryService.IsActive(ownerExpiryResult.handle));
    assert(ownerExpiryService.GetRenderInput().emitters.empty());
    assert(std::find(ownerExpiryService.GetRenderInput().retiredEmitters.begin(),
                     ownerExpiryService.GetRenderInput().retiredEmitters.end(),
                     ownerExpiryEmitterKey) != ownerExpiryService.GetRenderInput().retiredEmitters.end());

    VFX::VfxPlaybackService guardedService{};
    VFX::VfxPlayRequest guardedPlay = play;
    guardedPlay.parameters.front().value.numeric.x = (std::numeric_limits<float>::max)();
    const VFX::VfxPlayResult guardedResult = guardedService.PlayDetailed(guardedPlay);
    assert(guardedResult.IsAccepted());
    guardedService.Update(1u, 0.25f);
    const auto& saturatedSubmission = guardedService.GetRenderInput().emitters.front();
    assert(saturatedSubmission.spawnCount == compiled.program.emitters.front().simulation.capacity);
    VFX::VfxParameterOverride nonFiniteOverride = guardedPlay.parameters.front();
    nonFiniteOverride.value.numeric.x = (std::numeric_limits<float>::infinity)();
    assert(guardedService.SetParameters(guardedResult.handle, {nonFiniteOverride}, nullptr));
    guardedService.Update(2u, 0.25f);
    const auto& guardedSubmission = guardedService.GetRenderInput().emitters.front();
    // A non-finite runtime rate override clamps continuous spawning to zero,
    // but must not suppress an authored burst key crossing this frame.
    assert(guardedSubmission.spawnCount == emitter.spawn.burstTimeline[1u].count);
    assert(std::isfinite(guardedSubmission.overrides.radialSpeedMultiplier));

    RENDER3D::PARTICLES::ParticleCpuSimulation simulation{};
    simulation.Apply(serviceA.GetRenderInput());
    assert(simulation.GetEmitterCount() == 1u);
    assert(simulation.GetAliveParticleCount(keyA) > 0u);
    for (uint64_t frame = 2u; frame <= 16u; ++frame) {
        serviceA.Update(frame, 0.25f);
        const auto& renderInput = serviceA.GetRenderInput();
        assert(renderInput.emitters.size() == 1u);
        if (frame >= 5u) {
            assert(renderInput.emitters.front().spawnCount == 0u);
        }
        simulation.Apply(renderInput);
    }
    serviceA.Update(17u, 0.25f);
    assert(serviceA.GetRenderInput().emitters.empty());
    assert(!serviceA.GetRenderInput().retiredEmitters.empty());
    simulation.Apply(serviceA.GetRenderInput());
    assert(simulation.GetEmitterCount() == 0u);

    using namespace RENDER3D::PARTICLES;
    ParticleEmitterSubmission referenceSubmission{};
    referenceSubmission.key = {101u, 1u, 1u, 0u};
    referenceSubmission.resetSimulation = true;
    referenceSubmission.spawnCount = 1u;
    referenceSubmission.randomSeed = 12345u;
    referenceSubmission.simulation.capacity = 1u;
    referenceSubmission.simulation.lifetime = {ParticleDistributionMode::RandomRange, 1.0f, 2.0f, 77u};
    referenceSubmission.simulation.radialSpeed = {ParticleDistributionMode::RandomRange, 1.0f, 2.0f, 77u};
    referenceSubmission.simulation.shapeRadius = {ParticleDistributionMode::Constant, 2.0f, 2.0f, 0u};
    referenceSubmission.simulation.position.mode = ParticleValueMode::Fixed;
    referenceSubmission.simulation.position.initial = {
        ParticleDistributionMode::RandomRange, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 0u};
    referenceSubmission.simulation.scale.initial = {
        ParticleDistributionMode::Constant, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, 0u};
    referenceSubmission.simulation.color.mode = ParticleColorMode::RandomRange;
    referenceSubmission.simulation.color.fixed = {
        ParticleDistributionMode::RandomRange, {0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 0u};
    referenceSubmission.simulation.drag = 2.0f;

    ParticleRenderInput referenceInput{};
    referenceInput.frameIndex = 1u;
    referenceInput.emitters.push_back(referenceSubmission);
    ParticleCpuSimulation referenceSimulation{};
    referenceSimulation.Apply(referenceInput);
    const CpuParticleEmitterState* pointEmitter = referenceSimulation.Find(referenceSubmission.key);
    assert(pointEmitter != nullptr);
    auto pointParticle = std::find_if(pointEmitter->particles.begin(), pointEmitter->particles.end(),
                                      [](const CpuParticleState& particle) { return particle.alive; });
    assert(pointParticle != pointEmitter->particles.end());
    assert(std::abs(pointParticle->lifetimeSeconds - pointParticle->velocity.y) < 1.0e-6f);
    assert(std::abs(pointParticle->velocity.x) < 1.0e-6f);
    assert(std::abs(pointParticle->velocity.z) < 1.0e-6f);
    assert(std::abs(pointParticle->baseWorldPosition.x) < 1.0e-6f);
    assert(std::abs(pointParticle->baseWorldPosition.y) < 1.0e-6f);
    assert(std::abs(pointParticle->baseWorldPosition.z) < 1.0e-6f);
    assert(std::abs(pointParticle->position.x - pointParticle->color.x) > 1.0e-5f);
    const float pointSpeed = pointParticle->velocity.y;

    referenceSubmission.resetSimulation = false;
    referenceSubmission.spawnCount = 0u;
    referenceSubmission.deltaSeconds = 0.5f;
    referenceInput.frameIndex = 2u;
    referenceInput.emitters.front() = referenceSubmission;
    referenceSimulation.Apply(referenceInput);
    pointEmitter = referenceSimulation.Find(referenceSubmission.key);
    assert(pointEmitter != nullptr);
    pointParticle = std::find_if(pointEmitter->particles.begin(), pointEmitter->particles.end(),
                                 [](const CpuParticleState& particle) { return particle.alive; });
    assert(pointParticle != pointEmitter->particles.end());
    assert(std::abs(pointParticle->velocity.y - pointSpeed * std::exp(-1.0f)) < 1.0e-5f);

    const auto AssertRadialShape = [&referenceSubmission](ParticleShape shape, float coneAngleRadians,
                                                          uint64_t playbackNamespace) {
        ParticleEmitterSubmission submission = referenceSubmission;
        submission.key.playbackNamespace = playbackNamespace;
        submission.key.emitterIndex = 0u;
        submission.resetSimulation = true;
        submission.spawnCount = 1u;
        submission.deltaSeconds = 0.0f;
        submission.simulation.shape = shape;
        submission.simulation.coneAngleRadians = coneAngleRadians;
        submission.simulation.position.initial = {
            ParticleDistributionMode::Constant, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0u};
        submission.simulation.radialSpeed = {ParticleDistributionMode::Constant, 1.0f, 1.0f, 0u};
        ParticleRenderInput input{};
        input.frameIndex = 1u;
        input.emitters.push_back(submission);
        ParticleCpuSimulation simulation{};
        simulation.Apply(input);
        const CpuParticleEmitterState* emitterState = simulation.Find(submission.key);
        assert(emitterState != nullptr);
        const auto particle = std::find_if(emitterState->particles.begin(), emitterState->particles.end(),
                                           [](const CpuParticleState& candidate) { return candidate.alive; });
        assert(particle != emitterState->particles.end());
        const float offsetLength = std::sqrt(particle->baseWorldPosition.x * particle->baseWorldPosition.x +
                                             particle->baseWorldPosition.y * particle->baseWorldPosition.y +
                                             particle->baseWorldPosition.z * particle->baseWorldPosition.z);
        assert(offsetLength > 0.0f && offsetLength <= 2.0f);
        const float alignment = particle->baseWorldPosition.x * particle->velocity.x +
                                particle->baseWorldPosition.y * particle->velocity.y +
                                particle->baseWorldPosition.z * particle->velocity.z;
        assert(std::abs(alignment - offsetLength) < 1.0e-5f);
        if (shape == ParticleShape::Cone) {
            assert(particle->velocity.y >= std::cos(coneAngleRadians * 0.5f) - 1.0e-6f);
        }
    };
    AssertRadialShape(ParticleShape::Sphere, 0.0f, 102u);
    AssertRadialShape(ParticleShape::Cone, 3.14159265358979323846f / 3.0f, 103u);

    // Constant-force trajectories must not depend on the host's frame partition.
    for (const float drag : {0.0f, 0.0001f, 3.0f}) {
        ParticleEmitterSubmission step{};
        step.key.playbackNamespace = 880u;
        step.resetSimulation = true;
        step.spawnCount = 1u;
        step.simulation.capacity = 64u;
        step.simulation.lifetime.minimum = step.simulation.lifetime.maximum = 10.0f;
        step.simulation.radialSpeed.minimum = step.simulation.radialSpeed.maximum = 0.0f;
        step.simulation.gravity = {2.0f, -9.81f, 0.0f};
        step.simulation.drag = drag;
        ParticleRenderInput input{};
        input.emitters.push_back(step);
        ParticleCpuSimulation hitch{}, smooth{};
        hitch.Apply(input);
        smooth.Apply(input);
        input.emitters[0].resetSimulation = false;
        input.emitters[0].spawnCount = 0;
        input.emitters[0].deltaSeconds = 0.25f;
        hitch.Apply(input);
        input.emitters[0].deltaSeconds = 1.0f / 60.0f;
        for (unsigned frame = 0; frame < 15; ++frame)
            smooth.Apply(input);
        const auto& a = hitch.Find(step.key)->particles[0];
        const auto& b = smooth.Find(step.key)->particles[0];
        assert(std::abs(a.position.y - b.position.y) < 0.0001f);
        assert(std::abs(a.velocity.y - b.velocity.y) < 0.0001f);
        if (drag == 0)
            assert(std::abs(a.position.y - (-0.5f * 9.81f * 0.25f * 0.25f)) < 0.0001f);
    }
    // A moving meshEmitter must not drag world-space particles; local particles must follow.
    for (uint32_t local = 0u; local < 2u; ++local) {
        ParticleEmitterSubmission moving{};
        moving.key.playbackNamespace = 900u + local;
        moving.resetSimulation = true;
        moving.spawnCount = 1u;
        moving.simulation.capacity = 64u;
        moving.simulation.lifetime.minimum = moving.simulation.lifetime.maximum = 10.0f;
        moving.simulation.localSimulation = local;
        moving.simulation.radialSpeed.minimum = moving.simulation.radialSpeed.maximum = 0.0f;
        moving.worldPosition = {0.0f, 2.0f, 0.0f};
        ParticleRenderInput input{};
        input.emitters.push_back(moving);
        ParticleCpuSimulation movingSimulation{};
        movingSimulation.Apply(input);
        input.emitters[0].resetSimulation = false;
        input.emitters[0].spawnCount = 0u;
        input.emitters[0].deltaSeconds = 0.1f;
        input.emitters[0].worldPosition.x = 5.0f;
        movingSimulation.Apply(input);
        const auto* state = movingSimulation.Find(moving.key);
        assert(state && state->particles[0].alive);
        assert(std::abs(state->particles[0].position.x - (local ? 5.0f : 0.0f)) < 0.0001f);
        input.emitters[0].simulation.gravity = {0.0f, -100.0f, 0.0f};
        input.emitters[0].simulation.collisionPlaneEnabled = 1u;
        input.emitters[0].simulation.collisionPlaneHeight = 0.0f;
        input.emitters[0].simulation.restitution = 0.5f;
        input.emitters[0].deltaSeconds = 0.5f;
        movingSimulation.Apply(input);
        assert(std::isfinite(state->particles[0].velocity.y));
        assert(state->particles[0].position.y >= 0.0f);
    }
    for (const auto shape : {VFX::VfxRenderShapeType::MeshBox, VFX::VfxRenderShapeType::MeshSphere}) {
        auto meshAsset = VFX::MakeDefaultVfxAssetData();
        auto& meshEmitter = meshAsset.emitters.front();
        meshEmitter.renderShape.type = shape;
        meshEmitter.renderCommon.materialMode = VFX::VfxMaterialMode::StandardLit;
        meshEmitter.basic.localSimulation = true;
        meshEmitter.shape.circleArcDegrees = 137.0f;
        meshEmitter.forces.collisionPlaneEnabled = true;
        meshEmitter.forces.collisionPlaneHeight = 3.0f;
        std::string json, payload;
        assert(VFX::SERIALIZATION::SerializeVfxSourceJson(meshAsset, json, error));
        VFX::VfxAssetData decoded;
        assert(VFX::SERIALIZATION::ParseVfxSourceJson(json, decoded, error));
        const auto baked = VFX::CompileVfxAsset(decoded);
        assert(baked.success);
        assert(VFX::SerializeVfxProgram(baked.program, payload, error));
        VFX::VfxProgram loaded;
        assert(VFX::DeserializeVfxProgram(payload, loaded, error));
        const auto& result = loaded.emitters.front();
        assert(result.simulation.localSimulation == 1u && result.simulation.collisionPlaneEnabled == 1u);
        assert(result.simulation.collisionPlaneHeight == 3.0f);
        assert(result.simulation.shapeCircleArcDegrees == 137.0f);
        assert(result.render.materialMode == ParticleMaterialMode::StandardLit);
        assert(static_cast<uint32_t>(result.render.shape) == static_cast<uint32_t>(shape));
        decoded.emitters.front().forces.restitution = 2.0f;
        assert(!VFX::CompileVfxAsset(decoded).success);
    }

    {
        ParticleEmitterRenderParameters render{};
        render.shape = ParticleRenderShape::MeshSphere;
        render.ringSegmentCount = 9;
        assert(ParticleGeometryTriangleCount(render) == 72);
        render.shape = ParticleRenderShape::MeshTorus;
        render.ringSegmentCount = 32;
        assert(ParticleGeometryTriangleCount(render) == 256);
        render.shape = ParticleRenderShape::MeshCapsule;
        assert(ParticleGeometryTriangleCount(render) == 240);
        for (uint32_t segments = 3; segments <= 64; ++segments) {
            render.ringSegmentCount = segments;
            for (const auto shape :
                 {ParticleRenderShape::MeshSphere, ParticleRenderShape::MeshHemisphere, ParticleRenderShape::MeshTorus,
                  ParticleRenderShape::MeshCapsule, ParticleRenderShape::MeshCylinder, ParticleRenderShape::MeshCone}) {
                render.shape = shape;
                const auto count = ParticleGeometryTriangleCount(render);
                assert(count > 2 && count <= 256);
            }
        }
    }
    {
        // 空の VFX publisher でも、先行する通常オブジェクト更新を消してはならない。
        using namespace RENDER3D::RUNTIME;
        ModelAsset model;
        model.SetState(ModelAsset::State::Loaded);
        model.materials.emplace_back();
        auto& primitive = model.meshes.emplace_back().primitives.emplace_back();
        primitive.staticVertices.resize(3);
        primitive.staticVertices[0].position = {0, 0, 0};
        primitive.staticVertices[1].position = {1, 0, 0};
        primitive.staticVertices[2].position = {0, 1, 0};
        primitive.indices = {0, 1, 2};
        model.nodes.emplace_back().meshIndex = 0;
        RenderModelAsset renderModel;
        assert(BuildRenderModelAsset(model, renderModel));
        SceneRenderCache cache;
        SceneRenderObjectDesc object;
        object.id = {17};
        object.model = &model;
        object.renderModel = &renderModel;
        cache.BeginSync(1);
        cache.Upsert(object);
        cache.EndSync();
        cache.PreRenderSync();
        cache.BeginPatchSync(2);
        object.worldTransform.position.x = 3;
        cache.Upsert(object);
        cache.PreRenderSync();
        const auto changed = cache.GetDirtySurfaceIndices();
        assert(!changed.empty());
        const auto version = cache.GetSurfaceDataVersion();
        VFX::VfxMeshInstancePublisher publisher;
        VFX::VfxPlaybackService playback;
        AssetRegistry registry;
        ModelManager manager;
        RenderModelCache modelCache;
        publisher.Publish(playback, cache, modelCache, registry, manager, {});
        assert(cache.GetDirtySurfaceIndices() == changed);
        assert(cache.GetSurfaceDataVersion() == version);
        assert(cache.Find(object.id)->desc.worldTransform.position.x == 3);
    }
    return 0;
}
