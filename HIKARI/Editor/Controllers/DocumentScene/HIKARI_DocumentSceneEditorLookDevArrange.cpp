#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Assets/Models/HIKARI_ModelBounds.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Authoring/HIKARI_EditorObjectState.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Views/HIKARI_EditorViewportGeometry.h"
#include "Editor/Widgets/HIKARI_InspectorPropertyLayout.h"
#include "Editor/Widgets/HIKARI_NumberField.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevArrange.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevDocument.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevShapeHandles.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

#if defined(HIKARI_WITH_EDITOR)
namespace {

constexpr float kDegreesToRadians = 0.0174532925f;

MATH::Vec3 Rotate(const MATH::Vec3& eulerDegrees, const MATH::Vec3& vector) {
    const MATH::Quat rotation = MATH::Quat::FromEulerXYZ(
        eulerDegrees.x * kDegreesToRadians, eulerDegrees.y * kDegreesToRadians, eulerDegrees.z * kDegreesToRadians);
    const MATH::Vec4 rotated =
        MATH::Mat4::TRS({}, rotation, {1.0f, 1.0f, 1.0f}).TransformPoint({vector.x, vector.y, vector.z, 0.0f});
    return {rotated.x, rotated.y, rotated.z};
}

} // namespace

void DocumentSceneEditorController::EditLookDevDocument(DocumentSceneBase& scene, std::string label,
                                                        const std::function<bool(SceneDocument&)>& edit) {
    FinishLookDevObjectDrag(scene);
    selectionSync_.SyncNextSceneObjectId(scene, context_.nextSceneObjectId);
    SceneDocument& document = scene.GetSceneDocument();
    std::vector<SceneObjectData> beforeObjects = document.objects;
    const SceneCameraSettings camera = document.camera;
    const bool dirtyBefore = context_.sceneDirty || scene.HasUnsavedSceneChanges();
    if (!edit(document)) {
        return;
    }
    context_.sceneDirty = true;
    selectionSync_.SyncNextSceneObjectId(scene, context_.nextSceneObjectId);
    CommitSceneObjectAuthoringHistory(
        scene, SceneObjectAuthoringHistoryRequest{std::move(label), std::move(beforeObjects), document.objects, camera,
                                                  camera, dirtyBefore, true, true});
}

std::vector<EDITOR::LookDevArrangeItem> DocumentSceneEditorController::CollectLookDevArrangeItems(
    DocumentSceneBase& scene) {
    const SceneDocument& document = scene.GetSceneDocument();
    std::vector<EDITOR::LookDevArrangeItem> items;
    for (SceneObjectId root : CollectRootObjectIds(document, context_.selection.GetSelectedObjectIds())) {
        Bounds bounds = BOUNDS::EmptyBounds();
        bool found = false;
        for (uint64_t id : CollectObjectSubtreeIds(document, root)) {
            if (const auto objectBounds = EDITOR::FindLookDevObjectBounds(id)) {
                BOUNDS::Encapsulate(bounds, *objectBounds);
                found = true;
            }
        }
        if (!found) {
            // 描かれない物（空の物・灯）は位置の点。
            const MATH::Vec3 position = EDITOR::LookDevTranslation(EDITOR::LookDevWorldMatrix(scene, root));
            bounds = {position, position};
        }
        items.push_back({root.value, bounds});
    }
    return items;
}

void DocumentSceneEditorController::MoveLookDevItems(DocumentSceneBase& scene, std::string label,
                                                     const std::vector<EDITOR::LookDevArrangeItem>& items,
                                                     const std::vector<MATH::Vec3>& moves) {
    EditLookDevDocument(scene, std::move(label), [&](SceneDocument& document) {
        bool changed = false;
        for (size_t index = 0; index < items.size() && index < moves.size(); ++index) {
            SceneObjectData* object = FindSceneObjectData(document, SceneObjectId{items[index].id});
            if (object == nullptr || MATH::Length(moves[index]) < 1.0e-6f ||
                EDITOR::IsObjectEditorLocked(document, object->id)) {
                continue;
            }
            object->transform.position =
                object->transform.position + EDITOR::LookDevParentLocalVector(scene, *object, moves[index]);
            changed = true;
        }
        return changed;
    });
}

