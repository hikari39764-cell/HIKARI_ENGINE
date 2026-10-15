#include "Editor/Authoring/HIKARI_TerrainAuthoringOperations.h"
#include "Editor/Authoring/HIKARI_SceneObjectTargetResolver.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/History/HIKARI_SceneComponentPropertiesHistoryCommand.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Terrain/HIKARI_SurfaceLayerRegistry.h"
#include "Scene/Terrain/HIKARI_SurfacePreset.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

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
    if (!TEXT::TryParseEnum(TERRAIN::kTerrainProceduralModeNames, mode, spec.mode)) {
        result.code = "INVALID_TERRAIN_PROCEDURAL_MODE";
        result.message = "未知的程序化地形类型: " + mode;
        return output;
    }
    const std::string blendMode = parameters.value("blendMode", std::string("add"));
    if (!TEXT::TryParseEnum(TERRAIN::kTerrainProceduralBlendModeNames, blendMode, spec.blendMode)) {
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
namespace {
constexpr size_t kMaximumStrokeSamples = 4096u;

bool ReadWorldPoint(const nlohmann::json& value, MATH::Vec3& point) {
    if (!value.is_array() || (value.size() != 2u && value.size() != 3u)) {
        return false;
    }
    for (const nlohmann::json& component : value) {
        if (!component.is_number() || !std::isfinite(component.get<float>())) {
            return false;
        }
    }
    // [x, z] か [x, y, z]。高さは地形がブラシ位置で決めるので y は使わない。
    point = value.size() == 2u ? MATH::Vec3{value[0].get<float>(), 0.0f, value[1].get<float>()}
                               : MATH::Vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
    return true;
}

// 経路上か矩形内に、ブラシ半径に応じた間隔でサンプル点を並べる。
bool CollectStrokeSamples(const nlohmann::json& parameters, float spacing, std::vector<MATH::Vec3>& samples,
                          EditorOperationResult& result) {
    if (const auto found = parameters.find("points"); found != parameters.end()) {
        if (!found->is_array() || found->empty()) {
            result = OperationFailure("INVALID_POINTS", "points must be a non-empty array of [x, z] or [x, y, z].");
            return false;
        }
        MATH::Vec3 previous{};
        for (size_t index = 0; index < found->size(); ++index) {
            MATH::Vec3 point{};
            if (!ReadWorldPoint((*found)[index], point)) {
                result = OperationFailure("INVALID_POINTS", "points must contain finite [x, z] or [x, y, z] values.");
                return false;
            }
            if (index == 0u) {
                samples.push_back(point);
            } else {
                const float length = MATH::Length(point - previous);
                const size_t steps = (std::max)(size_t{1}, static_cast<size_t>(std::ceil(length / spacing)));
                for (size_t step = 1; step <= steps && samples.size() <= kMaximumStrokeSamples; ++step) {
                    samples.push_back(previous +
                                      (point - previous) * (static_cast<float>(step) / static_cast<float>(steps)));
                }
            }
            previous = point;
        }
    } else if (const auto rect = parameters.find("rect"); rect != parameters.end()) {
        MATH::Vec3 minimum{};
        MATH::Vec3 maximum{};
        if (!rect->is_object() || !ReadWorldPoint(rect->value("min", nlohmann::json{}), minimum) ||
            !ReadWorldPoint(rect->value("max", nlohmann::json{}), maximum)) {
            result = OperationFailure("INVALID_RECT", "rect must be {\"min\":[x,z],\"max\":[x,z]}.");
            return false;
        }
        const float x0 = (std::min)(minimum.x, maximum.x);
        const float x1 = (std::max)(minimum.x, maximum.x);
        const float z0 = (std::min)(minimum.z, maximum.z);
        const float z1 = (std::max)(minimum.z, maximum.z);
        for (float z = z0; z <= z1 + spacing * 0.5f && samples.size() <= kMaximumStrokeSamples; z += spacing) {
            for (float x = x0; x <= x1 + spacing * 0.5f && samples.size() <= kMaximumStrokeSamples; x += spacing) {
                samples.push_back({(std::min)(x, x1), 0.0f, (std::min)(z, z1)});
            }
        }
    } else {
        result = OperationFailure("MISSING_AREA", "Provide points (a path) or rect (an area).");
        return false;
    }
    if (samples.size() > kMaximumStrokeSamples) {
        result = OperationFailure("STROKE_TOO_LARGE",
                                  "The stroke needs more than 4096 brush stamps; enlarge radius or split the area.");
        return false;
    }
    return true;
}
} // namespace

TerrainAuthoringResult DescribeTerrain(DocumentSceneBase& document, const nlohmann::json& parameters) {
    TerrainAuthoringResult output;
    auto& result = output.result;
    TerrainTarget resolved;
    if (!ResolveTerrainTarget(document, parameters, resolved, result))
        return output;
    const TerrainSurfaceComponent& terrain = *resolved.surface;
    const float chunkSize = terrain.GetChunkWorldSize();
    nlohmann::json bounds = nullptr;
    if (!terrain.GetChunks().empty()) {
        int minX = (std::numeric_limits<int>::max)(), minZ = minX;
        int maxX = (std::numeric_limits<int>::min)(), maxZ = maxX;
        for (const auto& [key, chunk] : terrain.GetChunks()) {
            minX = (std::min)(minX, key.x);
            maxX = (std::max)(maxX, key.x);
            minZ = (std::min)(minZ, key.z);
            maxZ = (std::max)(maxZ, key.z);
        }
        const MATH::Vec3 low =
            terrain.LocalToWorld({static_cast<float>(minX) * chunkSize, 0.0f, static_cast<float>(minZ) * chunkSize});
        const MATH::Vec3 high = terrain.LocalToWorld(
            {static_cast<float>(maxX + 1) * chunkSize, 0.0f, static_cast<float>(maxZ + 1) * chunkSize});
        bounds = {{"min", {(std::min)(low.x, high.x), (std::min)(low.z, high.z)}},
                  {"max", {(std::max)(low.x, high.x), (std::max)(low.z, high.z)}}};
    }
    TERRAIN::SurfaceLayerRegistry::Get().RegisterBuiltins();
    nlohmann::json presets = nlohmann::json::array();
    for (const TERRAIN::SurfacePreset& layer : TERRAIN::SurfaceLayerRegistry::Get().Layers())
        presets.push_back(layer.id);
    nlohmann::json tools = nlohmann::json::array({"paint", "erase", "restore"});
    for (const auto& mode : TERRAIN::kTerrainBrushModeNames)
        tools.push_back(mode.name);
    const TERRAIN::TerrainBrush brush = terrain.GetBrush();
    nlohmann::json samples = nlohmann::json::array();
    for (const nlohmann::json& item : parameters.value("samples", nlohmann::json::array())) {
        MATH::Vec3 point{};
        float height = 0.0f;
        MATH::Vec3 normal{};
        if (ReadWorldPoint(item, point) && terrain.SampleWorldHeight(point, height, normal))
            samples.push_back({{"point", {point.x, point.z}}, {"height", height}});
        else
            samples.push_back({{"point", item}, {"height", nullptr}});
    }
    result.ok = true;
    result.code = "OK";
    result.message = "Terrain surface description.";
    result.data = {{"objectId", resolved.object->GetDocumentId().value},
                   {"chunkWorldSize", chunkSize},
                   {"chunkCount", terrain.GetChunks().size()},
                   {"worldBounds", std::move(bounds)},
                   {"basePreset", terrain.GetSurface().presetId},
                   {"surfacePresets", std::move(presets)},
                   {"tools", std::move(tools)},
                   {"brush",
                    {{"radius", brush.radius},
                     {"strength", brush.strength},
                     {"hardness", brush.hardness},
                     {"falloff", brush.falloff}}},
                   {"samples", std::move(samples)}};
    return output;
}

TerrainAuthoringResult StrokeTerrain(DocumentSceneBase& document, const nlohmann::json& parameters) {
    TerrainAuthoringResult output;
    auto& result = output.result;
    TerrainTarget resolved;
    if (!ResolveTerrainTarget(document, parameters, resolved, result))
        return output;
    TerrainSurfaceComponent& terrain = *resolved.surface;
    const std::string tool = parameters.value("tool", std::string{});
    const std::string presetId = parameters.value("presetId", std::string{});
    const auto sculptMode = TEXT::TryParseEnum(TERRAIN::kTerrainBrushModeNames, tool);
    if (!sculptMode && tool != "paint" && tool != "erase" && tool != "restore") {
        result = OperationFailure("INVALID_TOOL",
                                  "tool must be a sculpt mode, paint, erase or restore; see terrain.describe.");
        return output;
    }
    if (tool == "paint") {
        TERRAIN::SurfaceLayerRegistry::Get().RegisterBuiltins();
        if (TERRAIN::SurfaceLayerRegistry::Get().Find(presetId) == nullptr) {
            result = OperationFailure("TERRAIN_PRESET_NOT_FOUND", "paint needs a presetId from terrain.describe.");
            return output;
        }
    }
    // 呼び出しの間だけブラシを差し替え、保存される作者設定は元に戻す。
    const TERRAIN::TerrainBrush savedBrush = terrain.GetBrush();
    TERRAIN::TerrainBrush brush = savedBrush;
    brush.mode = sculptMode.value_or(brush.mode);
    brush.radius = parameters.value("radius", brush.radius);
    brush.strength = parameters.value("strength", brush.strength);
    brush.hardness = parameters.value("hardness", brush.hardness);
    brush.falloff = parameters.value("falloff", brush.falloff);
    brush.flattenHeight = parameters.value("flattenHeight", brush.flattenHeight);
    brush = TERRAIN::SanitizeTerrainBrush(brush);
    const float spacing = (std::max)(0.05f, brush.radius * 0.35f);
    std::vector<MATH::Vec3> samples;
    if (!CollectStrokeSamples(parameters, spacing, samples, result))
        return output;
    const size_t passes = static_cast<size_t>(std::clamp(parameters.value("passes", 1), 1, 16));
    constexpr float kStampSeconds = 1.0f / 30.0f;

    terrain.SetBrush(brush);
    terrain.BeginGeometryEditBatch();
    bool changed = false;
    for (size_t pass = 0; pass < passes; ++pass) {
        for (const MATH::Vec3& point : samples) {
            if (sculptMode)
                changed = terrain.SculptAtWorld(point, kStampSeconds) || changed;
            else if (tool == "paint")
                changed = terrain.PaintLayerAtWorld(presetId, point, kStampSeconds) || changed;
            else
                changed =
                    terrain.PaintCoverageAtWorld(tool == "restore" ? 1.0f : 0.0f, point, kStampSeconds) || changed;
        }
    }
    (void)terrain.EndGeometryEditBatch();
    terrain.SetBrush(savedBrush);
    nlohmann::json beforeDelta{};
    nlohmann::json afterDelta{};
    const bool hasDelta = terrain.ConsumeGeometryEditDelta(beforeDelta, afterDelta);
    if (!changed || !hasDelta) {
        result.ok = true;
        result.code = "NO_CHANGE";
        result.message = "The stroke did not change the terrain; check the area against terrain.describe bounds.";
        result.data = {{"objectId", resolved.object->GetDocumentId().value}, {"stamps", samples.size() * passes}};
        return output;
    }
    if (!EDITOR::ApplyTerrainSurfaceEditDelta(resolved.documentComponent->properties, afterDelta)) {
        // document の差分が受理されるまで runtime の変更は仮。失敗時は document の正本へ戻す。
        terrain.Deserialize(resolved.documentComponent->properties);
        result =
            OperationFailure("TERRAIN_STROKE_TRANSACTION_FAILED", "Terrain delta could not be recorded; rolled back.");
        return output;
    }
    output.command = EDITOR::MakeSceneTerrainSurfaceEditDeltaHistoryCommand(
        "AI Terrain Stroke", resolved.object->GetDocumentId(), resolved.documentComponent->id, std::move(beforeDelta),
        std::move(afterDelta), EDITOR::EditorDocumentImpact::RuntimeWorld);
    output.reveal = resolved.object->GetDocumentId();
    result.ok = true;
    result.code = "OK";
    result.message = "Terrain stroke applied as one undo step.";
    result.data = {{"objectId", resolved.object->GetDocumentId().value},
                   {"tool", tool},
                   {"stamps", samples.size() * passes},
                   {"workspace", "terrain_surface"}};
    return output;
}
} // namespace HIKARI::EDITOR
