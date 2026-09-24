#include "Editor/Authoring/HIKARI_TerrainAuthoringOperations.h"
#include "Editor/Authoring/HIKARI_SceneObjectTargetResolver.h"
#include "Editor/History/HIKARI_SceneComponentPropertiesHistoryCommand.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Terrain/HIKARI_SurfaceLayerRegistry.h"
#include "Scene/Terrain/HIKARI_SurfacePreset.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"
#include <algorithm>
#include <limits>

namespace HIKARI::EDITOR {
namespace {
struct TerrainTarget {
    GameObject* object = nullptr;
    TerrainSurfaceComponent* surface = nullptr;
    SceneComponentData* documentComponent = nullptr;
};

bool ResolveTerrainTarget(DocumentSceneBase& scene, const nlohmann::json& parameters, TerrainTarget& target,
                          EditorOperationResult& result) {
    std::optional<SceneObjectId> objectId;
    if (!ReadOptionalSceneObjectId(parameters, objectId, result))
        return false;
    const auto resolution = ResolveSceneObjectTarget(scene, objectId, parameters.value("name", std::string{}));
    if (!resolution.IsResolved()) {
        result = TargetResolutionFailure(resolution);
        return false;
    }
    target.object = resolution.runtimeObject;
    target.surface = target.object ? target.object->GetComponent<TerrainSurfaceComponent>() : nullptr;
    if (!target.surface) {
        result.code = "TERRAIN_COMPONENT_NOT_FOUND";
        result.message = "Target has no editable TerrainSurfaceComponent.";
        return false;
    }
    if (resolution.documentObject) {
        for (auto& component : resolution.documentObject->components) {
            if (component.type == "TerrainSurfaceComponent") {
                target.documentComponent = &component;
                return true;
            }
        }
    }
    result.code = "TERRAIN_DOCUMENT_COMPONENT_NOT_FOUND";
    result.message = "Target terrain has no matching document component.";
    return false;
}
} // namespace

TerrainAuthoringResult FillTerrainLayer(DocumentSceneBase& document, const nlohmann::json& parameters) {
    TerrainAuthoringResult output;
    auto& result = output.result;

    const std::string presetId = parameters.value("presetId", std::string{});
    if (presetId.empty()) {
        result.code = "INVALID_PRESET_ID";
        result.message = "presetId 不能为空";
        return output;
    }

    TerrainTarget resolved;
    if (!ResolveTerrainTarget(document, parameters, resolved, result))
        return output;
    auto* target = resolved.object;
    auto* terrain = resolved.surface;
    auto* documentComponent = resolved.documentComponent;

    TERRAIN::SurfaceLayerRegistry::Get().RegisterBuiltins();
    const TERRAIN::SurfacePreset* registeredPreset = TERRAIN::SurfaceLayerRegistry::Get().Find(presetId);
    const std::optional<TERRAIN::SurfacePreset> fallbackPreset =
        registeredPreset == nullptr ? TERRAIN::ResolveSurfacePreset(presetId) : std::nullopt;
    if (registeredPreset == nullptr && !fallbackPreset.has_value()) {
        result.code = "TERRAIN_PRESET_NOT_FOUND";
        result.message = "地形材质层 preset 不存在: " + presetId;
        return output;
    }

    nlohmann::json beforeProperties = documentComponent->properties;
    const bool fillChanged = terrain->FillLayer(presetId);
    if (!fillChanged) {
        // FillLayer=false is also the idempotent success path: a valid
        // preset whose layer weights already match the target has no
        // document delta and must not be reported as a lookup failure.
        result.ok = true;
        result.code = "NO_CHANGE";
        result.message = "地形已经是目标材质层，无需填充: " + presetId;
    } else {
        // FillLayer changes every chunk's layer weights. Serialize the
        // full component once and use the existing document history
        // command; this keeps the operation on the same authoritative
        // transaction path as the terrain workspace without introducing
        // a second history stack.
        nlohmann::json afterProperties = nlohmann::json::object();
        terrain->Serialize(afterProperties);
        if (afterProperties == beforeProperties) {
            // Keep the operation idempotent if a legacy runtime
            // projection reports no serialized difference.
            result.ok = true;
            result.code = "NO_CHANGE";
            result.message = "地形已经是目标材质层，无需填充: " + presetId;
        } else {
            documentComponent->properties = afterProperties;
            output.command = EDITOR::MakeSceneComponentPropertiesHistoryCommand(
                TEXT::Localize("ui.terrain.fillLayerHistory"), target->GetDocumentId(), documentComponent->id,
                std::move(beforeProperties), std::move(afterProperties), EDITOR::EditorDocumentImpact::RuntimeWorld);
            result.ok = true;
            result.code = "OK";
            result.message = "已将地形整体填充为材质层: " + presetId;
        }
    }

    output.reveal = target->GetDocumentId();
    result.data = {
        {"objectId", target->GetDocumentId().value},
        {"componentId", documentComponent->id.value},
        {"presetId", presetId},
        {"workspace", "terrain_surface"},
    };
    return output;
}

TerrainAuthoringResult GenerateTerrain(DocumentSceneBase& document, const nlohmann::json& parameters) {
    TerrainAuthoringResult output;
    auto& result = output.result;

    TerrainTarget resolved;
    if (!ResolveTerrainTarget(document, parameters, resolved, result))
        return output;
    auto* terrain = resolved.surface;
    auto* documentComponent = resolved.documentComponent;

    TERRAIN::TerrainProceduralSpec spec{};
    const std::string mode = parameters.value("mode", std::string("noise_hills"));
    if (!TERRAIN::TryParseTerrainProceduralMode(mode, spec.mode)) {
        result.code = "INVALID_TERRAIN_PROCEDURAL_MODE";
        result.message = "未知的程序化地形类型: " + mode;
        return output;
    }
    const std::string blendMode = parameters.value("blendMode", std::string("add"));
    if (!TERRAIN::TryParseTerrainProceduralBlendMode(blendMode, spec.blendMode)) {
        result.code = "INVALID_TERRAIN_PROCEDURAL_BLEND_MODE";
        result.message = "未知的程序化地形混合方式: " + blendMode;
        return output;
    }
    const auto readUnsigned64 = [&parameters](const char* key, uint64_t fallback) noexcept {
        const auto found = parameters.find(key);
        if (found == parameters.end()) {
            return fallback;
        }
        if (found->is_number_unsigned()) {
            return found->get<uint64_t>();
        }
        if (found->is_number_integer()) {
            const int64_t value = found->get<int64_t>();
            return value >= 0 ? static_cast<uint64_t>(value) : fallback;
        }
        return fallback;
    };
    const auto readUnsigned32 = [&readUnsigned64](const char* key, uint32_t fallback) noexcept {
        return static_cast<uint32_t>(std::min<uint64_t>(readUnsigned64(key, fallback),
                                                        static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())));
    };
    spec.seed = readUnsigned64("seed", spec.seed);
    spec.centerX = parameters.value("centerX", spec.centerX);
    spec.centerZ = parameters.value("centerZ", spec.centerZ);
    spec.extentX = parameters.value("extentX", spec.extentX);
    spec.extentZ = parameters.value("extentZ", spec.extentZ);
    spec.amplitude = parameters.value("amplitude", spec.amplitude);
    spec.baseHeight = parameters.value("baseHeight", spec.baseHeight);
    spec.scale = parameters.value("scale", spec.scale);
    spec.edgeFalloff = parameters.value("edgeFalloff", spec.edgeFalloff);
    spec.blendAmount = parameters.value("blendAmount", spec.blendAmount);
    spec.directionDegrees = parameters.value("directionDegrees", spec.directionDegrees);
    spec.octaves = readUnsigned32("octaves", spec.octaves);
    spec = TERRAIN::SanitizeTerrainProceduralSpec(spec);