void DocumentSceneEditorController::CloneLookDevSelection(DocumentSceneBase& scene, std::string label, size_t copies,
                                                          const LookDevClonePlacement& place) {
    std::vector<SceneObjectId> created;
    EditLookDevDocument(scene, std::move(label), [&](SceneDocument& document) {
        const std::vector<SceneObjectId> roots =
            CollectRootObjectIds(document, context_.selection.GetSelectedObjectIds());
        std::unordered_set<uint64_t> subtree;
        for (SceneObjectId root : roots) {
            const auto ids = CollectObjectSubtreeIds(document, root);
            subtree.insert(ids.begin(), ids.end());
        }
        std::vector<SceneObjectData> source;
        for (const SceneObjectData& object : document.objects) {
            if (subtree.contains(object.id.value)) {
                source.push_back(object);
            }
        }
        if (source.empty()) {
            return false;
        }
        for (size_t copy = 0; copy < copies; ++copy) {
            std::unordered_map<uint64_t, uint64_t> remapped;
            if (!AppendClonedSceneObjects(source, document, context_.nextSceneObjectId, remapped)) {
                return false;
            }
            place(copy, document, remapped, roots);
            for (SceneObjectId root : roots) {
                created.push_back(SceneObjectId{remapped.at(root.value)});
            }
        }
        return true;
    });
    if (!created.empty()) {
        SelectViewportObjects(scene, created, EditorObjectSelectionMode::Replace);
    }
}

void DocumentSceneEditorController::CreateLookDevArray(DocumentSceneBase& scene) {
    const EDITOR::LookDevArraySettings& settings = lookDevWorkspace_.ArraySettings();
    const std::vector<EDITOR::LookDevArrayCopy> copies = EDITOR::BuildLookDevArray(settings);
    const Bounds bounds = EDITOR::UnionLookDevBounds(CollectLookDevArrangeItems(scene));
    const MATH::Vec3 center = (bounds.min + bounds.max) * 0.5f;
    // 環形の円の中心は、選んだ物の中心から -X へ半径だけ離れた点。
    const MATH::Vec3 ringCenter{center.x - settings.radius, center.y, center.z};
    CloneLookDevSelection(
        scene, "Array", copies.size(),
        [&](size_t copy, SceneDocument& document, const std::unordered_map<uint64_t, uint64_t>& remapped,
            const std::vector<SceneObjectId>& roots) {
            const EDITOR::LookDevArrayCopy& placement = copies[copy];
            for (SceneObjectId sourceRoot : roots) {
                SceneObjectData* root = FindSceneObjectData(document, SceneObjectId{remapped.at(sourceRoot.value)});
                if (root == nullptr) {
                    continue;
                }
                const MATH::Vec3 position = EDITOR::LookDevTranslation(EDITOR::LookDevWorldMatrix(scene, sourceRoot));
                MATH::Vec3 target = position + placement.offset;
                if (settings.kind == EDITOR::LookDevArrayKind::Ring) {
                    const MATH::Vec3 local = position - ringCenter;
                    const float radians = placement.yawDegrees * kDegreesToRadians;
                    target = ringCenter + MATH::Vec3{local.x * std::cos(radians) + local.z * std::sin(radians), local.y,
                                                     -local.x * std::sin(radians) + local.z * std::cos(radians)};
                }
                root->transform.position =
                    root->transform.position + EDITOR::LookDevParentLocalVector(scene, *root, target - position);
                root->transform.rotationEulerDeg.y += placement.yawDegrees;
                root->name += " " + std::to_string(copy + 2u);
            }
        });
}

