#include "Editor/Authoring/HIKARI_EditorObjectFactory.h"

#include <algorithm>
#include <filesystem>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"

namespace HIKARI::EDITOR {
    namespace {

        bool CanCreateUi(
            DocumentSceneBase& scene,
            bool addSceneFlow) noexcept {
            const ComponentRegistry& registry = scene.GetComponentRegistry();
            return registry.Find(kUiCanvasComponentType) != nullptr &&
                registry.Find(kUiRectComponentType) != nullptr &&
                registry.Find(kUiImageComponentType) != nullptr &&
                registry.Find(kUiTextComponentType) != nullptr &&
                registry.Find(kUiButtonComponentType) != nullptr &&
                (!addSceneFlow ||
                    registry.Find("HIKARI.SceneFlow.Trigger") != nullptr);
        }

        SceneObjectData BuildUiObject(
            DocumentSceneBase& scene,
            const CreateObjectRequest& request,
            std::string_view fallbackName) {
            SceneObjectData object{};
            object.id = AllocateSceneObjectId(scene.GetSceneDocument());
            object.name = request.name.empty()
                ? std::string(fallbackName)
                : request.name;
            object.transform.position = request.position;
            object.transform.rotationEulerDeg = request.rotation;
            object.transform.scale = request.scale;
            return object;
        }

        SceneComponentData Component(
            std::string_view type,
            nlohmann::json properties) {
            return SceneComponentData{
                std::string(type),
                std::move(properties)
            };
        }

        SceneObjectData* FindFirstCanvas(SceneDocument& document) noexcept {
            for (SceneObjectData& object : document.objects) {
                for (const SceneComponentData& component : object.components) {
                    if (component.type == kUiCanvasComponentType) {
                        return &object;
                    }
                }
            }
            return nullptr;
        }

        std::string FindDefaultUiFont(DocumentSceneBase& scene) {
            constexpr std::string_view preferred =
                "Fonts/NotoSansCJKjp-Regular.otf";
            const AssetDatabase& database = scene.GetAssetDatabase();
            if (database.FindByPath(
                    std::filesystem::path("Assets") / preferred) != nullptr) {
                return std::string(preferred);
            }
            std::string best{};
            for (const AssetRecord* record : database.CollectAll()) {
                if (record == nullptr) continue;
                const std::string extension = TEXT::ToLowerAsciiCopy(
                    record->sourcePath.extension().string());
                if (extension != ".ttf" && extension != ".otf" &&
                    extension != ".ttc") {
                    continue;
                }
                const std::filesystem::path relative =
                    record->sourcePath.lexically_relative("Assets");
                if (relative.empty() || relative == "." ||
                    std::any_of(
                        relative.begin(),
                        relative.end(),
                        [](const std::filesystem::path& part) {
                            return part == "..";
                        })) {
                    continue;
                }
                const std::string candidate = relative.generic_string();
                if (best.empty() || candidate < best) best = candidate;
            }
            return best;
        }

        SceneObjectData BuildCanvas(
            DocumentSceneBase& scene,
            const CreateObjectRequest& request) {
            SceneObjectData canvas = BuildUiObject(
                scene, request, "UI Canvas");
            canvas.components.push_back(Component(
                kUiCanvasComponentType,
                {
                    { "version", 1 },
                    { "enabled", true },
                    { "interactable", true },
                    { "referenceWidth", 1920.0f },
                    { "referenceHeight", 1080.0f },
                    { "sortOrder", 0 }
                }));
            NormalizeSceneComponentIds(canvas);
            return canvas;
        }

    } // namespace

    GameObject* CreateUiCanvasObject(
        DocumentSceneBase& scene,
        const CreateObjectRequest& request) {
        if (!CanCreateUi(scene, false)) return nullptr;
        SceneObjectData canvas = BuildCanvas(scene, request);
        const SceneObjectId id = canvas.id;
        scene.GetSceneDocument().objects.push_back(std::move(canvas));
        scene.RebuildRuntimeWorld();
        return scene.GetWorld().FindObject(id);
    }

    GameObject* CreateUiButtonObject(
        DocumentSceneBase& scene,
        const CreateObjectRequest& request,
        bool addSceneFlow) {
        if (!CanCreateUi(scene, addSceneFlow)) return nullptr;
        SceneDocument& document = scene.GetSceneDocument();
        SceneObjectData* canvas = FindFirstCanvas(document);
        SceneObjectId canvasId{};
        if (canvas == nullptr) {
            CreateObjectRequest canvasRequest{};
            canvasRequest.name = "UI Canvas";
            SceneObjectData newCanvas = BuildCanvas(scene, canvasRequest);
            canvasId = newCanvas.id;
            document.objects.push_back(std::move(newCanvas));
        } else {
            canvasId = canvas->id;
        }

        SceneObjectData button = BuildUiObject(scene, request, "UI Button");
        button.parent = canvasId;
        const SceneObjectId buttonId = button.id;
        const std::string fontAssetPath = FindDefaultUiFont(scene);
        button.components.push_back(Component(
            kUiRectComponentType,
            {
                { "version", 1 },
                { "enabled", true },
                { "anchorMinimum", { 0.5f, 0.5f } },
                { "anchorMaximum", { 0.5f, 0.5f } },
                { "offsetMinimum", { -160.0f, -40.0f } },
                { "offsetMaximum", { 160.0f, 40.0f } },
                { "pivot", { 0.5f, 0.5f } },
                { "rotationDegrees", 0.0f },
                { "order", 0 }
            }));
        button.components.push_back(Component(
            kUiImageComponentType,
            {
                { "version", 1 },
                { "enabled", true },
                { "textureAssetGuid", "" },
                { "textureAssetPath", "generated://solid/ffffffff" },
                { "tint", { 0.08f, 0.12f, 0.20f, 0.96f } },
                { "orderOffset", 0 }
            }));
        button.components.push_back(Component(
            kUiTextComponentType,
            {
                { "version", 1 },
                { "enabled", true },
                { "text", "Button" },
                { "fontAssetPath", fontAssetPath },
                { "pixelHeight", 34.0f },
                { "color", { 1.0f, 1.0f, 1.0f, 1.0f } },
                { "horizontalAlign", "Center" },
                { "verticalAlign", "Center" },
                { "orderOffset", 1 }
            }));
        button.components.push_back(Component(
            kUiButtonComponentType,
            {
                { "version", 1 },
                { "enabled", true },
                { "interactable", true },
                { "defaultFocus", true },
                { "navigationOrder", 0 },
                { "submitActionId", "Gameplay.Interact" },
                { "onClickEventId", "" }
            }));
        if (addSceneFlow) {
            button.components.push_back(Component(
                "HIKARI.SceneFlow.Trigger",
                {
                    { "version", 1 },
                    { "enabled", true },
                    { "targetSceneGuid", "" },
                    { "targetSpawnPointId", "" },
                    { "actionEnabled", false },
                    { "uiButtonEnabled", true },
                    { "pointerRegionEnabled", false },
                    { "useTransition", true },
                    { "transitionProfileId", "noise_wipe" },
                    { "oneShotPerWorld", true },
                    { "cooldownSeconds", 0.5f }
                }));
        }
        NormalizeSceneComponentIds(button);
        document.objects.push_back(std::move(button));
        scene.RebuildRuntimeWorld();
        return scene.GetWorld().FindObject(buttonId);
    }

} // namespace HIKARI::EDITOR
