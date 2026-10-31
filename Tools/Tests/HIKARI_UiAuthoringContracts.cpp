#include "Editor/Authoring/HIKARI_SceneObjectOperations.h"
#include "Editor/History/HIKARI_SceneObjectsHistoryCommand.h"
#include "Editor/History/HIKARI_DocumentSnapshotHistory.h"
#include "Editor/Documents/HIKARI_VfxEditorDocument.h"
#include "Scene/Ui/HIKARI_UiDisplaySettings.h"
#include "Gameplay/Dialogue/HIKARI_DialogueProjectStyle.h"
#include "Gameplay/Dialogue/HIKARI_DialoguePresentation.h"
#include "HIKARI/Project/ActionViews.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_DocumentAssetStores.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Assets/Cooking/HIKARI_AssetCooker.h"
#include "Runtime/Host/HIKARI_IEditorUiServices.h"
#include "Runtime/Presentation/HIKARI_GamePresentationService.h"
#include "Runtime/Services/Internal/HIKARI_RuntimeServiceState.h"
#include "Input/Platform/HIKARI_IInputBackend.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_Utf8.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Authoring/HIKARI_ComponentPropertyAuthoringService.h"
#include "Editor/Authoring/HIKARI_UiAuthoring.h"
#include "Editor/Authoring/HIKARI_UiArrangement.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Commands/HIKARI_SceneObjectCommandService.h"
#include "Editor/HIKARI_DocumentComponentAuthoringService.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Editor/Workspaces/Ui/HIKARI_UiWorkspaceController.h"
#include "HIKARI/Project/FrameContext.h"
#include "HIKARI/Project/UiView.h"
#include "HIKARI/Project/WorldView.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/Features/HIKARI_RuntimeFeatureContextAccess.h"
#include "Scene/Components/Rendering/WorldText/HIKARI_WorldTextComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "HIKARI/Project/RuntimeFeature.h"
#include "Scene/Features/HIKARI_BuiltInRuntimeFeatures.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_WorldMassStore.h"
#include "Scene/Prefab/HIKARI_PrefabDocument.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "Scene/Ui/HIKARI_UiScrollViewComponent.h"
#include "Scene/Ui/HIKARI_UiSceneSystem.h"
#include "Scene/Ui/HIKARI_UiTextFieldComponent.h"
#include "Scene/Ui/HIKARI_UiUtilityComponents.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "Editor/Workspaces/TextStyle/HIKARI_TextStyleWorkspaceController.h"
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <utility>
#include <Windows.h>
// Keep active assertions after third-party headers, including in EditorRelease.
#include "Tools/Tests/Internal/HIKARI_ContractAssert.h"