void DocumentSceneEditorController::MirrorLookDevSelection(DocumentSceneBase& scene, int axis) {
    const Bounds bounds = EDITOR::UnionLookDevBounds(CollectLookDevArrangeItems(scene));
    const MATH::Vec3 center = (bounds.min + bounds.max) * 0.5f;
    CloneLookDevSelection(
        scene, "Mirror Copy", 1u,
        [&](size_t, SceneDocument& document, const std::unordered_map<uint64_t, uint64_t>& remapped,
            const std::vector<SceneObjectId>&) {
            // 複製した物をすべて世界で鏡に映す（位置と向き、拡縮はそのまま）。親が先に決まるよう何度か回し、
            // 子は鏡に映した親に対する局所へ戻す。形そのものは裏返らない（左右対称の白模は見た目どおり）。
            std::unordered_map<uint64_t, MATH::Mat4> mirroredWorld;
            std::unordered_map<uint64_t, uint64_t> cloneToSource;
            for (const auto& [source, clone] : remapped) {
                cloneToSource.emplace(clone, source);
            }
            for (size_t pass = 0; pass < remapped.size() && mirroredWorld.size() < remapped.size(); ++pass) {
                for (const auto& [source, cloneId] : remapped) {
                    SceneObjectData* clone = FindSceneObjectData(document, SceneObjectId{cloneId});
                    if (clone == nullptr || mirroredWorld.contains(cloneId)) {
                        continue;
                    }
                    const bool parentCloned = clone->parent && cloneToSource.contains(clone->parent->value);
                    if (parentCloned && !mirroredWorld.contains(clone->parent->value)) {
                        continue;
                    }
                    MATH::Vec3 translation{};
                    MATH::Quat rotation = MATH::Quat::Identity();
                    MATH::Vec3 scale{1.0f, 1.0f, 1.0f};
                    if (!MATH::DecomposeTRS(EDITOR::LookDevWorldMatrix(scene, SceneObjectId{source}), translation,
                                            rotation, scale)) {
                        mirroredWorld.emplace(cloneId, MATH::Mat4::Identity());
                        continue;
                    }
                    const MATH::Vec3 euler =
                        EDITOR::MirrorLookDevRotation(MATH::EulerXYZDegreesFromQuat(rotation), axis);
                    const MATH::Mat4 world = MATH::Mat4::TRS(EDITOR::MirrorLookDevPosition(translation, center, axis),
                                                             MATH::Quat::FromEulerXYZ(euler.x * kDegreesToRadians,
                                                                                      euler.y * kDegreesToRadians,
                                                                                      euler.z * kDegreesToRadians),
                                                             scale);
                    mirroredWorld.emplace(cloneId, world);
                    // 親（複製した親なら鏡に映した後、外の親ならそのまま）に対する局所。
                    MATH::Mat4 parentWorld = MATH::Mat4::Identity();
                    if (parentCloned) {
                        parentWorld = mirroredWorld.at(clone->parent->value);
                    } else if (clone->parent) {
                        parentWorld = EDITOR::LookDevWorldMatrix(scene, *clone->parent);
                    }
                    MATH::Vec3 localTranslation{};
                    MATH::Quat localRotation = MATH::Quat::Identity();
                    MATH::Vec3 localScale{1.0f, 1.0f, 1.0f};
                    if (MATH::DecomposeTRS(MATH::Inverse(parentWorld) * world, localTranslation, localRotation,
                                           localScale)) {
                        clone->transform.position = localTranslation;
                        clone->transform.rotationEulerDeg =
                            MATH::EulerXYZDegreesFromQuatNearest(localRotation, clone->transform.rotationEulerDeg);
                        clone->transform.scale = localScale;
                    }
                }
            }
        });
}

void DocumentSceneEditorController::DropLookDevSelection(DocumentSceneBase& scene) {
    const std::vector<EDITOR::LookDevArrangeItem> items = CollectLookDevArrangeItems(scene);
    std::vector<MATH::Vec3> moves;
    for (const EDITOR::LookDevArrangeItem& item : items) {
        const MATH::Vec3 center = (item.bounds.min + item.bounds.max) * 0.5f;
        const std::unordered_set<uint64_t> own =
            CollectObjectSubtreeIds(scene.GetSceneDocument(), SceneObjectId{item.id});
        const float ground = EDITOR::FindLookDevGroundHeight({center.x, item.bounds.min.y + 0.01f, center.z}, own);
        moves.push_back({0.0f, ground - item.bounds.min.y, 0.0f});
    }
    MoveLookDevItems(scene, "Drop To Ground", items, moves);
}

