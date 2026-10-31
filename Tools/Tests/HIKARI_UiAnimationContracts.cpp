#include "Scene/Ui/HIKARI_UiDataBindingComponent.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Assets/Ui/HIKARI_UiAnimationAsset.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Editor/Documents/HIKARI_UiAnimationEditorDocument.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Ui/HIKARI_UiAnimationComponents.h"
#include "Scene/Ui/HIKARI_UiAnimationPlayer.h"
#include "Scene/Ui/HIKARI_UiLayoutResolver.h"
#include "Scene/Ui/HIKARI_UiBindingSourceComponent.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "Scene/Ui/HIKARI_UiUtilityComponents.h"
#include "Scene/Ui/HIKARI_UiSceneSystem.h"
#include "HIKARI/Project/FrameContext.h"
#include <cmath>
#include <filesystem>
#include <iostream>
#include "Tools/Tests/Internal/HIKARI_ContractAssert.h"

using namespace HIKARI;
namespace {
bool Near(float a, float b) { return std::abs(a - b) < .001f; }
GameObject* Object(World& world, uint64_t id, GameObject* parent = nullptr) {
    auto* o = world.CreateObject("UI");
    assert(o && o->SetDocumentId({id}));
    if (parent)
        world.SetParent(o->GetRuntimeHandle(), parent->GetRuntimeHandle());
    return o;
}
void AssetContracts(const std::filesystem::path& root) {
    std::string error;
    auto asset = MakeUiAnimationPreset("panel");
    UiAnimationAsset decoded;
    assert(ReadUiAnimationAsset(WriteUiAnimationAsset(asset), decoded, error));
    auto invalid = asset;
    invalid.clips.push_back(invalid.clips.front());
    assert(!ReadUiAnimationAsset(WriteUiAnimationAsset(invalid), decoded, error));
    invalid = asset;
    invalid.clips.back().loop = true;
    assert(!ReadUiAnimationAsset(WriteUiAnimationAsset(invalid), decoded, error));
    invalid = asset;
    invalid.clips.front().tracks.front().property = UiMotionProperty::Count;
    assert(!ReadUiAnimationAsset(WriteUiAnimationAsset(invalid), decoded, error));
    invalid = asset;
    invalid.clips.front().tracks.front().curve.keys.front().time = -1;
    assert(!ReadUiAnimationAsset(WriteUiAnimationAsset(invalid), decoded, error));

    std::filesystem::create_directories(root / "Assets");
    AssetDatabase database;
    assert(database.Initialize(root));
    EDITOR::UiAnimationEditorDocument document;
    assert(document.New(asset, error));
    assert(document.IsDirty());
    assert(document.Save(database, error));
    const auto guid = document.Guid();
    auto edit = asset;
    edit.clips.front().tracks.front().curve.keys.back().value = .8f;
    assert(document.Apply(edit, error, 7));
    assert(document.CanUndo() && document.IsDirty());
    assert(document.Undo() && !document.IsDirty());
    assert(document.Redo());
    assert(!document.New(asset, error));
    assert(document.Save(database, error));
    EDITOR::UiAnimationEditorDocument reopened;
    assert(reopened.Open(database, guid, error));
    assert(WriteUiAnimationAsset(reopened.Profile()) == WriteUiAnimationAsset(edit));
    auto external = edit;
    external.name = "External change";
    assert(SaveUiAnimationAsset(database.GetProjectRoot() / database.FindByGuid(guid)->sourcePath, external, error));
    assert(reopened.Open(database, guid, error) && reopened.Profile().name == external.name);
    assert(reopened.Apply(edit, error));
    assert(reopened.Open(database, guid, error) && reopened.IsDirty() && reopened.Profile().name == edit.name);
    assert(reopened.Save(database, error));
    UiAnimationAssetStore store;
    store.Configure(&database);
    assert(store.Load(guid, error));
    SceneDocument scene;
    SceneObjectData object;
    object.id = {10};
    SceneComponentData component;
    component.type = "UiAnimatorComponent";
    component.properties = {{"animationAssetGuid", guid.value}};
    object.components.push_back(component);
    scene.objects.push_back(object);
    const auto usage = AnalyzeAssetUsage(scene, database);
    assert(usage.IsUsed(guid) && usage.missingReferences.empty());
    World world;
    world.Services().Register(store);
    auto* canvas = Object(world, 1);
    canvas->AddComponent<UiCanvasComponent>();
    auto* rootObject = Object(world, 2, canvas);
    rootObject->AddComponent<UiRectComponent>();
    auto* group = rootObject->AddComponent<UiGroupComponent>();
    group->SetVisible(false);
    rootObject->AddComponent<UiAnimatorComponent>()->Deserialize({{"animationAssetGuid", guid.value}});
    UiAnimationPlayer player;
    FrameContext frame;
    player.Tick(world, frame);
    assert(player.SetVisible(world, *rootObject, true) && group->IsVisible());
    frame.unscaledDt = .15f;
    frame.gameDt = 0;
    frame.paused = true;
    player.Tick(world, frame);
    assert(Near(player.GetStatus(rootObject->GetRuntimeHandle()).seconds, .15f));
    assert(player.SetVisible(world, *rootObject, false));
    assert(player.IsClosing(rootObject->GetRuntimeHandle()));
    UiLayoutResolver layout(&player);
    assert(!layout.Resolve(world, *rootObject).interactable && group->IsVisible());
    frame.unscaledDt = .21f;
    player.Tick(world, frame);
    assert(!group->IsVisible() && !player.IsClosing(rootObject->GetRuntimeHandle()));
    assert(player.SetVisible(world, *rootObject, true));
    assert(group->IsVisible() && player.GetStatus(rootObject->GetRuntimeHandle()).clip == "Open");
    world.Services().Remove<UiAnimationAssetStore>(&store);
    std::cout << "asset validation, save/reopen, history, import passed\n";
}
void PoseContracts() {
    World world;
    auto* canvas = Object(world, 1);
    canvas->AddComponent<UiCanvasComponent>();
    auto* root = Object(world, 2, canvas);
    auto* rect = root->AddComponent<UiRectComponent>();
    rect->Deserialize({{"anchorMinimum", {0, 0}},
                       {"anchorMaximum", {0, 0}},
                       {"offsetMinimum", {100, 100}},
                       {"offsetMaximum", {300, 300}},
                       {"rotationDegrees", 30}});
    root->AddComponent<UiAnimatorComponent>();
    root->AddComponent<UiGroupComponent>();
    auto* child = Object(world, 3, root);
    child->AddComponent<UiAnimationTargetComponent>()->Deserialize({{"slot", "caption"}});
    auto* childRect = child->AddComponent<UiRectComponent>();
    childRect->Deserialize({{"anchorMinimum", {0, 0}},
                            {"anchorMaximum", {0, 0}},
                            {"offsetMinimum", {25, 30}},
                            {"offsetMaximum", {105, 60}},
                            {"rotationDegrees", 45}});
    auto asset = std::make_shared<UiAnimationAsset>();
    UiAnimationClip clip;
    clip.name = "Move";
    clip.duration = 2;
    clip.tracks = {{1, "self", UiMotionProperty::ScaleX, {2}},
                   {2, "self", UiMotionProperty::ScaleY, {.5f}},
                   {3, "self", UiMotionProperty::OffsetX, {0, {{0, 0, 50, 50}, {2, 100, 50, 50}}}},
                   {4, "caption", UiMotionProperty::Opacity, {.4f}}};
    asset->clips.push_back(clip);
    UiAnimationPlayer player;
    nlohmann::json before;
    rect->Serialize(before);
    assert(player.Play(world, *root, asset, "Move"));
    assert(player.Seek(world, root->GetRuntimeHandle(), 1));
    const auto pose = player.Poses().at(root->GetRuntimeHandle().ToValue());
    assert(Near(pose.Get(UiMotionProperty::OffsetX), 50));
    UiLayoutResolver layout(&player);
    const auto r = layout.Resolve(world, *child);
    const auto parentTransform = Affine2D::Around(200, 200, 30 * .01745329252f, 2, .5f, 50, 0);
    const auto ownTransform = Affine2D::Around(165, 145, 45 * .01745329252f);
    float px = 165, py = 145;
    (parentTransform * ownTransform).Transform(px, py);
    assert(r.valid && ContainsUiPoint(r, px, py));
    assert(!ContainsUiPoint(r, px + 500, py + 500) && Near(r.opacity, .4f));
    float cornerX = 125, cornerY = 130;
    r.transform.Transform(cornerX, cornerY);
    float expectedX = 125, expectedY = 130;
    (parentTransform * ownTransform).Transform(expectedX, expectedY);
    assert(Near(cornerX, expectedX) && Near(cornerY, expectedY));
    assert(player.Pause(root->GetRuntimeHandle(), true));
    FrameContext frame;
    frame.unscaledDt = frame.gameDt = .25f;
    player.Tick(world, frame, false);
    assert(Near(player.GetStatus(root->GetRuntimeHandle()).seconds, 1));
    assert(player.Pause(root->GetRuntimeHandle(), false));
    player.Tick(world, frame, false);
    assert(Near(player.GetStatus(root->GetRuntimeHandle()).seconds, 1.25f));
    nlohmann::json after;
    rect->Serialize(after);
    assert(before == after);
    auto* duplicate = Object(world, 4, root);
    duplicate->AddComponent<UiAnimationTargetComponent>()->Deserialize({{"slot", "caption"}});
    assert(!player.Play(world, *root, asset, "Move"));
    assert(player.GetStatus(root->GetRuntimeHandle()).seconds == 1.25f);
    player.Refresh(world);
    assert(!player.GetStatus(root->GetRuntimeHandle()).error.empty());
    const auto handle = root->GetRuntimeHandle();
    world.DestroyObject(root);
    player.Refresh(world);
    assert(player.GetStatus(handle).clip.empty());
    std::cout << "nested affine rendering/hit transform, seek, pause, isolation and invalid slots passed\n";
}
void DataBindingContracts() {
    BINDING::Catalog catalog;
    BINDING::VariableDefinition score;
    score.id = "Game.Score";
    score.name = "Score";
    score.defaultValue.type = BINDING::ValueType::Int;
    score.defaultValue.integer = 42;
    BINDING::VariableDefinition enabled;
    enabled.id = "Game.Enabled";
    enabled.name = "Enabled";
    enabled.defaultValue.type = BINDING::ValueType::Bool;
    enabled.defaultValue.boolean = false;
    catalog.variables = {score, enabled};
    BINDING::RuntimeBindingStore store;
    assert(store.Initialize(catalog));
    const RuntimeObjectHandle oldGeneration{7u, 1u};
    const RuntimeObjectHandle currentGeneration{7u, 2u};
    const RuntimeObjectHandle otherObject{8u, 1u};
    BINDING::Value scopedValue{};
    assert(store.ReadObjectVariable(oldGeneration, "Game.Score", scopedValue) && scopedValue.integer == 42);
    assert(store.ReadObjectVariable(otherObject, "Game.Score", scopedValue) && scopedValue.integer == 42);
    assert(!store.ReadObjectVariable({}, "Game.Score", scopedValue));

    BINDING::Value oldScore = score.defaultValue;
    oldScore.integer = 75;
    BINDING::Value currentScore = score.defaultValue;
    currentScore.integer = 25;
    BINDING::Value otherScore = score.defaultValue;
    otherScore.integer = 10;
    assert(store.WriteObjectVariable(oldGeneration, "Game.Score", oldScore, "old-writer"));
    assert(store.WriteObjectVariable(currentGeneration, "Game.Score", currentScore, "current-writer"));
    assert(store.WriteObjectVariable(otherObject, "Game.Score", otherScore, "other-writer"));
    assert(store.ReadObjectVariable(oldGeneration, "Game.Score", scopedValue) && scopedValue.integer == 75);
    assert(store.ReadObjectVariable(currentGeneration, "Game.Score", scopedValue) && scopedValue.integer == 25);
    assert(store.ReadObjectVariable(otherObject, "Game.Score", scopedValue) && scopedValue.integer == 10);
    assert(store.ReadVariable("Game.Score", scopedValue) && scopedValue.integer == 42);

    store.ReleaseObjectVariableWriters(oldGeneration, "old-writer");
    assert(store.ReadObjectVariable(oldGeneration, "Game.Score", scopedValue) && scopedValue.integer == 42);
    assert(store.ReadObjectVariable(currentGeneration, "Game.Score", scopedValue) && scopedValue.integer == 25);
    assert(store.ReleaseObjectVariableWriter(currentGeneration, "Game.Score", "current-writer"));
    assert(store.ReadObjectVariable(currentGeneration, "Game.Score", scopedValue) && scopedValue.integer == 42);

    World sourceWorld;
    auto* globalWidget = sourceWorld.CreateObject("Global Bound Widget");
    UiDataBindingComponent* binding =
        globalWidget != nullptr ? globalWidget->AddComponent<UiDataBindingComponent>() : nullptr;
    assert(binding != nullptr);
    binding->Deserialize({{"textVariable", "Game.Score"},
                          {"prefix", "Score: "},
                          {"suffix", " pts"},
                          {"visibleVariable", "Game.Enabled"},
                          {"interactableVariable", "Game.Enabled"}});
    assert(binding->HasTextBinding());
    UiDataBindingComponent disabledBinding;
    disabledBinding.Deserialize({{"enabled", false}, {"textVariable", "Game.Score"}});
    assert(!disabledBinding.HasTextBinding());
    assert(binding->ResolveText(&store, "fallback") == "Score: 42 pts");
    assert(!binding->IsVisible(&store) && !binding->IsInteractable(&store));
    enabled.defaultValue.boolean = true;
    assert(store.WriteVariable("Game.Enabled", enabled.defaultValue, "test"));
    assert(binding->IsVisible(&store) && binding->IsInteractable(&store));
    assert(binding->ResolveText(nullptr, "fallback") == "fallback");

    auto* firstSourceOwner = Object(sourceWorld, 100u);
    auto* secondSourceOwner = Object(sourceWorld, 101u);
    assert(firstSourceOwner != nullptr && secondSourceOwner != nullptr);
    UiBindingSourceComponent* firstSource = firstSourceOwner->AddComponent<UiBindingSourceComponent>();
    UiBindingSourceComponent* secondSource = secondSourceOwner->AddComponent<UiBindingSourceComponent>();
    assert(firstSource != nullptr && secondSource != nullptr);
    firstSource->SetTargetObjectId(SceneObjectId{100u});
    secondSource->SetTargetObjectId(SceneObjectId{101u});
    auto* firstBoundObject = Object(sourceWorld, 102u, firstSourceOwner);
    auto* secondBoundObject = Object(sourceWorld, 103u, secondSourceOwner);
    UiDataBindingComponent* firstScopedBinding = firstBoundObject->AddComponent<UiDataBindingComponent>();
    UiDataBindingComponent* secondScopedBinding = secondBoundObject->AddComponent<UiDataBindingComponent>();
    assert(firstScopedBinding != nullptr && secondScopedBinding != nullptr);
    firstScopedBinding->Deserialize({{"textVariable", "Game.Score"}});
    secondScopedBinding->Deserialize({{"textVariable", "Game.Score"}});
    assert(firstScopedBinding->ResolveText(&store, "fallback") == "42");
    assert(secondScopedBinding->ResolveText(&store, "fallback") == "42");
    BINDING::Value firstObjectScore = score.defaultValue;
    firstObjectScore.integer = 17;
    BINDING::Value secondObjectScore = score.defaultValue;
    secondObjectScore.integer = 18;
    assert(store.WriteObjectVariable(firstSourceOwner->GetRuntimeHandle(), "Game.Score", firstObjectScore,
                                     "first-ui-owner"));
    assert(store.WriteObjectVariable(secondSourceOwner->GetRuntimeHandle(), "Game.Score", secondObjectScore,
                                     "second-ui-owner"));
    assert(firstScopedBinding->ResolveText(&store, "fallback") == "17");
    assert(secondScopedBinding->ResolveText(&store, "fallback") == "18");
    assert(store.ReleaseObjectVariableWriter(firstSourceOwner->GetRuntimeHandle(), "Game.Score", "first-ui-owner"));
    assert(firstScopedBinding->ResolveText(&store, "fallback") == "42");
    assert(secondScopedBinding->ResolveText(&store, "fallback") == "18");

    auto* sourceOwner = sourceWorld.CreateObject("Binding Source Without Target");
    auto* boundObject = sourceWorld.CreateObject("Bound Widget");
    assert(sourceOwner && boundObject &&
           sourceWorld.SetParent(boundObject->GetRuntimeHandle(), sourceOwner->GetRuntimeHandle()) ==
               HIKARI::WorldMutationResult::Success);
    sourceOwner->AddComponent<UiBindingSourceComponent>();
    UiDataBindingComponent* scopedBinding = boundObject->AddComponent<UiDataBindingComponent>();
    assert(scopedBinding != nullptr);
    scopedBinding->Deserialize({{"textVariable", "Game.Score"},
                                {"visibleVariable", "Game.Enabled"},
                                {"interactableVariable", "Game.Enabled"}});
    BINDING::Value globalScore = score.defaultValue;
    globalScore.integer = 99;
    assert(store.WriteVariable("Game.Score", globalScore, "global-writer"));
    BINDING::Value previewValue{};
    previewValue.type = BINDING::ValueType::Int;
    previewValue.integer = 123;
    assert(ReadUiBoundValue(*scopedBinding, &store, "Game.Score", previewValue) ==
           UiBoundValueStatus::InvalidObjectSource);
    assert(ReadUiBoundValue(*scopedBinding, &store, "Game.Score", previewValue, UiBindingReadMode::AuthoringPreview) ==
           UiBoundValueStatus::Unbound);
    assert(previewValue.type == BINDING::ValueType::Int && previewValue.integer == 123);
    assert(scopedBinding->ResolveText(&store, "runtime fallback").empty());
    assert(scopedBinding->ResolveTextForPreview(&store, "design fallback") == "design fallback");
    assert(!scopedBinding->IsVisible(&store) && scopedBinding->IsVisibleForPreview(&store));
    assert(!scopedBinding->IsInteractable(&store) && scopedBinding->IsInteractableForPreview(&store));

    catalog.variables.front().defaultValue.type = BINDING::ValueType::Float;
    catalog.variables.front().defaultValue.integer = 0;
    catalog.variables.front().defaultValue.numeric.x = 3.5f;
    assert(store.ReloadCatalog(catalog));
    assert(store.ReadObjectVariable(otherObject, "Game.Score", scopedValue));
    assert(scopedValue.type == BINDING::ValueType::Float && Near(scopedValue.numeric.x, 3.5f));
    std::cout << "UI variable projection passed\n";
}
void NestedTransitionContracts() {
    World world;
    auto* canvas = Object(world, 1);
    canvas->AddComponent<UiCanvasComponent>();
    auto* root = Object(world, 2, canvas);
    root->AddComponent<UiRectComponent>();
    auto* child = Object(world, 3, root);
    child->AddComponent<UiRectComponent>();
    child->AddComponent<UiAnimatorComponent>();
    child->AddComponent<UiAnimationTargetComponent>()->Deserialize({{"slot", "child"}});
    auto outer = std::make_shared<UiAnimationAsset>();
    UiAnimationClip dim;
    dim.name = "Dim";
    dim.tracks = {{1, "child", UiMotionProperty::Opacity, {.5f}}};
    outer->clips.push_back(dim);
    auto inner = std::make_shared<UiAnimationAsset>();
    UiAnimationClip feedback;
    feedback.name = "Feedback";
    feedback.tracks = {{1, "self", UiMotionProperty::Opacity, {1, {{0, 1, 0, 0}, {.3f, .4f, 0, 0}}}}};
    inner->clips.push_back(feedback);
    UiAnimationPlayer player;
    assert(player.Play(world, *root, outer, "Dim"));
    assert(player.Play(world, *child, inner, "Feedback"));
    assert(player.Seek(world, child->GetRuntimeHandle(), .15f));
    const auto before = player.Poses().at(child->GetRuntimeHandle().ToValue()).Get(UiMotionProperty::Opacity);
    assert(player.Play(world, *child, inner, "Feedback", true));
    assert(Near(before, player.Poses().at(child->GetRuntimeHandle().ToValue()).Get(UiMotionProperty::Opacity)));
    // Canvas の変形は子に継承し、余分な Rect で基準解像度を変えない。
    canvas->AddComponent<UiRectComponent>();
    auto whole = std::make_shared<UiAnimationAsset>();
    UiAnimationClip move;
    move.name = "Move";
    move.tracks = {{1, "self", UiMotionProperty::OffsetX, {100}}};
    whole->clips.push_back(move);
    assert(player.Play(world, *canvas, whole, "Move"));
    UiLayoutResolver layout(&player);
    const auto resolved = layout.Resolve(world, *canvas);
    assert(resolved.valid && resolved.width == resolved.referenceWidth && Near(resolved.transform.tx, 100));
    std::cout << "nested transition isolation and canvas animation passed\n";
}
void ButtonFeedbackContracts() {
    World world;
    auto* canvas = Object(world, 1);
    canvas->AddComponent<UiCanvasComponent>();
    auto* object = Object(world, 2, canvas);
    object->AddComponent<UiRectComponent>();
    auto* button = object->AddComponent<UiButtonComponent>();
    button->Deserialize({{"transitionSeconds", .1f}, {"normalTint", {1, 1, 1, 1}}, {"hoverTint", {0, 0, 0, 1}}});
    UiAnimationPlayer player;
    FrameContext frame;
    frame.unscaledDt = .1f;
    player.Tick(world, frame);
    button->SetVisualState(UiButtonVisualState::Hovered);
    frame.unscaledDt = .05f;
    player.Tick(world, frame);
    assert(Near(player.Poses().at(object->GetRuntimeHandle().ToValue()).Get(UiMotionProperty::Red), .5f));
    button->SetVisualState(UiButtonVisualState::Normal);
    frame.unscaledDt = 0;
    player.Tick(world, frame);
    assert(Near(player.Poses().at(object->GetRuntimeHandle().ToValue()).Get(UiMotionProperty::Red), .5f));
    frame.unscaledDt = .1f;
    player.Tick(world, frame);
    assert(Near(player.Poses().at(object->GetRuntimeHandle().ToValue()).Get(UiMotionProperty::Red), 1));
    std::cout << "shared button feedback and interrupted transition continuity passed\n";
}
} // namespace
int main(int, char** argv) {
    const auto root = std::filesystem::absolute(argv[0]).parent_path() / "ui-animation-contract";
    AssetContracts(root);
    PoseContracts();
    ButtonFeedbackContracts();
    DataBindingContracts();
    NestedTransitionContracts();
    std::cout << "UI animation contracts passed\n";
}