    terrain->BeginGeometryEditBatch();
    TERRAIN::TerrainProceduralPreview preview{};
    const bool applied = terrain->ApplyProceduralTerrain(spec, preview);
    (void)terrain->EndGeometryEditBatch();
    nlohmann::json beforeDelta{};
    nlohmann::json afterDelta{};
    const bool hasDelta = terrain->ConsumeGeometryEditDelta(beforeDelta, afterDelta);
    if (!applied && preview.budgetExceeded) {
        result.code = "TERRAIN_PROCEDURAL_BUDGET_EXCEEDED";
        result.message = "程序化地形范围超过可安全撤销的生成预算，请缩小范围或分块生成";
        result.data = {
            {"objectId", resolved.object->GetDocumentId().value},
            {"candidateVertices", preview.candidateVertices},
            {"maximumCandidateVertices", TERRAIN::kTerrainProceduralMaximumCandidateVertices},
        };
        return output;
    }
    if (!applied) {
        result.ok = true;
        result.code = "NO_CHANGE";
        result.message = "程序化地形范围没有产生变化";
        result.data = {
            {"objectId", resolved.object->GetDocumentId().value},
            {"affectedChunks", preview.affectedChunks},
            {"affectedVertices", preview.affectedVertices},
        };
        return output;
    }
    if (!hasDelta || !EDITOR::ApplyTerrainSurfaceEditDelta(documentComponent->properties, afterDelta)) {
        // The runtime edit is provisional until the keyed document delta
        // is admitted. Restore the authoritative serialized component so
        // an internal history failure cannot leave runtime and document
        // terrain out of sync.
        terrain->Deserialize(documentComponent->properties);
        result.code = "TERRAIN_PROCEDURAL_TRANSACTION_FAILED";
        result.message = "程序化地形差分写入失败，运行时改动已回滚";
        return output;
    }

    output.command = EDITOR::MakeSceneTerrainSurfaceEditDeltaHistoryCommand(
        TEXT::Localize("ui.terrain.proceduralHistory"), resolved.object->GetDocumentId(), documentComponent->id,
        std::move(beforeDelta), std::move(afterDelta), EDITOR::EditorDocumentImpact::RuntimeWorld);
    output.reveal = resolved.object->GetDocumentId();
    result.ok = true;
    result.code = "OK";
    result.message = "已生成程序化地形，并写入一次可撤销的地形差分";
    result.data = {
        {"objectId", resolved.object->GetDocumentId().value},
        {"affectedChunks", preview.affectedChunks},
        {"newChunks", preview.newChunks},
        {"affectedVertices", preview.affectedVertices},
        {"mode", TERRAIN::ToString(spec.mode)},
        {"seed", spec.seed},
        {"workspace", "terrain_surface"},
    };
    return output;
}
} // namespace HIKARI::EDITOR
