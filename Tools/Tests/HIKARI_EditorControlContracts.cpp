#include "Editor/Authoring/HIKARI_ComponentAuthoringQueryService.h"
#include "Editor/Authoring/HIKARI_SceneObjectObservation.h"
#include "Editor/Tools/HIKARI_EditorToolHost.h"
#include "Editor/Tools/HIKARI_BuiltInEditorTools.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"
#include "Editor/Performance/HIKARI_PerformanceAuditPanel.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Render3D/Views/HIKARI_EditorViewCapture.h"
#include "Tools/Baking/HIKARI_LightingBakeService.h"
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Project/HIKARI_GameProjectEditorOperationRegistration.h"
#include "Editor/Project/HIKARI_GameProjectEditorDocumentService.h"
#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceHost.h"
#include "HIKARI/Project/EditorOperations.h"
#include "HIKARI/Project/FrameContext.h"
#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"
#include "Editor/Gizmos/HIKARI_EditorTransformGizmoShortcuts.h"
#include "Editor/Viewport/HIKARI_ViewportAuthoringToolbar.h"
#include "Editor/HIKARI_DocumentComponentAuthoringService.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#include "Editor/Views/Director/HIKARI_DirectorViewPanel.h"
#include "Editor/Views/HIKARI_EditorViewportGeometry.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Gameplay/Motion/HIKARI_MotionIntentService.h"
#include "Input/Platform/HIKARI_IInputBackend.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Physics/Backends/Jolt/HIKARI_JoltPhysicsBackend.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Physics/HIKARI_KinematicMotionService.h"
#include "Physics/HIKARI_PhysicsSystem.h"
#include "Physics/HIKARI_PhysicsRuntimeStatusService.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Components/Rendering/WorldSprite/HIKARI_WorldSpriteComponent.h"
#include "HIKARI/Project/ActionViews.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/Features/HIKARI_RuntimeFeatureContextAccess.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Scene/Components/HIKARI_CharacterInputComponent.h"
#include "Scene/Components/HIKARI_CharacterLocomotionComponent.h"
#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"
#include "Scene/HIKARI_CharacterLocomotionSystem.h"
#include "Scene/Features/HIKARI_BuiltInRuntimeFeatures.h"
#include "HIKARI/Project/RuntimeFeature.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_SystemScheduler.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/HIKARI_World.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"

using namespace HIKARI;
using namespace HIKARI::EDITOR;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)

namespace {
class ProjectOperationHandler final : public EDITOR_PROJECT::IEditorOperationHandler {
public:
    void Execute(std::string_view parametersJson, EDITOR_PROJECT::IEditorOperationResultSink& result) override {
        ++callCount;
        lastParameters.assign(parametersJson);
        result.Receive(true, "OK", "Validated.", R"({"valid":true})");
    }

    uint32_t callCount = 0u;
    std::string lastParameters{};
};

class ProjectDocumentTextSink final : public EDITOR_PROJECT::IEditorDocumentReadSink {
public:
    void Receive(const EDITOR_PROJECT::EditorDocumentResult& result, std::string_view text) override {
        ++receiveCount;
        observed = result;
        value.assign(text);
    }

