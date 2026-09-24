#include "Editor/Workspaces/Vfx/HIKARI_VfxOperationSerialization.h"

#include <string>

#include "Assets/Vfx/HIKARI_VfxPropertySchema.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxSerializedTarget.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceDomain.h"

namespace HIKARI::EDITOR {
namespace {

nlohmann::json PropertyAddress(const VFX::VfxPropertyAddress& address) {
    return {
        {"emitterId", address.emitterId},
        {"moduleId", address.moduleId},
        {"propertyId", address.propertyId},
    };
}

nlohmann::json EmitterSummary(const VFX::VfxEmitterData& emitter) {
    nlohmann::json links = nlohmann::json::array();
    for (const VFX::VfxInputLinkData& link : emitter.inputLinks) {
        links.push_back({
            {"id", link.id},
            {"inputId", link.inputId},
            {"target", PropertyAddress(link.target)},
        });
    }
    nlohmann::json modules = nlohmann::json::object();
    for (uint8_t value = 0u; value <= static_cast<uint8_t>(VFX::VfxModuleKind::RenderShape); ++value) {
        const auto kind = static_cast<VFX::VfxModuleKind>(value);
        modules[std::string(VFX::ToString(kind))] = GetVfxModuleId(emitter, kind);
    }
    nlohmann::json bursts = nlohmann::json::array();
    for (const VFX::VfxBurstKeyData& key : emitter.spawn.burstTimeline) {
        bursts.push_back({
            {"id", key.id},
            {"time", key.time},
            {"count", key.count},
        });
    }
    nlohmann::json outputs = nlohmann::json::array();
    for (const VFX::VfxSpecialOutputData& output : emitter.specialOutputs) {
        outputs.push_back({
            {"id", output.id},
            {"type", VFX::ToString(output.type)},
            {"enabled", output.enabled},
        });
    }
    return {
        {"id", emitter.id},
        {"name", emitter.name},
        {"kind", emitter.kind == VFX::VfxLayerKind::Shape        ? "Shape"
                 : emitter.kind == VFX::VfxLayerKind::Group      ? "Group"
                 : emitter.kind == VFX::VfxLayerKind::Adjustment ? "Adjustment"
                                                                 : "Particles"},
        {"parentEmitterId", emitter.parentEmitterId},
        {"order", emitter.order},
        {"enabled", emitter.basic.enabled},
        {"startDelay", emitter.startDelay},
        {"activeDuration", emitter.activeDuration},
        {"attachment",
         {
             {"id", emitter.attachment.id},
             {"mode", VFX::ToString(emitter.attachment.mode)},
             {"follow", VFX::ToString(emitter.attachment.follow)},
             {"socketName", emitter.attachment.socketName},
         }},
        {"modules", std::move(modules)},
        {"burstTimeline", std::move(bursts)},
        {"specialOutputs", std::move(outputs)},
        {"links", std::move(links)},
    };
}

nlohmann::json PropertySchemas(const VFX::VfxEmitterData& emitter, const VFX::VfxModuleKind* moduleFilter) {
    nlohmann::json schemas = nlohmann::json::array();
    for (const VFX::VfxPropertySchema& schema : VFX::GetVfxPropertySchemas()) {
        if (moduleFilter != nullptr && schema.moduleKind != *moduleFilter) {
            continue;
        }
        nlohmann::json range = nlohmann::json::object();
        if (schema.range.hasMinimum) {
            range["minimum"] = schema.range.minimum;
        }
        if (schema.range.hasMaximum) {
            range["maximum"] = schema.range.maximum;
        }
        schemas.push_back({
            {"address", PropertyAddress({
                            emitter.id,
                            std::string(GetVfxModuleId(emitter, schema.moduleKind)),
                            std::string(schema.propertyId),
                        })},
            {"moduleKind", VFX::ToString(schema.moduleKind)},
            {"propertyId", schema.propertyId},
            {"displayKey", schema.displayKey},
            {"valueType", VFX::ToString(schema.valueType)},
            {"bindable", schema.bindable},
            {"runtimeMutable", schema.runtimeMutable},
            {"range", std::move(range)},
            {"units", schema.units},
        });
    }
    return schemas;
}

bool DescribeTypedTarget(const VfxWorkspaceController& controller, std::string_view targetId,
                         nlohmann::json& description) {
    if (targetId == "asset" || (controller.GetDocument().GetAssetGuid().IsValid() &&
                                targetId == controller.GetDocument().GetAssetGuid().value)) {
        description["kind"] = "asset";
        description["targetId"] = "asset";
        return true;
    }
    if (const VFX::VfxExposedInputData* input = controller.FindInput(targetId)) {
        description["kind"] = "input";
        description["targetId"] = input->id;
        return true;
    }
    if (const VFX::VfxEmitterData* emitter = controller.FindEmitter(targetId)) {
        description["kind"] = "emitter";
        description["targetId"] = emitter->id;
        description["propertySchemas"] = PropertySchemas(*emitter, nullptr);
        return true;
    }
    const VFX::VfxAssetData* asset = controller.GetAsset();
    if (asset == nullptr)
        return false;
    for (const VFX::VfxEmitterData& emitter : asset->emitters) {
        VFX::VfxModuleKind kind{};
        if (VFX::TryResolveVfxModuleKind(emitter, targetId, kind)) {
            description["kind"] = "module";
            description["targetId"] = targetId;
            description["emitterId"] = emitter.id;
            description["moduleKind"] = VFX::ToString(kind);
            description["propertySchemas"] = PropertySchemas(emitter, &kind);
            return true;
        }
    }
    const VFX::VfxEmitterData* owner = nullptr;
    if (const VFX::VfxInputLinkData* link = controller.FindInputLink(targetId, &owner)) {
        description["kind"] = "inputLink";
        description["targetId"] = link->id;
        description["emitterId"] = owner != nullptr ? owner->id : "";
        description["propertyAddress"] = PropertyAddress(link->target);
        return true;
    }
    if (const VFX::VfxBurstKeyData* key = controller.FindBurstKey(targetId, &owner)) {
        description["kind"] = "burstKey";
        description["targetId"] = key->id;
        description["emitterId"] = owner != nullptr ? owner->id : "";
        return true;
    }
    if (const VFX::VfxSpecialOutputData* output = controller.FindSpecialOutput(targetId, &owner)) {
        description["kind"] = "specialOutput";
        description["targetId"] = output->id;
        description["emitterId"] = owner != nullptr ? owner->id : "";
        description["type"] = VFX::ToString(output->type);
        return true;
    }
    VfxCurveAddress curveAddress{};
    if (const VFX::VfxCurveKeyData* key = controller.FindCurveKey(targetId, &owner, &curveAddress)) {
        description["kind"] = "curveKey";
        description["targetId"] = key->id;
        description["emitterId"] = curveAddress.emitterId;
        description["moduleId"] = curveAddress.moduleId;
        description["propertyId"] = curveAddress.propertyId;
        description["channel"] = curveAddress.channel;
        return true;
    }
    std::string moduleId{};
    std::string propertyId{};
    if (const VFX::VfxColorStopData* stop = controller.FindColorStop(targetId, &owner, &moduleId, &propertyId)) {
        description["kind"] = "colorStop";
        description["targetId"] = stop->id;
        description["emitterId"] = owner != nullptr ? owner->id : "";
        description["moduleId"] = moduleId;
        description["propertyId"] = propertyId;
        return true;
    }
    return false;
}

} // namespace

nlohmann::json WriteVfxWorkspaceSummary(const VfxWorkspaceController& controller) {
    const VFX::VfxAssetData* asset = controller.GetAsset();
    if (asset == nullptr) {
        return {
            {"workspace", "vfx"},
            {"open", false},
        };
    }
    nlohmann::json inputs = nlohmann::json::array();
    for (const VFX::VfxExposedInputData& input : asset->inputs) {
        inputs.push_back({
            {"id", input.id},
            {"name", input.name},
            {"type", VFX::ToString(input.type)},
        });
    }
    nlohmann::json emitters = nlohmann::json::array();
    for (const VFX::VfxEmitterData& emitter : asset->emitters) {
        nlohmann::json summary = EmitterSummary(emitter);
        summary["hidden"] = controller.Preview().IsEmitterHidden(emitter.id);
        summary["previewVisible"] = controller.Preview().IsEmitterPreviewVisible(emitter.id);
        emitters.push_back(std::move(summary));
    }
    const VfxEditorDocument& document = controller.GetDocument();
    return {
        {"workspace", "vfx"},
        {"open", true},
        {"assetGuid", document.GetAssetGuid().value},
        {"sourcePath", document.GetSourcePath().generic_string()},
        {"name", asset->name},
        {"durationSeconds", asset->durationSeconds},
        {"authoringSemantics",
         {
             {"time",
              "startDelay uses absolute composition seconds, not parent-local time. Group Move/Retime transforms descendant timing together; Trim changes the visibility window. Direct timing property edits affect only the addressed layer."},
             {"duration",
              "activeDuration=0 lasts to composition end. Particle emission windows and particle lifetimes differ; emitted particles may outlive emission."},
             {"composition",
              "Shape surfaces, Particles, Group transforms/tint and Adjustment effects can combine. Spatial movement, surface flow and temporal envelopes are separate controls."},
             {"inspection",
              "Preview visibility flags and drawable submissions do not prove visible pixels or visual quality. Check the actual frame at the relevant time."},
         }},
        {"loop", asset->loop},
        {"dirty", document.IsDirty()},
        {"canUndo", document.CanUndo()},
        {"canRedo", document.CanRedo()},
        {"revision", document.GetRevision()},
        {"runtimeAssetRefresh",
         {
             {"attempted", controller.WasLastAssetRefreshAttempted()},
             {"succeeded", controller.DidLastAssetRefreshSucceed()},
             {"message", controller.GetLastAssetRefreshMessage()},
         }},
        {"preview",
         {
             {"playing", controller.Preview().IsPreviewPlaying()},
             {"paused", controller.Preview().IsPreviewPaused()},
             {"elapsedSeconds", controller.Preview().GetPreviewElapsedSeconds()},
             {"playbackSpeed", controller.Preview().GetPreviewPlaybackSpeed()},
             {"loopEnabled", controller.Preview().IsPreviewLoopEnabled()},
             {"loopStartSeconds", controller.Preview().GetPreviewLoopStartSeconds()},
             {"loopEndSeconds", controller.Preview().GetPreviewLoopEndSeconds()},
             {"soloEmitterId", controller.Preview().GetSoloEmitterId()},
         }},
        {"inputs", std::move(inputs)},
        {"emitters", std::move(emitters)},
    };
}

nlohmann::json WriteVfxDiagnostics(const VfxWorkspaceController& controller) {
    nlohmann::json diagnostics = nlohmann::json::array();
    for (const VFX::VfxDiagnostic& diagnostic : controller.GetDiagnostics()) {
        diagnostics.push_back({
            {"severity", diagnostic.severity == VFX::VfxDiagnosticSeverity::Error     ? "error"
                         : diagnostic.severity == VFX::VfxDiagnosticSeverity::Warning ? "warning"
                                                                                      : "info"},
            {"code", diagnostic.code},
            {"message", diagnostic.message},
            {"subjectId", diagnostic.subjectId},
        });
    }
    return diagnostics;
}

bool DescribeVfxTarget(const VfxWorkspaceController& controller, std::string_view targetId,
                       nlohmann::json& outDescription) {
    const VFX::VfxAssetData* asset = controller.GetAsset();
    if (asset == nullptr || !DescribeTypedTarget(controller, targetId, outDescription)) {
        return false;
    }
    nlohmann::json document{};
    std::string message{};
    if (!BuildVfxSerializedDocument(*asset, document, message)) {
        return false;
    }
    const std::string resolvedTarget = outDescription.value("targetId", std::string(targetId));
    const nlohmann::json* target = FindVfxSerializedTarget(document, resolvedTarget);
    if (target == nullptr)
        return false;
    outDescription["properties"] = DescribeVfxSerializedProperties(*target);
    if (resolvedTarget != "asset") {
        outDescription["serializedValue"] = *target;
    }
    for (const auto& layer : asset->emitters) {
        const auto effect = std::find_if(layer.effects.begin(), layer.effects.end(),
                                         [&](const auto& e) { return e.id == resolvedTarget; });
        if (effect == layer.effects.end())
            continue;
        outDescription["effectCategory"] = MaterialEffectCategoryName(GetMaterialEffectCategory(effect->kind));
        const bool spatial = effect->space == MaterialEffectSpace::Group3D;
        const bool composite = layer.kind == VFX::VfxLayerKind::Adjustment && IsCompositeAdjustmentEffect(*effect);
        outDescription["executionStage"] =
            spatial ? "3D geometry"
                    : (composite ? "Lower-layer composition"
                                 : MaterialEffectStageName(GetMaterialEffectStage(effect->kind)));
        outDescription["coordinates"] =
            spatial
                ? "Owner transform defines the 3D frame; center 0.5 is its origin. Geometry and thickness remain 3D. Planar geometry uses particle orientation, not camera-facing billboards."
                : (composite
                       ? "Post-render image of lower layers in this group; widths are normalized image units. Does not affect scene background."
                       : "Layer-local UV; widths/radii/cell sizes are UV units, independent of camera and viewport.");
        for (auto& property : outDescription["properties"]) {
            const auto id = property.value("id", std::string{});
            if (id == "space")
                property["choices"] = SupportsSpatialMaterialEffect(effect->kind)
                                          ? nlohmann::json::array({"LayerUv", "Group3D"})
                                          : nlohmann::json::array({"LayerUv"});
            if (id == "mix") {
                property["range"] = {0, 1};
                property["animatable"] = true;
            }
            if (id == "mask") {
                property["choices"] = nlohmann::json::object();
                const auto masks = GetMaterialEffectMasks(effect->space);
                for (size_t i = 0; i < masks.size(); ++i)
                    property["choices"][std::to_string(i)] = masks[i];
            }
            if (id == "textureTarget")
                property["choices"] = GetMaterialEffectTextureTargets(effect->kind);
            if (id == "maskParameters")
                property["components"] = {
                    {"x", "Center X"}, {"y", "Center Y"}, {"z", "Radius or boundary"}, {"w", "Feather"}};
            if (id != "parameters" && id != "parameters2")
                continue;
            const auto info = GetMaterialEffectParameters(*effect, id == "parameters2");
            property["components"] = nlohmann::json::object();
            for (size_t c = 0; c < 4; ++c)
                if (info[c].name)
                    property["components"][std::string(1, "xyzw"[c])] = {{"name", info[c].name},
                                                                         {"range", {info[c].minimum, info[c].maximum}},
                                                                         {"animatable", info[c].animatable}};
        }
        break;
    }
    return true;
}

} // namespace HIKARI::EDITOR