void DocumentSceneEditorController::GroupLookDevSelection(DocumentSceneBase& scene) {
    const std::vector<EDITOR::LookDevArrangeItem> items = CollectLookDevArrangeItems(scene);
    if (items.size() < 2u) {
        return;
    }
    const Bounds bounds = EDITOR::UnionLookDevBounds(items);
    // 組の基点は全体の底の中心（床に置いた物をまとめて動かしやすい）。
    const MATH::Vec3 pivot{(bounds.min.x + bounds.max.x) * 0.5f, bounds.min.y, (bounds.min.z + bounds.max.z) * 0.5f};
    SceneObjectId group{};
    EditLookDevDocument(scene, "Group", [&](SceneDocument& document) {
        SceneObjectData parent{};
        parent.id = SceneObjectId{context_.nextSceneObjectId++};
        parent.name = TEXT::Localize("ui.lookDev.groupName");
        parent.transform.position = pivot;
        size_t insertAt = document.objects.size();
        for (const EDITOR::LookDevArrangeItem& item : items) {
            SceneObjectData* object = FindSceneObjectData(document, SceneObjectId{item.id});
            if (object == nullptr) {
                continue;
            }
            MATH::Vec3 translation{};
            MATH::Quat rotation = MATH::Quat::Identity();
            MATH::Vec3 scale{1.0f, 1.0f, 1.0f};
            if (!MATH::DecomposeTRS(EDITOR::LookDevWorldMatrix(scene, object->id), translation, rotation, scale)) {
                continue;
            }
            object->parent = parent.id;
            object->transform.position = translation - pivot;
            object->transform.rotationEulerDeg =
                MATH::EulerXYZDegreesFromQuatNearest(rotation, object->transform.rotationEulerDeg);
            object->transform.scale = scale;
            insertAt = (std::min)(insertAt, static_cast<size_t>(object - document.objects.data()));
        }
        group = parent.id;
        document.objects.insert(document.objects.begin() + static_cast<std::ptrdiff_t>(insertAt), std::move(parent));
        return true;
    });
    if (group.value != 0u) {
        SelectViewportObjects(scene, {group}, EditorObjectSelectionMode::Replace);
    }
}

void DocumentSceneEditorController::ReplaceLookDevShapeWithModel(DocumentSceneBase& scene, SceneObjectId objectId,
                                                                 const std::string& modelGuid) {
    ModelAsset* model = ResolveModelAssetNow(scene.GetModelManager(), scene.GetAssetRegistry(), modelGuid);
    if (model == nullptr || !BOUNDS::IsUsable(model->bounds)) {
        viewportDropMessage_ = TEXT::Localize("ui.lookDev.replaceFailed");
        return;
    }
    const Bounds modelBounds = model->bounds;
    EditLookDevDocument(scene, "Replace With Model", [&](SceneDocument& document) {
        SceneObjectData* object = FindSceneObjectData(document, objectId);
        SceneComponentData* shape =
            object != nullptr ? EDITOR::FindLookDevComponent(*object, "ProceduralMeshComponent") : nullptr;
        SceneComponentData* modelComponent =
            object != nullptr ? EDITOR::FindLookDevComponent(*object, "ModelComponent") : nullptr;
        if (shape == nullptr || modelComponent == nullptr) {
            return false;
        }
        ProceduralMeshComponent probe;
        probe.Deserialize(shape->properties);
        const MATH::Vec3 objectScale = object->transform.scale;
        const MATH::Vec3 local = EDITOR::LookDevShapeExtent(probe.GetSettings());
        const MATH::Vec3 box{local.x * objectScale.x, local.y * objectScale.y, local.z * objectScale.z};
        const MATH::Vec3 modelSize = modelBounds.max - modelBounds.min;
        // 比率を保ったまま白模の箱に収まる倍率。
        float fit = 0.0f;
        for (int axis = 0; axis < 3; ++axis) {
            const float boxSize = axis == 0 ? box.x : (axis == 1 ? box.y : box.z);
            const float size = axis == 0 ? modelSize.x : (axis == 1 ? modelSize.y : modelSize.z);
            if (boxSize > 1.0e-4f && size > 1.0e-4f) {
                fit = fit > 0.0f ? (std::min)(fit, boxSize / size) : boxSize / size;
            }
        }
        if (!(fit > 0.0f)) {
            return false;
        }
        // 模型の底の中心を白模の底の中心へ合わせる（向きは白模のまま）。
        const MATH::Vec3& euler = object->transform.rotationEulerDeg;
        const MATH::Vec3 boxBottom = object->transform.position + Rotate(euler, {0.0f, -box.y * 0.5f, 0.0f});
        const MATH::Vec3 modelBottom{(modelBounds.min.x + modelBounds.max.x) * 0.5f, modelBounds.min.y,
                                     (modelBounds.min.z + modelBounds.max.z) * 0.5f};
        object->transform.position = boxBottom - Rotate(euler, modelBottom * fit);
        object->transform.scale = {fit, fit, fit};
        modelComponent->properties["assetId"] = modelGuid;
        object->components.erase(object->components.begin() + (shape - object->components.data()));
        return true;
    });
}