    uint32_t receiveCount = 0u;
    EDITOR_PROJECT::EditorDocumentResult observed{};
    std::string value{};
};

void ProjectEditorDocumentContracts() {
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() / ("hikari-editor-documents-" + unique);
    const auto dataRoot = root / "Data";
    std::filesystem::create_directories(dataRoot);
    const auto cleanup = [&root]() {
        std::error_code ignored{};
        std::filesystem::remove_all(root, ignored);
    };

    try {
        GameProjectEditorDocumentService documents{dataRoot};
        const std::string initial = R"({"title":"Start","lives":3})";
        const std::string edited = R"({"title":"Forest","lives":5})";
        auto created = documents.Create("levels/main.json", initial);
        CHECK(created.Succeeded() && created.IsDirty() && !created.CanUndo() && documents.HasUnsavedDocuments());
        const auto duplicate = documents.Create("levels//main.json", "ignored");
        CHECK(duplicate.code == EDITOR_PROJECT::EditorDocumentCode::AlreadyExists &&
              duplicate.document == created.document);
        CHECK(documents.Open("../outside.json").code == EDITOR_PROJECT::EditorDocumentCode::InvalidPath);

        ProjectDocumentTextSink sink{};
        CHECK(documents.Read(created.document, created.revision, sink).Succeeded());
        CHECK(sink.receiveCount == 1u && sink.value == initial && sink.observed.revision == created.revision);
        const auto staleRead = documents.Read(created.document, created.revision + 1u, sink);
        CHECK(staleRead.code == EDITOR_PROJECT::EditorDocumentCode::RevisionConflict && sink.receiveCount == 1u);

        const auto applied = documents.Apply(created.document, created.revision, edited);
        CHECK(applied.Succeeded() && applied.revision == created.revision + 1u && applied.IsDirty() &&
              applied.CanUndo());
        const auto noOp = documents.Apply(created.document, applied.revision, edited);
        CHECK(noOp.code == EDITOR_PROJECT::EditorDocumentCode::NoChange && noOp.revision == applied.revision);
        CHECK(documents.Apply(created.document, created.revision, initial).code ==
              EDITOR_PROJECT::EditorDocumentCode::RevisionConflict);

        const auto undone = documents.Undo(created.document, applied.revision);
        CHECK(undone.Succeeded() && undone.CanRedo() && undone.IsDirty());
        CHECK(documents.Read(created.document, undone.revision, sink).Succeeded() && sink.value == initial);
        const auto redone = documents.Redo(created.document, undone.revision);
        CHECK(redone.Succeeded() && redone.CanUndo());
        CHECK(documents.Read(created.document, redone.revision, sink).Succeeded() && sink.value == edited);

        const auto saved = documents.Save(created.document, redone.revision);
        CHECK(saved.Succeeded() && !saved.IsDirty() && !documents.HasUnsavedDocuments());
        const auto path = dataRoot / "levels" / "main.json";
        std::ifstream savedFile(path, std::ios::binary);
        const std::string savedText{std::istreambuf_iterator<char>(savedFile), std::istreambuf_iterator<char>()};
        CHECK(savedText == edited);
        CHECK(documents.Close(created.document, false).Succeeded());
        const auto reopened = documents.Open("levels//main.json");
        CHECK(reopened.Succeeded() && reopened.document != created.document);
        CHECK(documents.Read(reopened.document, reopened.revision, sink).Succeeded() && sink.value == edited);

        const std::string nextDraft = R"({"title":"Cave","lives":2})";
        const auto dirty = documents.Apply(reopened.document, reopened.revision, nextDraft);
        CHECK(dirty.Succeeded() && dirty.IsDirty() && documents.HasUnsavedDocuments());
        {
            std::ofstream external(path, std::ios::binary | std::ios::trunc);
            external << R"({"title":"External","lives":1})";
        }
        const auto conflict = documents.Save(reopened.document, dirty.revision);
        CHECK(conflict.code == EDITOR_PROJECT::EditorDocumentCode::ExternalConflict && conflict.IsDirty());
        CHECK(documents.Close(reopened.document, false).code == EDITOR_PROJECT::EditorDocumentCode::Dirty);
        CHECK(documents.Read(reopened.document, conflict.revision, sink).Succeeded() && sink.value == nextDraft);
        CHECK(documents.Close(reopened.document, true).Succeeded());
        CHECK(documents.Read(reopened.document, conflict.revision, sink).code ==
              EDITOR_PROJECT::EditorDocumentCode::InvalidHandle);

        {
            std::ofstream invalid(dataRoot / "invalid.json", std::ios::binary | std::ios::trunc);
            invalid.put(static_cast<char>(0xC0));
        }
        CHECK(documents.Open("invalid.json").code == EDITOR_PROJECT::EditorDocumentCode::InvalidUtf8);
        {
            std::ofstream oversized(dataRoot / "oversized.json", std::ios::binary | std::ios::trunc);
            oversized << std::string(HIKARI::kMaximumProjectDataTextBytes + 1u, 'x');
        }
        CHECK(documents.Open("oversized.json").code == EDITOR_PROJECT::EditorDocumentCode::TooLarge);
    } catch (...) {
        cleanup();
        throw;
    }
    cleanup();
    std::cout << "Project Data document identity, revisions, history, save conflict and dirty close: PASS\n";
}

EDITOR_PROJECT::EditorOperationDescriptor MakeProjectOperation(ProjectOperationHandler& handler, const char* id) {
    EDITOR_PROJECT::EditorOperationDescriptor descriptor{};
    descriptor.id = id;
    descriptor.summary = "Validate a project level.";
    descriptor.workspace = "level";
    descriptor.kind = EDITOR_PROJECT::EditorOperationKind::Query;
    descriptor.permission = EDITOR_PROJECT::EditorOperationPermission::ReadEditorState;
    descriptor.exposureMask =
        EDITOR_PROJECT::EditorOperationExposureMask::Human | EDITOR_PROJECT::EditorOperationExposureMask::Agent;
    descriptor.handler = &handler;
    return descriptor;
}

void ProjectEditorOperationContracts() {
    EditorOperationRegistry registry{};
    bool targetExecutorCalled = false;
    registry.SetTargetExecutor([&targetExecutorCalled](const std::string&, const nlohmann::json&) {
        targetExecutorCalled = true;
        return OperationFailure("UNEXPECTED_SESSION_ROUTE", "Project operation was routed through a session.");
    });

    auto moduleHandler = std::make_unique<ProjectOperationHandler>();
    const auto owner = registry.CreateOwnerToken();
    EDITOR_PROJECT::EditorOperationParameterDescriptor parameter{};
    parameter.name = "value";
    parameter.type = EDITOR_PROJECT::EditorOperationParameterType::Number;
    parameter.required = 1u;
    parameter.schemaJson = R"({"type":"number","minimum":1})";
    auto operation = MakeProjectOperation(*moduleHandler, "project.level.validate");
    operation.parameters = &parameter;
    operation.parameterCount = 1u;

    GameProjectEditorOperationRegistration registrar{registry, owner, nullptr};
    CHECK(registrar.RegisterOperation(operation).ok);
    CHECK(registry.IsRegistered("project.level.validate"));
    CHECK(registry.Find("project.level.validate")->exposureMask ==
          (EditorOperationExposureMask::Human | EditorOperationExposureMask::Agent));
    const uint64_t registeredRevision = registry.Revision();

    const auto rejected = registry.Execute("project.level.validate", {{"value", 0}});
    CHECK(!rejected.ok && rejected.code == "PARAMETER_CONSTRAINT");
    CHECK(moduleHandler->callCount == 0u);
    const auto executed = registry.Execute("project.level.validate", {{"value", 2}});
    CHECK(executed.ok && executed.data.value("valid", false));
    CHECK(moduleHandler->callCount == 1u);
    CHECK(nlohmann::json::parse(moduleHandler->lastParameters).value("value", 0) == 2);
    CHECK(!targetExecutorCalled);

    auto invalidParameterOperation = MakeProjectOperation(*moduleHandler, "project.level.invalid_type");
    auto invalidParameter = parameter;
    invalidParameter.name = "value";
    invalidParameter.type = static_cast<EDITOR_PROJECT::EditorOperationParameterType>(99u);
    invalidParameterOperation.parameters = &invalidParameter;
    invalidParameterOperation.parameterCount = 1u;
    const auto invalidOwner = registry.CreateOwnerToken();
    GameProjectEditorOperationRegistration invalidRegistrar{registry, invalidOwner, nullptr};
    const auto invalidResult = invalidRegistrar.RegisterOperation(invalidParameterOperation);
    CHECK(!invalidResult.ok && invalidResult.code == EDITOR_PROJECT::EditorOperationRegistrationCode::InvalidParameter);
    CHECK(!registry.IsRegistered("project.level.invalid_type"));
    CHECK(registry.Revision() == registeredRevision);

    EditorOperationDefinition engineOperation{};
    engineOperation.id = "project.level.conflict";
    engineOperation.summary = "Engine-owned conflict test.";
    engineOperation.workspace = "scene";
    engineOperation.kind = EditorOperationKind::Query;
    engineOperation.permission = EditorOperationPermission::ReadEditorState;
    engineOperation.execute = [](const nlohmann::json&) { return OperationOk("Engine operation remains registered."); };
    CHECK(registry.Register(std::move(engineOperation)).ok);

    const auto rollbackOwner = registry.CreateOwnerToken();
    ProjectOperationHandler rollbackHandler{};
    auto partial = MakeProjectOperation(rollbackHandler, "project.level.partial");
    auto conflict = MakeProjectOperation(rollbackHandler, "project.level.conflict");
    GameProjectEditorOperationRegistration rollbackRegistrar{registry, rollbackOwner, nullptr};
    CHECK(rollbackRegistrar.RegisterOperation(partial).ok);
    const auto duplicate = rollbackRegistrar.RegisterOperation(conflict);
    CHECK(!duplicate.ok && duplicate.code == EDITOR_PROJECT::EditorOperationRegistrationCode::DuplicateId);
    CHECK(rollbackRegistrar.Failed());
    const auto* preservedConflict = registry.Find("project.level.conflict");
    CHECK(preservedConflict && preservedConflict->summary == "Engine-owned conflict test.");
    CHECK(registry.UnregisterOwner(rollbackOwner) == 1u);
    CHECK(!registry.IsRegistered("project.level.partial"));
    CHECK(registry.IsRegistered("project.level.conflict"));

    CHECK(registry.UnregisterOwner(owner) == 1u);
    CHECK(registry.Revision() > registeredRevision);
    CHECK(!registry.IsRegistered("project.level.validate"));
    // Module-owned handlers are destroyed only after their registry callbacks are removed.
    moduleHandler.reset();
    CHECK(registry.UnregisterOwner(owner) == 0u);
}

class Keyboard final : public HIKARI::INPUT::IInputBackend {
public:
    bool forward = false;
    void SetHostWindow(void*) override {}
    bool HasInputFocus() const noexcept override { return true; }
    void SetMouseCaptureMode(HIKARI::INPUT::MouseCaptureMode) override {}
    void SetMouseCaptureRegion(const HIKARI::INPUT::MouseCaptureRegion&) override {}
    void ClearMouseCaptureRegion() override {}
    HIKARI::INPUT::MouseCaptureMode GetMouseCaptureMode() const noexcept override {
        return HIKARI::INPUT::MouseCaptureMode::Free;
    }
    void SetExternalMouseWheel(float) override {}
    void SetPointerCursorVisible(bool) override {}
    void Reset() override { forward = false; }
    void Poll(HIKARI::INPUT::InputDeviceState& out) override {
        out = {};
        out.keyboard['W'] = forward;
    }
    float ReadControl(const HIKARI::INPUT::InputDeviceState&, HIKARI::INPUT::InputBindingSource source,
                      std::string_view name, uint32_t) const override {
        return source == HIKARI::INPUT::InputBindingSource::Keyboard && name == "W" && forward ? 1.f : 0.f;
    }
    bool IsAnyControlActive(const HIKARI::INPUT::InputDeviceState&, float) const override { return forward; }
    void EnumerateNewControls(const HIKARI::INPUT::InputDeviceState&, const HIKARI::INPUT::InputDeviceState&, float,
                              bool, std::vector<HIKARI::INPUT::InputControlActuation>& out) const override {
        out.clear();
    }
    bool SetGamepadVibration(uint32_t, float, float) override { return false; }
};

void CharacterBootstrap() {
    ComponentRegistry components;
    WorldMassSchemaRegistry mass;
    SystemTypeRegistry systems;
    ComponentSystemPolicy policy;
    auto registration = RuntimeFeatureContextAccess::Create(components, mass, systems, policy);
    CHECK(CreatePhysicsRuntimeFeature()->Register(registration));
    CHECK(CreateGameplayRuntimeFeature()->Register(registration));
    DocumentComponentAuthoringService authoring;
    SceneDocument document;
    SceneObjectData actor;
    actor.id = {1};
    actor.name = "Player";
    CHECK(authoring.AddComponent(components, actor, "CharacterLocomotionComponent").success);
    CHECK(actor.components.size() == 3); // Locomotion + body + collider; NPCs need no keyboard.
    CHECK(authoring.GetMissingOptionalComponents(components, actor) ==
          std::vector<std::string>{"CharacterInputComponent"});
    CHECK(authoring.AddComponent(components, actor, "CharacterInputComponent").success);
    CHECK(actor.components.size() == 4);
    CHECK(authoring.GetMissingOptionalComponents(components, actor).empty());
    SceneObjectData ground;
    ground.id = {2};
    ground.name = "Ground";
    CHECK(authoring.AddComponent(components, ground, "PhysicsBodyComponent").success);
    for (auto& c : ground.components) {
        if (c.type == "PhysicsBodyComponent")
            c.properties["motionType"] = "Static";
        if (c.type == "ColliderComponent")
            c.properties = {{"shape", "Box"},
                            {"fitMode", "Manual"},
                            {"size", {30, 1, 30}},
                            {"center", {0, -.5f, 0}},
                            {"trigger", false}};
    }
    document.objects = {actor, ground};
    World world;
    HIKARI::INPUT::InputService input;
    auto keyboard = std::make_unique<Keyboard>();
    auto* keys = keyboard.get();
    input.SetBackend(std::move(keyboard));
    CHECK(input.InitializeTransient());
    input.Contexts().SetActive("Editor", false);
    input.Contexts().SetActive("Gameplay", true);
    GAMEPLAY::MotionIntentService intents;
    PHYSICS::KinematicMotionService motion;
    PHYSICS::PhysicsWorldService physics;
    CHECK(physics.InstallBackend(PHYSICS::CreateJoltPhysicsBackend()));
    bool playing = true;
    RuntimePlayStateService play{&playing};
    CHECK(world.Services().Register(input));
    CHECK(world.Services().Register(intents));
    CHECK(world.Services().Register(motion));
    CHECK(world.Services().Register(physics));
    CHECK(world.Services().Register(play));
    for (const auto& source : document.objects) {
        auto* object = world.CreateObject(source.name);
        CHECK(object->SetDocumentId(source.id));
        for (const auto& c : source.components) {
            auto runtime = components.Find(c.type)->factory();
            runtime->Deserialize(c.properties);
            CHECK(object->AddComponentInstance(std::move(runtime)));
        }
    }
    SystemScheduler scheduler;
    CHECK(policy.InstallRequiredSystems(document, systems, scheduler).success);
    scheduler.AttachWorld(world);
    FrameContext frame{};
    frame.fixedDt = 1.f / 60;
    frame.gameDt = frame.fixedDt;
    auto step = [&] {
        ++frame.frameIndex;
        ++frame.fixedTickIndex;
        input.Update(frame.fixedDt);
        scheduler.PreUpdate(world, frame);
        world.BeginFixedStep(frame.fixedTickIndex);
        scheduler.PreFixedUpdate(world, frame);
        scheduler.FixedUpdate(world, frame);
        scheduler.PostFixedUpdate(world, frame);
    };
    for (int i = 0; i < 10; ++i)
        step();
    keys->forward = true;
    for (int i = 0; i < 60; ++i)
        step();
    auto* player = world.FindObject(SceneObjectId{1});
    const auto* moved = motion.FindState(player->GetRuntimeHandle());
    CHECK(moved && moved->pose.position.z > 2.f);
    const float z = moved->pose.position.z;
    player->GetComponent<CharacterInputComponent>()->Deserialize({{"enabled", false}});
    for (int i = 0; i < 30; ++i)
        step();
    moved = motion.FindState(player->GetRuntimeHandle());
    CHECK(moved && moved->pose.position.z < z + 1.f);
    // No player-input component is required by a project-owned intent source.
    for (int i = 0; i < 40; ++i) {
        GAMEPLAY::MotionIntent intent{};
        intent.move = {1, 0};
        intents.SubmitIntent(player->GetRuntimeHandle(), 42, 200, intent, frame.frameIndex + 1);
        step();
    }
    CHECK(motion.FindState(player->GetRuntimeHandle())->pose.position.x > 1.f);
    scheduler.DetachWorld(world);
    std::cout << "Character input / authoring dependencies / Jolt movement / code intent: PASS\n";
}

void ScriptKinematicPresentation() {
    World world;
    PHYSICS::PhysicsWorldService backend;
    PHYSICS::PhysicsRuntimeStatusService status;
    PHYSICS::KinematicMotionService motion;
    PresentationTransformService presentation;
    bool playing = true;
    RuntimePlayStateService play{&playing};
    CHECK(backend.InstallBackend(PHYSICS::CreateJoltPhysicsBackend()));
    CHECK(world.Services().Register(backend) && world.Services().Register(status) &&
          world.Services().Register(motion) && world.Services().Register(presentation) &&
          world.Services().Register(play));
    auto* actor = world.CreateObject("Script Kinematic sprite");
    CHECK(actor->SetDocumentId({1}));
    actor->AddComponent<ColliderComponent>()->Deserialize(
        {{"shape", "Box"}, {"fitMode", "Manual"}, {"size", {1, 1, 1}}});
    auto* body = actor->AddComponent<PhysicsBodyComponent>();
    body->SetMotionType(PHYSICS::PhysicsMotionType::Kinematic);
    actor->AddComponent<WorldSpriteComponent>()->Deserialize(
        {{"textureAssetGuid", "b3100000000040008000000000000001"}, {"preserveAspect", false}, {"facing", "Fixed"}});
    auto* wall = world.CreateObject("Nonblocking decoration");
    CHECK(wall->SetLocalPosition({0.16f, 0, 0}));
    wall->AddComponent<ColliderComponent>()->Deserialize(
        {{"shape", "Box"}, {"fitMode", "Manual"}, {"size", {1, 1, 1}}});
    wall->AddComponent<PhysicsBodyComponent>()->SetMotionType(PHYSICS::PhysicsMotionType::Static);
    PHYSICS::PhysicsWorldSettings settings;
    settings.gravity = {};
    PHYSICS::PhysicsSystem physics(settings);
    physics.OnWorldAttached(world);
    const auto* binding = status.FindBodyStatus(actor->GetRuntimeHandle());
    CHECK(binding && binding->body.IsValid());
    const auto actorBody = binding->body;
    const auto view = GameProjectSdkAccess::MakeObjectView(*actor);
    const auto worldView = GameProjectSdkAccess::MakeWorldView(world);
    RenderSubmissionSystem render;
    RenderSubmissionSystem::SetAssetContext(nullptr, {});
    FrameContext frame;
    frame.fixedDt = 1.f / 60;
    auto fixed = [&](std::optional<float> x) {
        ++frame.fixedTickIndex;
        world.BeginFixedStep(frame.fixedTickIndex);
        physics.PreFixedUpdate(world, frame);
        if (x) {
            GameProjectTransform target;
            CHECK(view.GetWorldTransform(target));
            target.position.x = *x;
            CHECK(view.SetWorldTransform(target).Succeeded());
        }
        physics.FixedUpdate(world, frame);
        physics.PostFixedUpdate(world, frame);
        PHYSICS::PhysicsBodyState actual;
        GameProjectTransform authoritative;
        CHECK(backend.TryGetBodyState(actorBody, actual) && view.GetWorldTransform(authoritative));
        CHECK(std::abs(actual.pose.position.x - authoritative.position.x) < 0.0001f);
    };
    auto displayed = [&](float alpha, float expected, float authority) {
        ++frame.frameIndex;
        frame.fixedInterpolationAlpha = alpha;
        physics.LateUpdate(world, frame);
        MATH::Mat4 presented;
        CHECK(presentation.TryGetWorldMatrix(actor->GetRuntimeHandle(), presented));
        const auto position = presented.TransformPoint({0, 0, 0, 1});
        CHECK(std::abs(position.x - expected) < .0001f);
        render.PreRender(world, frame);
        const auto& sprites = RenderSubmissionSystem::GetWorldSpriteFrameSource().instances;
        CHECK(sprites.size() == 1 && std::abs(sprites[0].worldPosition.x - expected) < 0.0001f);
        CHECK(std::abs((sprites[0].worldBounds.min.x + sprites[0].worldBounds.max.x) * .5f - expected) < .0001f);
        GameProjectTransform authoritative;
        CHECK(view.GetWorldTransform(authoritative) && std::abs(authoritative.position.x - authority) < .0001f);
        PHYSICS::PhysicsRaycastQuery ray;
        ray.origin = {authority + .49f, 3, 0};
        ray.direction = {0, -1, 0};
        ray.maxDistance = 5;
        ray.filter.ignoredObject = wall->GetRuntimeHandle();
        PHYSICS::PhysicsHit hit;
        CHECK(backend.Raycast(ray, hit) && hit.object == actor->GetRuntimeHandle());
        std::cout << "Kinematic alpha=" << alpha << " presentation=" << position.x
                  << " sprite=" << sprites[0].worldPosition.x << " authority=" << authoritative.position.x << '\n';
    };
    fixed(0.f);
    displayed(0, 0, 0);
    fixed(.08f);
    displayed(0, 0, .08f);
    displayed(.25f, .02f, .08f);
    displayed(.5f, .04f, .08f);
    displayed(.75f, .06f, .08f); // 固定 tick の無い描画フレームでも前進する。
    displayed(1, .08f, .08f);
    fixed(.16f);
    fixed(.24f); // catch-up 後は最後の二姿勢だけを補間する。
    displayed(.5f, .20f, .24f);
    fixed(std::nullopt);
    displayed(.5f, .24f, .24f);
    fixed(std::nullopt);
    displayed(.25f, .24f, .24f);
    GameProjectKinematicState unused;
    CHECK(!worldView.GetMotion().GetKinematicState(view.GetHandle().value, unused));
    GameProjectKinematicTeleport teleport;
    teleport.objectHandle = view.GetHandle().value;
    teleport.sourceId = 1;
    teleport.position = {10, 0, 0};
    CHECK(worldView.GetMotion().RequestTeleport(teleport));
    fixed(std::nullopt);
    displayed(0, 10, 10);
    displayed(.5f, 10, 10);
    displayed(1, 10, 10);
    fixed(10.08f);
    displayed(.5f, 10.04f, 10.08f);
    physics.OnWorldDetached(world);
    MATH::Mat4 detached;
    CHECK(!presentation.TryGetWorldMatrix(actor->GetRuntimeHandle(), detached));
    RenderSubmissionSystem::InvalidateSceneResources(false);
    std::cout
        << "Script Kinematic: same-tick physics / sprite+bounds interpolation / catch-up / stop / teleport / authoritative raycast: PASS\n";
}

struct GizmoFixture {
    EditorContext editor;
    EditorTransformGizmo gizmo;
    World world;
    GameObject* object = nullptr;
    Camera3D camera;
    EditorViewportRect rect{0, 60, 800, 540};
    GizmoFixture() {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.DisplaySize = {800, 600};
        io.DeltaTime = 1.f / 60;
        unsigned char* pixels;
        int w, h;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        camera.SetPerspective(1.f, 800.f / 540, .1f, 1000.f);
        camera.SetLookAt({0, 0, 5}, {0, 0, 0});
        object = world.CreateObject("Selected");
        CHECK(object->SetDocumentId({1}));
        editor.transformGizmo.enabled = true;
    }
    ~GizmoFixture() {
        ClearGameViewportInputRect();
        ImGui::DestroyContext();
    }
    EditorTransformGizmoResult Frame(ImVec2 mouse, bool down = false) {
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({800, 600});
        ImGui::Begin("View", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ImGui::SetCursorScreenPos({8, 8});
        DrawViewportAuthoringToolbar(editor, false);
        ImGui::SetCursorScreenPos({rect.x, rect.y});
        ImGui::Dummy({rect.width, rect.height});
        HandleTransformGizmoShortcuts(editor.transformGizmo, true);
        const int clips = ImGui::GetWindowDrawList()->_ClipRectStack.Size;
        auto result = gizmo.Draw(*object, camera, editor.transformGizmo, rect);
        CHECK(ImGui::GetWindowDrawList()->_ClipRectStack.Size == clips);
        ImGui::End();
        ImGui::Render();
        return result;
    }
    void Click(ImVec2 p) {
        Frame(p);
        Frame(p, true);
        Frame(p);
    }
};

class ObservationScene final : public DocumentSceneBase {
public:
    ObservationScene() : DocumentSceneBase("observation-contracts") {}
    const char* GetSceneName() const override { return "Object observations"; }
};

void SceneObservationAndCaptureContracts() {
    ModelAsset asset;
    asset.SetState(ModelAsset::State::Loaded);
    asset.bounds = {{-1, -2, -3}, {1, 2, 3}};
    ObservationScene scene;
    SceneObjectData authored;
    authored.id = {2};
    authored.name = "Fighter";
    authored.parent = SceneObjectId{1};
    authored.components.push_back({});
    authored.components.back().type = "ModelComponent";
    authored.components.back().properties = {{"assetId", "missing-model"}};
    SceneObjectData parentData;
    parentData.id = {1};
    parentData.name = "Parent";
    scene.GetSceneDocument().objects = {parentData, authored};
    ComponentAuthoringQueryService query;
    auto first = query.QueryObjects(scene, {{"limit", 1}});
    CHECK(first.ok && first.data["total"] == 2 && first.data["nextOffset"] == 1);
    auto second = query.QueryObjects(scene, {{"offset", 1}, {"limit", 1}});
    CHECK(second.data["objects"][0]["objectId"] == 2 && !second.data.contains("nextOffset"));
    auto unresolved = query.QueryObjects(scene, {{"keyword", "Fight"}, {"detail", true}});
    CHECK(unresolved.data["count"] == 1);
    CHECK(unresolved.data["objects"][0]["model"]["state"] == "unresolved");
    CHECK(!unresolved.data["objects"][0]["runtimeAvailable"].get<bool>());
    auto* parent = scene.GetWorld().CreateObject("Parent");
    CHECK(parent->SetDocumentId({1}) && parent->SetLocalPosition({10, 0, 0}));
    auto* modelObject = scene.GetWorld().CreateObject("Fighter");
    CHECK(modelObject->SetDocumentId({2}) && modelObject->SetParent(parent));
    CHECK(modelObject->SetLocalPosition({2, 0, 0}));
    modelObject->AddComponent<ModelComponent>()->SetModelAsset(&asset);
    const auto bounds = ObserveSceneObjectBounds(scene, {2});
    CHECK(std::abs(bounds.min.x - 11) < .001f && std::abs(bounds.max.z - 3) < .001f);
    auto observed = query.QueryObjects(scene, {{"objectIds", nlohmann::json::array({2})}, {"detail", true}});
    CHECK(observed.data["count"] == 1 && observed.data["objects"][0]["worldPosition"][0] == 12);
    auto summary = query.QueryObjects(scene, {{"objectIds", nlohmann::json::array({2})}});
    CHECK(summary.data["objects"][0]["worldPosition"][0] == 12 && !summary.data["objects"][0].contains("model"));
    CHECK(observed.data["objects"][0]["model"]["state"] == "loaded");
    CHECK(!TOOLS::BAKING::LightingBakeService{}.GetBlockReason(scene).empty());
    CHECK(!scene.RequestLightingBake(false) && !scene.ClearLightingBake());
    using namespace RENDER3D;
    const auto ticket = EDITORVIEW::RequestViewCapture(kPrimaryRenderViewId, "capture-contract-unused.png");
    CHECK(ticket != 0);
    bool scheduledSecondary = false;
    EDITORVIEW::ScheduleViewCaptures([&](RenderViewId) -> std::optional<uint64_t> {
        scheduledSecondary = true;
        return std::nullopt;
    });
    CHECK(!scheduledSecondary && !EDITORVIEW::GetViewCaptureResult(ticket).done);
    EDITORVIEW::CancelViewCapture(ticket);
    CHECK(EDITORVIEW::GetViewCaptureResult(ticket).done && !EDITORVIEW::GetViewCaptureResult(ticket).error.empty());
}

void ToolReopenContracts() {
    DocumentSceneEditorController controller;
    auto& registry = controller.Operations();
    CHECK(!registry.Find("capture.preview")->requiresDocumentSession);
    CHECK(registry.Find("scene.capture_views")->sessionDispatch == EditorOperationSessionDispatch::Operation);
    CHECK(!registry.Find("scene.object.select")->requiresDocumentSession);
    CHECK(!registry.Find("scene.object.focus")->requiresDocumentSession);
    CHECK(!registry.Find("lighting.bake.start")->requiresDocumentSession);
    CHECK(registry.Find("lighting.bake.clear")->permission == EditorOperationPermission::ModifyProjectFiles);

    // 実際の項目 Workspace callback から実行し、登録漏れと公開対象の判定を区別する。
    // Scene を開かず NO_SCENE まで届けば、Play の入口が UI adapter を通過した証拠になる。
    struct OperationProbe final : EDITOR_PROJECT::IEditorWorkspace, EDITOR_PROJECT::IEditorOperationResultSink {
        EDITOR_PROJECT::EditorWorkspaceDescriptor GetDescriptor() const override {
            EDITOR_PROJECT::EditorWorkspaceDescriptor result{};
            result.workspaceId = "contract.preview";
            result.displayName = "Preview contract";
            return result;
        }
        void OnActivate(EDITOR_PROJECT::EditorWorkspaceHostContext& context) override {
            context.commands->ExecuteOperation("play.preview", R"({"spawnPointId":"td2.preview:2,3","userData":42})",
                                               *this);
            previewCode = code;
            context.commands->ExecuteOperation("contract.missing", "{}", *this);
            missingCode = code;
            context.commands->ExecuteOperation("contract.agent_only", "{}", *this);
            hiddenCode = code;
            context.commands->ExecuteOperation("play.preview", R"({"userData":-1})", *this);
            invalidCode = code;
        }
        void OnDeactivate(EDITOR_PROJECT::EditorWorkspaceHostContext&) override {}
        void Draw(EDITOR_PROJECT::EditorWorkspaceHostContext&) override {}
        bool IsDirty() const override { return false; }
        EDITOR_PROJECT::EditorWorkspaceSaveResult Save(EDITOR_PROJECT::EditorWorkspaceHostContext&) override {
            return EDITOR_PROJECT::EditorWorkspaceSaveResult::Saved;
        }
        EDITOR_PROJECT::EditorWorkspaceCloseDisposition QueryClose(
            EDITOR_PROJECT::EditorWorkspaceCloseReason) const override {
            return EDITOR_PROJECT::EditorWorkspaceCloseDisposition::Allow;
        }
        void Receive(bool, std::string_view resultCode, std::string_view, std::string_view) override {
            code.assign(resultCode);
        }
        std::string code, previewCode, missingCode, hiddenCode, invalidCode;
    } operationProbe;
    const auto* preview = registry.Find("play.preview");
    CHECK(preview && preview->exposureMask == EditorOperationExposureMask::Human && !preview->requiresDocumentSession);
    EditorOperationDefinition hidden{
        "contract.agent_only", "Agent-only contract probe", "runtime", {}, [](const nlohmann::json&) {
            return OperationOk("Allowed.");
        }};
    hidden.exposureMask = EditorOperationExposureMask::Agent;
    hidden.permission = EditorOperationPermission::ReadEditorState;
    CHECK(registry.Register(std::move(hidden), EditorOperationKind::Query, false).ok);
    GameProjectEditorWorkspaceHost projectHost;
    EDITOR_PROJECT::IEditorWorkspace* probeWorkspaces[] = {&operationProbe};
    std::string registrationError;
    CHECK(projectHost.Registry().RegisterModule("contract", probeWorkspaces, 1, registrationError));
    projectHost.Registry().SetOperationRegistry(&registry);
    CHECK(projectHost.Registry().RequestActivateWorkspace("contract.preview"));
    EditorWorkspaceHost nativeHost;
    projectHost.UpdateNavigation(nativeHost, 0);
    CHECK(operationProbe.previewCode == "NO_SCENE");
    CHECK(operationProbe.missingCode == "OPERATION_NOT_FOUND");
    CHECK(operationProbe.hiddenCode == "UNAVAILABLE");
    CHECK(operationProbe.invalidCode == "PARAMETER_CONSTRAINT");
    projectHost.Shutdown();

    struct ProbeTool final : IEditorTool {
        int draws = 0;
        int opens = 0;
        void Draw(EditorToolContext& context, bool&) override {
            ++draws;
            opens += context.openRequest != nullptr ? 1 : 0;
        }
    };
    static ProbeTool* probe = nullptr;
    EditorToolHost host;
    CHECK(host.Register({"probe", "Probe", {}, false, []() -> std::unique_ptr<IEditorTool> {
                             auto instance = std::make_unique<ProbeTool>();
                             probe = instance.get();
                             return instance;
                         }}));
    ObservationScene scene;
    EditorContext editor;
    EditorOperationRegistry operations;
    EditorToolContext context{scene, editor, operations};
    CHECK(host.RequestOpen({"probe"}));
    host.Draw(context);
    host.Draw(context);
    CHECK(probe && probe->draws == 2 && probe->opens == 1);
    CHECK(host.RequestOpen({"probe"}));
    host.Draw(context);
    CHECK(probe->draws == 3 && probe->opens == 2);

    RegisterBuiltInEditorTools(host);
    // 同じ非表示ドックの回帰条件を、ツール窓と共通の性能窓の両方で確認する。
    for (const bool performanceAudit : {false, true}) {
        PerformanceAuditPanel auditPanel;
        bool auditOpen = false;
        const auto drawPanel = [&](bool revealRequested) {
            if (performanceAudit) {
                auditOpen = auditOpen || revealRequested;
                auditPanel.Draw(auditOpen, revealRequested);
            } else {
                if (revealRequested)
                    CHECK(host.RequestOpen({std::string(kLightingBakeToolId)}));
                host.Draw(context);
            }
        };
        // 実際のパネルを使い、open フラグだけでなく描画データの可視性を確認する。
        GizmoFixture fixture;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        const char* windowName = performanceAudit ? "###Performance Audit" : "###Lighting Bake";
        const ImGuiID dockRoot = ImHashStr("OldWorkspace");
        ImGui::NewFrame();
        ImGui::DockBuilderAddNode(dockRoot, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockRoot, {800, 600});
        const ImGuiID dockLeaf = ImGui::DockBuilderSplitNode(dockRoot, ImGuiDir_Right, .4f, nullptr, nullptr);
        ImGui::DockBuilderDockWindow(windowName, dockLeaf);
        ImGui::DockBuilderFinish(dockRoot);
        ImGui::DockSpace(dockRoot, {}, ImGuiDockNodeFlags_KeepAliveOnly);
        CHECK(!ImGui::FindWindowByName(windowName));
        drawPanel(true);
        ImGui::Render();
        auto* window = ImGui::FindWindowByName(windowName);
        CHECK(window && window->DockId == 0);
        // 初回 Begin はサイズ測定のため非表示になり得る。追加のオープン要求なしで次フレームを描く。
        ImGui::NewFrame();
        ImGui::DockSpace(dockRoot, {}, ImGuiDockNodeFlags_KeepAliveOnly);
        drawPanel(false);
        ImGui::Render();
        CHECK(window && !window->Hidden && window->DockId == 0);
        CHECK(window->Viewport == ImGui::GetMainViewport());
        CHECK(window->DrawList->VtxBuffer.Size > 0);

        // 表示中のドックに戻した場合、再オープンしてもユーザーの配置を壊さない。
        ImGui::NewFrame();
        ImGui::DockBuilderDockWindow(windowName, dockLeaf);
        ImGui::DockSpaceOverViewport(dockRoot, ImGui::GetMainViewport());
        drawPanel(true);
        ImGui::Render();
        CHECK(window->DockId == dockLeaf);
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(dockRoot, ImGui::GetMainViewport());
        drawPanel(true);
        ImGui::Render();
        CHECK(!window->Hidden && window->DockId == dockLeaf);

        // 別ワークスペースへ移動した後、既存ウィンドウも明示的なオープンで復帰する。
        ImGui::NewFrame();
        ImGui::DockSpace(dockRoot, {}, ImGuiDockNodeFlags_KeepAliveOnly);
        drawPanel(false);
        ImGui::Render();
        CHECK(window->Hidden && window->DockId == dockLeaf);
        ImGui::NewFrame();
        ImGui::DockSpace(dockRoot, {}, ImGuiDockNodeFlags_KeepAliveOnly);
        drawPanel(true);
        ImGui::Render();
        CHECK(!window->Hidden && !window->Collapsed && window->DockId == 0);
        CHECK(window->DrawList->VtxBuffer.Size > 0);
        std::cout << windowName << ": saved hidden dock / visible dock / workspace switch: PASS\n";
    }
}

void DirectorNavigationDuringPlay() {
    GizmoFixture fixture;
    class Scene final : public DocumentSceneBase {
    public:
        Scene() : DocumentSceneBase("director-contracts") {}
        const char* GetSceneName() const override { return "Director navigation"; }
    } scene;
    DirectorViewPanel panel;
    EditorViewInstance view;
    view.renderViewId = kEditorDirectorRenderViewId;
    panel.SetFreeCamera(fixture.camera);
    auto frame = [&](bool navigate) {
        auto& io = ImGui::GetIO();
        io.ConfigInputTrickleEventQueue = false;
        io.AddMousePosEvent(400, 300);
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, navigate);
        io.AddKeyEvent(ImGuiKey_W, navigate);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({800, 600});
        ImGui::Begin("Director", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove);
        const auto result = panel.DrawContents(scene, view, {}, fixture.editor.transformGizmo, .1f, true, false);
        CHECK(!result.pilotPoseChanged && !result.alignCameraRequested && !result.gizmo.changed);
        ImGui::End();
        ImGui::Render();
    };
    frame(false);
    frame(false);
    const auto before = panel.GetViewCamera().GetPosition();
    for (int i = 0; i < 3; ++i)
        frame(true);
    CHECK(MATH::Length(panel.GetViewCamera().GetPosition() - before) > .01f);
    CHECK(view.cameraBinding.kind == EditorViewCameraSourceKind::OwnedEditorCamera);
    CHECK(scene.GetSceneDocument().objects.empty());
    frame(false);
    const auto stopped = panel.GetViewCamera().GetPosition();
    frame(false);
    CHECK(MATH::Length(panel.GetViewCamera().GetPosition() - stopped) < .0001f);
    panel.LeaveWorkspace();
}

void CameraRelativeMovement() {
    for (const MATH::Vec3 forward : {MATH::Vec3{0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {-1, 0, 0}, {0, -1, 0}}) {
        World world;
        GAMEPLAY::MotionIntentService intents;
        PHYSICS::KinematicMotionService motion;
        Camera3D camera;
        const auto up = std::abs(forward.y) > .9f ? MATH::Vec3{0, 0, 1} : MATH::Vec3{0, 1, 0};
        camera.SetLookAt({0, 0, 0}, forward, up);
        GameplayCameraService cameras{&camera};
        CHECK(world.Services().Register(cameras));
        CHECK(world.Services().Register(intents));
        CHECK(world.Services().Register(motion));
        auto* actor = world.CreateObject("Actor");
        CHECK(actor->AddComponent<CharacterLocomotionComponent>());
        CHECK(actor->AddComponent<PhysicsBodyComponent>());
        CharacterLocomotionSystem system;
        system.OnWorldAttached(world);
        FrameContext frame{};
        frame.frameIndex = 1;
        frame.fixedTickIndex = 1;
        frame.fixedDt = 1.f / 60;
        GAMEPLAY::MotionIntent intent{};
        intent.move = {1, 0};
        intents.SubmitIntent(actor->GetRuntimeHandle(), 1, 100, intent, frame.frameIndex);
        system.FixedUpdate(world, frame);
        PHYSICS::KinematicMotionRequest request;
        CHECK(motion.ConsumeMove(actor->GetRuntimeHandle(), frame.fixedTickIndex, request));
        const auto right = MATH::BuildViewBasisRH(forward, up).right;
        CHECK(MATH::Dot(request.horizontalVelocity, right) > 0);
        system.OnWorldDetached(world);
    }
    std::cout << "Camera-relative movement agrees with rendered right axis, including top-down: PASS\n";
}

void EditorPointerRayMatchesCameraProjection() {
    constexpr float viewportX = 23.0f;
    constexpr float viewportY = 41.0f;
    constexpr float viewportWidth = 800.0f;
    constexpr float viewportHeight = 600.0f;

    Camera3D camera;
    camera.SetPerspective(0.9f, viewportWidth / viewportHeight, 0.1f, 100.0f);
    camera.SetLookAt({4.0f, 3.0f, 6.0f}, {0.0f, 0.0f, 0.0f});
    const MATH::Mat4 viewProjection = camera.GetViewProj();
    const auto projectToScreen = [&](const MATH::Vec3& point) {
        const MATH::Vec4 clip = viewProjection.TransformPoint({point.x, point.y, point.z, 1.0f});
        CHECK(clip.w > 1.0e-4f);
        const float inverseW = 1.0f / clip.w;
        return MATH::Vec2{viewportX + (clip.x * inverseW * 0.5f + 0.5f) * viewportWidth,
                          viewportY + (0.5f - clip.y * inverseW * 0.5f) * viewportHeight};
    };
    const MATH::Vec3 left{-0.5f, 0.0f, 0.0f};
    const MATH::Vec3 right{0.5f, 0.0f, 0.0f};
    const MATH::Vec3 upper{0.0f, 0.5f, 0.0f};
    const MATH::Vec3 lower{0.0f, -0.5f, 0.0f};
    const MATH::Vec2 leftScreen = projectToScreen(left);
    const MATH::Vec2 rightScreen = projectToScreen(right);
    const MATH::Vec2 upperScreen = projectToScreen(upper);
    const MATH::Vec2 lowerScreen = projectToScreen(lower);
    CHECK(leftScreen.x < rightScreen.x);
    CHECK(upperScreen.y < lowerScreen.y);

    const auto rayHitsProjectedPoint = [&](const MATH::Vec2& screen, const MATH::Vec3& point) {
        EDITOR::EditorPointerRay ray{};
        CHECK(EDITOR::BuildEditorPointerRay(camera, (screen.x - viewportX) / viewportWidth,
                                            (screen.y - viewportY) / viewportHeight, ray));
        constexpr float halfExtent = 0.025f;
        const MATH::Vec3 extent{halfExtent, halfExtent, halfExtent};
        float distance = 0.0f;
        CHECK(EDITOR::IntersectEditorBounds(ray, point - extent, point + extent, distance));
        const MATH::Vec3 hit = ray.origin + ray.direction * distance;
        constexpr float tolerance = 1.0e-5f;
        CHECK(std::abs(hit.x - point.x) <= halfExtent + tolerance &&
              std::abs(hit.y - point.y) <= halfExtent + tolerance &&
              std::abs(hit.z - point.z) <= halfExtent + tolerance);
    };
    rayHitsProjectedPoint(leftScreen, left);
    rayHitsProjectedPoint(rightScreen, right);
    rayHitsProjectedPoint(upperScreen, upper);
    rayHitsProjectedPoint(lowerScreen, lower);
    std::cout << "Editor pointer rays follow camera projection left/right and up/down: PASS\n";
}

void MirroredRotation(bool parented) {
    GizmoFixture f;
    if (parented) {
        auto* parent = f.world.CreateObject("Parent");
        CHECK(parent->SetLocalRotation(MATH::Quat::FromEulerXYZ(.4f, .3f, .2f)));
        CHECK(parent->SetLocalScale({2, 3, 1}));
        CHECK(f.object->SetParent(parent));
    }
    f.editor.transformGizmo.operation = EditorTransformGizmoOperation::Rotate;
    CHECK(f.object->SetLocalScale({1, -2, 3}));
    CHECK(f.object->SetLocalRotation(MATH::Quat::FromEulerXYZ(.1f, .2f, .3f)));
    const auto original = f.object->GetTransform().rotation;
    f.Frame({400, 330});
    f.Frame({400, 330});
    ImVec2 start{};
    for (float radius = 25; radius < 150; ++radius) {
        f.Frame({400 + radius, 330});
        if (ImGuizmo::GetHoveredHandleType() == ImGuizmo::MT_ROTATE_SCREEN) {
            start = {400 + radius, 330};
            break;
        }
    }
    CHECK(start.x > 400);
    CHECK(f.Frame(start, true).manipulating);
    const float r = start.x - 400;
    const ImVec2 end{400 + r * std::cos(.3f), 330 + r * std::sin(.3f)};
    CHECK(f.Frame(end, true).changed);
    const auto rotated = f.object->GetTransform();
    const auto angularDistance = [](const MATH::Quat& a, const MATH::Quat& b) {
        const float dot = std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
        return 2 * std::acos(std::min(1.f, dot));
    };
    CHECK(std::abs(angularDistance(original, rotated.rotation) - .3f) < .01f);
    f.Frame(end, true);
    const auto repeated = f.object->GetTransform();
    CHECK(std::abs(rotated.rotation.x * repeated.rotation.x + rotated.rotation.y * repeated.rotation.y +
                   rotated.rotation.z * repeated.rotation.z + rotated.rotation.w * repeated.rotation.w) > .99999f);
    CHECK(repeated.scale.x == 1 && repeated.scale.y == -2 && repeated.scale.z == 3);
    // A tiny next mouse movement must remain tiny under a scaled/rotated parent.
    const ImVec2 next{400 + r * std::cos(.31f), 330 + r * std::sin(.31f)};
    CHECK(f.Frame(next, true).changed);
    CHECK(angularDistance(rotated.rotation, f.object->GetTransform().rotation) < .02f);
    f.Frame(next);
    // Toolbar modifiers must not become viewport-tool shortcuts.
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_W, true);
    f.Frame({400, 330});
    CHECK(f.editor.transformGizmo.operation == EditorTransformGizmoOperation::Rotate);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_W, false);
    f.Frame({400, 330});
    std::cout << "Mirrored rotation, " << (parented ? "scaled parent" : "root")
              << ": angle, continuity, scale and shortcut modifiers: PASS\n";
}

