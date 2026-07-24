#include "HIKARI_PortableObjectToolsPanel.h"

#if defined(HIKARI_ENABLE_IMGUI)
#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <json.hpp>

#include "imgui.h"

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"
#include "Editor/Widgets/HIKARI_InputActionFieldWidget.h"
#include "HIKARI_Services.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#endif

namespace HIKARI::RUNTIME_TOOLS {

#if defined(HIKARI_ENABLE_IMGUI)
    namespace {
        GameObject* FindObjectById(DocumentSceneBase& scene, SceneObjectId id) {
            return scene.GetWorld().FindObject(id);
        }

        GameObject* FirstObject(DocumentSceneBase& scene) {
            const auto& objects = scene.GetWorld().GetObjects();
            if (objects.empty() || !objects.front()) {
                return nullptr;
            }
            return objects.front().get();
        }

        struct AssetPickerEntry {
            std::string id{};
            std::string label{};
            std::string preview{};
        };

        class PortableImGuiInspectorBuilder final : public IInspectorBuilder {
        public:
            void SetContext(const InspectorContext& context) override {
                context_ = context;
            }

            const InspectorContext& GetContext() const noexcept override {
                return context_;
            }

            void Text(std::string_view text) override {
                ImGui::TextWrapped(
                    "%.*s",
                    static_cast<int>(text.size()),
                    text.data());
            }

            bool Button(std::string_view label) override {
                return ImGui::Button(std::string(label).c_str());
            }

            bool Bool(std::string_view label, bool& value) override {
                return ImGui::Checkbox(std::string(label).c_str(), &value);
            }

            bool Int(std::string_view label, int& value) override {
                return ImGui::InputInt(std::string(label).c_str(), &value);
            }

            bool Float(std::string_view label, float& value) override {
                return ImGui::DragFloat(std::string(label).c_str(), &value, 0.1f);
            }

            bool FloatRange(
                std::string_view label,
                float& value,
                float minimum,
                float maximum,
                float speed = 0.1f) override {

                return ImGui::DragFloat(
                    std::string(label).c_str(),
                    &value,
                    speed,
                    minimum,
                    maximum,
                    "%.3f",
                    ImGuiSliderFlags_AlwaysClamp);
            }

            bool String(std::string_view label, std::string& value) override {
                std::array<char, 256> buffer{};
                const size_t copyCount = (std::min)(value.size(), buffer.size() - 1);
                std::copy_n(value.data(), copyCount, buffer.data());
                if (!ImGui::InputText(std::string(label).c_str(), buffer.data(), buffer.size())) {
                    return false;
                }
                value = buffer.data();
                return true;
            }

            bool InputActionIdPicker(
                std::string_view label,
                INPUT::InputActionValueType expectedType,
                std::string& value) override {

                const auto* inputService = context_.worldServices != nullptr
                    ? context_.worldServices->Find<INPUT::InputService>()
                    : nullptr;
                return inputService != nullptr
                    ? EDITOR::DrawInputActionField(
                        inputService->GetActionMap(),
                        label,
                        expectedType,
                        value)
                    : String(label, value);
            }

