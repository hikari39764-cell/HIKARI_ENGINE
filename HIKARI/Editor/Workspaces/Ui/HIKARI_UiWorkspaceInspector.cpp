#include "Editor/Authoring/HIKARI_UiAuthoring.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Commands/HIKARI_SceneObjectCommandService.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Editor/Workspaces/Ui/HIKARI_UiWorkspaceController.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "imgui.h"
#include <array>

namespace HIKARI::EDITOR {
namespace {
const char* L(const char* key) { return TEXT::LocalizeCStr(key); }
const SceneComponentData* Component(const SceneObjectData& object, std::string_view type) {
    for (const auto& c : object.components)
        if (c.type == type)
            return &c;
    return nullptr;
}
} // namespace
void UiWorkspaceController::DrawInspector(DocumentSceneBase& scene, EditorContext& context,
                                          SelectionSyncService& selection, SceneObjectCommandService& objects,
                                          EditorOperationRegistry& operations, UiWorkspaceResult& result) {
    if (!ImGui::Begin(Window("uix.inspector", "Inspector").c_str())) {
        ImGui::End();
        return;
    }
    const auto id = context.selection.GetActiveObjectId();
    auto* object = selection.FindDocumentObjectById(scene, id);
    if (!object) {
        ImGui::TextWrapped("%s", L("uix.select"));
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("%s", L("uix.sceneSaved"));
    ImGui::BeginDisabled(scene.IsRuntimePlayActive() || object->editorLocked);
    if (nameObject_ != id || !ImGui::IsAnyItemActive()) {
        nameObject_ = id;
        const auto count = std::min(object->name.size(), sizeof(name_) - 1);
        std::copy_n(object->name.data(), count, name_);
        name_[count] = '\0';
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##ElementName", name_, sizeof(name_), ImGuiInputTextFlags_EnterReturnsTrue)) {
        objects.Execute(SceneObjectCommandId::Rename, scene, context, selection, name_);
        object = selection.FindDocumentObjectById(scene, id);
    }
    std::string addType;
    if ((Component(*object, "UiRectComponent") || Component(*object, "UiCanvasComponent")) &&
        ImGui::BeginCombo("##AddComponent", L("uix.addComponent"))) {
        for (const auto* type : scene.GetComponentRegistry().GetTypeInfos()) {
            if (type->presentation.category != "UI" && type->typeName != "HIKARI.SceneFlow.Trigger")
                continue;
            if (type->typeName == "UiCanvasComponent")
                continue;
            if (objects.CanExecute(SceneObjectCommandId::AddComponent, scene, context, type->typeName) &&
                ImGui::Selectable(TEXT::Localize(type->presentation.displayName).c_str()))
                addType = type->typeName;
        }
        ImGui::EndCombo();
    }
    if (!addType.empty()) {
        objects.Execute(SceneObjectCommandId::AddComponent, scene, context, selection, addType);
        object = selection.FindDocumentObjectById(scene, id);
    }

    if (object && ImGui::BeginTabBar("UiProperties")) {
        const std::array<std::string_view, 3> appearance{"UiImageComponent", "UiTextComponent", "UiProgressComponent"};
        const std::array<std::string_view, 3> layout{"UiCanvasComponent", "UiRectComponent", "UiLayoutGroupComponent"};
        const std::array<std::string_view, 6> interaction{
            "UiButtonComponent",         "UiGroupComponent",         "UiDisplaySettingsPanelComponent",
            "UiDisplaySettingComponent", "HIKARI.SceneFlow.Trigger", "UiDataBindingComponent"};
        auto tab = [&](const char* label, std::span<const std::string_view> types) {
            if (!ImGui::BeginTabItem(L(label)))
                return;
            result.history = inspector_.Draw(scene, context, selection, *object, types);
            ImGui::EndTabItem();
        };
        tab("uix.appearance", appearance);
        const std::array<std::string_view, 2> animation{"UiAnimatorComponent", "UiAnimationTargetComponent"};
        tab("uianim.timeline", animation);
        if (ImGui::BeginTabItem(L("uix.layout"))) {
            if (Component(*object, "UiRectComponent") && !Component(*object, "UiCanvasComponent")) {
                const auto parent = object->parent.value_or(SceneObjectId{});
                const auto* owner = selection.FindDocumentObjectById(scene, parent);
                SceneObjectId chosen{};
                if (ImGui::BeginCombo(L("uix.parent"), owner ? owner->name.c_str() : "-")) {
                    for (const auto& candidate : scene.GetSceneDocument().objects) {
                        if (candidate.id == id || candidate.editorLocked ||
                            (!Component(candidate, "UiCanvasComponent") && !Component(candidate, "UiGroupComponent") &&
                             !Component(candidate, "UiLayoutGroupComponent")))
                            continue;
                        ImGui::PushID(std::to_string(candidate.id.value).c_str());
                        if (ImGui::Selectable(candidate.name.c_str(), parent == candidate.id))
                            chosen = candidate.id;
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                if (chosen.value && chosen != parent) {
                    const auto response =
                        operations.Execute("ui.parent.set", {{"objectId", id.value}, {"parentId", chosen.value}});
                    status_ = response.ok ? std::string{} : response.message;
                    object = selection.FindDocumentObjectById(scene, id);
                }
            }
            if (const auto* rect = Component(*object, "UiRectComponent")) {
                ImGui::TextUnformatted(L("uix.anchor"));
                const std::array<const char*, 9> labels{"Top Left", "Top",         "Top Right", "Left",        "Center",
                                                        "Right",    "Bottom Left", "Bottom",    "Bottom Right"};
                int anchor = -1;
                for (int i = 0; i < 9; ++i) {
                    ImGui::PushID(i);
                    if (ImGui::Button("##Anchor", {32, 26}))
                        anchor = i;
                    const auto p = ImGui::GetItemRectMin();
                    auto* draw = ImGui::GetWindowDrawList();
                    draw->AddRect({p.x + 6, p.y + 5}, {p.x + 26, p.y + 21}, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                    draw->AddCircleFilled({p.x + 6 + (i % 3) * 10.f, p.y + 5 + (i / 3) * 8.f}, 2.5f,
                                          ImGui::GetColorU32(ImGuiCol_Text));
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", L(labels[i]));
                    if (i % 3 != 2)
                        ImGui::SameLine();
                    ImGui::PopID();
                }
                if (anchor >= 0) {
                    float width = 1920, height = 1080;
                    if (auto* runtime = scene.GetWorld().FindObject(id)) {
                        auto* parent = runtime->GetParent();
                        if (parent && parent->GetComponent<UiRectComponent>()) {
                            const auto r = projector_.ResolveLayout(scene.GetWorld(), *parent);
                            width = r.width;
                            height = r.height;
                        } else if (parent && parent->GetComponent<UiCanvasComponent>()) {
                            const auto* c = parent->GetComponent<UiCanvasComponent>();
                            width = c->GetReferenceWidth();
                            height = c->GetReferenceHeight();
                        }
                    }
                    const std::array<float, 2> a{(anchor % 3) * .5f, (anchor / 3) * .5f};
                    const auto lo = rect->properties.value("anchorMinimum", std::array<float, 2>{.5f, .5f});
                    const auto hi = rect->properties.value("anchorMaximum", lo);
                    auto offsetLo = rect->properties.value("offsetMinimum", std::array<float, 2>{-160, -40});
                    auto offsetHi = rect->properties.value("offsetMaximum", std::array<float, 2>{160, 40});
                    offsetLo[0] += (lo[0] - a[0]) * width;
                    offsetLo[1] += (lo[1] - a[1]) * height;
                    offsetHi[0] += (hi[0] - a[0]) * width;
                    offsetHi[1] += (hi[1] - a[1]) * height;
                    EditProperties(operations, id, *rect,
                                   {{"anchorMinimum", a},
                                    {"anchorMaximum", a},
                                    {"offsetMinimum", offsetLo},
                                    {"offsetMaximum", offsetHi}});
                    object = selection.FindDocumentObjectById(scene, id);
                }
            }
            if (object)
                result.history = inspector_.Draw(scene, context, selection, *object, layout);
            ImGui::EndTabItem();
        }
        tab("uix.interaction", interaction);
        tab("uix.advanced", {});
        ImGui::EndTabBar();
    }
    ImGui::EndDisabled();
    ImGui::End();
}
} // namespace HIKARI::EDITOR