void GizmoInteractions() {
    GizmoFixture f;
    f.Frame({400, 330});
    f.Frame({400, 330});
    for (int i = 0; i < 4; ++i) {
        f.Click({81, 21});
        CHECK(f.editor.transformGizmo.operation == EditorTransformGizmoOperation::Rotate);
        f.Click({51, 21});
        CHECK(f.editor.transformGizmo.operation == EditorTransformGizmoOperation::Translate);
    }
    // The handle must be drawn at the world pivot, including a parent's translation.
    auto* parent = f.world.CreateObject("Parent");
    CHECK(parent->SetLocalPosition({4, 0, 0}));
    CHECK(f.object->SetParent(parent));
    f.camera.SetLookAt({4, 0, 5}, {4, 0, 0});
    CHECK(f.Frame({400, 330}).interacting);
    f.Frame({400, 330}, true);
    auto drag = f.Frame({425, 330}, true);
    f.Frame({425, 330});
    CHECK(drag.changed && std::abs(f.object->GetTransform().position.x) > .01f);
    CHECK(std::abs(f.object->GetTransform().position.x) < 1.f);
    // A tool switch during an unfinished drag cannot retain the previous handle's state.
    f.Frame({425, 330}, true);
    f.editor.transformGizmo.operation = EditorTransformGizmoOperation::Rotate;
    CHECK(!f.Frame({430, 330}, true).manipulating);
    f.Frame({430, 330});
    // Behind-camera early exits must balance the drawlist's clip stack.
    f.camera.SetLookAt({4, 0, 5}, {4, 0, 10});
    f.Frame({400, 330});
    std::cout << "Gizmo toolbar switches / parent pivot / interrupted drag / clip stack: PASS\n";
}
} // namespace

void VerifyComponentInspectorProject(const std::filesystem::path& root);
void VerifySceneObjectMenuLifetime();

void SequenceCameraEditingContracts();

int main(int argc, char** argv) {
    try {
        SequenceCameraEditingContracts();
        if (argc == 2 && std::string(argv[1]) == "--sequence-camera")
            return 0;
        if (argc == 2) {
            VerifyComponentInspectorProject(std::filesystem::path(argv[1]));
            return 0;
        }
        ProjectEditorOperationContracts();
        ProjectEditorDocumentContracts();
        CharacterBootstrap();
        ScriptKinematicPresentation();
        CameraRelativeMovement();
        EditorPointerRayMatchesCameraProjection();
        DirectorNavigationDuringPlay();
        SceneObservationAndCaptureContracts();
        VerifySceneObjectMenuLifetime();
        ToolReopenContracts();
        GizmoInteractions();
        MirroredRotation(false);
        MirroredRotation(true);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