using namespace HIKARI;
namespace {
void SnapshotHistory() {
    EDITOR::DocumentSnapshotHistory<int> history;
    int value = 0;
    history.Record(0, 1, 7);
    history.Record(1, 2, 7);
    assert(history.Undo(value) && value == 0 && !history.IsDirty());
    assert(history.Redo(value) && value == 2 && history.IsDirty());
    history.MarkSaved();
    history.Record(2, 3, 7);
    history.Record(3, 4, 7);
    assert(history.Undo(value) && value == 2 && !history.IsDirty());
    assert(history.Undo(value) && value == 0 && history.IsDirty());
    history.Record(0, 8);
    assert(!history.CanRedo() && history.IsDirty());
    assert(history.Undo(value) && value == 0 && history.IsDirty());
    history.Reset(false);
    assert(history.IsDirty() && !history.CanUndo() && !history.CanRedo());
    history.Reset();
    assert(!history.IsDirty());

    EDITOR::DocumentSnapshotHistory<int, int> bounded(2);
    bounded.Record(0, 1, 1, 10);
    bounded.Record(1, 0, 1, 20, std::equal_to<int>{});
    assert(!bounded.IsDirty() && !bounded.CanUndo());
    bounded.Record(0, 1, 1, 10);
    bounded.MarkSaved();
    bounded.Record(1, 2, 1, 20);
    bounded.Record(2, 3, 2, 30);
    int metadata = 0;
    assert(bounded.Undo(value, &metadata) && value == 2 && metadata == 30);
    assert(bounded.Undo(value, &metadata) && value == 1 && !bounded.IsDirty());
    assert(!bounded.CanUndo());
    bounded.Record(1, 9, 1, 90);
    assert(!bounded.CanRedo() && bounded.IsDirty());
    assert(bounded.Undo(value) && value == 1 && !bounded.IsDirty());

    EDITOR::DocumentSnapshotHistory<int> resized(4);
    resized.Record(0, 1);
    resized.Record(1, 2);
    resized.Record(2, 3);
    resized.MarkSaved();
    resized.SetCapacity(1);
    assert(resized.CanUndo() && !resized.CanRedo());
    assert(resized.Undo(value) && value == 2 && resized.IsDirty());
    assert(resized.Redo(value) && value == 3 && !resized.IsDirty());

    EDITOR::DocumentSnapshotHistory<int> resizedAtCursor(4);
    resizedAtCursor.Record(0, 1);
    resizedAtCursor.Record(1, 2);
    resizedAtCursor.Record(2, 3);
    assert(resizedAtCursor.Undo(value) && value == 2);
    resizedAtCursor.MarkSaved();
    assert(resizedAtCursor.Undo(value) && value == 1);
    resizedAtCursor.SetCapacity(1);
    assert(!resizedAtCursor.CanUndo() && resizedAtCursor.CanRedo() && resizedAtCursor.IsDirty());
    assert(resizedAtCursor.Redo(value) && value == 2 && !resizedAtCursor.IsDirty());
}

void DisplayOptions() {
    GameProjectDisplaySettings settings;
    settings.antiAliasing = GameProjectAntiAliasing::Off;
    GameProjectDisplayCapabilities caps;
    caps.gameWindowActive = true;
    caps.antiAliasingMask =
        (1u << static_cast<int>(GameProjectAntiAliasing::Off)) | (1u << static_cast<int>(GameProjectAntiAliasing::TAA));
    const auto& aa = UiDisplaySettings().front();
    assert(CycleUiDisplaySetting(settings, aa, caps, 1));
    assert(settings.antiAliasing == GameProjectAntiAliasing::TAA);
    assert(CycleUiDisplaySetting(settings, aa, caps, -1));
    assert(settings.antiAliasing == GameProjectAntiAliasing::Off);
    caps.gameWindowActive = false;
    const auto before = settings;
    assert(!CycleUiDisplaySetting(settings, aa, caps, 1) && settings == before);
    UiDisplaySettingComponent control;
    control.Deserialize({{"action", "Cycle"}, {"setting", "WindowMode"}, {"direction", -1}});
    nlohmann::json encoded;
    control.Serialize(encoded);
    assert(encoded.at("direction") == -1 && encoded.at("setting") == "WindowMode");
}
void AssetSaveRetry(const std::filesystem::path& executable) {
    const auto root = std::filesystem::absolute(executable).parent_path() / "asset-save-retry";
    AssetDatabase database;
    assert(database.Initialize(root));
    assert(database.ScanAssets(true));
    EDITOR::TextStyleEditorDocument draft;
    std::string error;
    assert(draft.New("Retry", error));
    const auto before = database.CollectByType(AssetType::TextStyle).size();
    auto profile = draft.Profile();
    profile.font = {"ffffffffffffffffffffffffffffffff"};
    assert(draft.Apply(profile, error));
    assert(!draft.Save(database, error) && draft.IsDirty());
    const auto guid = draft.Guid();
    assert(guid.IsValid());
    assert(database.CollectByType(AssetType::TextStyle).size() == before + 1);
    profile.font = {};
    assert(draft.Apply(profile, error) && draft.Save(database, error));
    assert(draft.Guid() == guid && !draft.IsDirty());
    assert(database.CollectByType(AssetType::TextStyle).size() == before + 1);

    profile.font = {"ffffffffffffffffffffffffffffffff"};
    assert(draft.Apply(profile, error) && !draft.Save(database, error));
    const auto path = root / database.FindByGuid(guid)->sourcePath;
    profile.font = {};
    profile.name = "External change";
    assert(SaveTextStyleAsset(path, profile, error));
    assert(!draft.Save(database, error));               // 外部変更の保護は維持する。
    assert(draft.Save(database, error, true) == false); // import 失敗した Save As も同じ identity を保つ。
    const auto copy = draft.Guid();
    assert(copy.IsValid() && copy != guid);
    assert(draft.Apply(profile, error) && draft.Save(database, error));
    assert(draft.Guid() == copy);

    EDITOR::VfxEditorDocument vfx;
    vfx.NewAsset("Metadata retry");
    assert(vfx.Save(database, error));
    const auto vfxGuid = vfx.GetAssetGuid();
    auto effect = vfx.Asset();
    effect.name += " edited";
    assert(vfx.ApplyAsset(effect, error));
    const auto meta = database.GetMetaPathForSource(vfx.GetSourcePath());
    const HANDLE metaLock = CreateFileW(meta.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(metaLock != INVALID_HANDLE_VALUE);
    assert(!vfx.Save(database, error) && vfx.IsDirty());
    CloseHandle(metaLock);
    assert(vfx.Save(database, error) && vfx.GetAssetGuid() == vfxGuid && !vfx.IsDirty());

    // メタデータ作成前に scan が失敗しても、既に保存したパスを忘れない。
    AssetDatabase fresh;
    const auto freshRoot = root / "scan-retry";
    assert(fresh.Initialize(freshRoot));
    const auto metadata = fresh.GetSourceMetaRoot();
    const auto held = metadata.parent_path() / "AssetMeta-held";
    std::filesystem::rename(metadata, held);
    {
        std::ofstream blocked(metadata);
        blocked << "blocked";
    }
    EDITOR::TextStyleEditorDocument pending;
    assert(pending.New("Pending", error));
    assert(!pending.Save(fresh, error) && pending.IsDirty() && !pending.Guid().IsValid());
    size_t filesBeforeRetry = 0;
    for (const auto& entry : std::filesystem::directory_iterator(fresh.GetAssetsRoot() / "TextStyles"))
        filesBeforeRetry += entry.path().extension() == ".htextstyle";
    assert(!pending.Save(database, error)); // 別プロジェクトへ同じ草稿を上書きしない。
    std::filesystem::remove(metadata);
    std::filesystem::rename(held, metadata);
    assert(pending.Save(fresh, error));
    assert(fresh.CollectByType(AssetType::TextStyle).size() == filesBeforeRetry);
}

void TextAssets(const std::filesystem::path& executable) {
    const auto root = std::filesystem::absolute(executable).parent_path() / "text-assets-contract-v1";
    std::filesystem::create_directories(root / "Assets/Fonts");
    const auto fontPath = root / "Assets/Fonts/Example.ttf";
    const auto* fixture = std::getenv("HIKARI_TEST_FONT");
    if (fixture)
        std::filesystem::copy_file(TEXT::PathFromUtf8(fixture), fontPath,
                                   std::filesystem::copy_options::overwrite_existing);
    else {
        std::ofstream file(fontPath, std::ios::binary);
        file << "invalid font";
    }
    AssetDatabase database;
    assert(database.Initialize(root));
    assert(database.ScanAssets(true));
    auto* record = database.FindByPath(fontPath);
    assert(record && record->type == AssetType::Font);
    const auto font = record->guid;
    assert(database.ImportAsset(font) == (fixture != nullptr));
    assert(EDITOR::FindUiFont(database) == (fixture ? font.value : std::string{}));
    TextStyleAsset style;
    style.font = font;
    style.name = "Shared";
    style.pixelHeight = 64;
    style.color = {1, .2f, .1f, 1};
    const auto path = root / "Assets/TextStyles/Shared.htextstyle";
    std::string error;
    assert(SaveTextStyleAsset(path, style, error));
    assert(database.ScanAssets(true));
    record = database.FindByPath(path);
    assert(record && record->type == AssetType::TextStyle);
    const auto guid = record->guid;
    assert(database.ImportAsset(guid));
    record = database.FindByGuid(guid);
    assert(record->artifactManifest.dependencies.size() == 1);
    assert(record->artifactManifest.dependencies.front().guid == font);
    TextAssetStore store;
    store.Configure(&database);
    assert(bool(store.LoadStyle(guid, error)));
    TextStyleBinding binding;
    binding.style = guid;
    TextStyleAsset local;
    local.pixelHeight = 24;
    local.effects.reveal = .25f;
    auto resolved = ResolveTextStyle(&store, binding, local);
    assert(resolved.pixelHeight == 64 && resolved.effects.reveal == .25f && resolved.font == font);
    binding.overrideSize = true;
    assert(ResolveTextStyle(&store, binding, local).pixelHeight == 24);
    if (fixture)
        assert(store.ResolveFont(font) == "Fonts/Example.ttf");
    if (fixture) {
        const auto revision = store.FontRevision(fontPath);
        assert(revision);
        assert(database.ImportAsset(font));
        assert(store.FontRevision(fontPath) == revision);
        {
            std::ofstream append(fontPath, std::ios::binary | std::ios::app);
            append << "font-revision-contract";
        }
        assert(database.ImportAsset(font));
        assert(store.FontRevision(fontPath) != revision);
        GAME_UI::GameUiPresentationService ui;
        ui.SetProjectRoot(root);
        ui.SetAssetDatabase(&database);
        GameProjectUiTextRequest request;
        request.sourceId = 44;
        request.revealAmount = .3f;
        assert(ui.SubmitTextStyle(guid, "Shared appearance", request, 1.5f));
        ui.Tick(1);
        const auto& draw = ui.GetFrameSource().texts.at(0);
        assert(draw.fontRevision == store.FontRevision(fontPath));
        assert(draw.request.pixelHeight == 96 && draw.request.revealAmount == .3f);
        WorldTextComponent spatial;
        spatial.Deserialize({{"textStyleAssetGuid", guid.value},
                             {"fontHeight", 2},
                             {"overrideTextEffects", true},
                             {"letterSpacing", .25f},
                             {"outlineWidth", .02f}});
        const auto appearance = spatial.ResolveAppearance(&store);
        assert(appearance.pixelHeight == 64 && std::abs(appearance.effects.letterSpacing / 64 * 2 - .25f) < .0001f);
        assert(std::abs(appearance.effects.outlineWidth / 64 - .02f) < .0001f);
    }
    EDITOR::TextStyleEditorDocument draft;
    assert(draft.Open(database, guid, error));
    style.pixelHeight = 72;
    assert(draft.Apply(style, error, 7));
    style.pixelHeight = 80;
    assert(draft.Apply(style, error, 7));
    assert(draft.Undo() && draft.Profile().pixelHeight == 64);
    assert(draft.Redo() && draft.Profile().pixelHeight == 80);
    assert(!draft.New("Protected", error));
    assert(draft.Save(database, error));
    assert(store.LoadStyle(guid, error)->pixelHeight == 80);
    auto invalid = WriteTextStyleAsset(style);
    invalid["version"] = 99;
    assert(!ReadTextStyleAsset(invalid, style, error));
    // Selected-scene export closes scene -> shared style -> font, without a
    // payload-directory workaround.
    if (fixture) {
        SceneDocument scene;
        scene.sceneName = "Shared Text";
        SceneObjectData object;
        object.id = {1};
        object.name = "Caption";
        object.components.push_back({"UiTextComponent", {{"text", "Hello"}, {"textStyleAssetGuid", guid.value}}});
        scene.objects.push_back(object);
        const auto usage = AnalyzeAssetUsage(scene, database);
        assert(usage.IsUsed(guid) && usage.IsUsed(font));
        const auto scenePath = root / "Assets/Text.hscene";
        assert(SceneSerializer{}.SaveToFile(TEXT::PathToUtf8(scenePath), scene));
        assert(database.ScanAssets(true));
        const auto sceneGuid = database.FindByPath(scenePath)->guid;
        assert(database.ImportAsset(sceneGuid));
        {
            std::ofstream descriptor(root / "Project.hikari");
            descriptor << R"({"formatVersion":1,"name":"Text contracts"})";
        }
        ASSETS::COOKING::AssetCookOptions options;
        DialogueAssetStore dialogues;
        dialogues.SetAssetDatabase(&database);
        auto dialogueStyle = DIALOGUE::MakeDefaultDialogueProjectStyle();
        dialogueStyle.theme.speakerTextStyle = dialogueStyle.theme.bodyTextStyle = dialogueStyle.theme.choiceTextStyle =
            guid;
        assert(DIALOGUE::SaveDialogueProjectStyle(root, dialogueStyle, &error));
        assert(dialogues.LoadProjectStyle(&error));
        assert(AnalyzeAssetUsage(SceneDocument{}, database).IsUsed(font));
        DIALOGUE::DialogueDefinition story;
        DIALOGUE::DialogueNode line;
        line.speaker.values.emplace("en-US", "Guide");
        line.text.values.emplace("en-US", "Shared typography");
        DIALOGUE::DialoguePresentationState view;
        view.definition = &story;
        view.node = &line;
        view.style = &dialogueStyle;
        view.locale = "en-US";
        view.sessionElapsed = 1;
        GAME_UI::GameUiPresentationService dialogueUi;
        dialogueUi.SetProjectRoot(root);
        dialogueUi.SetAssetDatabase(&database);
        assert(DIALOGUE::SubmitDialoguePresentation(dialogueUi, view));
        dialogueUi.Tick(1);
        assert(dialogueUi.GetFrameSource().texts.size() == 2);
        dialogueStyle.theme.bodyTextStyle = {};
        assert(!DIALOGUE::SubmitDialoguePresentation(dialogueUi, view));
        options.projectRoot = root;
        options.contentRoot = root / "Cooked";
        options.startupSceneGuid = sceneGuid.value;
        options.sceneGuids = {sceneGuid.value};
        const auto cooked = ASSETS::COOKING::AssetCooker{}.Cook(options);
        if (!cooked.success)
            std::cerr << cooked.message << "\n";
        assert(cooked.success && cooked.assetCount == 3);
        AssetDatabase runtime;
        assert(runtime.Initialize(options.contentRoot));
        assert(runtime.ScanAssets(false));
        assert(runtime.IsCookedContent());
        TextAssetStore shipped;
        shipped.Configure(&runtime);
        assert(shipped.LoadStyle(guid, error)->pixelHeight == 80);
        assert(shipped.ResolveFont(font) == "Fonts/Example.ttf");
        assert(std::filesystem::exists(options.contentRoot / "RuntimePayload/Assets/Fonts/Example.ttf"));
    }
}
bool Near(float a, float b) { return std::isfinite(a) && std::abs(a - b) < .001f; }
GameObject* Object(World& world, uint64_t id, GameObject* parent = nullptr) {
    auto* o = world.CreateObject("UI");
    assert(o && o->SetDocumentId({id}));
    if (parent)
        assert(o->SetParent(parent));
    return o;
}
void GameViewportPointer() {
    struct EditorPointer final : RUNTIME::IEditorUiServices {
        bool active = true, down = true;
        bool Initialize(PLATFORM::Win32Window&, const GFX::Context&) override { return true; }
        void BeginFrame(PLATFORM::Win32Window&, const GFX::Context&, float, bool) override {}
        void RenderFrame(const GFX::Context&, bool) override {}
        void Shutdown(PLATFORM::Win32Window&) noexcept override {}
        bool IsGameViewportPlayInputActive() const noexcept override { return active; }
        bool TryGetGameViewportPointer(HIKARI::INPUT::InputPointerState& p, uint32_t& w,
                                       uint32_t& h) const noexcept override {
            if (!active)
                return false;
            w = 960;
            h = 540;
            p.x = 240;
            p.y = 135;
            p.normalizedX = .25f;
            p.normalizedY = .25f;
            p.primaryDown = down;
            return true;
        }
    } editor;
    auto& state = SERVICES::INTERNAL::GetRuntimeServiceState();
    const auto previous = state.runtimeHostMode;
    state.runtimeHostMode = RuntimeHostMode::Editor;
    assert(RUNTIME::RegisterEditorUiServices(editor));
    HIKARI::INPUT::InputService input;
    HIKARI::INPUT::InputPointerState pointer;
    uint32_t w = 0, h = 0;
    assert(SERVICES::TryGetGamePointer(input, pointer, w, h));
    assert(w == 960 && h == 540 && pointer.x == 240 && pointer.y == 135 && pointer.primaryDown);
    World world;
    world.Services().Register(input);
    GameProjectPointerState sdk;
    assert(GameProjectSdkAccess::MakeInputView(world).GetPointerState(sdk));
    assert(sdk.logicalWidth == w && sdk.x == pointer.x && sdk.y == pointer.y);
    GAME_UI::GameUiPresentationService presentation;
    bool playing = true;
    RuntimePlayStateService play{&playing};
    assert(world.Services().Register(presentation));
    assert(world.Services().Register(play));
    auto* canvas = Object(world, 1);
    canvas->AddComponent<UiCanvasComponent>()->Deserialize(
        {{"referenceWidth", 960}, {"referenceHeight", 540}, {"inputMode", "Menu"}});
    auto* object = Object(world, 2, canvas);
    object->AddComponent<UiRectComponent>()->Deserialize({{"anchorMinimum", {0, 0}},
                                                          {"anchorMaximum", {0, 0}},
                                                          {"offsetMinimum", {220, 115}},
                                                          {"offsetMaximum", {280, 175}}});
    auto* button = object->AddComponent<UiButtonComponent>();
    UiSceneSystem ui;
    ui.OnWorldAttached(world);
    FrameContext frame{};
    frame.frameIndex = 1;
    editor.down = false;
    ui.PreUpdate(world, frame);
    editor.down = true;
    ++frame.frameIndex;
    ui.PreUpdate(world, frame);
    assert(button->GetVisualState() == UiButtonVisualState::Pressed);
    ui.OnWorldDetached(world);
    editor.active = false;
    assert(!SERVICES::TryGetGamePointer(input, pointer, w, h));
    assert(!GameProjectSdkAccess::MakeInputView(world).GetPointerState(sdk));
    RUNTIME::UnregisterEditorUiServices(editor);
    state.runtimeHostMode = previous;
}

class UiContractInputBackend final : public HIKARI::INPUT::IInputBackend {
public:
    void SetHostWindow(void*) override {}
    bool HasInputFocus() const noexcept override { return true; }
    void SetMouseCaptureMode(HIKARI::INPUT::MouseCaptureMode mode) override { captureMode_ = mode; }
    void SetMouseCaptureRegion(const HIKARI::INPUT::MouseCaptureRegion&) override {}
    void ClearMouseCaptureRegion() override {}
    HIKARI::INPUT::MouseCaptureMode GetMouseCaptureMode() const noexcept override { return captureMode_; }
    void SetExternalMouseWheel(float delta) override { state_.mouseWheel += delta; }
    void SetPointerCursorVisible(bool) override {}
    void Reset() override { state_ = {}; }
    void Poll(HIKARI::INPUT::InputDeviceState& out) override {
        out = state_;
        state_.mouseWheel = 0.0f;
    }
    float ReadControl(const HIKARI::INPUT::InputDeviceState&, HIKARI::INPUT::InputBindingSource, std::string_view,
                      uint32_t) const override {
        return 0.0f;
    }
    bool IsAnyControlActive(const HIKARI::INPUT::InputDeviceState&, float) const override { return false; }
    void EnumerateNewControls(const HIKARI::INPUT::InputDeviceState&, const HIKARI::INPUT::InputDeviceState&, float,
                              bool, std::vector<HIKARI::INPUT::InputControlActuation>& candidates) const override {
        candidates.clear();
    }
    bool SetGamepadVibration(uint32_t, float, float) override { return true; }

private:
    HIKARI::INPUT::InputDeviceState state_{};
    HIKARI::INPUT::MouseCaptureMode captureMode_ = HIKARI::INPUT::MouseCaptureMode::Free;
};

class UiContractEditorPointer final : public RUNTIME::IEditorUiServices {
public:
    bool Initialize(PLATFORM::Win32Window&, const GFX::Context&) override { return true; }
    void BeginFrame(PLATFORM::Win32Window&, const GFX::Context&, float, bool) override {}
    void RenderFrame(const GFX::Context&, bool) override {}
    void Shutdown(PLATFORM::Win32Window&) noexcept override {}
    bool IsGameViewportPlayInputActive() const noexcept override { return active; }
    bool TryGetGameViewportPointer(HIKARI::INPUT::InputPointerState& pointer, uint32_t& width,
                                   uint32_t& height) const noexcept override {
        if (!active)
            return false;
        width = 960;
        height = 540;
        pointer.x = x;
        pointer.y = y;
        pointer.normalizedX = x / static_cast<float>(width);
        pointer.normalizedY = y / static_cast<float>(height);
        pointer.primaryDown = down;
        return true;
    }

    float x = 900.0f;
    float y = 500.0f;
    bool active = true;
    bool down = false;
};

class UiContractRuntime final {
public:
    UiContractRuntime() : previousMode_(SERVICES::INTERNAL::GetRuntimeServiceState().runtimeHostMode) {
        SERVICES::INTERNAL::GetRuntimeServiceState().runtimeHostMode = RuntimeHostMode::Editor;
        input.SetBackend(std::make_unique<UiContractInputBackend>());
        assert(input.InitializeTransient());
        assert(RUNTIME::RegisterEditorUiServices(editor));
    }
    ~UiContractRuntime() {
        RUNTIME::UnregisterEditorUiServices(editor);
        SERVICES::INTERNAL::GetRuntimeServiceState().runtimeHostMode = previousMode_;
    }
    UiContractRuntime(const UiContractRuntime&) = delete;
    UiContractRuntime& operator=(const UiContractRuntime&) = delete;

    void Attach(World& world, UiSceneSystem& ui) {
        assert(world.Services().Register(input));
        assert(world.Services().Register(presentation));
        assert(world.Services().Register(playState));
        ui.OnWorldAttached(world);
    }
    void Tick(World& world, UiSceneSystem& ui, float delta = 1.0f / 60.0f) {
        frame.frameIndex++;
        frame.unscaledDt = delta;
        frame.gameDt = delta;
        input.Update(delta);
        ui.PreUpdate(world, frame);
    }
    void Detach(World& world, UiSceneSystem& ui) { ui.OnWorldDetached(world); }

    UiContractEditorPointer editor;
    HIKARI::INPUT::InputService input;
    GAME_UI::GameUiPresentationService presentation;
    bool playing = true;
    RuntimePlayStateService playState{&playing};
    FrameContext frame{};

private:
    RuntimeHostMode previousMode_;
};

void SetUiRect(GameObject& object, float left, float top, float right, float bottom) {
    object.AddComponent<UiRectComponent>()->Deserialize({{"anchorMinimum", {0, 0}},
                                                         {"anchorMaximum", {0, 0}},
                                                         {"offsetMinimum", {left, top}},
                                                         {"offsetMaximum", {right, bottom}}});
}

void TextFieldInputContracts() {
    UiTextComponent text;
    assert(text.SetText(""));
    UiTextFieldComponent field;
    field.Deserialize({{"maximumCharacters", 2}});
    assert(field.InsertCommittedText(text, "猫😀犬"));
    assert(text.GetText() == "猫😀" && TEXT::IsValidUtf8(text.GetText()));

    field.Deserialize({{"maximumCharacters", 1}});
    assert(field.SetCaret(text, text.GetText().size(), false));
    const std::string before = text.GetText();
    assert(!field.InsertCommittedText(text, "界"));
    assert(text.GetText() == before && TEXT::IsValidUtf8(text.GetText()));
}

void UiInteractionContracts() {
    UiContractRuntime runtime;
    {
        World world;
        auto* canvas = Object(world, 1);
        canvas->AddComponent<UiCanvasComponent>()->Deserialize(
            {{"referenceWidth", 960}, {"referenceHeight", 540}, {"inputMode", "Menu"}});
        auto* fieldObject = Object(world, 2, canvas);
        SetUiRect(*fieldObject, 100, 100, 300, 150);
        fieldObject->AddComponent<UiTextComponent>();
        fieldObject->AddComponent<UiTextFieldComponent>();
        auto* buttonObject = Object(world, 3, canvas);
        SetUiRect(*buttonObject, 400, 100, 600, 150);
        buttonObject->AddComponent<UiButtonComponent>();

        assert(canvas->GetComponent<UiCanvasComponent>()->RequestFocus(fieldObject->GetRuntimeHandle()));
        runtime.editor.x = 150.0f;
        runtime.editor.y = 125.0f;
        UiSceneSystem ui;
        runtime.Attach(world, ui);
        runtime.Tick(world, ui);
        assert(ui.GetFocusedControl() == fieldObject->GetRuntimeHandle());
        runtime.editor.x = 500.0f;
        runtime.Tick(world, ui);
        assert(ui.GetFocusedControl() == fieldObject->GetRuntimeHandle());
        runtime.Detach(world, ui);
    }
    {
        World world;
        auto* canvas = Object(world, 10);
        canvas->AddComponent<UiCanvasComponent>()->Deserialize(
            {{"referenceWidth", 960}, {"referenceHeight", 540}, {"inputMode", "Menu"}});
        auto* parentModal = Object(world, 11, canvas);
        SetUiRect(*parentModal, 20, 20, 900, 520);
        parentModal->AddComponent<UiGroupComponent>()->Deserialize({{"modal", true}});
        auto* parentField = Object(world, 12, parentModal);
        SetUiRect(*parentField, 20, 20, 260, 70);
        parentField->AddComponent<UiTextComponent>();
        parentField->AddComponent<UiTextFieldComponent>();
        auto* childModal = Object(world, 13, parentModal);
        SetUiRect(*childModal, 300, 80, 760, 420);
        auto* childGroup = childModal->AddComponent<UiGroupComponent>();
        childGroup->Deserialize({{"modal", true}, {"visible", false}});
        auto* childField = Object(world, 14, childModal);
        SetUiRect(*childField, 20, 20, 260, 70);
        childField->AddComponent<UiTextComponent>();
        childField->AddComponent<UiTextFieldComponent>();

        assert(canvas->GetComponent<UiCanvasComponent>()->RequestFocus(parentField->GetRuntimeHandle()));
        UiSceneSystem ui;
        runtime.Attach(world, ui);
        runtime.Tick(world, ui);
        assert(ui.GetFocusedControl() == parentField->GetRuntimeHandle());
        childGroup->SetVisible(true);
        runtime.Tick(world, ui);
        assert(ui.GetFocusedControl() == childField->GetRuntimeHandle());
        childGroup->SetVisible(false);
        runtime.Tick(world, ui);
        assert(ui.GetFocusedControl() == parentField->GetRuntimeHandle());
        runtime.Detach(world, ui);
    }
    {
        World world;
        auto* canvas = Object(world, 20);
        canvas->AddComponent<UiCanvasComponent>()->Deserialize(
            {{"referenceWidth", 960}, {"referenceHeight", 540}, {"inputMode", "Menu"}});
        auto* viewport = Object(world, 21, canvas);
        SetUiRect(*viewport, 0, 0, 200, 200);
        auto* viewportScroll = viewport->AddComponent<UiScrollViewComponent>();
        viewportScroll->Deserialize({{"axis", "Vertical"}, {"contentExtent", 1000}});
        viewportScroll->SetScrollOffset(400.0f, 200.0f);
        auto* clippedField = Object(world, 22, viewport);
        SetUiRect(*clippedField, 10, 300, 190, 350);
        clippedField->AddComponent<UiTextComponent>();
        clippedField->AddComponent<UiTextFieldComponent>();
        auto* autoViewport = Object(world, 23, canvas);
        SetUiRect(*autoViewport, 300, 0, 500, 100);
        auto* autoScroll = autoViewport->AddComponent<UiScrollViewComponent>();
        autoScroll->Deserialize({{"axis", "Vertical"}, {"contentExtent", 0}});
        autoViewport->AddComponent<UiLayoutGroupComponent>()->Deserialize(
            {{"direction", 1}, {"cellSize", {100, 30}}, {"spacing", 5}, {"padding", 10}});
        auto* first = Object(world, 24, autoViewport);
        SetUiRect(*first, 0, 0, 100, 30);
        auto* second = Object(world, 25, autoViewport);
        SetUiRect(*second, 0, 0, 100, 30);
        auto* third = Object(world, 26, autoViewport);
        SetUiRect(*third, 0, 0, 100, 30);
        third->AddComponent<UiGroupComponent>()->SetVisible(false);
        UiLayoutResolver resolver;
        (void)resolver.Resolve(world, *first);
        assert(Near(autoScroll->GetEffectiveContentExtent(), 85.0f));
        third->GetComponent<UiGroupComponent>()->SetVisible(true);
        resolver.Clear();
        (void)resolver.Resolve(world, *first);
        assert(Near(autoScroll->GetEffectiveContentExtent(), 120.0f));

        assert(canvas->GetComponent<UiCanvasComponent>()->RequestFocus(clippedField->GetRuntimeHandle()));
        runtime.editor.x = 900.0f;
        runtime.editor.y = 500.0f;
        UiSceneSystem ui;
        runtime.Attach(world, ui);
        runtime.Tick(world, ui);
        assert(!ui.GetFocusedControl().IsValid());
        runtime.Detach(world, ui);
    }
    {
        World world;
        auto* canvas = Object(world, 30);
        canvas->AddComponent<UiCanvasComponent>()->Deserialize(
            {{"referenceWidth", 960}, {"referenceHeight", 540}, {"inputMode", "Menu"}});
        auto* background = Object(world, 31, canvas);
        SetUiRect(*background, 0, 0, 960, 540);
        auto* scroll = background->AddComponent<UiScrollViewComponent>();
        scroll->Deserialize({{"axis", "Vertical"}, {"contentExtent", 1000}});
        scroll->SetScrollOffset(100.0f, 540.0f);
        auto* modal = Object(world, 32, canvas);
        SetUiRect(*modal, 400, 100, 800, 400);
        auto* group = modal->AddComponent<UiGroupComponent>();
        group->Deserialize({{"modal", true}});

        runtime.editor.x = 100.0f;
        runtime.editor.y = 100.0f;
        UiSceneSystem ui;
        runtime.Attach(world, ui);
        runtime.input.SetExternalMouseWheel(1.0f);
        runtime.Tick(world, ui);
        assert(Near(scroll->GetScrollOffset(540.0f), 100.0f));
        group->SetVisible(false);
        runtime.input.SetExternalMouseWheel(1.0f);
        runtime.Tick(world, ui);
        assert(Near(scroll->GetScrollOffset(540.0f), 52.0f));
        runtime.Detach(world, ui);
    }
}

void DisplayPanelTransaction() {
    auto& host = SERVICES::INTERNAL::GetRuntimeServiceState();
    const auto oldMode = host.runtimeHostMode;
    const auto oldQuality = RENDER3D::GetRenderQualitySettings();
    const auto oldRequest = host.requestedRenderQuality;
    const auto oldPending = host.hasRequestedRenderQuality;
    host.runtimeHostMode = RuntimeHostMode::Game;
    host.hasRequestedRenderQuality = false;
    auto baseline = oldQuality;
    baseline.antiAliasingMode = RENDER3D::RenderAntiAliasingMode::TAA;
    baseline.frameGenerationMode = RENDER3D::RenderFrameGenerationMode::Off;
    baseline.windowMode = RENDER3D::WindowPresentationMode::Windowed;
    baseline.windowSize = RENDER3D::RenderResolutionPreset::P720;
    RENDER3D::SetRenderQualitySettings(baseline);
    World world;
    GAME_UI::GameUiPresentationService ui;
    bool playing = true;
    RuntimePlayStateService play{&playing};
    assert(world.Services().Register(ui) && world.Services().Register(play));
    auto* root = Object(world, 1);
    auto* panel = root->AddComponent<UiDisplaySettingsPanelComponent>();
    const auto control = [&](uint64_t id, const char* action) {
        auto* object = Object(world, id, root);
        object->AddComponent<UiTextComponent>();
        object->AddComponent<UiButtonComponent>();
        object->AddComponent<UiDisplaySettingComponent>()->Deserialize({{"action", action}, {"setting", "WindowMode"}});
        return object;
    };
    auto* cycle = control(2, "Cycle");
    auto* apply = control(3, "Apply");
    auto* keep = control(4, "Keep");
    FrameContext frame{};
    const auto tick = [&](GameObject* clicked = nullptr, float dt = .016f) {
        ++frame.frameIndex;
        frame.unscaledDt = dt;
        frame.gameDt = 0;
        ui.BeginInteractionFrame(frame.frameIndex);
        if (clicked)
            assert(ui.PublishButtonActivation(clicked->GetRuntimeHandle(), frame.frameIndex));
        panel->Tick(world, frame);
    };
    const auto completeFrameBoundary = [&] {
        assert(host.hasRequestedRenderQuality);
        // OS ウィンドウは作らず、キューの完了通知に相当する確定値だけを反映する。
        RENDER3D::SetRenderQualitySettings(host.requestedRenderQuality);
        host.hasRequestedRenderQuality = false;
        tick();
    };
    tick();
    tick(cycle);
    tick(apply);
    assert(RENDER3D::GetRenderQualitySettings().windowMode == baseline.windowMode);
    completeFrameBoundary();
    assert(keep->GetComponent<UiButtonComponent>()->IsInteractable());
    tick(keep);
    tick();
    assert(!keep->GetComponent<UiButtonComponent>()->IsInteractable());
    const auto accepted = RENDER3D::GetRenderQualitySettings().windowMode;
    tick(cycle);
    tick(apply);
    completeFrameBoundary();
    tick(nullptr, 16.f);
    assert(host.hasRequestedRenderQuality && host.requestedRenderQuality.windowMode == accepted);
    completeFrameBoundary();
    assert(RENDER3D::GetRenderQualitySettings().windowMode == accepted);
    RENDER3D::SetRenderQualitySettings(oldQuality);
    host.runtimeHostMode = oldMode;
    host.requestedRenderQuality = oldRequest;
    host.hasRequestedRenderQuality = oldPending;
}
void LayoutAndPresentation() {
    World world;
    auto* canvas = Object(world, 1);
    canvas->AddComponent<UiCanvasComponent>()->Deserialize({{"referenceWidth", 1000}, {"referenceHeight", 1000}});
    auto* parent = Object(world, 2, canvas);
    parent->AddComponent<UiRectComponent>()->Deserialize({{"anchorMinimum", {0, 0}},
                                                          {"anchorMaximum", {0, 0}},
                                                          {"offsetMinimum", {100, 100}},
                                                          {"offsetMaximum", {300, 300}},
                                                          {"rotationDegrees", 90}});
    auto* group = parent->AddComponent<UiGroupComponent>();
    group->Deserialize({{"opacity", .5}, {"interactable", false}});
    auto* child = Object(world, 3, parent);
    child->AddComponent<UiRectComponent>()->Deserialize({{"anchorMinimum", {0, 0}},
                                                         {"anchorMaximum", {0, 0}},
                                                         {"offsetMinimum", {0, 0}},
                                                         {"offsetMaximum", {100, 100}}});
    child->AddComponent<UiImageComponent>()->Deserialize({{"tint", {1, 1, 1, 1}}});
    UiLayoutResolver resolver;
    auto r = resolver.Resolve(world, *child);
    assert(r.valid && Near(r.x, 100) && Near(r.y, 100) && Near(r.opacity, .5f) && !r.interactable);
    assert(ContainsUiPoint(r, 250, 150) && !ContainsUiPoint(r, 0, 0));
    parent->GetComponent<UiRectComponent>()->Deserialize({{"enabled", false}});
    resolver.Clear();
    assert(!resolver.Resolve(world, *child).valid);
    parent->GetComponent<UiRectComponent>()->Deserialize({});
    group->SetVisible(false);
    resolver.Clear();
    assert(!resolver.Resolve(world, *child).valid);
    group->SetVisible(true);
    group->Deserialize({{"opacity", .5}});
    child->AddComponent<UiGroupComponent>()->Deserialize({{"opacity", .5}});
    resolver.Clear();
    assert(Near(resolver.Resolve(world, *child).opacity, .25f));

    // 自動配置は文書のオフセットを書き換えず、無効な子を飛ばす。
    parent->AddComponent<UiLayoutGroupComponent>()->Deserialize(
        {{"direction", 2}, {"columns", 2}, {"cellSize", {40, 30}}, {"spacing", 10}, {"padding", 5}});
    auto* second = Object(world, 4, parent);
    second->AddComponent<UiRectComponent>();
    auto* third = Object(world, 5, parent);
    third->AddComponent<UiRectComponent>();
    resolver.Clear();
    auto a = resolver.Resolve(world, *child), b = resolver.Resolve(world, *second), c = resolver.Resolve(world, *third);
    assert(Near(b.x - a.x, 50) && Near(c.y - a.y, 40) && Near(a.width, 40));
    second->GetComponent<UiRectComponent>()->Deserialize({{"enabled", false}});
    resolver.Clear();
    a = resolver.Resolve(world, *child);
    c = resolver.Resolve(world, *third);
    assert(Near(c.x - a.x, 50) && Near(c.y, a.y));

    auto* progress = child->AddComponent<UiProgressComponent>();
    progress->Deserialize({{"value", 25}, {"minimum", 0}, {"maximum", 100}});
    UiSceneSystem projector;
    GAME_UI::GameUiPresentationService presentation;
    presentation.SetProjectRoot(std::filesystem::path(HIKARI_CONTRACT_OUTPUT_ROOT));
    FrameContext frame{};
    frame.frameIndex = 1;
    projector.BuildPresentation(world, frame, presentation, {1});
    presentation.Tick(1);
    const auto& draws = presentation.GetFrameSource().sprites;
    assert(draws.size() == 1 && Near(draws[0].request.width, 10) && Near(draws[0].request.tint.a, .25f));
    progress->SetValue(0);
    presentation.DiscardPending();
    projector.BuildPresentation(world, frame, presentation, {1});
    presentation.Tick(2);
    assert(presentation.GetFrameSource().sprites.empty());
    auto* missingFont = third->AddComponent<UiTextComponent>();
    missingFont->Deserialize({{"text", "Missing font"}});
    presentation.DiscardPending();
    projector.BuildPresentation(world, frame, presentation, {1});
    assert(projector.RejectedTextRequests() == 1);
    missingFont->Deserialize({{"text", ""}});
    presentation.DiscardPending();
    projector.BuildPresentation(world, frame, presentation, {1});
    assert(projector.RejectedTextRequests() == 0);
}
void ActionsAndStyles() {
    World world;
    auto* group = Object(world, 7)->AddComponent<UiGroupComponent>();
    UiButtonComponent button;
    const auto handle = GameProjectSdkAccess::MakeObjectView(*world.FindObject(SceneObjectId{7})).GetHandle();
    auto ui = GameProjectSdkAccess::MakeUiView(world);
    assert(ui.SetGroupOpacity(handle, .4f) && Near(group->GetOpacity(), .4f));
    assert(!ui.SetGroupOpacity(handle, std::numeric_limits<float>::quiet_NaN()) && Near(group->GetOpacity(), .4f));
    assert(!ui.SetGroupVisible({}, true));
    assert(ui.SetGroupVisible(handle, false) && !group->IsVisible());
    group->SetVisible(true);
    button.Deserialize({{"groupTarget", SCENE::SERIALIZATION::WriteSceneObjectReference(7)}, {"groupAction", 3}});
    button.ApplyGroupAction(world);
    assert(!group->IsVisible());
    button.ApplyGroupAction(world);
    assert(group->IsVisible());
    nlohmann::json properties;
    button.Serialize(properties);
    SCENE::SERIALIZATION::RemapSceneObjectReferences(properties, {{7, 9}});
    assert(SCENE::SERIALIZATION::SceneObjectReferenceOr(properties["groupTarget"]) == 9);
    UiTextComponent text;
    text.Deserialize({{"text", "First\n第二行"},
                      {"lineHeight", 1.4},
                      {"letterSpacing", 2},
                      {"outlineWidth", 4},
                      {"outlineColor", {.1, .2, .3, .4}},
                      {"shadowOffset", {3, 4}},
                      {"reveal", .5},
                      {"glowWidth", 5},
                      {"glowIntensity", 2}});
    nlohmann::json before, after;
    text.Serialize(before);
    UiTextComponent restored;
    restored.Deserialize(before);
    restored.Serialize(after);
    assert(before == after);
    GameProjectUiTextRequest request;
    text.GetStyle().Apply(request);
    assert(Near(request.outlineWidth, 4) && Near(request.revealAmount, .5f) && Near(request.shadowOffsetY, 4));
    UiTextComponent legacy;
    legacy.Deserialize({});
    GameProjectUiTextRequest defaults, legacyRequest;
    legacy.GetStyle().Apply(legacyRequest);
    assert(Near(defaults.outlineWidth, legacyRequest.outlineWidth) && defaults.wrap == legacyRequest.wrap);
    UiProgressComponent progress;
    progress.Deserialize({{"value", 20}, {"minimum", 10}, {"maximum", 30}});
    assert(Near(progress.ResolveFraction(nullptr), .5f));
    progress.Deserialize({{"minimum", 3}, {"maximum", 3}});
    assert(Near(progress.ResolveFraction(nullptr), 0));
}
void Templates(const std::filesystem::path& output) {
    ComponentRegistry registry;
    WorldMassSchemaRegistry schemas;
    SystemTypeRegistry systems;
    ComponentSystemPolicy policy;
    auto context = RuntimeFeatureContextAccess::Create(registry, schemas, systems, policy);
    assert(CreateUiRuntimeFeature()->Register(context));
    ComponentTypeInfo worldText;
    worldText.typeName = "WorldTextComponent";
    worldText.factory = [] { return IComponent::Create<WorldTextComponent>(); };
    assert(registry.Register(std::move(worldText)));
    // SceneFlow のないプロジェクトでは部分的なメニューを残さない。
    SceneDocument scene;
    assert(!EDITOR::CreateUiTemplate(scene, registry, "scene_button", {}, "font.ttf").ok && scene.objects.empty());
    for (const auto& t : EDITOR::UiTemplates()) {
        if (t.id == "scene_button")
            continue;
        const auto created = EDITOR::CreateUiTemplate(scene, registry, t.id, {}, "font.ttf");
        assert(created.ok);
    }
    std::set<uint64_t> ids;
    for (const auto& object : scene.objects) {
        assert(object.id.value && ids.insert(object.id.value).second);
        if (object.parent)
            assert(object.parent->value);
        std::set<uint64_t> components;
        for (const auto& c : object.components)
            assert(c.id.value && components.insert(c.id.value).second);
    }
    const auto findComponent = [](const SceneObjectData& object, std::string_view type) -> const SceneComponentData* {
        const auto component = std::find_if(object.components.begin(), object.components.end(),
                                            [&](const auto& value) { return value.type == type; });
        return component == object.components.end() ? nullptr : &*component;
    };
    const auto hasComponent = [&](const SceneObjectData& object, std::string_view type) {
        return findComponent(object, type) != nullptr;
    };
    for (const auto [templateId, expectedInputMode] : {std::pair{"menu", "Menu"}, std::pair{"hud", "Hud"}}) {
        SceneDocument preset;
        const auto root = EDITOR::CreateUiTemplate(preset, registry, templateId, {}, "font.ttf");
        assert(root.ok);
        const auto canvas = std::find_if(preset.objects.begin(), preset.objects.end(),
                                         [&](const auto& object) { return hasComponent(object, "UiCanvasComponent"); });
        assert(canvas != preset.objects.end());
        const auto* canvasComponent = findComponent(*canvas, "UiCanvasComponent");
        assert(canvasComponent && canvasComponent->properties.at("inputMode") == expectedInputMode);
    }
    const auto scroll = std::find_if(scene.objects.begin(), scene.objects.end(),
                                     [&](const auto& object) { return hasComponent(object, "UiScrollViewComponent"); });
    assert(scroll != scene.objects.end() && hasComponent(*scroll, "UiRectComponent") &&
           hasComponent(*scroll, "UiScrollViewComponent") && hasComponent(*scroll, "UiLayoutGroupComponent"));
    const auto* scrollView = findComponent(*scroll, "UiScrollViewComponent");
    const auto* layoutGroup = findComponent(*scroll, "UiLayoutGroupComponent");
    assert(scrollView && scrollView->properties.at("contentExtent") == 0.0 &&
           scrollView->properties.at("axis") == "Vertical");
    assert(layoutGroup && layoutGroup->properties.at("direction") == 1);
    assert(std::count_if(scene.objects.begin(), scene.objects.end(), [&](const auto& object) {
               return object.parent == scroll->id && hasComponent(object, "UiButtonComponent");
           }) == 3);
    const auto textField = std::find_if(scene.objects.begin(), scene.objects.end(), [&](const auto& object) {
        return hasComponent(object, "UiTextFieldComponent");
    });
    assert(textField != scene.objects.end() && hasComponent(*textField, "UiRectComponent") &&
           hasComponent(*textField, "UiTextComponent"));
    const auto* textFieldSettings = findComponent(*textField, "UiTextFieldComponent");
    const auto* text = findComponent(*textField, "UiTextComponent");
    assert(textFieldSettings && textFieldSettings->properties.at("enabled") == true &&
           textFieldSettings->properties.at("maximumCharacters") == 256);
    assert(text && text->properties.at("fontAssetPath") == "font.ttf" &&
           text->properties.at("text") == TEXT::Localize("uix.textFieldSample"));
    const auto count = scene.objects.size();
    assert(!EDITOR::CreateUiTemplate(scene, registry, "button", {9999}, "font.ttf").ok &&
           scene.objects.size() == count);
    const auto hud = EDITOR::CreateUiTemplate(scene, registry, "hud", {}, "font.ttf");
    assert(hud.ok);
    const auto textId = EDITOR::CreateUiTemplate(scene, registry, "label", hud.id, "font.ttf");
    assert(textId.ok);
    auto& label = scene.objects.back();
    const auto c = std::find_if(label.components.begin(), label.components.end(),
                                [](const auto& value) { return value.type == "UiTextComponent"; });
    assert(c != label.components.end());
    EDITOR::ComponentPropertyAuthoringService properties;
    const auto schema = properties.Describe(registry, scene, {label.id, c->id}, {});
    assert(schema.ok);
    const std::array changes{EDITOR::ComponentPropertyMutation{{label.id, c->id}, "text", "Line 1\nLine 2"},
                             EDITOR::ComponentPropertyMutation{{label.id, c->id}, "outlineWidth", 3.0f},
                             EDITOR::ComponentPropertyMutation{{label.id, c->id}, "glowIntensity", 2.0f}};
    assert(properties.Apply(registry, scene, changes, {}).ok);
    std::string parentError;
    assert(!EDITOR::ReparentUiElement(scene, hud.id, textId.id, parentError));
    assert(EDITOR::ReparentUiElement(scene, textId.id, scene.objects.front().id, parentError));
    PrefabDocument prefab;
    std::string error;
    assert(CapturePrefabDocument(scene, hud.id, "HUD", prefab, error));
    assert(prefab.descendants.size() == 2);
    SceneObjectId root;
    uint64_t nextId = 1000;
    assert(InstantiatePrefabDocument(prefab, "HUD", scene, nextId, root, error));
    assert(root != hud.id);
    assert(EDITOR::AttachUiPrefabToCanvas(scene, registry, root, {}, error));
    SceneDocument empty;
    assert(InstantiatePrefabDocument(prefab, "HUD", empty, nextId, root, error));
    assert(EDITOR::AttachUiPrefabToCanvas(empty, registry, root, {}, error));
    assert(empty.objects.size() == 4);
    if (!output.empty()) {
        std::filesystem::create_directories(output);
        SceneDocument demo;
        demo.sceneName = "UI Studio";
        demo.systems = {{"UiSceneSystem", true, 70}, {"ModelRenderSystem", true, 100}};
        assert(EDITOR::CreateUiTemplate(demo, registry, "hud", {}, "Fonts/RobotoMono.ttf").ok);
        assert(EDITOR::CreateUiTemplate(demo, registry, "menu", {}, "Fonts/RobotoMono.ttf").ok);
        assert(EDITOR::CreateUiTemplate(demo, registry, "scroll", {}, "Fonts/RobotoMono.ttf").ok);
        assert(EDITOR::CreateUiTemplate(demo, registry, "text_field", {}, "Fonts/RobotoMono.ttf").ok);
        SceneSerializer serializer;
        assert(serializer.SaveToFile(TEXT::PathToUtf8(output / "UiStudio.hscene"), demo));
        demo.objects.clear();
        demo.sceneName = "Display Settings";
        assert(EDITOR::CreateUiTemplate(demo, registry, "settings", {}, "Fonts/RobotoMono.ttf").ok);
        assert(serializer.SaveToFile(TEXT::PathToUtf8(output / "Settings.hscene"), demo));
        demo.objects.clear();
        demo.sceneName = "Text Studio";
        assert(EDITOR::CreateUiTemplate(demo, registry, "title", {}, "Fonts/RobotoMono.ttf", "Typography").ok);
        assert(EDITOR::CreateUiTemplate(demo, registry, "caption", {}, "Fonts/RobotoMono.ttf",
                                        "Edit text, outline, shadow and glow in Text Design.")
                   .ok);
        const auto spatialText =
            EDITOR::CreateUiTemplate(demo, registry, "world_text", {}, "Fonts/RobotoMono.ttf", "3D Text");
        assert(spatialText.ok);
        demo.objects.back().transform.position = {0, 2, 0};
        assert(serializer.SaveToFile(TEXT::PathToUtf8(output / "TextStudio.hscene"), demo));
        SceneDocument loaded;
        assert(serializer.LoadFromFile(TEXT::PathToUtf8(output / "TextStudio.hscene"), loaded));
        assert(loaded.objects.size() == demo.objects.size());
    }
}
class WorkspaceScene final : public DocumentSceneBase {
public:
    WorkspaceScene() : DocumentSceneBase("ui-contracts") {}
    const char* GetSceneName() const override { return "UI contracts"; }
};
void UiWorkspaceEditing() {
    WorkspaceScene scene;
    auto& world = scene.GetWorld();
    auto* canvas = world.CreateObject("Canvas");
    assert(canvas && canvas->SetDocumentId({1}));
    canvas->AddComponent<UiCanvasComponent>();
    for (uint64_t i = 2; i <= 4; ++i) {
        auto* object = world.CreateObject("Element");
        assert(object && object->SetDocumentId({i}));
        assert(world.SetParent(object->GetRuntimeHandle(), canvas->GetRuntimeHandle()) == WorldMutationResult::Success);
        const float left = i == 2 ? 0.f : i == 3 ? 30.f : 100.f;
        auto* rect = object->AddComponent<UiRectComponent>();
        rect->Deserialize({{"anchorMinimum", {0, 0}},
                           {"anchorMaximum", {0, 0}},
                           {"offsetMinimum", {left, 0}},
                           {"offsetMaximum", {left + 10, 10}}});
        SceneObjectData source;
        source.id = {i};
        source.parent = SceneObjectId{1};
        SceneComponentData component;
        component.id = {i};
        component.type = "UiRectComponent";
        rect->Serialize(component.properties);
        source.components.push_back(component);
        scene.GetSceneDocument().objects.push_back(source);
    }
    std::string error;
    nlohmann::json commands;
    const std::array ids{SceneObjectId{2}, SceneObjectId{3}, SceneObjectId{4}};
    const auto before = scene.GetSceneDocument().objects[1].components.front().properties;
    assert(EDITOR::BuildUiArrangement(scene.GetSceneDocument(), world, ids, "horizontal", commands, error));
    assert(commands.size() == 2);
    assert(commands[0]["objectId"] == 3 && commands[0]["value"][0] == 50.f);
    assert(scene.GetSceneDocument().objects[1].components.front().properties == before);
    scene.GetSceneDocument().objects[1].editorLocked = true;
    assert(!EDITOR::BuildUiArrangement(scene.GetSceneDocument(), world, ids, "left", commands, error));
    scene.GetSceneDocument().objects[1].editorLocked = false;

    EDITOR::UiAnimationEditor motion;
    EDITOR::EditorOperationRegistry operations;
    EDITOR::RegisterUiAnimationOperations(operations, motion, [&] { return &scene; });
    const auto asset = MakeUiAnimationPreset("panel");
    assert(motion.Document().New(asset, error));
    assert(motion.Preview(scene, {2}, "Open", .15f, false));
    const auto original = WriteUiAnimationAsset(motion.Document().Profile());
    const auto base = scene.GetSceneDocument().objects.front().components.front().properties;
    assert(motion.BeginPoseDrag(scene, {2}));
    motion.UpdatePoseDrag(scene, 30, 20, 1, 1);
    assert(WriteUiAnimationAsset(motion.Document().Profile()) == original);
    motion.FinishPoseDrag(scene, nullptr);
    assert(WriteUiAnimationAsset(motion.Document().Profile()) == original);
    assert(motion.BeginPoseDrag(scene, {2}));
    motion.UpdatePoseDrag(scene, 30, 20, 1, 1);
    motion.FinishPoseDrag(scene, &operations);
    assert(WriteUiAnimationAsset(motion.Document().Profile()) != original);
    assert(scene.GetSceneDocument().objects.front().components.front().properties == base);
    assert(motion.TravelHistory(false));
    assert(WriteUiAnimationAsset(motion.Document().Profile()) == original);
    assert(motion.TravelHistory(true));
    assert(WriteUiAnimationAsset(motion.Document().Profile()) != original);

    TextStyleAsset local, draft;
    local.pixelHeight = 20;
    draft.pixelHeight = 44;
    TextStyleBinding binding;
    assert(ResolveTextStyle(nullptr, binding, local, &draft).pixelHeight == 44);
    binding.overrideSize = true;
    assert(ResolveTextStyle(nullptr, binding, local, &draft).pixelHeight == 20);
    assert(local.pixelHeight == 20 && draft.pixelHeight == 44);

    BINDING::Catalog catalog;
    BINDING::VariableDefinition visible;
    visible.id = "ui.visible";
    visible.name = "Visible";
    visible.defaultValue.type = BINDING::ValueType::Bool;
    catalog.variables.push_back(visible);
    BINDING::RuntimeBindingStore live, preview;
    assert(live.Initialize(catalog) && preview.Initialize(catalog));
    world.Services().Register(live);
    auto* object = world.FindObject(SceneObjectId{2});
    object->AddComponent<UiDataBindingComponent>()->Deserialize({{"visibleVariable", visible.id}});
    auto value = visible.defaultValue;
    value.boolean = true;
    assert(preview.WriteVariable(visible.id, value, "preview"));
    UiLayoutResolver layout;
    layout.SetAuthoringPreview(true);
    layout.SetPreviewBindings(&preview);
    assert(layout.Resolve(world, *object).valid);
    layout.SetPreviewBindings(nullptr);
    assert(!layout.Resolve(world, *object).valid);
    assert(live.ReadVariable(visible.id, value) && !value.boolean);
    world.Services().Remove<BINDING::RuntimeBindingStore>(&live);
}

void SceneOperationHistory() {
    WorkspaceScene scene;
    EditorContext editor;
    SelectionSyncService selection;
    EDITOR::SceneObjectCommandService commands;
    EDITOR::SceneObjectOperationContext context{scene, editor, selection, commands};
    const auto created =
        EDITOR::CreateSceneEmptyObject(context, {{"name", "History probe"}, {"position", {3.0f, 0.0f, 0.0f}}});
    assert(created.ok && context.history);
    assert(scene.GetSceneDocument().objects.size() == 1);
    auto edit = std::move(*context.history);
    EDITOR::EditorDocumentHistory history;
    history.Reset(1);
    history.RecordApplied(EDITOR::MakeSceneObjectsHistoryCommand(edit.label, edit.beforeObjects, edit.afterObjects,
                                                                 edit.beforeCamera, edit.afterCamera));
    assert(history.IsDirty() && history.CanUndo());
    // 履歴は変わったオブジェクトだけを持ち、撤销・重做で runtime へ渡す差分を返す。
    const auto undone = history.Undo(scene.GetSceneDocument());
    assert(undone.changed && undone.objectChanges && undone.objectChanges->removed.size() == 1u &&
           undone.objectChanges->added.empty() && undone.objectChanges->changed.empty());
    assert(scene.GetSceneDocument().objects.empty() && !history.IsDirty());
    const auto redone = history.Redo(scene.GetSceneDocument());
    assert(redone.changed && redone.objectChanges && redone.objectChanges->added.size() == 1u);
    assert(scene.GetSceneDocument().objects.size() == 1);
    assert(scene.GetSceneDocument().objects.front() == edit.afterObjects.front());
    context.history.reset();
    const auto moved =
        EDITOR::SetSceneObjectTransform(context, {{"name", "History probe"}, {"position", {6.0f, 0.0f, 0.0f}}});
    assert(moved.ok && context.history);
    context.history.reset();
    const auto unchanged =
        EDITOR::SetSceneObjectTransform(context, {{"name", "History probe"}, {"position", {6.0f, 0.0f, 0.0f}}});
    assert(unchanged.ok && unchanged.code == "NO_CHANGE" && !context.history);
    const auto invalid =
        EDITOR::SetSceneObjectTransform(context, {{"name", "History probe"}, {"scale", {0.0f, 1.0f, 1.0f}}});
    assert(!invalid.ok && !context.history);

    // 追加・削除は差分反映。runtime だけにある位置が残れば World は作り直されていない。
    const auto runtimeMatchesDocument = [&scene] {
        const auto& objects = scene.GetSceneDocument().objects;
        if (scene.GetWorld().GetTotalObjectCount() != objects.size()) {
            return false;
        }
        return std::all_of(objects.begin(), objects.end(), [&scene](const SceneObjectData& object) {
            const GameObject* runtime = scene.GetWorld().FindObject(object.id);
            const GameObject* parent = runtime != nullptr ? runtime->GetParent() : nullptr;
            return runtime != nullptr && runtime->GetName() == object.name &&
                   (object.parent ? parent != nullptr && parent->GetDocumentId() == *object.parent : parent == nullptr);
        });
    };
    const SceneObjectId probeId = scene.GetSceneDocument().objects.front().id;
    assert(runtimeMatchesDocument() && scene.GetWorld().FindObject(probeId)->SetLocalPosition({100.0f, 0.0f, 0.0f}));
    const auto keepsRuntimeOnlyState = [&scene, probeId] {
        const GameObject* probe = scene.GetWorld().FindObject(probeId);
        return probe != nullptr && probe->GetTransform().position.x == 100.0f;
    };
    // 対象を指定した複製は人の選択を変えず、複製した物の id を返す。
    const auto duplicated = EDITOR::DuplicateSceneObjects(context, {{"name", "History probe"}});
    assert(duplicated.ok && context.history && duplicated.data.contains("objectId"));
    assert(editor.selection.GetSelectedObjectCount() == 0u);
    assert(scene.GetSceneDocument().objects.size() == 2);
    assert(runtimeMatchesDocument() && keepsRuntimeOnlyState());
    context.history.reset();
    assert(EDITOR::DeleteSceneObjects(context, {{"objectId", duplicated.data["objectId"]}}).ok && context.history);
    assert(scene.GetSceneDocument().objects.size() == 1);
    assert(runtimeMatchesDocument() && keepsRuntimeOnlyState());

    // 変更されたオブジェクトは作り直し、作り直した親の下の既存の子は付け直す。
    context.history.reset();
    assert(EDITOR::CreateSceneEmptyObject(context, {{"name", "Child probe"}}).ok);
    const SceneObjectId childId = scene.GetSceneDocument().objects.back().id;
    FindSceneObjectData(scene.GetSceneDocument(), childId)->parent = probeId;
    assert(scene.ApplyRuntimeObjectChanges({.changed = {childId}}) && runtimeMatchesDocument());
    FindSceneObjectData(scene.GetSceneDocument(), probeId)->name = "Renamed probe";
    const SceneObjectChanges renamed = DiffSceneObjects(edit.afterObjects, scene.GetSceneDocument().objects);
    assert(renamed.changed.size() == 1u && renamed.changed.front() == probeId && renamed.added.size() == 1u);
    assert(scene.ApplyRuntimeObjectChanges({.changed = {probeId}}) && runtimeMatchesDocument());
    assert(!keepsRuntimeOnlyState());

    // 一組（智能体の一括操作・一回の依頼）は複数のコマンドを一回で撤销・重做し、差分をまとめて返す。
    EDITOR::EditorDocumentHistory grouped;
    grouped.Reset(2);
    const size_t objectsBefore = scene.GetSceneDocument().objects.size();
    const auto record = [&](const char* name) {
        context.history.reset();
        assert(EDITOR::CreateSceneEmptyObject(context, {{"name", name}}).ok && context.history);
        auto applied = std::move(*context.history);
        grouped.RecordApplied(EDITOR::MakeSceneObjectsHistoryCommand(
            applied.label, applied.beforeObjects, applied.afterObjects, applied.beforeCamera, applied.afterCamera));
    };
    record("Outside group");
    grouped.BeginUndoGroup();
    record("Group A");
    record("Group B");
    grouped.EndUndoGroup();
    const auto groupUndone = grouped.Undo(scene.GetSceneDocument());
    assert(groupUndone.changed && groupUndone.objectChanges && groupUndone.objectChanges->removed.size() == 2u);
    assert(scene.GetSceneDocument().objects.size() == objectsBefore + 1u && grouped.CanUndo());
    const auto groupRedone = grouped.Redo(scene.GetSceneDocument());
    assert(groupRedone.changed && groupRedone.objectChanges && groupRedone.objectChanges->added.size() == 2u);
    assert(scene.GetSceneDocument().objects.size() == objectsBefore + 3u && !grouped.CanRedo());
    context.history.reset();
}

void WorkspaceLayouts() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    auto scene = std::make_unique<WorkspaceScene>();
    EditorContext editor;
    SelectionSyncService selection;
    EDITOR::SceneObjectCommandService objects;
    EDITOR::EditorCommandRouter commands;
    EDITOR::EditorOperationRegistry operations;
    auto* canvas = Object(scene->GetWorld(), 1);
    canvas->AddComponent<UiCanvasComponent>();
    for (bool text : {false, true}) {
        EDITOR::UiWorkspaceController controller;
        EDITOR::TextStyleWorkspaceController styles;
        std::string error;
        assert(styles.Document().New("Headless style", error));
        assert(controller.Animation().Document().New(MakeUiAnimationPreset("panel"), error));
        assert(controller.Animation().Preview(*scene, {1}, "Open", .1f, false));
        for (auto language : {TEXT::Language::English, TEXT::Language::Chinese, TEXT::Language::Japanese}) {
            TEXT::SetLocalizationLanguage(language);
            for (auto size : {ImVec2{1280, 720}, ImVec2{2048, 1221}}) {
                io.DisplaySize = size;
                for (int frame = 0; frame < 3; ++frame) {
                    ImGui::NewFrame();
                    if (text) {
                        styles.DrawDockSpace(frame == 0);
                        styles.Draw(*scene, operations, commands);
                    } else {
                        controller.DrawDockSpace(frame == 0);
                        controller.Draw(*scene, editor, selection, objects, commands, operations);
                    }
                    ImGui::Render();
                    assert(ImGui::GetDrawData()->TotalVtxCount > 0);
                }
                for (const char* panel : {"Library", "Canvas", "Inspector", "Animation"}) {
                    if (text && std::string_view(panel) == "Animation")
                        continue;
                    const auto name = std::string("###") + (text ? "TextStyle/" : "UiWorkspace/") +
                                      (text && std::string_view(panel) == "Canvas" ? "Preview" : panel);
                    const auto* window = ImGui::FindWindowByName(name.c_str());
                    assert(window && window->DockNode);
                    assert(window->Size.x > 100 && window->Size.y > 100 && window->Pos.x >= -1 && window->Pos.y >= -1);
                    assert(window->Pos.x + window->Size.x <= size.x + 1 &&
                           window->Pos.y + window->Size.y <= size.y + 1);
                }
                assert(ImGui::GetCurrentContext()->DisabledStackSize == 0);
            }
        }
        controller.Deactivate(*scene);
    }
    TEXT::SetLocalizationLanguage(TEXT::Language::English);
    ImGui::DestroyContext();
}
} // namespace
int main(int argc, char** argv) {
    try {
        SnapshotHistory();
        std::cerr << "Text asset contracts\n";
        AssetSaveRetry(TEXT::PathFromUtf8(argv[0]));
        TextAssets(TEXT::PathFromUtf8(argv[0]));
        std::cerr << "Game pointer contracts\n";
        GameViewportPointer();
        std::cerr << "UI text and interaction contracts\n";
        TextFieldInputContracts();
        UiInteractionContracts();
        DisplayOptions();
        DisplayPanelTransaction();
        std::cerr << "UI layout contracts\n";
        LayoutAndPresentation();
        ActionsAndStyles();
        Templates(argc == 2 ? TEXT::PathFromUtf8(argv[1]) : std::filesystem::path{});
        std::cerr << "Workspace layout contracts\n";
        SceneOperationHistory();
        UiWorkspaceEditing();
        WorkspaceLayouts();
        std::cout << "UI and text asset contracts passed.\n";
    } catch (const std::exception& error) {
        std::cerr << "UI contract exception: " << error.what() << "\n";
        return 1;
    }
}