            bool Choice(
                std::string_view label,
                int& selectedIndex,
                std::span<const char* const> choices) override {

                if (choices.empty()) {
                    return false;
                }
                selectedIndex = std::clamp(
                    selectedIndex,
                    0,
                    static_cast<int>(choices.size() - 1));
                bool changed = false;
                if (ImGui::BeginCombo(
                        std::string(label).c_str(),
                        choices[static_cast<size_t>(selectedIndex)])) {
                    for (size_t index = 0; index < choices.size(); ++index) {
                        const bool selected = static_cast<int>(index) ==
                            selectedIndex;
                        if (ImGui::Selectable(choices[index], selected)) {
                            selectedIndex = static_cast<int>(index);
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                return changed;
            }

            bool Vec2(std::string_view label, float& x, float& y) override {
                float values[2]{ x, y };
                if (!ImGui::DragFloat2(std::string(label).c_str(), values, 0.1f)) {
                    return false;
                }
                x = values[0];
                y = values[1];
                return true;
            }

            bool AssetIdPicker(std::string_view label, AssetType assetType, std::string& value) override {
                if (!context_.assetRegistry) {
                    return String(label, value);
                }

                std::vector<AssetPickerEntry> entries{};
                for (const AssetDescriptor* descriptor : context_.assetRegistry->CollectByType(assetType)) {
                    if (!descriptor || descriptor->id.value.empty()) {
                        continue;
                    }

                    AssetPickerEntry entry{};
                    entry.id = descriptor->id.value;
                    entry.preview = descriptor->id.value;
                    std::string sourcePath = descriptor->sourcePath;
                    if (context_.assetDatabase) {
                        if (const AssetRecord* record = context_.assetDatabase->FindByGuid(AssetGuid{ descriptor->id.value })) {
                            entry.preview = record->displayName.empty()
                                ? record->sourcePath.stem().string()
                                : record->displayName;
                            sourcePath = record->sourcePath.generic_string();
                        }
                    }
                    if (entry.preview.empty()) {
                        entry.preview = descriptor->sourcePath.empty()
                            ? descriptor->id.value
                            : std::filesystem::path(descriptor->sourcePath).stem().string();
                    }
                    entry.label = entry.preview + "##" + entry.id;
                    if (!sourcePath.empty()) {
                        entry.label += "  ";
                        entry.label += sourcePath;
                    }
                    entries.push_back(std::move(entry));
                }

                std::sort(entries.begin(), entries.end(), [](const AssetPickerEntry& lhs, const AssetPickerEntry& rhs) {
                    return lhs.preview < rhs.preview;
                });

                std::string preview = value.empty() ? std::string("<none>") : value;
                for (const AssetPickerEntry& entry : entries) {
                    if (entry.id == value) {
                        preview = entry.preview;
                        break;
                    }
                }

                bool changed = false;
                const std::string labelText(label);
                if (ImGui::BeginCombo(labelText.c_str(), preview.c_str())) {
                    if (ImGui::Selectable("<none>", value.empty())) {
                        value.clear();
                        changed = true;
                    }
                    for (const AssetPickerEntry& entry : entries) {
                        const bool selected = value == entry.id;
                        if (ImGui::Selectable(entry.label.c_str(), selected)) {
                            value = entry.id;
                            changed = true;
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    if (entries.empty()) {
                        ImGui::TextDisabled("No assets");
                    }
                    ImGui::EndCombo();
                }
                return changed;
            }

            bool SceneIdPicker(std::string_view label, std::string& value) override {
                if (!context_.assetDatabase) {
                    return String(label, value);
                }

                std::vector<const AssetRecord*> sceneRecords =
                    context_.assetDatabase->CollectByType(AssetType::Scene);
                std::sort(sceneRecords.begin(), sceneRecords.end(), [](const AssetRecord* lhs, const AssetRecord* rhs) {
                    if (!lhs || !rhs) {
                        return lhs < rhs;
                    }
                    return GetAssetRecordDisplayName(*lhs) <
                        GetAssetRecordDisplayName(*rhs);
                });

                bool changed = false;
                const std::string labelText(label);
                const std::string preview = value.empty() ? std::string("<none>") : value;
                if (ImGui::BeginCombo(labelText.c_str(), preview.c_str())) {
                    if (ImGui::Selectable("<none>", value.empty())) {
                        value.clear();
                        changed = true;
                    }
                    for (const AssetRecord* record : sceneRecords) {
                        if (!record || !record->guid.IsValid()) {
                            continue;
                        }
                        if (!SERVICES::IsRuntimeSceneGuidAllowed(record->guid.value)) {
                            continue;
                        }
                        const bool selected = value == record->guid.value;
                        std::string itemLabel =
                            GetAssetRecordDisplayName(*record) +
                            "##" +
                            record->guid.value;
                        if (ImGui::Selectable(itemLabel.c_str(), selected)) {
                            value = record->guid.value;
                            changed = true;
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                return changed;
            }

            bool SceneObjectIdPicker(
                std::string_view label,
                SceneObjectId& value) override {

                uint64_t rawValue = value.value;
                if (!ImGui::InputScalar(
                        std::string(label).c_str(),
                        ImGuiDataType_U64,
                        &rawValue)) {
                    return false;
                }
                value.value = rawValue;
                return true;
            }

        private:
            InspectorContext context_{};
        };

        void DrawSceneSwitcher(
            DocumentSceneBase& scene,
            SceneObjectId& selectedObjectId,
            std::string& lastSceneMessage) {

            ImGui::SeparatorText("Scenes");

            std::vector<const AssetRecord*> sceneRecords =
                scene.GetAssetDatabase().CollectByType(AssetType::Scene);
            std::sort(sceneRecords.begin(), sceneRecords.end(), [](const AssetRecord* lhs, const AssetRecord* rhs) {
                if (!lhs || !rhs) {
                    return lhs < rhs;
                }
                return GetAssetRecordDisplayName(*lhs) <
                    GetAssetRecordDisplayName(*rhs);
            });

            const AssetGuid& currentGuid = scene.GetCurrentSceneAssetGuid();
            std::string preview = scene.GetCurrentSceneDisplayName();
            if (preview.empty()) {
                preview = currentGuid.IsValid() ? currentGuid.value : scene.GetSceneId();
            }

            bool changedScene = false;
            if (ImGui::BeginCombo("Scene", preview.c_str())) {
                for (const AssetRecord* record : sceneRecords) {
                if (!record || !record->guid.IsValid()) {
                    continue;
                }
                if (!SERVICES::IsRuntimeSceneGuidAllowed(record->guid.value)) {
                    continue;
                }

                std::string label = GetAssetRecordDisplayName(*record);
                    label += "##";
                    label += record->guid.value;

                    const bool selected = record->guid == currentGuid;
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        const AssetGuid targetGuid = record->guid;
                        const std::string targetLabel =
                            GetAssetRecordDisplayName(*record);
                        if (scene.RequestOpenSceneAsset(targetGuid)) {
                            selectedObjectId = {};
                            lastSceneMessage = "Opened scene: " + targetLabel;
                            changedScene = true;
                        } else {
                            lastSceneMessage = "Failed to open scene: " + targetLabel;
                        }
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                    if (changedScene) {
                        break;
                    }
                }
                ImGui::EndCombo();
            }

            if (!lastSceneMessage.empty()) {
                ImGui::TextWrapped("%s", lastSceneMessage.c_str());
            }
        }

        void DrawTransform(GameObject& object) {
            Transform3D transform = object.GetTransform();
            ImGui::SeparatorText("Transform");
            bool changed = ImGui::DragFloat3(
                "Position", &transform.position.x, 0.05f);
            changed |= ImGui::DragFloat3(
                "Scale", &transform.scale.x, 0.02f, 0.001f, 1000.0f);
            if (changed) {
                transform.useExplicitMatrix = false;
                (void)object.SetLocalTransform(transform);
            }
            ImGui::Text("Rotation quat: %.3f, %.3f, %.3f, %.3f",
                transform.rotation.x,
                transform.rotation.y,
                transform.rotation.z,
                transform.rotation.w);
        }

        void DrawComponents(DocumentSceneBase& scene, GameObject& object) {
            ImGui::SeparatorText("Components");
            const auto& components = object.GetComponents();
            if (components.empty()) {
                ImGui::TextDisabled("No components");
                return;
            }

            PortableImGuiInspectorBuilder builder{};
            const RuntimeObjectHandle runtimeObject =
                object.GetRuntimeHandle();
            builder.SetContext(InspectorContext{
                &scene.GetAssetRegistry(),
                &scene.GetAssetDatabase(),
                nullptr,
                &scene.GetWorld().Services(),
                &runtimeObject
            });

            for (const auto& component : components) {
                if (!component) {
                    continue;
                }

                const std::string typeName(component->GetTypeName());
                if (ImGui::TreeNode(typeName.c_str())) {
                    component->BuildInspector(builder);
                    component->RenderImGui();

                    nlohmann::json serialized = nlohmann::json::object();
                    component->Serialize(serialized);
                    ImGui::SeparatorText("Serialized");
                    ImGui::TextWrapped("%s", serialized.dump(2).c_str());

                    ImGui::TreePop();
                }
            }
        }
    }
#endif

    void PortableObjectToolsPanel::Draw(DocumentSceneBase& scene) {
#if defined(HIKARI_ENABLE_IMGUI)
        ImGui::SetNextWindowSize(ImVec2(560.0f, 620.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Object Tools")) {
            ImGui::End();
            return;
        }

        DrawSceneSwitcher(scene, selectedObjectId_, lastSceneMessage_);

        GameObject* selected = FindObjectById(scene, selectedObjectId_);
        if (!selected) {
            selected = FirstObject(scene);
            selectedObjectId_ = selected ? selected->GetDocumentId() : SceneObjectId{};
        }

        ImGui::Text("Scene: %s", scene.GetSceneId().c_str());
        ImGui::Separator();

        const float listWidth = 150.0f;
        if (ImGui::BeginChild("ObjectList", ImVec2(listWidth, 0.0f), true)) {
            for (const auto& object : scene.GetWorld().GetObjects()) {
                if (!object) {
                    continue;
                }
                const bool isSelected = object->GetDocumentId() == selectedObjectId_;
                std::string label = object->GetName();
                label += "##";
                label += std::to_string(object->GetDocumentId().value);
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    selectedObjectId_ = object->GetDocumentId();
                    selected = object.get();
                }
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();
        if (ImGui::BeginChild("ObjectDetails", ImVec2(0.0f, 0.0f), true)) {
            if (selected) {
                ImGui::Text("Object: %s", selected->GetName().c_str());
                ImGui::Text("Document ID: %llu", static_cast<unsigned long long>(selected->GetDocumentId().value));
                DrawTransform(*selected);
                DrawComponents(scene, *selected);
            } else {
                ImGui::TextDisabled("No objects in scene");
            }
        }
        ImGui::EndChild();

        ImGui::End();
#else
        (void)scene;
#endif
    }

} // namespace HIKARI::RUNTIME_TOOLS
