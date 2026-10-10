#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include <string>
#include <utility>

#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevArrange.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevShapeHandles.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"

// 布景の操作。画面のボタン（RunLookDevOperation）と智能体が同じ定義を呼ぶ。文書の変更は場面の取り消しに積み、
// 視口の手柄・主光のドラッグ・放置の下書き・A/B 分屏のような操作の途中は開放しない（結果は灯と変形の操作で変える）。
namespace HIKARI {
namespace {

using EDITOR::EditorOperationParameterType;

template <class E, size_t N>
nlohmann::json EnumSchema(const TEXT::EnumName<E> (&table)[N]) {
    nlohmann::json names = nlohmann::json::array();
    for (const auto& entry : table) {
        names.push_back(entry.name);
    }
    return {{"enum", std::move(names)}};
}

nlohmann::json IdArraySchema() { return {{"type", "array"}, {"items", {{"type", "integer"}}}}; }

} // namespace

void DocumentSceneEditorController::RegisterLookDevOperations() {
    const auto mutation = [this](EDITOR::EditorOperationDefinition definition) {
        definition.permission = EDITOR::EditorOperationPermission::ModifyEditorState;
        operationRegistry_.Register(std::move(definition), EDITOR::EditorOperationKind::Mutation, false);
    };
    mutation({"scene.arrange",
              "Arrange scene objects by their world bounds as one undo step: align, distribute, drop onto the surface "
              "below, group under a new parent, mirror-copy, or array-copy (linear, grid, ring). Children move with "
              "their parent. Copies are selected afterwards.",
              "scene",
              {{"action", EditorOperationParameterType::String, true, "align, distribute, drop, group, mirror, array",
                EnumSchema(EDITOR::kLookDevArrangeActionNames)},
               {"objectIds", EditorOperationParameterType::Json, false, "Objects to arrange; default is the selection",
                IdArraySchema()},
               {"axis", EditorOperationParameterType::String, false,
                "World axis for align (default y), distribute and mirror (default x)",
                EnumSchema(EDITOR::kLookDevAxisNames)},
               {"anchor", EditorOperationParameterType::String, false, "align: min, center (default) or max",
                EnumSchema(EDITOR::kLookDevAlignNames)},
               {"referenceId", EditorOperationParameterType::Int, false,
                "align: object whose bounds the others match; default is the active selection"},
               {"name", EditorOperationParameterType::String, false, "group: name of the new parent"},
               {"kind", EditorOperationParameterType::String, false,
                "array: linear (along +X), grid (X by Z) or ring (around a center radius metres toward -X)",
                EnumSchema(EDITOR::kLookDevArrayKindNames)},
               {"count", EditorOperationParameterType::Int, false, "array: items per row including the original, 2-24"},
               {"rows", EditorOperationParameterType::Int, false, "array grid: rows, 1-24"},
               {"spacing", EditorOperationParameterType::Float, false, "array linear/grid: metres between copies"},
               {"radius", EditorOperationParameterType::Float, false, "array ring: radius in metres"}},
              [this](const nlohmann::json& p) { return ArrangeLookDevObjects(p); }});
    mutation({"procedural.opening",
              "Add, edit or remove an opening cut through a procedural Box (door, window, hole; round holes and "
              "windows go straight through the thin axis). Presets: door 1.0x2.1 m from the floor, window 1.2x1.2 m "
              "on a 0.9 m sill, hole 2 m square, round hole 1.6 m, round window 1.0 m on a 1.0 m sill. center/size are "
              "in the object's local units with the box centre at the origin. Collision follows the openings.",
              "scene",
              {{"action",
                EditorOperationParameterType::String,
                true,
                "add, edit or remove",
                {{"enum", {"add", "edit", "remove"}}}},
               {"objectId", EditorOperationParameterType::Int, true, "Procedural Box object"},
               {"kind", EditorOperationParameterType::String, false, "add: opening preset",
                EnumSchema(EDITOR::kLookDevOpeningKindNames)},
               {"index", EditorOperationParameterType::Int, false, "edit/remove: opening index"},
               {"center", EditorOperationParameterType::Json, false, "add/edit: local centre XYZ",
                EDITOR::EditorOperationVec3Schema()},
               {"size", EditorOperationParameterType::Json, false, "add/edit: local size XYZ",
                EDITOR::EditorOperationVec3Schema()}},
              [this](const nlohmann::json& p) { return EditLookDevOpening(p); }});
    mutation({"procedural.subtract",
              "Cut the volume of a procedural cutter shape out of the procedural Boxes it overlaps (each becomes an "
              "opening), then delete the cutter unless keepCutter is true.",
              "scene",
              {{"cutterId", EditorOperationParameterType::Int, true, "Procedural shape used as the cutter"},
               {"targetIds", EditorOperationParameterType::Json, false,
                "Boxes to cut; default is every overlapping unlocked Box", IdArraySchema()},
               {"keepCutter", EditorOperationParameterType::Bool, false, "Keep the cutter object (default false)"}},
              [this](const nlohmann::json& p) { return SubtractLookDevShapes(p); }});
    mutation({"scene.replace_model",
              "Replace a procedural blockout shape with a model asset, scaled uniformly to fit inside the shape and "
              "standing on the shape's bottom centre.",
              "scene",
              {{"objectId", EditorOperationParameterType::Int, true, "Procedural shape object"},
               {"modelId", EditorOperationParameterType::String, true, "Model asset GUID"}},
              [this](const nlohmann::json& p) { return ReplaceLookDevShapeWithModel(p); }});
    mutation({"scene.create_light",
              "Create a light object. Brightness defaults to scene-relative (a multiple of the environment's key "
              "illuminance, so it stays balanced across presets); pass settings.intensityUnit Lumens for physical "
              "units. Aim with aimAt (world point) or rotation; default points straight down.",
              "scene",
              {{"type", EditorOperationParameterType::String, true, "Point, Spot, Tube or Rect",
                EnumSchema(RENDER3D::LIGHTING::kSceneLightTypeNames)},
               {"position", EditorOperationParameterType::Json, true, "World XYZ", EDITOR::EditorOperationVec3Schema()},
               {"aimAt", EditorOperationParameterType::Json, false, "World XYZ the light faces",
                EDITOR::EditorOperationVec3Schema()},
               {"rotation", EditorOperationParameterType::Json, false, "Euler XYZ degrees when aimAt is absent",
                EDITOR::EditorOperationVec3Schema()},
               {"name", EditorOperationParameterType::String, false, "Display name"},
               {"settings",
                EditorOperationParameterType::Json,
                false,
                "Partial LightSettings: color, sceneRelativeIntensity, intensity (lm), intensityUnit, range, "
                "castsShadows, innerConeAngleDegrees, outerConeAngleDegrees, length, height, sourceRadius",
                {{"type", "object"}}}},
              [this](const nlohmann::json& p) { return CreateLookDevLight(p); }});
    mutation({"lighting.three_point",
              "Light an object with key, fill and rim lights sized to its bounds (scene-relative brightness; only the "
              "key casts shadows).",
              "scene",
              {{"objectId", EditorOperationParameterType::Int, true, "Subject object"},
               {"viewAzimuth", EditorOperationParameterType::Float, false,
                "Horizontal direction from the subject toward the viewer in degrees (0 = +Z, 90 = +X); default is the "
                "editor camera"}},
              [this](const nlohmann::json& p) { return CreateThreePointLights(p); }});
    EDITOR::EditorOperationDefinition solo{
        "view.light_solo",
        "Show only one light's local lighting in the editor viewport (0 shows all). Editor view state: not saved and "
        "not undoable.",
        "scene",
        {{"objectId", EditorOperationParameterType::Int, true, "Light object, or 0 to show every light"}},
        [this](const nlohmann::json& p) {
            const uint64_t id = p.at("objectId").get<uint64_t>();
            const DocumentSceneBase* scene = aiOperationScene_;
            const GameObject* object = scene != nullptr ? scene->GetWorld().FindObject(SceneObjectId{id}) : nullptr;
            if (id != 0u && (object == nullptr || object->GetComponent<LightComponent>() == nullptr)) {
                return EDITOR::OperationFailure("NOT_A_LIGHT", "objectId must be a light object or 0.");
            }
            context_.overlays.soloLightObjectId = id;
            return EDITOR::OperationOk(id != 0u ? "Only this light is shown in the editor viewport."
                                                : "Every light is shown.",
                                       {{"soloLightId", id}});
        }};
    solo.permission = EDITOR::EditorOperationPermission::ModifyEditorState;
    operationRegistry_.Register(std::move(solo), EDITOR::EditorOperationKind::Session, false);
}

} // namespace HIKARI