void DocumentSceneEditorController::DrawLookDevArrangeSection(DocumentSceneBase& scene) {
    if (!ImGui::CollapsingHeader(TEXT::Localize("ui.lookDev.arrange").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const size_t selected =
        CollectRootObjectIds(scene.GetSceneDocument(), context_.selection.GetSelectedObjectIds()).size();
    if (selected == 0u) {
        ImGui::TextDisabled("%s", TEXT::Localize("ui.lookDev.arrangeHint").c_str());
        return;
    }
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float labelWidth = ImGui::CalcTextSize("XX").x;
    const auto buttonWidth = [&](int count) {
        return (ImGui::GetContentRegionAvail().x - labelWidth - spacing * static_cast<float>(count)) /
               static_cast<float>(count);
    };
    // 揃え：基準は主に選んでいる物（检查器に出ている物）。
    ImGui::BeginDisabled(selected < 2u);
    ImGui::TextDisabled("%s", TEXT::Localize("ui.lookDev.align").c_str());
    constexpr const char* kAxisNames[] = {"X", "Y", "Z"};
    constexpr const char* kAnchorKeys[] = {"ui.lookDev.alignMin", "ui.lookDev.alignCenter", "ui.lookDev.alignMax"};
    for (int axis = 0; axis < 3; ++axis) {
        ImGui::PushID(axis);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(kAxisNames[axis]);
        ImGui::SameLine(0.0f, labelWidth - ImGui::CalcTextSize(kAxisNames[axis]).x + spacing);
        const float width = buttonWidth(3);
        for (int anchor = 0; anchor < 3; ++anchor) {
            if (anchor > 0) {
                ImGui::SameLine();
            }
            ImGui::PushID(anchor);
            if (EDITOR::ActionButton(TEXT::Localize(kAnchorKeys[anchor]).c_str(), "##align",
                                     EDITOR::EditorButtonTone::Neutral, ImVec2(width, 0.0f))) {
                const auto items = CollectLookDevArrangeItems(scene);
                const auto reference = EDITOR::FindLookDevObjectBounds(context_.selection.GetActiveObjectId().value);
                const auto active = std::find_if(items.begin(), items.end(), [&](const auto& item) {
                    return item.id == context_.selection.GetActiveObjectId().value;
                });
                const Bounds target =
                    active != items.end() ? active->bounds : (reference ? *reference : items.front().bounds);
                MoveLookDevItems(
                    scene, "Align", items,
                    EDITOR::AlignLookDevItems(items, target, axis, static_cast<EDITOR::LookDevAlign>(anchor)));
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(selected < 3u);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TEXT::Localize("ui.lookDev.distribute").c_str());
    for (int axis = 0; axis < 3; ++axis) {
        ImGui::SameLine();
        ImGui::PushID(10 + axis);
        if (EDITOR::ActionButton(kAxisNames[axis], "##distribute", EDITOR::EditorButtonTone::Neutral)) {
            const auto items = CollectLookDevArrangeItems(scene);
            MoveLookDevItems(scene, "Distribute", items, EDITOR::DistributeLookDevItems(items, axis));
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();

    const float half = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
    if (EDITOR::ActionButton(TEXT::Localize("ui.lookDev.dropToGround").c_str(), "##drop",
                             EDITOR::EditorButtonTone::Neutral, ImVec2(half, 0.0f),
                             TEXT::Localize("ui.lookDev.dropToGroundHint").c_str())) {
        DropLookDevSelection(scene);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selected < 2u);
    if (EDITOR::ActionButton(TEXT::Localize("ui.lookDev.group").c_str(), "##group", EDITOR::EditorButtonTone::Neutral,
                             ImVec2(half, 0.0f), TEXT::Localize("ui.lookDev.groupHint").c_str())) {
        GroupLookDevSelection(scene);
    }
    ImGui::EndDisabled();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TEXT::Localize("ui.lookDev.mirror").c_str());
    for (const int axis : {0, 2}) {
        ImGui::SameLine();
        ImGui::PushID(20 + axis);
        if (EDITOR::ActionButton(kAxisNames[axis], "##mirror", EDITOR::EditorButtonTone::Neutral, ImVec2(0.0f, 0.0f),
                                 TEXT::Localize("ui.lookDev.mirrorHint").c_str())) {
            MirrorLookDevSelection(scene, axis);
        }
        ImGui::PopID();
    }

    // 阵列：線形・格子・環形。
    EDITOR::LookDevArraySettings& array = lookDevWorkspace_.ArraySettings();
    ImGui::SeparatorText(TEXT::Localize("ui.lookDev.array").c_str());
    for (const auto& entry : EDITOR::kLookDevArrayKindNames) {
        if (entry.value != EDITOR::LookDevArrayKind::Linear) {
            ImGui::SameLine();
        }
        if (EDITOR::ToggleButton(TEXT::Localize(std::string("ui.lookDev.arrayKind.") + entry.name).c_str(), entry.name,
                                 array.kind == entry.value)) {
            array.kind = entry.value;
        }
    }
    if (EDITOR::InspectorPropertyRow row(TEXT::Localize("ui.lookDev.arrayCount")); row.IsVisible()) {
        (void)EDITOR::SliderNumber("##count", array.count, 2.0, 24.0);
    }
    if (array.kind == EDITOR::LookDevArrayKind::Grid) {
        if (EDITOR::InspectorPropertyRow row(TEXT::Localize("ui.lookDev.arrayRows")); row.IsVisible()) {
            (void)EDITOR::SliderNumber("##rows", array.rows, 1.0, 24.0);
        }
    }
    if (EDITOR::InspectorPropertyRow row(TEXT::Localize(
            array.kind == EDITOR::LookDevArrayKind::Ring ? "ui.lookDev.arrayRadius" : "ui.lookDev.arraySpacing"));
        row.IsVisible()) {
        float& distance = array.kind == EDITOR::LookDevArrayKind::Ring ? array.radius : array.spacing;
        (void)EDITOR::DragNumber("##distance", distance, {.minimum = 0.05, .maximum = 500.0});
    }
    if (EDITOR::ActionButton(TEXT::Localize("ui.lookDev.arrayCreate").c_str(), "##arrayCreate",
                             EDITOR::EditorButtonTone::Primary, ImVec2(-1.0f, 0.0f),
                             TEXT::Localize("ui.lookDev.arrayHint").c_str())) {
        CreateLookDevArray(scene);
    }

    DrawLookDevOpeningsSection(scene);

    // 换成模型：白模の箱を選んだ模型に置き換え、箱に収まる大きさで底を合わせる。
    const SceneObjectId active = context_.selection.GetActiveObjectId();
    const GameObject* activeObject = selected == 1u ? scene.GetWorld().FindObject(active) : nullptr;
    if (activeObject == nullptr || activeObject->GetComponent<ProceduralMeshComponent>() == nullptr) {
        return;
    }
    ImGui::SeparatorText(TEXT::Localize("ui.lookDev.replace").c_str());
    std::string& model = lookDevWorkspace_.ReplacementModel();
    const AssetDatabase& database = scene.GetAssetDatabase();
    const std::vector<const AssetRecord*> models = database.CollectByType(AssetType::Model);
    const auto current = std::find_if(models.begin(), models.end(),
                                      [&](const AssetRecord* record) { return record->guid.value == model; });
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##replaceModel", current != models.end()
                                                ? GetAssetRecordDisplayName(**current).c_str()
                                                : TEXT::Localize("ui.lookDev.replacePick").c_str())) {
        for (const AssetRecord* record : models) {
            if (ImGui::Selectable((GetAssetRecordDisplayName(*record) + "##" + record->guid.value).c_str(),
                                  record->guid.value == model)) {
                model = record->guid.value;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(current == models.end());
    if (EDITOR::ActionButton(TEXT::Localize("ui.lookDev.replaceApply").c_str(), "##replaceApply",
                             EDITOR::EditorButtonTone::Primary, ImVec2(-1.0f, 0.0f),
                             TEXT::Localize("ui.lookDev.replaceHint").c_str())) {
        ReplaceLookDevShapeWithModel(scene, active, model);
    }
    ImGui::EndDisabled();
}
#endif

} // namespace HIKARI
