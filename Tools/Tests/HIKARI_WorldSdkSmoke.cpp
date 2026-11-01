#include "HIKARI/Project/EnvironmentView.h"
#include "HIKARI/Project/LocalFogView.h"
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Scene/Environment/HIKARI_EnvironmentRuntime.h"
#include "Scene/Lighting/HIKARI_LocalFogService.h"
#include "Core/HIKARI_TimeService.h"
#include <cmath>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <json.hpp>

#include "HIKARI/Project/WorldView.h"
#include "HIKARI/Project/ActionViews.h"
#include "Physics/HIKARI_KinematicMotionService.h"
#include "HIKARI/Project/PrefabView.h"
#include "HIKARI/Project/PropertyFields.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "RenderContracts/Asset/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Assets/Cooking/HIKARI_CookedContentManifest.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "Assets/Prefab/HIKARI_PrefabAssetStore.h"
#include "Assets/RenderStyle/HIKARI_RenderStyleAssetStore.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Sprite/HIKARI_SpriteAssetStore.h"
#include "Assets/Sprite/HIKARI_SpriteAssetSerialization.h"
#include "Assets/Sprite/HIKARI_SpriteMaterialAsset.h"
#include "Assets/Sprite/HIKARI_SpriteMaterialStore.h"
#include "RenderContracts/Shader/HIKARI_ProjectSpriteShaderContract.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Assets/Models/HIKARI_ModelManager.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Scene/HIKARI_WorldMassStore.h"
#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Components/Rendering/Decal/HIKARI_DecalComponent.h"
#include "Scene/Components/Rendering/MaterialFx/HIKARI_MaterialFxComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Components/Rendering/WorldSprite/HIKARI_WorldSpriteComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Render3D/Lighting/HIKARI_SkyManager.h"
#include "Scene/Prefab/HIKARI_PrefabDocument.h"
#include "Scene/Prefab/HIKARI_PrefabSerializer.h"
#include "Scene/HIKARI_World.h"
#include "Tools/Tests/Internal/HIKARI_ContractAssert.h"
#include "Scene/Capture/HIKARI_SceneCaptureService.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

namespace {

class DirtyOnDetachComponent final : public HIKARI::IComponent {
public:
    static inline uint32_t queryVisitsDuringDetach = 0u;

    std::string_view GetTypeName() const override { return "Test.DirtyOnDetachComponent"; }

    void OnDetach() override {
        HIKARI::GameObject* owner = GetOwner();
        assert(owner != nullptr);
        owner->MarkRenderStateDirty();
        owner->GetWorld()->ForEachObjectWith<DirtyOnDetachComponent>(
            [](HIKARI::GameObject&, DirtyOnDetachComponent&) { ++queryVisitsDuringDetach; });
    }
};

class ClosingWorldSpritePatchProbe final : public HIKARI::IComponent {
public:
    std::string_view GetTypeName() const override { return "Test.ClosingWorldSpritePatchProbe"; }

    void Configure(HIKARI::ObjectView target, HIKARI::GameProjectWorldResultCode* result) noexcept {
        target_ = target;
        result_ = result;
    }

    void OnDetach() override {
        HIKARI::GameProjectWorldSpritePatch patch{};
        patch.fieldMask = HIKARI::GameProjectWorldSpritePatchFields(HIKARI::GameProjectWorldSpritePatchField::Visible);
        patch.visible = 0u;
        if (result_ != nullptr) {
            *result_ = target_.GetWorldSprite().ApplyPatch(patch).code;
        }
    }

private:
    HIKARI::ObjectView target_{};
    HIKARI::GameProjectWorldResultCode* result_ = nullptr;
};

struct PrefabTransactionProbeState {
    std::string nestedPrefabGuid{};
    HIKARI::GameProjectPrefabSubmission nestedRequest{};
    HIKARI::GameProjectObjectHandle queuedSpawn{};
    HIKARI::GameProjectWorldResult canceledSpawn{};
    HIKARI::GameProjectWorldResult sceneBuildSpawn{};
    bool hierarchyWasComplete = false;
};

PrefabTransactionProbeState* activePrefabProbeState = nullptr;

class RuntimeWorldContextScope final {
public:
    explicit RuntimeWorldContextScope(HIKARI::World& world)
        : previousWorld_(HIKARI::RuntimeSceneContext::GetCurrentWorld()),
          previousSceneId_(HIKARI::RuntimeSceneContext::GetCurrentSceneId()),
          previousCameraReady_(HIKARI::RuntimeSceneContext::IsCameraReady(previousWorld_)) {
        HIKARI::RuntimeSceneContext::SetCurrentWorld(&world, "WorldSdkSmoke");
    }

    ~RuntimeWorldContextScope() {
        HIKARI::RuntimeSceneContext::SetCurrentWorld(previousWorld_, std::move(previousSceneId_));
        if (previousWorld_ != nullptr && previousCameraReady_) {
            HIKARI::RuntimeSceneContext::MarkCameraReady(*previousWorld_);
        }
    }

    RuntimeWorldContextScope(const RuntimeWorldContextScope&) = delete;
    RuntimeWorldContextScope& operator=(const RuntimeWorldContextScope&) = delete;

private:
    HIKARI::World* previousWorld_ = nullptr;
    std::string previousSceneId_{};
    bool previousCameraReady_ = false;
};

class PrefabTransactionProbe final : public HIKARI::IComponent {
public:
    std::string_view GetTypeName() const override { return "Test.PrefabTransactionProbe"; }

    bool TryDeserialize(const nlohmann::json& in, std::string& outDiagnostic) override {
        if (!in.is_object() || !in.contains("mode") || !in.at("mode").is_string()) {
            outDiagnostic = "Prefab probe mode is missing.";
            return false;
        }
        mode_ = in.at("mode").get<std::string>();
        outDiagnostic.clear();
        return true;
    }

    void OnAttach() override {
        assert(activePrefabProbeState != nullptr);
        HIKARI::GameObject* owner = GetOwner();
        assert(owner != nullptr && owner->GetWorld() != nullptr);
        const HIKARI::WorldView worldView = HIKARI::GameProjectSdkAccess::MakeWorldView(*owner->GetWorld());
        if (mode_ == "checkHierarchy") {
            activePrefabProbeState->hierarchyWasComplete = owner->GetChildren().size() == 1u;
            return;
        }
        if (mode_ == "spawnDuringSceneBuild") {
            activePrefabProbeState->hierarchyWasComplete =
                owner->GetChildren().size() == 1u &&
                owner->GetWorld()->FindObject(HIKARI::SceneObjectId{2u}) != nullptr;
            HIKARI::GameProjectSpawnObjectDesc desc{};
            std::snprintf(desc.name, sizeof(desc.name), "%s", "SpawnedDuringSceneBuild");
            activePrefabProbeState->sceneBuildSpawn = worldView.SpawnObject(desc);
            assert(activePrefabProbeState->sceneBuildSpawn.IsQueued());
            return;
        }
        if (mode_ == "queueAndCancelSpawn") {
            HIKARI::GameProjectSpawnObjectDesc desc{};
            std::snprintf(desc.name, sizeof(desc.name), "%s", "CanceledByPrefabRollback");
            const HIKARI::GameProjectWorldResult spawn = worldView.SpawnObject(desc);
            assert(spawn.IsQueued());
            activePrefabProbeState->queuedSpawn = spawn.object;
            activePrefabProbeState->canceledSpawn = worldView.DestroyObject(spawn.object);
            assert(activePrefabProbeState->canceledSpawn.IsQueued());
            return;
        }
        throw std::runtime_error("Unknown prefab transaction probe mode.");
    }

private:
    std::string mode_{};
};

class PrefabNestedFailProbe final : public HIKARI::IComponent {
public:
    std::string_view GetTypeName() const override { return "Test.PrefabNestedFailProbe"; }

    void OnAttach() override {
        assert(activePrefabProbeState != nullptr);
        HIKARI::GameObject* owner = GetOwner();
        assert(owner != nullptr && owner->GetWorld() != nullptr);
        const HIKARI::WorldView worldView = HIKARI::GameProjectSdkAccess::MakeWorldView(*owner->GetWorld());
        activePrefabProbeState->nestedRequest =
            worldView.GetPrefabs().RequestInstantiate(activePrefabProbeState->nestedPrefabGuid);
        assert(activePrefabProbeState->nestedRequest.Accepted());
        throw std::runtime_error("Expected prefab attachment failure.");
    }
};

HIKARI::PrefabDocument MakePrefabProbeDocument(std::string name, std::vector<nlohmann::json> rootProperties) {
    HIKARI::PrefabDocument document{};
    document.prefabName = std::move(name);
    document.rootObject.id = HIKARI::SceneObjectId{1u};
    document.rootObject.name = document.prefabName;
    for (nlohmann::json& properties : rootProperties) {
        document.rootObject.components.push_back(
            HIKARI::SceneComponentData{"Test.PrefabTransactionProbe", std::move(properties), {}});
    }
    HIKARI::SceneObjectData child{};
    child.id = HIKARI::SceneObjectId{2u};
    child.name = "子オブジェクト";
    child.parent = document.rootObject.id;
    document.descendants.push_back(std::move(child));
    return document;
}

void VerifyPrefabRuntimeTransactions() {
    struct TemporaryProject {
        std::filesystem::path root{};
        ~TemporaryProject() {
            std::error_code ignored{};
            std::filesystem::remove_all(root, ignored);
        }
    } temporaryProject{};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch();
    temporaryProject.root = std::filesystem::temp_directory_path() /
                            ("HikariWorldSdkPrefab-" +
                             std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(timestamp).count()));
    const std::filesystem::path prefabDirectory = temporaryProject.root / "Assets" / "Prefabs";
    std::filesystem::create_directories(prefabDirectory);
    const std::filesystem::path hierarchyPath = prefabDirectory / HIKARI::TEXT::PathFromUtf8("玩家机.hprefab");
    const std::filesystem::path failurePath = prefabDirectory / HIKARI::TEXT::PathFromUtf8("失败机.hprefab");
    std::string serializationError{};
    const HIKARI::PrefabSerializer serializer{};
    assert(serializer.SaveToFile(hierarchyPath,
                                 MakePrefabProbeDocument("玩家机", {nlohmann::json{{"mode", "checkHierarchy"}}}),
                                 serializationError));
    HIKARI::PrefabDocument failureDocument =
        MakePrefabProbeDocument("失败机", {nlohmann::json{{"mode", "queueAndCancelSpawn"}}});
    failureDocument.rootObject.components.push_back(
        HIKARI::SceneComponentData{"Test.PrefabNestedFailProbe", nlohmann::json::object(), {}});
    assert(serializer.SaveToFile(failurePath, failureDocument, serializationError));

    HIKARI::AssetDatabase database{};
    assert(database.Initialize(temporaryProject.root));
    assert(database.ScanAssets(true));
    const HIKARI::AssetRecord* hierarchyRecord = database.FindByPath(hierarchyPath);
    const HIKARI::AssetRecord* failureRecord = database.FindByPath(failurePath);
    assert(hierarchyRecord != nullptr && hierarchyRecord->guid.IsValid());
    assert(failureRecord != nullptr && failureRecord->guid.IsValid());
    const HIKARI::AssetGuid hierarchyGuid = hierarchyRecord->guid;
    const HIKARI::AssetGuid failureGuid = failureRecord->guid;
    assert(database.ImportAsset(hierarchyGuid));
    assert(database.ImportAsset(failureGuid));

    nlohmann::json taggedProperties = nlohmann::json::object();
    std::string writtenGuid = hierarchyGuid.value;
    HIKARI::GameProjectSdkAccess::MakePropertyFields(HIKARI::PropertyFieldsMode::Save, nullptr, &taggedProperties,
                                                     nullptr)
        .Asset("prefab", "Prefab", HIKARI::GameProjectAssetKind::Prefab, writtenGuid);
    char archivedGuid[128]{};
    assert(HIKARI::GameProjectSdkAccess::MakePropertyFields(HIKARI::PropertyFieldsMode::Load, &taggedProperties,
                                                            nullptr, nullptr)
               .Asset("prefab", "Prefab", HIKARI::GameProjectAssetKind::Prefab, archivedGuid));
    assert(std::string_view(archivedGuid) == hierarchyGuid.value);
    HIKARI::SceneDocument referencingDocument{};
    HIKARI::SceneObjectData referencingObject{};
    referencingObject.components.push_back(
        HIKARI::SceneComponentData{"Test.GameProject", std::move(taggedProperties), {}});
    referencingDocument.objects.push_back(std::move(referencingObject));
    const HIKARI::AssetUsageSummary taggedUsage =
        HIKARI::AnalyzeAssetUsage(referencingDocument, database, HIKARI::AssetUsageScope::ObjectComponents);
    assert(taggedUsage.IsUsed(hierarchyGuid));

    HIKARI::ComponentRegistry componentRegistry{};
    HIKARI::ComponentTypeInfo probeType{};
    probeType.typeName = "Test.PrefabTransactionProbe";
    probeType.factory = []() -> std::unique_ptr<HIKARI::IComponent> {
        return std::make_unique<PrefabTransactionProbe>();
    };
    probeType.registrationSource = HIKARI::ComponentRegistrationSource::GameProject;
    assert(componentRegistry.Register(std::move(probeType)));
    HIKARI::ComponentTypeInfo nestedFailType{};
    nestedFailType.typeName = "Test.PrefabNestedFailProbe";
    nestedFailType.factory = []() -> std::unique_ptr<HIKARI::IComponent> {
        return std::make_unique<PrefabNestedFailProbe>();
    };
    nestedFailType.registrationSource = HIKARI::ComponentRegistrationSource::GameProject;
    assert(componentRegistry.Register(std::move(nestedFailType)));

    HIKARI::AssetRegistry assetRegistry{};
    HIKARI::ModelManager modelManager{};
    HIKARI::PrefabAssetStore prefabAssetStore{};
    prefabAssetStore.Configure(&database);
    HIKARI::RuntimeAssetContextService assetContext{};
    assetContext.assetRegistry = &assetRegistry;
    assetContext.modelManager = &modelManager;
    assetContext.prefabAssetStore = &prefabAssetStore;
    assetContext.preparePrefabRuntimeAssets = [&database, &prefabAssetStore](std::string_view prefabGuid,
                                                                             std::string& outError) {
        const HIKARI::AssetGuid guid{std::string(prefabGuid)};
        const HIKARI::AssetRecord* record = database.FindByGuid(guid);
        if (record == nullptr || record->type != HIKARI::AssetType::Prefab || !record->lastImportSucceeded) {
            outError = "Prefab was not successfully imported in the transaction fixture: " + guid.value;
            return false;
        }

        const std::shared_ptr<const HIKARI::PrefabDocument> prefab = prefabAssetStore.Load(guid, outError);
        if (prefab == nullptr) {
            return false;
        }

        HIKARI::SceneDocument prefabScene{};
        prefabScene.objects.reserve(prefab->descendants.size() + 1u);
        prefabScene.objects.push_back(prefab->rootObject);
        prefabScene.objects.insert(prefabScene.objects.end(), prefab->descendants.begin(), prefab->descendants.end());
        const HIKARI::AssetUsageSummary usage =
            HIKARI::AnalyzeAssetUsage(prefabScene, database, HIKARI::AssetUsageScope::ObjectComponents);
        if (!usage.missingReferences.empty()) {
            const HIKARI::AssetMissingReference& missing = usage.missingReferences.front();
            outError = "Prefab dependency is missing from the transaction fixture: " + missing.assetId;
            return false;
        }
        for (const std::string& dependencyGuid : usage.usedGuids) {
            const HIKARI::AssetRecord* dependency = database.FindByGuid(HIKARI::AssetGuid{dependencyGuid});
            if (dependency == nullptr || !dependency->lastImportSucceeded) {
                outError =
                    "Prefab dependency was not successfully imported in the transaction fixture: " + dependencyGuid;
                return false;
            }
        }
        outError.clear();
        return true;
    };

    HIKARI::World world{};
    assert(world.BindComponentRegistry(&componentRegistry));
    assert(world.Services().Register(assetContext));
    RuntimeWorldContextScope worldContext(world);
    const HIKARI::WorldView worldView = HIKARI::GameProjectSdkAccess::MakeWorldView(world);
    const HIKARI::PrefabView prefabs = worldView.GetPrefabs();
    PrefabTransactionProbeState probeState{};
    probeState.nestedPrefabGuid = hierarchyGuid.value;
    activePrefabProbeState = &probeState;

    const HIKARI::GameProjectPrefabSubmission firstSubmission = prefabs.RequestInstantiate(hierarchyGuid.value);
    if (!firstSubmission.Accepted()) {
        std::fprintf(stderr, "First Prefab request rejected: error=%u\n", static_cast<unsigned>(firstSubmission.error));
    }
    assert(firstSubmission.Accepted());
    assert(world.FlushQueuedCommands(HIKARI::WorldCommandPhase::QueryEnd) == 1u);
    HIKARI::GameProjectPrefabRequestResult firstResult{};
    assert(prefabs.ReadRequestResult(firstSubmission.request, firstResult));
    assert(firstResult.state == HIKARI::GameProjectPrefabRequestState::Succeeded);
    assert(firstResult.objectCount == 2u && probeState.hierarchyWasComplete);
    const HIKARI::ObjectView firstRoot = prefabs.FindInstanceObject(firstResult.instance, 1u);
    const HIKARI::ObjectView firstChild = prefabs.FindInstanceObject(firstResult.instance, 2u);
    assert(firstRoot.IsValid() && firstChild.IsValid());
    const uint64_t firstRootSceneId = firstRoot.GetSceneObjectId();
    const uint64_t firstChildSceneId = firstChild.GetSceneObjectId();
    assert(firstRootSceneId != 0u && firstChildSceneId != 0u && firstRootSceneId != firstChildSceneId);
    assert(prefabs.DestroyInstance(firstResult.instance).Succeeded());
    assert(world.GetTotalObjectCount() == 0u);
    assert(!firstRoot.IsValid() && !firstChild.IsValid());

    const HIKARI::GameProjectPrefabSubmission partialSubmission = prefabs.RequestInstantiate(hierarchyGuid.value);
    assert(partialSubmission.Accepted());
    assert(world.FlushQueuedCommands(HIKARI::WorldCommandPhase::QueryEnd) == 1u);
    HIKARI::GameProjectPrefabRequestResult partialResult{};
    assert(prefabs.ReadRequestResult(partialSubmission.request, partialResult));
    assert(partialResult.state == HIKARI::GameProjectPrefabRequestState::Succeeded);
    const HIKARI::ObjectView partialRoot = prefabs.FindInstanceObject(partialResult.instance, 1u);
    const HIKARI::ObjectView partialChild = prefabs.FindInstanceObject(partialResult.instance, 2u);
    assert(partialRoot.IsValid() && partialChild.IsValid());
    const uint64_t lastSceneObjectId = partialRoot.GetSceneObjectId();
    assert(worldView.DestroyObject(partialChild.GetHandle()).Succeeded());
    assert(partialRoot.IsValid() && !partialChild.IsValid());
    assert(worldView.DestroyObject(partialRoot.GetHandle()).Succeeded());
    assert(!partialRoot.IsValid());
    assert(prefabs.DestroyInstance(partialResult.instance).code == HIKARI::GameProjectWorldResultCode::InvalidObject);

    const HIKARI::GameProjectPrefabSubmission failureSubmission = prefabs.RequestInstantiate(failureGuid.value);
    if (!failureSubmission.Accepted()) {
        std::fprintf(stderr, "Failure Prefab request rejected: error=%u\n",
                     static_cast<unsigned>(failureSubmission.error));
    }
    assert(failureSubmission.Accepted());
    assert(world.FlushQueuedCommands(HIKARI::WorldCommandPhase::QueryEnd) == 1u);
    HIKARI::GameProjectPrefabRequestResult failureResult{};
    assert(prefabs.ReadRequestResult(failureSubmission.request, failureResult));
    assert(failureResult.state == HIKARI::GameProjectPrefabRequestState::Failed);
    assert(failureResult.error == HIKARI::GameProjectPrefabError::InitializationFailed);
    assert(world.GetTotalObjectCount() == 0u);
    assert(probeState.queuedSpawn.IsValid() && !worldView.FindObject(probeState.queuedSpawn).IsValid());
    HIKARI::GameProjectPrefabRequestResult nestedResult{};
    assert(probeState.nestedRequest.Accepted());
    assert(prefabs.ReadRequestResult(probeState.nestedRequest.request, nestedResult));
    assert(nestedResult.state == HIKARI::GameProjectPrefabRequestState::Cancelled);
    assert(prefabs.ReleaseRequest(probeState.nestedRequest.request));
    assert(prefabs.ReleaseRequest(failureSubmission.request));
    assert(prefabs.ReleaseRequest(partialSubmission.request));
    assert(prefabs.ReleaseRequest(firstSubmission.request));

    const uint64_t lastRequestId = probeState.nestedRequest.request.value;
    const uint64_t lastInstanceId = partialResult.instance.value;
    world.Clear();
    assert(world.BindComponentRegistry(&componentRegistry));
    assert(world.Services().Register(assetContext));
    assert(prefabs.DestroyInstance(partialResult.instance).code == HIKARI::GameProjectWorldResultCode::InvalidObject);
    const HIKARI::GameProjectPrefabSubmission afterClearSubmission = prefabs.RequestInstantiate(hierarchyGuid.value);
    assert(afterClearSubmission.Accepted() && afterClearSubmission.request.value > lastRequestId);
    assert(world.FlushQueuedCommands(HIKARI::WorldCommandPhase::QueryEnd) == 1u);
    HIKARI::GameProjectPrefabRequestResult afterClearResult{};
    assert(prefabs.ReadRequestResult(afterClearSubmission.request, afterClearResult));
    assert(afterClearResult.state == HIKARI::GameProjectPrefabRequestState::Succeeded);
    assert(afterClearResult.instance.value > lastInstanceId);
    const HIKARI::ObjectView afterClearRoot = prefabs.FindInstanceObject(afterClearResult.instance, 1u);
    assert(afterClearRoot.IsValid() && afterClearRoot.GetSceneObjectId() > lastSceneObjectId);
    assert(prefabs.DestroyInstance(afterClearResult.instance).Succeeded());
    assert(prefabs.ReleaseRequest(afterClearSubmission.request));

    HIKARI::SceneDocument sceneDocument{};
    HIKARI::SceneObjectData sceneRoot{};
    sceneRoot.id = HIKARI::SceneObjectId{1u};
    sceneRoot.name = "SceneRoot";
    sceneRoot.components.push_back(HIKARI::SceneComponentData{
        "Test.PrefabTransactionProbe", nlohmann::json{{"mode", "spawnDuringSceneBuild"}}, {}});
    HIKARI::SceneObjectData sceneChild{};
    sceneChild.id = HIKARI::SceneObjectId{2u};
    sceneChild.name = "SceneChild";
    sceneChild.parent = sceneRoot.id;
    sceneDocument.objects = {std::move(sceneRoot), std::move(sceneChild)};
    HIKARI::World sceneWorld{};
    HIKARI::AssetRegistry sceneAssetRegistry{};
    HIKARI::ModelManager sceneModelManager{};
    HIKARI::SkyManager skyManager{};
    HIKARI::WorldMassSchemaRegistry massSchemaRegistry{};
    const HIKARI::SceneRuntimeBuilder sceneBuilder{};
    RuntimeWorldContextScope sceneWorldContext(sceneWorld);
    assert(sceneBuilder.BuildWorldFromDocument(sceneDocument, sceneWorld, sceneAssetRegistry, componentRegistry,
                                               massSchemaRegistry, sceneModelManager, skyManager));
    assert(sceneWorld.FlushQueuedCommands(HIKARI::WorldCommandPhase::QueryEnd) == 1u);
    assert(probeState.hierarchyWasComplete);
    assert(sceneWorld.FindObject(HIKARI::SceneObjectId{1u}) != nullptr);
    assert(sceneWorld.FindObject(HIKARI::SceneObjectId{2u}) != nullptr);
    const HIKARI::ObjectView sceneSpawn =
        HIKARI::GameProjectSdkAccess::MakeWorldView(sceneWorld).FindObject(probeState.sceneBuildSpawn.object);
    assert(sceneSpawn.IsValid() && sceneSpawn.GetSceneObjectId() > 2u);
    assert(sceneWorld.GetTotalObjectCount() == 3u);
    activePrefabProbeState = nullptr;
}

void VerifySceneObjectReferences() {
    HIKARI::World world;
    const auto view = HIKARI::GameProjectSdkAccess::MakeWorldView(world);
    assert(!view.FindSceneObject(0u).IsValid());
    assert(!view.FindSceneObject(42u).IsValid());
    auto* object = world.CreateObject("Authored");
    const auto retained = HIKARI::GameProjectSdkAccess::MakeObjectView(*object);
    assert(retained.GetSceneObjectId() == 0u);
    assert(object->SetDocumentId(HIKARI::SceneObjectId{42u}));
    assert(view.FindSceneObject(42u).GetHandle() == retained.GetHandle());
    assert(retained.GetSceneObjectId() == 42u);
    assert(object->SetDocumentId(HIKARI::SceneObjectId{43u}));
    assert(!view.FindSceneObject(42u).IsValid());
    assert(view.FindSceneObject(43u).IsValid());
    std::thread worker([&] {
        assert(!view.FindSceneObject(43u).IsValid());
        assert(retained.GetSceneObjectId() == 0u);
    });
    worker.join();
    (void)world.DestroyObject(object);
    assert(!view.FindSceneObject(43u).IsValid());
    assert(!retained.IsValid());
    auto* replacement = world.CreateObject("Replacement");
    assert(replacement->SetDocumentId(HIKARI::SceneObjectId{43u}));
    assert(view.FindSceneObject(43u).IsValid());
    assert(!retained.IsValid());
}

std::shared_ptr<const HIKARI::ASSETS::SHADER::ShaderCookedArtifact> MakeSpriteMaterialShaderArtifact(
    const HIKARI::AssetGuid& shaderGuid, std::vector<HIKARI::ASSETS::SHADER::ShaderParameterDefinition> parameters) {
    using namespace HIKARI::ASSETS::SHADER;

    const ShaderAssetContractExpectation contract = MakeProjectSpriteShaderContract({60u, 10u, 0u});
    auto artifact = std::make_shared<ShaderCookedArtifact>();
    artifact->sourceGuid = shaderGuid;
    artifact->programId = "sprite-material-sdk-fixture";
    artifact->engineInterfaceId = contract.engineInterfaceId;
    artifact->engineInterfaceVersion = contract.engineInterfaceVersion;
    artifact->engineInterfaceHash = contract.engineInterfaceHash;
    artifact->rootLayoutId = contract.rootLayoutId;
    artifact->rootLayoutVersion = contract.rootLayoutVersion;
    artifact->rootLayoutHash = contract.rootLayoutHash;
    artifact->requiredCaps = {60u, 10u, 0u};
    artifact->parameters = std::move(parameters);

    ShaderSourceDependency spriteInterface{};
    spriteInterface.logicalPath = kProjectSpriteShaderInterfaceDependency;
    assert(HIKARI::HASH::ParseSha256(HIKARI::kGameProjectSpriteShaderEngineInterfaceHash, spriteInterface.contentHash));
    artifact->dependencies.push_back(std::move(spriteInterface));
    ShaderSourceDependency geometryInterface{};
    geometryInterface.logicalPath = kRenderGeometryInterfaceDependency;
    assert(HIKARI::HASH::ParseSha256(kRenderGeometryEngineInterfaceHash, geometryInterface.contentHash));
    artifact->dependencies.push_back(std::move(geometryInterface));

    for (const std::string_view entryId :
         {HIKARI::kGameProjectSpriteColorPixelEntryId, HIKARI::kGameProjectSpriteDepthOfFieldPixelEntryId}) {
        ShaderCookedEntry entry{};
        entry.entry.id = entryId;
        entry.entry.entry = entryId;
        entry.entry.stage = ShaderStage::Pixel;
        entry.entry.profile = "ps_6_0";
        artifact->entries.push_back(std::move(entry));
    }
    return artifact;
}

void VerifySpriteAnimationSdk() {
    struct TemporaryProject {
        std::filesystem::path root{};
        ~TemporaryProject() {
            std::error_code ignored{};
            std::filesystem::remove_all(root, ignored);
        }
    } temporaryProject{};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch();
    temporaryProject.root = std::filesystem::path(HIKARI_CONTRACT_OUTPUT_ROOT) /
                            ("SpriteAnimationSdkFixture-" +
                             std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(timestamp).count()));
    const std::string spriteGuidText = "sprite-animation-sdk-fixture";
    const HIKARI::AssetGuid spriteGuid{spriteGuidText};
    const HIKARI::AssetGuid materialGuid{"a3200000000040008000000000000001"};
    const HIKARI::AssetGuid shaderGuid{"a3200000000040008000000000000002"};
    const std::string runtimeArtifact = "RuntimePayload/Sprites/runner.hspritec";
    const std::string pageArtifact = "RuntimePayload/Sprites/page.htex";
    const std::string normalArtifact = "RuntimePayload/Sprites/page-normal.htex";
    const std::string materialRuntimeArtifact = "RuntimePayload/Sprites/runner.hspritematerialc";
    const std::filesystem::path artifactDirectory = temporaryProject.root / "RuntimePayload" / "Sprites";
    std::filesystem::create_directories(artifactDirectory);
    HIKARI::SPRITE::SpriteAssetData spriteData{};
    spriteData.name = "Runner";
    spriteData.pixelsPerUnit = 2.0f;
    spriteData.defaultClip = "loop";
    HIKARI::SPRITE::SpritePage page{};
    page.id = "page";
    page.textureAssetId = HIKARI::SPRITE::MakeSpritePageTextureAssetId(spriteGuid.value, page.id);
    page.artifactPath = pageArtifact;
    const std::string normalTextureAssetId =
        HIKARI::SPRITE::MakeSpritePageNormalTextureAssetId(spriteGuid.value, page.id);
    page.normalTextureAssetId = normalTextureAssetId;
    page.normalArtifactPath = normalArtifact;
    page.colorAlphaClass = HIKARI::TextureAssetAlphaClass::Opaque;
    page.width = 3u;
    page.height = 2u;
    spriteData.pages.push_back(std::move(page));
    const auto addFrame = [&spriteData](std::string id, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                                        bool rotated) {
        HIKARI::SPRITE::SpriteFrame frame{};
        frame.id = std::move(id);
        frame.pageIndex = 0u;
        frame.x = x;
        frame.y = y;
        frame.width = width;
        frame.height = height;
        frame.sourceWidth = 3u;
        frame.sourceHeight = 1u;
        frame.trimX = 1u;
        frame.pivotX = 1.25f;
        frame.pivotY = 0.75f;
        frame.rotated = rotated;
        spriteData.frames.push_back(std::move(frame));
    };
    addFrame("f0", 0u, 0u, 2u, 1u, false);
    addFrame("f1", 0u, 1u, 2u, 1u, false);
    addFrame("f2", 2u, 0u, 1u, 2u, true);
    const auto addClip = [&spriteData](std::string id, bool loop) {
        HIKARI::SPRITE::SpriteClip clip{};
        clip.id = std::move(id);
        clip.loop = loop;
        clip.frames = {{0u, 100u, 0.0}, {1u, 200u, 0.0}, {2u, 300u, 0.0}};
        spriteData.clips.push_back(std::move(clip));
    };
    addClip("loop", true);
    addClip("once", false);
    std::string fixtureError{};
    assert(HIKARI::SPRITE::WriteSpriteRuntimeAsset(artifactDirectory / "runner.hspritec", spriteGuid.value, spriteData,
                                                   fixtureError));

    using ShaderParameterDefinition = HIKARI::ASSETS::SHADER::ShaderParameterDefinition;
    std::vector<ShaderParameterDefinition> shaderParameters{};
    ShaderParameterDefinition strength{};
    strength.key = "strength";
    strength.label = "強度";
    strength.group = "Treatment";
    strength.slot = 0u;
    strength.defaultValue = HIKARI::BINDING::MakeDefaultValue(HIKARI::BINDING::ValueType::Float);
    strength.defaultValue.numeric.x = 0.2f;
    strength.hasRange = true;
    strength.minimum = 0.0f;
    strength.maximum = 1.0f;
    strength.step = 0.05f;
    shaderParameters.push_back(strength);
    ShaderParameterDefinition color{};
    color.key = "tint";
    color.label = "Tint";
    color.group = "Treatment";
    color.slot = 1u;
    color.defaultValue = HIKARI::BINDING::MakeDefaultValue(HIKARI::BINDING::ValueType::Float3);
    color.defaultValue.numeric = {0.8f, 0.9f, 1.0f, 0.0f};
    shaderParameters.push_back(color);
    ShaderParameterDefinition mask{};
    mask.key = "useMask";
    mask.label = "Use mask";
    mask.group = "Treatment";
    mask.slot = 2u;
    mask.defaultValue = HIKARI::BINDING::MakeDefaultValue(HIKARI::BINDING::ValueType::Bool);
    mask.defaultValue.boolean = true;
    shaderParameters.push_back(mask);

    HIKARI::SPRITE::SpriteMaterialAssetData materialData{};
    materialData.name = "Fixture material";
    materialData.shaderAssetGuid = shaderGuid;
    HIKARI::ASSETS::SHADER::ShaderParameterOverride strengthPreset{};
    strengthPreset.key = "strength";
    strengthPreset.value = HIKARI::BINDING::MakeDefaultValue(HIKARI::BINDING::ValueType::Float);
    strengthPreset.value.numeric.x = 0.35f;
    materialData.parameters.push_back(std::move(strengthPreset));
    assert(HIKARI::SPRITE::WriteSpriteMaterialAssetArtifact(artifactDirectory / "runner.hspritematerialc", materialGuid,
                                                            materialData, fixtureError));
    {
        std::ofstream pageFile(artifactDirectory / "page.htex", std::ios::binary);
        assert(pageFile.good());
        pageFile.put('\0');
        assert(pageFile.good());
    }
    {
        std::ofstream normalFile(artifactDirectory / "page-normal.htex", std::ios::binary);
        assert(normalFile.good());
        normalFile.put('\0');
        assert(normalFile.good());
    }
    HIKARI::ASSETS::COOKING::CookedContentManifest manifest{};
    HIKARI::AssetRecord spriteRecord{};
    spriteRecord.guid = spriteGuid;
    spriteRecord.type = HIKARI::AssetType::Sprite;
    spriteRecord.sourcePath = "RuntimePayload/Sprites/runner.hsprite";
    spriteRecord.displayName = "Runner";
    spriteRecord.artifactManifest.artifacts.push_back(HIKARI::ASSETS::SEMANTICS::MakeAssetArtifact(
        HIKARI::ASSETS::SEMANTICS::AssetArtifactKind::SpriteRuntime, runtimeArtifact));
    spriteRecord.artifactManifest.artifacts.push_back(HIKARI::ASSETS::SEMANTICS::MakeAssetArtifact(
        HIKARI::ASSETS::SEMANTICS::AssetArtifactKind::SpritePageTexture, pageArtifact));
    spriteRecord.artifactManifest.artifacts.push_back(HIKARI::ASSETS::SEMANTICS::MakeAssetArtifact(
        HIKARI::ASSETS::SEMANTICS::AssetArtifactKind::SpritePageNormalTexture, normalArtifact));
    manifest.assets.push_back(std::move(spriteRecord));
    HIKARI::AssetRecord materialRecord{};
    materialRecord.guid = materialGuid;
    materialRecord.type = HIKARI::AssetType::SpriteMaterial;
    materialRecord.sourcePath = "RuntimePayload/Sprites/runner.hspritematerial";
    materialRecord.displayName = "Fixture material";
    materialRecord.artifactManifest.artifacts.push_back(HIKARI::ASSETS::SEMANTICS::MakeAssetArtifact(
        HIKARI::ASSETS::SEMANTICS::AssetArtifactKind::SpriteMaterialRuntime, materialRuntimeArtifact));
    manifest.assets.push_back(std::move(materialRecord));
    assert(HIKARI::ASSETS::COOKING::WriteCookedContentManifest(
        temporaryProject.root / HIKARI::ASSETS::COOKING::kCookedContentManifestFilename, manifest, &fixtureError));
    HIKARI::AssetDatabase database{};
    assert(database.Initialize(temporaryProject.root));
    assert(database.ScanAssets(true));

    HIKARI::SPRITE::SpriteAssetStore spriteStore{};
    spriteStore.Configure(&database);
    HIKARI::AssetRegistry shaderRegistry{};
    auto shaderDescriptor = std::make_unique<HIKARI::ShaderAssetDescriptor>();
    shaderDescriptor->id = {shaderGuid.value};
    shaderDescriptor->type = HIKARI::AssetType::Shader;
    shaderDescriptor->program = MakeSpriteMaterialShaderArtifact(shaderGuid, std::move(shaderParameters));
    assert(shaderRegistry.RegisterDescriptor(std::move(shaderDescriptor)));
    HIKARI::SPRITE::SpriteMaterialStore materialStore{};
    materialStore.Configure(&database, &shaderRegistry);
    const auto materialBinding = materialStore.Resolve(materialGuid, fixtureError);
    if (!materialBinding)
        std::fprintf(stderr, "Sprite material fixture: %s\n", fixtureError.c_str());
    assert(materialBinding);
    HIKARI::ComponentRegistry componentRegistry{};
    HIKARI::ComponentTypeInfo spriteType{};
    spriteType.typeName = "WorldSpriteComponent";
    spriteType.factory = []() -> std::unique_ptr<HIKARI::IComponent> {
        return std::make_unique<HIKARI::WorldSpriteComponent>();
    };
    assert(componentRegistry.Register(std::move(spriteType)));
    HIKARI::World world{};
    assert(world.BindComponentRegistry(&componentRegistry));
    assert(world.Services().Register(spriteStore));
    assert(world.Services().Register(materialStore));

    HIKARI::GameObject* object = world.CreateObject("AnimatedSprite");
    assert(object != nullptr);
    HIKARI::WorldSpriteComponent* component = object->AddComponent<HIKARI::WorldSpriteComponent>();
    assert(component != nullptr);
    const HIKARI::ObjectView objectView = HIKARI::GameProjectSdkAccess::MakeObjectView(*object);
    const HIKARI::SpriteAnimationView animation = objectView.GetSpriteAnimation();
    assert(animation.IsValid());
    assert(animation.Bind(spriteGuid.value).Succeeded());
    assert(std::abs(component->GetSize().x - 1.5f) < 0.001f);
    assert(std::abs(component->GetSize().y - 0.5f) < 0.001f);
    HIKARI::WorldSpriteRenderProjection unrotatedProjection{};
    assert(component->ResolveRenderProjection({1.0f, 1.0f, 1.0f}, unrotatedProjection));
    assert(!unrotatedProjection.rotated);
    assert(std::abs(unrotatedProjection.pivot.x - 0.125f) < 0.001f);
    assert(std::abs(unrotatedProjection.pivot.y - 0.25f) < 0.001f);
    assert(unrotatedProjection.normalTextureAssetId.value == normalTextureAssetId);
    HIKARI::GameProjectWorldSpriteState spriteState{};
    assert(objectView.GetWorldSprite().TryGetState(spriteState));
    assert(std::abs(spriteState.pivot.x - (1.25f / 3.0f)) < 0.001f);
    assert(std::abs(spriteState.pivot.y - 0.25f) < 0.001f);

    const HIKARI::SpriteMaterialView material = objectView.GetSpriteMaterial();
    assert(material.IsValid());
    HIKARI::GameProjectSpriteMaterialState materialState{};
    assert(material.TryGetState(materialState).Succeeded());
    assert(materialState.materialAssetGuid[0] == '\0' && materialState.enabled == 0u &&
           materialState.parameterCount == 0u);
    assert(material.BindAsset("missing-material-guid").code == HIKARI::GameProjectWorldResultCode::InvalidAsset);
    assert(material.TryGetState(materialState).Succeeded() && materialState.materialAssetGuid[0] == '\0');
    assert(material.BindAsset(materialGuid.value).Succeeded());
    assert(material.TryGetState(materialState).Succeeded());
    assert(std::string_view(materialState.materialAssetGuid) == materialGuid.value);
    assert(materialState.enabled == 1u && materialState.parameterCount == 3u && materialState.stateRevision > 0u);

    HIKARI::GameProjectSpriteMaterialParameterInfo strengthInfo{};
    assert(material.TryGetParameterInfo(0u, strengthInfo));
    assert(std::string_view(strengthInfo.key) == "strength" && std::string_view(strengthInfo.label) == "強度" &&
           strengthInfo.slot == 0u && strengthInfo.type == HIKARI::GameProjectBindingValueType::Float &&
           strengthInfo.hasRange == 1u && strengthInfo.minimum == 0.0f && strengthInfo.maximum == 1.0f &&
           strengthInfo.step == 0.05f);
    HIKARI::GameProjectSpriteMaterialParameterInfo infoCanary = strengthInfo;
    assert(!material.TryGetParameterInfo(99u, infoCanary));
    assert(std::memcmp(&infoCanary, &strengthInfo, sizeof(infoCanary)) == 0);

    HIKARI::GameProjectBindingValue materialValue{};
    assert(material.TryGetParameter("strength", materialValue));
    assert(materialValue.type == HIKARI::GameProjectBindingValueType::Float && materialValue.numeric[0] == 0.35f);
    materialValue.numeric[0] = 0.7f;
    assert(material.SetParameter("strength", materialValue).Succeeded());
    assert(material.TryGetState(materialState).Succeeded());
    const uint64_t materialRevisionAfterWrite = materialState.stateRevision;
    HIKARI::GameProjectBindingValue invalidMaterialValue = materialValue;
    invalidMaterialValue.numeric[0] = 1.5f;
    assert(material.SetParameter("strength", invalidMaterialValue).code ==
           HIKARI::GameProjectWorldResultCode::InvalidRequest);
    assert(material.TryGetState(materialState).Succeeded() &&
           materialState.stateRevision == materialRevisionAfterWrite);
    assert(material.TryGetParameter("strength", materialValue) && materialValue.numeric[0] == 0.7f);

    HIKARI::GameProjectBindingValue maskValue{};
    maskValue.type = HIKARI::GameProjectBindingValueType::Bool;
    maskValue.boolean = false;
    assert(material.SetParameter("useMask", maskValue).Succeeded());
    assert(material.ResetParameter("strength").Succeeded());
    assert(material.TryGetParameter("strength", materialValue) && materialValue.numeric[0] == 0.35f);
    assert(material.ResetParameters().Succeeded());
    assert(material.TryGetParameter("useMask", maskValue) && maskValue.boolean);
    materialValue.numeric[0] = 0.7f;
    assert(material.SetParameter("strength", materialValue).Succeeded());
    assert(material.SetEnabled(false).Succeeded());
    assert(material.TryGetState(materialState).Succeeded() && materialState.enabled == 0u);
    assert(material.TryGetParameter("strength", materialValue) && materialValue.numeric[0] == 0.7f);
    assert(material.BindAsset("missing-material-guid").code == HIKARI::GameProjectWorldResultCode::InvalidAsset);
    assert(material.TryGetState(materialState).Succeeded() && materialState.enabled == 0u &&
           std::string_view(materialState.materialAssetGuid) == materialGuid.value);
    assert(material.TryGetParameter("strength", materialValue) && materialValue.numeric[0] == 0.7f);
    assert(material.BindAsset(materialGuid.value).Succeeded());
    assert(material.TryGetState(materialState).Succeeded() && materialState.enabled == 1u);
    assert(material.TryGetParameter("strength", materialValue) && materialValue.numeric[0] == 0.35f);

    HIKARI::GameProjectSpriteMaterialState stateCanary{};
    stateCanary.materialAssetGuid[0] = 'C';
    stateCanary.enabled = 1u;
    stateCanary.parameterCount = 7u;
    stateCanary.stateRevision = 0x12345678u;
    const HIKARI::GameProjectSpriteMaterialState savedStateCanary = stateCanary;
    HIKARI::GameProjectWorldResultCode materialStateWrongThread = HIKARI::GameProjectWorldResultCode::Success;
    std::thread materialStateWorker([&] { materialStateWrongThread = material.TryGetState(stateCanary).code; });
    materialStateWorker.join();
    assert(materialStateWrongThread == HIKARI::GameProjectWorldResultCode::WrongThread);
    assert(std::memcmp(&stateCanary, &savedStateCanary, sizeof(stateCanary)) == 0);

    assert(material.ClearBinding().Succeeded());
    assert(material.TryGetState(materialState).Succeeded());
    assert(materialState.materialAssetGuid[0] == '\0' && materialState.enabled == 0u &&
           materialState.parameterCount == 0u);
    assert(!material.TryGetParameter("strength", materialValue));
    assert(material.SetEnabled(true).code == HIKARI::GameProjectWorldResultCode::InvalidRequest);

    const auto readState = [&animation]() {
        HIKARI::GameProjectSpriteAnimationState state{};
        assert(animation.TryGetState(state).Succeeded());
        return state;
    };
    const auto assertSameState = [](const HIKARI::GameProjectSpriteAnimationState& left,
                                    const HIKARI::GameProjectSpriteAnimationState& right) {
        assert(left.playbackState == right.playbackState && left.frameIndex == right.frameIndex);
        assert(left.elapsedSeconds == right.elapsedSeconds && left.durationSeconds == right.durationSeconds);
        assert(left.speed == right.speed && left.flags == right.flags);
    };
    HIKARI::GameProjectSpriteAnimationState state = readState();
    assert(state.playbackState == HIKARI::GameProjectSpriteAnimationPlaybackState::Playing);
    assert(state.durationSeconds == 0.6f);
    assert((state.flags &
            HIKARI::GameProjectSpriteAnimationStateFlags(HIKARI::GameProjectSpriteAnimationStateFlag::Loop)) != 0u);
    HIKARI::GameProjectSpriteAnimationClipInfo clipInfo{};
    assert(animation.TryGetClipInfo("once", clipInfo).Succeeded());
    assert(clipInfo.loop == 0 && clipInfo.frameCount > 0 && clipInfo.durationSeconds == 0.6f);
    assert(animation.TryGetClipInfo("loop", clipInfo).Succeeded());
    assert(clipInfo.loop == 1 && clipInfo.durationSeconds == 0.6f);
    const auto beforeClipFailure = clipInfo;
    assert(!animation.TryGetClipInfo("missing", clipInfo).Succeeded());
    assert(std::memcmp(&clipInfo, &beforeClipFailure, sizeof(clipInfo)) == 0);
    assertSameState(state, readState());
    assert(animation.SetSpeed(1.25f).Succeeded());
    assert(readState().speed == 1.25f);
    assert(animation.SetSpeed(1.0f).Succeeded());

    component->Update(3.95f);
    state = readState();
    assert(state.frameIndex == 2u && std::abs(state.elapsedSeconds - 0.35f) < 0.002f);
    HIKARI::WorldSpriteRenderProjection rotatedProjection{};
    assert(component->ResolveRenderProjection({1.0f, 1.0f, 1.0f}, rotatedProjection));
    assert(rotatedProjection.rotated);
    assert(std::abs(rotatedProjection.size.x - 1.0f) < 0.001f);
    assert(std::abs(rotatedProjection.size.y - 0.5f) < 0.001f);
    assert(std::abs(rotatedProjection.size.x - unrotatedProjection.size.x) < 0.001f);
    assert(std::abs(rotatedProjection.size.y - unrotatedProjection.size.y) < 0.001f);

    HIKARI::GameProjectWorldSpritePatch flipPatch{};
    flipPatch.fieldMask = HIKARI::GameProjectWorldSpritePatchFields(HIKARI::GameProjectWorldSpritePatchField::FlipX);
    flipPatch.flipX = 1u;
    assert(objectView.GetWorldSprite().ApplyPatch(flipPatch).Succeeded());
    HIKARI::GameProjectWorldSpriteState flippedSpriteState{};
    assert(objectView.GetWorldSprite().TryGetState(flippedSpriteState));
    assert(flippedSpriteState.flipX == 1u);
    assert(std::abs(flippedSpriteState.pivot.x - (1.25f / 3.0f)) < 0.001f);
    assert(std::abs(flippedSpriteState.pivot.y - 0.25f) < 0.001f);
    assert(std::abs(component->GetEffectivePivot().x - (1.25f / 3.0f)) < 0.001f);
    HIKARI::WorldSpriteRenderProjection flippedProjection{};
    assert(component->ResolveRenderProjection({1.0f, 1.0f, 1.0f}, flippedProjection));
    assert(flippedProjection.rotated && flippedProjection.flipX);
    assert(std::abs(flippedProjection.pivot.x - (1.0f - rotatedProjection.pivot.x)) < 0.001f);
    assert(std::abs(flippedProjection.pivot.y - rotatedProjection.pivot.y) < 0.001f);
    assert(std::abs(flippedProjection.size.x - rotatedProjection.size.x) < 0.001f);
    assert(std::abs(flippedProjection.size.y - rotatedProjection.size.y) < 0.001f);

    assert(animation.Pause().Succeeded());
    assert(animation.Seek(0.25f).Succeeded());
    state = readState();
    assert(state.playbackState == HIKARI::GameProjectSpriteAnimationPlaybackState::Paused);
    assert(std::abs(state.elapsedSeconds - 0.25f) < 0.001f);
    assert(animation.Resume().Succeeded());
    assert(animation.Play("loop", false).Succeeded());
    assert(std::abs(readState().elapsedSeconds - 0.25f) < 0.001f);
    assert(animation.Seek(0.6f).Succeeded());
    state = readState();
    assert(state.frameIndex == 0u && std::abs(state.elapsedSeconds) < 0.001f);
    assert(animation.Restart().Succeeded());
    assert(readState().elapsedSeconds == 0.0f);

    assert(animation.Play("once").Succeeded());
    component->Update(0.35f);
    assert(component->ResolveRenderProjection({1.0f, 1.0f, 1.0f}, unrotatedProjection));
    assert(unrotatedProjection.rotated);
    component->Update(0.8f);
    state = readState();
    assert(state.playbackState == HIKARI::GameProjectSpriteAnimationPlaybackState::Finished);
    assert(state.frameIndex == 2u && state.elapsedSeconds == state.durationSeconds);
    assert(animation.Restart().Succeeded());
    assert(readState().playbackState == HIKARI::GameProjectSpriteAnimationPlaybackState::Playing);

    const HIKARI::GameProjectSpriteAnimationState beforeFailures = readState();
    assert(animation.Play("missing").code == HIKARI::GameProjectWorldResultCode::InvalidName);
    assert(animation.Bind("missing-asset-guid").code == HIKARI::GameProjectWorldResultCode::InvalidAsset);
    assert(animation.SetSpeed(0.0f).code == HIKARI::GameProjectWorldResultCode::InvalidRequest);
    assert(animation.Seek(-1.0f).code == HIKARI::GameProjectWorldResultCode::InvalidRequest);
    assertSameState(beforeFailures, readState());

    HIKARI::GameProjectWorldSpritePatch explicitGeometryPatch{};
    explicitGeometryPatch.fieldMask =
        HIKARI::GameProjectWorldSpritePatchFields(HIKARI::GameProjectWorldSpritePatchField::Size) |
        HIKARI::GameProjectWorldSpritePatchFields(HIKARI::GameProjectWorldSpritePatchField::Pivot);
    explicitGeometryPatch.size = {1.0f, 1.0f};
    explicitGeometryPatch.pivot = {0.5f, 0.5f};
    HIKARI::GameObject* explicitObject = world.CreateObject("ExplicitSpriteGeometry");
    assert(explicitObject != nullptr);
    HIKARI::WorldSpriteComponent* explicitComponent = explicitObject->AddComponent<HIKARI::WorldSpriteComponent>();
    assert(explicitComponent != nullptr);
    const HIKARI::ObjectView explicitView = HIKARI::GameProjectSdkAccess::MakeObjectView(*explicitObject);
    assert(explicitView.GetWorldSprite().ApplyPatch(explicitGeometryPatch).Succeeded());
    const HIKARI::SpriteAnimationView explicitAnimation = explicitView.GetSpriteAnimation();
    assert(explicitAnimation.Bind(spriteGuid.value).Succeeded());
    assert(explicitComponent->GetSize().x == 1.0f && explicitComponent->GetSize().y == 1.0f);
    assert(explicitComponent->GetPivot().x == 0.5f && explicitComponent->GetPivot().y == 0.5f);
    HIKARI::GameProjectWorldSpriteState explicitSpriteState{};
    assert(explicitView.GetWorldSprite().TryGetState(explicitSpriteState));
    assert(explicitSpriteState.pivot.x == 0.5f && explicitSpriteState.pivot.y == 0.5f);

    HIKARI::GameObject* unsupportedObject = world.CreateObject("NoSpriteComponent");
    assert(unsupportedObject != nullptr);
    const HIKARI::SpriteAnimationView unsupportedAnimation =
        HIKARI::GameProjectSdkAccess::MakeObjectView(*unsupportedObject).GetSpriteAnimation();
    assert(unsupportedAnimation.Bind(spriteGuid.value).code == HIKARI::GameProjectWorldResultCode::Unsupported);
    const HIKARI::SpriteMaterialView unsupportedMaterial =
        HIKARI::GameProjectSdkAccess::MakeObjectView(*unsupportedObject).GetSpriteMaterial();
    assert(unsupportedMaterial.BindAsset(materialGuid.value).code == HIKARI::GameProjectWorldResultCode::Unsupported);

    assert(material.BindAsset(materialGuid.value).Succeeded());
    component->SetCanvasSourceOnly(true);
    assert(animation.Pause().code == HIKARI::GameProjectWorldResultCode::Unsupported);
    assert(material.SetEnabled(false).code == HIKARI::GameProjectWorldResultCode::Unsupported);
    component->SetCanvasSourceOnly(false);
    assert(material.ClearBinding().Succeeded());
    assert(objectView.GetWorldSprite().SetTexture("static-texture-guid").Succeeded());
    assert(readState().playbackState == HIKARI::GameProjectSpriteAnimationPlaybackState::Unbound);

    HIKARI::GameProjectWorldResultCode wrongThreadResult = HIKARI::GameProjectWorldResultCode::Success;
    std::thread worker([&] { wrongThreadResult = animation.Pause().code; });
    worker.join();
    assert(wrongThreadResult == HIKARI::GameProjectWorldResultCode::WrongThread);
    assert(world.DestroyObject(object) == HIKARI::WorldMutationResult::Success);
    assert(animation.Pause().code == HIKARI::GameProjectWorldResultCode::InvalidObject);
    assert(material.TryGetState(stateCanary).code == HIKARI::GameProjectWorldResultCode::InvalidObject);
    assert(std::memcmp(&stateCanary, &savedStateCanary, sizeof(stateCanary)) == 0);

    {
        HIKARI::World noStoreWorld{};
        assert(noStoreWorld.BindComponentRegistry(&componentRegistry));
        HIKARI::GameObject* noStoreObject = noStoreWorld.CreateObject("NoSpriteStore");
        assert(noStoreObject != nullptr);
        assert(noStoreObject->AddComponent<HIKARI::WorldSpriteComponent>() != nullptr);
        const HIKARI::SpriteAnimationView noStoreAnimation =
            HIKARI::GameProjectSdkAccess::MakeObjectView(*noStoreObject).GetSpriteAnimation();
        assert(noStoreAnimation.Bind(spriteGuid.value).code == HIKARI::GameProjectWorldResultCode::Unsupported);
        const HIKARI::SpriteMaterialView noStoreMaterial =
            HIKARI::GameProjectSdkAccess::MakeObjectView(*noStoreObject).GetSpriteMaterial();
        assert(noStoreMaterial.BindAsset(materialGuid.value).code == HIKARI::GameProjectWorldResultCode::Unsupported);
    }

    HIKARI::SpriteAnimationView expiredWorldAnimation{};
    {
        HIKARI::World shortLivedWorld{};
        assert(shortLivedWorld.BindComponentRegistry(&componentRegistry));
        HIKARI::GameObject* shortLivedObject = shortLivedWorld.CreateObject("ShortLivedSprite");
        assert(shortLivedObject != nullptr);
        assert(shortLivedObject->AddComponent<HIKARI::WorldSpriteComponent>() != nullptr);
        expiredWorldAnimation = HIKARI::GameProjectSdkAccess::MakeObjectView(*shortLivedObject).GetSpriteAnimation();
    }
    assert(expiredWorldAnimation.Pause().code == HIKARI::GameProjectWorldResultCode::InvalidWorld);
}

} // namespace

void CheckKinematicMoveOptionsSdk() {
    using namespace HIKARI;
    PHYSICS::KinematicMotionService service;
    World world;
    world.Services().Register(service);
    RuntimeWorldContextScope current(world);
    const auto view = GameProjectSdkAccess::MakeWorldView(world).GetMotion();
    auto* actor = world.CreateObject("GravityApiActor");
    auto* ignored = world.CreateObject("GravityApiIgnored");
    GameProjectKinematicMove move{};
    move.objectHandle = actor->GetRuntimeHandle().ToValue();
    move.sourceId = 1u;
    move.fixedTickIndex = 1u;
    GameProjectKinematicMoveOptions options;
    options.collision.ignoredObjectHandle = ignored->GetRuntimeHandle().ToValue();
    options.overrideVerticalGravity = 1u;
    options.verticalGravityAcceleration = -24.f;
    assert(view.SubmitKinematicMoveWithOptions(move, options));
    PHYSICS::KinematicMotionRequest selected;
    assert(service.ConsumeMove(actor->GetRuntimeHandle(), 1u, selected));
    assert(selected.overrideVerticalGravity && selected.verticalGravityAcceleration == -24.f &&
           selected.ignoredCollisionObject == ignored->GetRuntimeHandle());
    for (int invalid = 0; invalid < 7; ++invalid) {
        auto bad = options;
        switch (invalid) {
        case 0: bad.structSize = 0; break;
        case 1: bad.version = 2; break;
        case 2: bad.overrideVerticalGravity = 2; break;
        case 3: bad.verticalGravityAcceleration = std::numeric_limits<float>::quiet_NaN(); break;
        case 4: bad.verticalGravityAcceleration = std::numeric_limits<float>::infinity(); break;
        case 5: bad.collision.reserved0 = 1; break;
        case 6: bad.collision.ignoredObjectHandle = move.objectHandle; break;
        }
        assert(!view.SubmitKinematicMoveWithOptions(move, bad));
    }
    assert(service.ConsumeMove(actor->GetRuntimeHandle(), 1u, selected) &&
           selected.verticalGravityAcceleration == -24.f);
    bool wrongThreadAccepted = true;
    std::thread worker([&] { wrongThreadAccepted = view.SubmitKinematicMoveWithOptions(move, options); });
    worker.join();
    assert(!wrongThreadAccepted);
    options.verticalGravityAcceleration = 0.f;
    assert(view.SubmitKinematicMoveWithOptions(move, options));
    assert(service.ConsumeMove(actor->GetRuntimeHandle(), 1u, selected) && selected.overrideVerticalGravity &&
           selected.verticalGravityAcceleration == 0.f);
    move.fixedTickIndex = 2u;
    assert(view.SubmitKinematicMoveWithCollisionOptions(move, options.collision));
    assert(service.ConsumeMove(actor->GetRuntimeHandle(), 2u, selected) && !selected.overrideVerticalGravity &&
           selected.ignoredCollisionObject == ignored->GetRuntimeHandle());
    move.fixedTickIndex = 3u;
    assert(view.SubmitKinematicMove(move));
    assert(service.ConsumeMove(actor->GetRuntimeHandle(), 3u, selected) && !selected.overrideVerticalGravity &&
           !selected.ignoredCollisionObject.IsValid());
    assert(!service.ConsumeMove(actor->GetRuntimeHandle(), 4u, selected) && !selected.overrideVerticalGravity);
    assert(world.DestroyObject(actor) == WorldMutationResult::Success);
    assert(!view.SubmitKinematicMoveWithOptions(move, options));
}

// 光源（State 4 / Patch 3）とデカールのビュー。値の丸めは作者値と同じ正規化に従い、光源の種類に合わない欄、
// 資産 GUID でない文字列、旧版の構造は拒否する。
void VerifyLocalRecordComponentSdk() {
    using Code = HIKARI::GameProjectWorldResultCode;
    using HIKARI::GameProjectLightPatchField;
    constexpr char kGuid[] = "0123456789abcdef0123456789abcdef";
    HIKARI::ComponentRegistry registry{};
    HIKARI::ComponentTypeInfo lightType{};
    lightType.typeName = "LightComponent";
    lightType.factory = []() -> std::unique_ptr<HIKARI::IComponent> {
        return std::make_unique<HIKARI::LightComponent>();
    };
    assert(registry.Register(std::move(lightType)));
    HIKARI::ComponentTypeInfo decalType{};
    decalType.typeName = "DecalComponent";
    decalType.factory = []() -> std::unique_ptr<HIKARI::IComponent> {
        return std::make_unique<HIKARI::DecalComponent>();
    };
    assert(registry.Register(std::move(decalType)));
    HIKARI::World world{};
    assert(world.BindComponentRegistry(&registry));

    HIKARI::GameObject* lamp = world.CreateObject("Lamp");
    HIKARI::LightComponent* light = lamp->AddComponent<HIKARI::LightComponent>();
    HIKARI::LightSettings settings{};
    settings.type = HIKARI::RENDER3D::LIGHTING::SceneLightType::Tube;
    light->SetSettings(settings);
    const HIKARI::LightView lightView = HIKARI::GameProjectSdkAccess::MakeObjectView(*lamp).GetLight();
    HIKARI::GameProjectLightState state{};
    assert(lightView.TryGetState(state) && state.type == HIKARI::GameProjectLightType::Tube && state.length == 1.0f &&
           state.iesProfileAssetGuid[0] == '\0');
    HIKARI::GameProjectLightState previous{};
    previous.structSize = 72u;
    previous.version = 3u;
    assert(!lightView.TryGetState(previous));
    HIKARI::GameProjectLightPatch patch{};
    patch.fieldMask = HIKARI::GameProjectLightPatchFields(GameProjectLightPatchField::AreaExtent);
    patch.length = 500.0f;
    patch.height = 2.0f;
    assert(lightView.ApplyPatch(patch).Succeeded());
    assert(lightView.TryGetState(state) && state.length == HIKARI::kMaximumAreaLightExtent && state.height == 2.0f);
    std::memcpy(patch.iesProfileAssetGuid, kGuid, sizeof(kGuid));
    patch.fieldMask = HIKARI::GameProjectLightPatchFields(GameProjectLightPatchField::IesProfile);
    assert(lightView.ApplyPatch(patch).code == Code::Unsupported);
    settings.type = HIKARI::RENDER3D::LIGHTING::SceneLightType::Spot;
    light->SetSettings(settings);
    assert(lightView.ApplyPatch(patch).Succeeded());
    patch.fieldMask = HIKARI::GameProjectLightPatchFields(GameProjectLightPatchField::AreaExtent);
    assert(lightView.ApplyPatch(patch).code == Code::Unsupported);
    std::memcpy(patch.cookieAssetGuid, "not-a-guid", sizeof("not-a-guid"));
    patch.fieldMask = HIKARI::GameProjectLightPatchFields(GameProjectLightPatchField::Cookie);
    assert(lightView.ApplyPatch(patch).code == Code::InvalidRequest);
    std::memcpy(patch.cookieAssetGuid, kGuid, sizeof(kGuid));
    assert(lightView.ApplyPatch(patch).Succeeded() && lightView.TryGetState(state) &&
           std::string_view(state.cookieAssetGuid) == kGuid && std::string_view(state.iesProfileAssetGuid) == kGuid);
    patch.structSize = 64u;
    patch.version = 2u;
    assert(lightView.ApplyPatch(patch).code == Code::InvalidRequest);

    HIKARI::GameObject* sticker = world.CreateObject("Sticker");
    sticker->AddComponent<HIKARI::DecalComponent>();
    const HIKARI::DecalView decalView = HIKARI::GameProjectSdkAccess::MakeObjectView(*sticker).GetDecal();
    HIKARI::GameProjectDecalState missing{};
    assert(!HIKARI::GameProjectSdkAccess::MakeObjectView(*lamp).GetDecal().TryGetState(missing));
    HIKARI::GameProjectDecalState decal{};
    assert(decalView.TryGetState(decal) && decal.size.z == 0.5f && decal.baseColorTextureAssetGuid[0] == '\0');
    HIKARI::GameProjectDecalPatch decalPatch{};
    decalPatch.fieldMask = HIKARI::GameProjectDecalPatchFields(HIKARI::GameProjectDecalPatchField::Size |
                                                               HIKARI::GameProjectDecalPatchField::Weights |
                                                               HIKARI::GameProjectDecalPatchField::BaseColorTexture);
    decalPatch.size = {2.0f, 0.0f, 3.0f};
    decalPatch.colorWeight = 0.0f;
    decalPatch.normalWeight = 4.0f;
    decalPatch.materialWeight = 1.0f;
    std::memcpy(decalPatch.baseColorTextureAssetGuid, kGuid, sizeof(kGuid));
    assert(decalView.ApplyPatch(decalPatch).Succeeded() && decalView.TryGetState(decal));
    assert(decal.size.x == 2.0f && decal.size.y == 0.01f && decal.normalWeight == 1.0f &&
           decal.materialWeight == 1.0f && std::string_view(decal.baseColorTextureAssetGuid) == kGuid);
    decalPatch.reserved1[0] = 1u;
    assert(decalView.ApplyPatch(decalPatch).code == Code::InvalidRequest);
}

void VerifyPropertyFieldChoice() {
    enum class Mode { Loop, PingPong, Once, Unlisted };
    static constexpr HIKARI::GameProjectEnumName<Mode> kModes[] = {
        {Mode::Loop, "loop"}, {Mode::PingPong, "pingPong"}, {Mode::Once, "once"}};
    const auto fields = [](HIKARI::PropertyFieldsMode mode, nlohmann::json& archive) {
        return HIKARI::GameProjectSdkAccess::MakePropertyFields(mode, &archive, &archive, nullptr);
    };
    using HIKARI::PropertyFieldsMode;
    nlohmann::json archive = nlohmann::json::object();
    Mode mode = Mode::Once;
    fields(PropertyFieldsMode::Save, archive).Choice("mode", "Mode", mode, kModes);
    assert(archive.at("mode") == "once");
    Mode loaded = Mode::Loop;
    assert(fields(PropertyFieldsMode::Load, archive).Choice("mode", "Mode", loaded, kModes) && loaded == Mode::Once);
    // 保存値は名前なので、並べ替えた表でも同じ選択肢を読む。知らない名前は現在値を保つ。
    constexpr std::string_view reordered[] = {"once", "loop", "pingPong"};
    int index = 1;
    assert(fields(PropertyFieldsMode::Load, archive).Choice("mode", "Mode", index, reordered) && index == 0);
    archive["mode"] = "sprint";
    assert(!fields(PropertyFieldsMode::Load, archive).Choice("mode", "Mode", loaded, kModes) && loaded == Mode::Once);
    // 表に無い現在値と、名前が重複した表は保存しない。
    nlohmann::json untouched = nlohmann::json::object();
    Mode unlisted = Mode::Unlisted;
    fields(PropertyFieldsMode::Save, untouched).Choice("mode", "Mode", unlisted, kModes);
    constexpr std::string_view duplicated[] = {"loop", "loop"};
    index = 0;
    fields(PropertyFieldsMode::Save, untouched).Choice("other", "Other", index, duplicated);
    assert(untouched.empty());
}

int main() {
    // 保護だけなら従来の後貼り、時系列解決の保持を加えると保護の印を残したまま後貼りだけを外す。
    {
        using Flag = HIKARI::GameProjectSceneCaptureMaterialBindingFlag;
        const auto bit = HIKARI::GameProjectSceneCaptureMaterialBindingFlags;
        HIKARI::SCENE_CAPTURE::SceneCaptureMaterialBinding binding{};
        assert(!binding.ShouldCompositeAfterTemporal());
        binding.flags = bit(Flag::ProtectReadability);
        assert(binding.ShouldCompositeAfterTemporal());
        for (const auto excluded : {Flag::PreserveTemporalResolve, Flag::AlphaBlend, Flag::AlphaMask}) {
            binding.flags = bit(Flag::ProtectReadability) | bit(excluded);
            assert((binding.flags & ~HIKARI::kGameProjectSceneCaptureMaterialBindingKnownFlags) == 0u);
            assert((binding.flags & bit(Flag::ProtectReadability)) != 0u);
            assert(!binding.ShouldCompositeAfterTemporal());
        }
        binding.flags = bit(Flag::ProtectReadability) | bit(Flag::FlipTextureX) | bit(Flag::Unstyled);
        assert(binding.ShouldCompositeAfterTemporal());
    }
    CheckKinematicMoveOptionsSdk();
    VerifyLocalRecordComponentSdk();
    VerifyPropertyFieldChoice();
    VerifySceneObjectReferences();
    VerifyPrefabRuntimeTransactions();
    VerifySpriteAnimationSdk();
    HIKARI::ComponentRegistry componentRegistry{};
    HIKARI::ComponentTypeInfo renderDirtyType{};
    renderDirtyType.typeName = "Test.DirtyOnDetachComponent";
    renderDirtyType.factory = []() -> std::unique_ptr<HIKARI::IComponent> {
        return std::make_unique<DirtyOnDetachComponent>();
    };
    renderDirtyType.affectsRenderState = true;
    assert(componentRegistry.Register(std::move(renderDirtyType)));
    HIKARI::ComponentTypeInfo modelType{};
    modelType.typeName = "ModelComponent";
    modelType.factory = []() -> std::unique_ptr<HIKARI::IComponent> {
        return std::make_unique<HIKARI::ModelComponent>();
    };
    modelType.affectsRenderState = true;
    assert(componentRegistry.Register(std::move(modelType)));
    HIKARI::ComponentTypeInfo worldSpriteType{};
    worldSpriteType.typeName = "WorldSpriteComponent";
    worldSpriteType.factory = []() -> std::unique_ptr<HIKARI::IComponent> {
        return std::make_unique<HIKARI::WorldSpriteComponent>();
    };
    worldSpriteType.affectsRenderState = true;
    assert(componentRegistry.Register(std::move(worldSpriteType)));
    HIKARI::ComponentTypeInfo materialFxType{};
    materialFxType.typeName = "MaterialFxComponent";
    materialFxType.factory = []() -> std::unique_ptr<HIKARI::IComponent> {
        return std::make_unique<HIKARI::MaterialFxComponent>();
    };
    materialFxType.affectsRenderState = true;
    assert(componentRegistry.Register(std::move(materialFxType)));
    HIKARI::ComponentTypeInfo closingPatchProbeType{};
    closingPatchProbeType.typeName = "Test.ClosingWorldSpritePatchProbe";
    closingPatchProbeType.factory = []() -> std::unique_ptr<HIKARI::IComponent> {
        return std::make_unique<ClosingWorldSpritePatchProbe>();
    };
    assert(componentRegistry.Register(std::move(closingPatchProbeType)));
    HIKARI::World world{};
    assert(world.BindComponentRegistry(&componentRegistry));
    HIKARI::WorldView worldView = HIKARI::GameProjectSdkAccess::MakeWorldView(world);
    const HIKARI::World::LiveObjectRange retainedRange = world.GetObjects();
    size_t crossThreadRangeCount = 1u;
    bool retainedIteratorConverged = false;
    auto retainedIterator = retainedRange.begin();
    std::thread rangeWorker(
        [retainedRange, retainedIterator, &crossThreadRangeCount, &retainedIteratorConverged]() mutable {
            crossThreadRangeCount = 0u;
            for (const auto& object : retainedRange) {
                crossThreadRangeCount += object != nullptr ? 1u : 0u;
            }
            assert(retainedRange.size() == 0u);
            ++retainedIterator;
            retainedIteratorConverged = retainedIterator == retainedRange.end();
        });
    rangeWorker.join();
    assert(crossThreadRangeCount == 0u);
    assert(retainedIteratorConverged);

    HIKARI::GameObject* runtimeSpriteObject = world.CreateObject("RuntimeWorldSpritePatch");
    assert(runtimeSpriteObject != nullptr);
    HIKARI::WorldSpriteComponent* runtimeSprite = runtimeSpriteObject->AddComponent<HIKARI::WorldSpriteComponent>();
    assert(runtimeSprite != nullptr);
    const HIKARI::ObjectView runtimeSpriteView = HIKARI::GameProjectSdkAccess::MakeObjectView(*runtimeSpriteObject);

    HIKARI::GameObject* runtimeMaterialFxObject = world.CreateObject("RuntimeMaterialFxView");
    assert(runtimeMaterialFxObject != nullptr);
    HIKARI::MaterialFxComponent* runtimeMaterialFx =
        runtimeMaterialFxObject->AddComponent<HIKARI::MaterialFxComponent>();
    assert(runtimeMaterialFx != nullptr);
    const HIKARI::ObjectView runtimeMaterialFxObjectView =
        HIKARI::GameProjectSdkAccess::MakeObjectView(*runtimeMaterialFxObject);
    const HIKARI::MaterialFxView runtimeMaterialFxView = runtimeMaterialFxObjectView.GetMaterialFx();
    assert(runtimeMaterialFxView.IsValid());
    // 疎な文書 ID や配列の容量に依存せず、実在する物体だけを返す。
    std::array<HIKARI::GameProjectObjectHandle, 3> handles{};
    assert(worldView.CopyObjectHandles(handles) == 2u);
    assert(handles[0] == runtimeSpriteView.GetHandle() && handles[1] == runtimeMaterialFxObjectView.GetHandle());
    assert(worldView.CopyObjectHandles(std::span(handles).first(1)) == 1u);
    assert(worldView.CopyObjectHandles({}) == 0u && HIKARI::WorldView{}.CopyObjectHandles(handles) == 0u);
    std::thread snapshotWorker([&] { assert(worldView.CopyObjectHandles(handles) == 0u); });
    snapshotWorker.join();
    world.AcknowledgeRenderDirtyObjects();

    const uint64_t initialSpriteRevision = runtimeSprite->GetStateRevision();
    const uint64_t initialSpriteObjectRevision = runtimeSpriteObject->GetRenderStateRevision();
    HIKARI::GameProjectWorldSpritePatch invalidSpritePatch{};
    invalidSpritePatch.fieldMask = 0u;
    assert(runtimeSpriteView.GetWorldSprite().ApplyPatch(invalidSpritePatch).code ==
           HIKARI::GameProjectWorldResultCode::InvalidRequest);
    invalidSpritePatch.fieldMask =
        HIKARI::GameProjectWorldSpritePatchFields(HIKARI::GameProjectWorldSpritePatchField::Visible);
    invalidSpritePatch.visible = 2u;
    assert(runtimeSpriteView.GetWorldSprite().ApplyPatch(invalidSpritePatch).code ==
           HIKARI::GameProjectWorldResultCode::InvalidRequest);
    invalidSpritePatch.visible = 0u;
    invalidSpritePatch.reserved0 = 1u;
    assert(runtimeSpriteView.GetWorldSprite().ApplyPatch(invalidSpritePatch).code ==
           HIKARI::GameProjectWorldResultCode::InvalidRequest);
    invalidSpritePatch.reserved0 = 0u;
    invalidSpritePatch.reserved1[0] = 1u;
    assert(runtimeSpriteView.GetWorldSprite().ApplyPatch(invalidSpritePatch).code ==
           HIKARI::GameProjectWorldResultCode::InvalidRequest);
    invalidSpritePatch.reserved1[0] = 0u;
    invalidSpritePatch.fieldMask =
        HIKARI::GameProjectWorldSpritePatchFields(HIKARI::GameProjectWorldSpritePatchField::Size);
    invalidSpritePatch.size.x = (std::numeric_limits<float>::quiet_NaN)();
    assert(runtimeSpriteView.GetWorldSprite().ApplyPatch(invalidSpritePatch).code ==
           HIKARI::GameProjectWorldResultCode::InvalidRequest);
    assert(runtimeSprite->GetStateRevision() == initialSpriteRevision);
    assert(runtimeSpriteObject->GetRenderStateRevision() == initialSpriteObjectRevision);
    assert(world.GetRenderDirtyObjects().empty());

    HIKARI::GameProjectWorldSpritePatch supportedSpritePatch{};
    supportedSpritePatch.fieldMask =
        HIKARI::GameProjectWorldSpritePatchFields(HIKARI::GameProjectWorldSpritePatchField::Visible) |
        HIKARI::GameProjectWorldSpritePatchFields(HIKARI::GameProjectWorldSpritePatchField::Size) |
        HIKARI::GameProjectWorldSpritePatchFields(HIKARI::GameProjectWorldSpritePatchField::PreserveAspect) |
        HIKARI::GameProjectWorldSpritePatchFields(HIKARI::GameProjectWorldSpritePatchField::Facing) |
        HIKARI::GameProjectWorldSpritePatchFields(HIKARI::GameProjectWorldSpritePatchField::Tint) |
        HIKARI::GameProjectWorldSpritePatchFields(HIKARI::GameProjectWorldSpritePatchField::Opacity) |
        HIKARI::GameProjectWorldSpritePatchFields(HIKARI::GameProjectWorldSpritePatchField::AlphaMode) |
        HIKARI::GameProjectWorldSpritePatchFields(HIKARI::GameProjectWorldSpritePatchField::AlphaCutoff) |
        HIKARI::GameProjectWorldSpritePatchFields(HIKARI::GameProjectWorldSpritePatchField::TransparentSortPriority);
    supportedSpritePatch.visible = 0u;
    supportedSpritePatch.size = {0.0f, 200000.0f};
    supportedSpritePatch.preserveAspect = 0u;
    supportedSpritePatch.facing = HIKARI::GameProjectWorldSpriteFacingMode::FixedYAxis;
    supportedSpritePatch.tint = {-4.0f, 2.0f, 20.0f};
    supportedSpritePatch.opacity = 0.4f;
    supportedSpritePatch.pivot.x = (std::numeric_limits<float>::quiet_NaN)();
    supportedSpritePatch.alphaMode = HIKARI::GameProjectWorldSpriteAlphaMode::Mask;
    supportedSpritePatch.alphaCutoff = 0.25f;
    supportedSpritePatch.transparentSortPriority = 50000;
    assert(runtimeSpriteView.GetWorldSprite().ApplyPatch(supportedSpritePatch).Succeeded());
    assert(runtimeSprite->GetStateRevision() == initialSpriteRevision + 1u);
    assert(runtimeSpriteObject->GetRenderStateRevision() == initialSpriteObjectRevision + 1u);
    assert(world.GetRenderDirtyObjects().size() == 1u);
    HIKARI::GameProjectWorldSpriteState spriteState{};
    assert(runtimeSpriteView.GetWorldSprite().TryGetState(spriteState));
    assert(spriteState.stateRevision == initialSpriteRevision + 1u);
    assert(spriteState.visible == 0u);
    assert(spriteState.preserveAspect == 0u);
    assert(spriteState.size.x == 0.001f);
    assert(spriteState.size.y == 100000.0f);
    assert(spriteState.tint.x == 0.0f);
    assert(spriteState.tint.y == 2.0f);
    assert(spriteState.tint.z == 16.0f);
    assert(spriteState.opacity == 0.4f);
    assert(spriteState.pivot.x == 0.5f);
    assert(spriteState.pivot.y == 0.5f);
    assert(spriteState.alphaCutoff == 0.25f);
    assert(spriteState.transparentSortPriority == 32767);
    assert(spriteState.facing == HIKARI::GameProjectWorldSpriteFacingMode::FixedYAxis);
    assert(spriteState.alphaMode == HIKARI::GameProjectWorldSpriteAlphaMode::Mask);
    world.AcknowledgeRenderDirtyObjects();
    assert(runtimeSpriteView.GetWorldSprite().ApplyPatch(supportedSpritePatch).Succeeded());
    assert(runtimeSprite->GetStateRevision() == initialSpriteRevision + 1u);
    assert(runtimeSpriteObject->GetRenderStateRevision() == initialSpriteObjectRevision + 1u);
    assert(world.GetRenderDirtyObjects().empty());

    HIKARI::GameProjectWorldSpritePatch invalidCombinedFlipPatch{};
    invalidCombinedFlipPatch.fieldMask =
        HIKARI::GameProjectWorldSpritePatchFields(HIKARI::GameProjectWorldSpritePatchField::FlipX) |
        HIKARI::GameProjectWorldSpritePatchFields(HIKARI::GameProjectWorldSpritePatchField::Visible);
    invalidCombinedFlipPatch.flipX = 2u;
    invalidCombinedFlipPatch.visible = 1u;
    const uint64_t revisionBeforeInvalidFlip = runtimeSprite->GetStateRevision();
    const uint64_t objectRevisionBeforeInvalidFlip = runtimeSpriteObject->GetRenderStateRevision();
    assert(runtimeSpriteView.GetWorldSprite().ApplyPatch(invalidCombinedFlipPatch).code ==
           HIKARI::GameProjectWorldResultCode::InvalidRequest);
    assert(!runtimeSprite->GetFlipX() && !runtimeSprite->IsVisible());
    assert(runtimeSprite->GetStateRevision() == revisionBeforeInvalidFlip);
    assert(runtimeSpriteObject->GetRenderStateRevision() == objectRevisionBeforeInvalidFlip);
    assert(world.GetRenderDirtyObjects().empty());

    HIKARI::GameProjectWorldSpritePatch missingComponentPatch{};
    missingComponentPatch.fieldMask =
        HIKARI::GameProjectWorldSpritePatchFields(HIKARI::GameProjectWorldSpritePatchField::Visible);
    missingComponentPatch.visible = 0u;
    assert(runtimeMaterialFxObjectView.GetWorldSprite().ApplyPatch(missingComponentPatch).code ==
           HIKARI::GameProjectWorldResultCode::Unsupported);

    const uint64_t initialMaterialObjectRevision = runtimeMaterialFxObject->GetRenderStateRevision();
    char emptyProfileId[1] = {'x'};
    assert(runtimeMaterialFxView.GetProfileIdSize() == 0u);
    assert(runtimeMaterialFxView.CopyProfileId(emptyProfileId, std::size(emptyProfileId)));
    assert(emptyProfileId[0] == '\0');
    assert(runtimeMaterialFxView.ClearProfile());
    assert(!runtimeMaterialFxView.SetFloat("missing", (std::numeric_limits<float>::quiet_NaN)()));
    assert(!runtimeMaterialFxView.SetToggle("missing", 2u));
    DirectX::XMFLOAT4 rawMaterialValues[HIKARI::VFX::kMaterialFxUserCount]{};
    assert(runtimeMaterialFx->SetProfileId({}));
    runtimeMaterialFx->SetParamValues(rawMaterialValues, false);
    assert(runtimeMaterialFxObject->GetRenderStateRevision() == initialMaterialObjectRevision);
    assert(world.GetRenderDirtyObjects().empty());
    rawMaterialValues[0].x = 0.25f;
    runtimeMaterialFx->SetParamValues(rawMaterialValues, true);
    assert(runtimeMaterialFxObject->GetRenderStateRevision() == initialMaterialObjectRevision + 1u);
    assert(world.GetRenderDirtyObjects().size() == 1u);
    world.AcknowledgeRenderDirtyObjects();
    runtimeMaterialFx->SetParamValues(rawMaterialValues, true);
    assert(runtimeMaterialFxObject->GetRenderStateRevision() == initialMaterialObjectRevision + 1u);
    assert(world.GetRenderDirtyObjects().empty());

    HIKARI::MaterialFxProfile::ClearCache();
    const uint64_t materialRevisionBeforeDeserialize = runtimeMaterialFxObject->GetRenderStateRevision();
    runtimeMaterialFx->Deserialize(
        nlohmann::json{{"profileId", "hikari-test-persisted-missing-profile"},
                       {"valuesInitialized", true},
                       {"paramValues", nlohmann::json::array({nlohmann::json::array({0.75f, 0.5f, 0.25f, 1.0f})})}});
    const HIKARI::MaterialFxProfileCacheStats cacheStatsAfterDeserialize = HIKARI::MaterialFxProfile::GetCacheStats();
    assert(cacheStatsAfterDeserialize.hitCount == 0u);
    assert(cacheStatsAfterDeserialize.missCount == 0u);
    assert(cacheStatsAfterDeserialize.failCount == 0u);
    assert(runtimeMaterialFx->GetProfileId() == "hikari-test-persisted-missing-profile");
    assert(runtimeMaterialFx->AreValuesInitialized());
    assert(runtimeMaterialFx->GetParamValues()[0].x == 0.75f);
    assert(runtimeMaterialFxObject->GetRenderStateRevision() == materialRevisionBeforeDeserialize + 1u);
    assert(world.GetRenderDirtyObjects().size() == 1u);
    world.AcknowledgeRenderDirtyObjects();

    HIKARI::MaterialFxProfile::ClearCache();
    const uint64_t spriteRevisionBeforeWrongThread = runtimeSprite->GetStateRevision();
    bool wrongThreadSpritePatchRejected = false;
    bool wrongThreadMaterialProfileRejected = false;
    std::thread runtimeViewWorker([&]() {
        wrongThreadSpritePatchRejected = runtimeSpriteView.GetWorldSprite().ApplyPatch(missingComponentPatch).code ==
                                         HIKARI::GameProjectWorldResultCode::WrongThread;
        wrongThreadMaterialProfileRejected = !runtimeMaterialFxView.SetProfileId("hikari-test-missing-profile");
    });
    runtimeViewWorker.join();
    assert(wrongThreadSpritePatchRejected);
    assert(wrongThreadMaterialProfileRejected);
    assert(runtimeSprite->GetStateRevision() == spriteRevisionBeforeWrongThread);
    const HIKARI::MaterialFxProfileCacheStats cacheStatsAfterWrongThread = HIKARI::MaterialFxProfile::GetCacheStats();
    assert(cacheStatsAfterWrongThread.hitCount == 0u);
    assert(cacheStatsAfterWrongThread.missCount == 0u);
    assert(cacheStatsAfterWrongThread.failCount == 0u);
    assert(world.GetRenderDirtyObjects().empty());
    assert(world.DestroyObject(runtimeSpriteObject) == HIKARI::WorldMutationResult::Success);
    assert(world.DestroyObject(runtimeMaterialFxObject) == HIKARI::WorldMutationResult::Success);
    assert(!runtimeMaterialFxView.IsValid());
    world.AcknowledgeRenderDirtyObjects();

    HIKARI::GameProjectSpawnObjectDesc parentDesc{};
    std::snprintf(parentDesc.name, HIKARI::kGameProjectObjectNameCapacity, "%s", "CodeOnlyParent");
    parentDesc.localTransform.position.x = 10.0f;
    parentDesc.localTransform.rotation.z = 0.70710677f;
    parentDesc.localTransform.rotation.w = 0.70710677f;
    parentDesc.localTransform.scale = {2.0f, 3.0f, 4.0f};
    const HIKARI::GameProjectWorldResult parentSpawn = worldView.SpawnObject(parentDesc);
    assert(parentSpawn.Succeeded());
    HIKARI::ObjectView parentView = worldView.FindObject(parentSpawn.object);
    assert(parentView.IsValid());

    HIKARI::GameProjectSpawnObjectDesc childDesc{};
    std::snprintf(childDesc.name, HIKARI::kGameProjectObjectNameCapacity, "%s", "CodeOnlyChild");
    childDesc.parent = parentSpawn.object;
    childDesc.localTransform.position.x = 2.0f;
    const HIKARI::GameProjectWorldResult childSpawn = worldView.SpawnObject(childDesc);
    assert(childSpawn.Succeeded());
    HIKARI::ObjectView childView = worldView.FindObject(childSpawn.object);
    assert(childView.IsValid());
    assert(childView.GetParent().GetHandle() == parentSpawn.object);

    HIKARI::GameProjectTransform childWorld{};
    assert(childView.GetWorldTransform(childWorld));
    assert(std::abs(childWorld.position.x - 10.0f) < 1.0e-4f);
    assert(std::abs(childWorld.position.y - 4.0f) < 1.0e-4f);
    assert(worldView.SetParent(childSpawn.object, {}, HIKARI::GameProjectParentMode::KeepWorld).Succeeded());
    HIKARI::GameProjectTransform childLocal{};
    assert(childView.GetLocalTransform(childLocal));
    assert(std::abs(childLocal.position.x - childWorld.position.x) < 1.0e-4f);
    assert(std::abs(childLocal.position.y - childWorld.position.y) < 1.0e-4f);
    assert(std::abs(childLocal.position.z - childWorld.position.z) < 1.0e-4f);
    assert(std::abs(childLocal.scale.x - childWorld.scale.x) < 1.0e-4f);
    assert(std::abs(childLocal.scale.y - childWorld.scale.y) < 1.0e-4f);
    assert(std::abs(childLocal.scale.z - childWorld.scale.z) < 1.0e-4f);
    const float rotationDot =
        std::abs(childLocal.rotation.x * childWorld.rotation.x + childLocal.rotation.y * childWorld.rotation.y +
                 childLocal.rotation.z * childWorld.rotation.z + childLocal.rotation.w * childWorld.rotation.w);
    assert(std::abs(rotationDot - 1.0f) < 1.0e-4f);
    assert(!childView.GetParent().IsValid());
    assert(worldView.SetParent(childSpawn.object, parentSpawn.object, static_cast<HIKARI::GameProjectParentMode>(255u))
               .code == HIKARI::GameProjectWorldResultCode::InvalidRequest);
    assert(!childView.GetParent().IsValid());

    const HIKARI::GameProjectObjectHandle staleChild = childSpawn.object;
    assert(worldView.DestroyObject(childSpawn.object).Succeeded());
    assert(!childView.IsValid());
    assert(!worldView.FindObject(staleChild).IsValid());

    HIKARI::GameProjectSpawnObjectDesc replacementDesc{};
    std::snprintf(replacementDesc.name, HIKARI::kGameProjectObjectNameCapacity, "%s", "GenerationReplacement");
    const HIKARI::GameProjectWorldResult replacementSpawn = worldView.SpawnObject(replacementDesc);
    assert(replacementSpawn.Succeeded());
    assert(replacementSpawn.object != staleChild);
    assert(worldView.FindObject(replacementSpawn.object).IsValid());
    assert(!worldView.FindObject(staleChild).IsValid());
    assert(worldView.DestroyObject(replacementSpawn.object).Succeeded());
    assert(worldView.DestroyObject(parentSpawn.object).Succeeded());

    HIKARI::GameObject* modelObject = world.CreateObject("RenderTrackedModel");
    assert(modelObject != nullptr);
    HIKARI::ModelComponent* modelComponent = modelObject->AddComponent<HIKARI::ModelComponent>();
    assert(modelComponent != nullptr);
    assert(world.GetRenderDirtyObjects().size() == 1u);
    assert(world.GetRenderDirtyObjects().capacity() >= 1u);
    world.AcknowledgeRenderDirtyObjects();
    const HIKARI::ObjectView modelView = HIKARI::GameProjectSdkAccess::MakeObjectView(*modelObject);
    modelComponent->Deserialize({{"postGroupMask", 4u}});
    world.AcknowledgeRenderDirtyObjects();
    assert(modelView.GetModel().SetPostGroupMask(1u).Succeeded() && modelComponent->GetPostGroupMask() == 1u);
    assert(world.GetRenderDirtyObjects().size() == 1u);
    nlohmann::json authored;
    modelComponent->Serialize(authored);
    assert(authored["postGroupMask"] == 4u);
    world.AcknowledgeRenderDirtyObjects();
    assert(!modelView.GetModel().SetPostGroupMask(0x80000001u).Succeeded() && modelComponent->GetPostGroupMask() == 1u);
    assert(world.GetRenderDirtyObjects().empty());
    assert(modelView.GetModel().SetPostGroupMask(0u).Succeeded() && modelComponent->GetPostGroupMask() == 0u);
    assert(modelView.GetModel().ClearPostGroupMask().Succeeded() && modelComponent->GetPostGroupMask() == 4u);
    bool foreignThreadAccepted = true;
    std::thread foreignThread([&] { foreignThreadAccepted = modelView.GetModel().SetPostGroupMask(2u).Succeeded(); });
    foreignThread.join();
    assert(!foreignThreadAccepted && modelComponent->GetPostGroupMask() == 4u);
    assert(!HIKARI::ObjectView{}.GetModel().SetPostGroupMask(1u).Succeeded());
    assert(!HIKARI::ObjectView{}.GetModel().ClearPostGroupMask().Succeeded());
    world.AcknowledgeRenderDirtyObjects();
    assert(modelView.GetModel()
               .SetNodeVisibility("Digit7", false, HIKARI::GameProjectModelNodeVisibilityScope::NodeOnly)
               .Succeeded());
    assert(modelComponent->GetRuntimeNodeVisibilityOverrides().size() == 1u);
    assert(!modelComponent->GetRuntimeNodeVisibilityOverrides().front().visible);
    assert(!modelComponent->GetRuntimeNodeVisibilityOverrides().front().includeDescendants);
    assert(world.GetRenderDirtyObjects().size() == 1u);
    world.AcknowledgeRenderDirtyObjects();
    assert(modelView.GetModel()
               .SetNodeVisibility("Digit7", false, HIKARI::GameProjectModelNodeVisibilityScope::NodeOnly)
               .Succeeded());
    assert(world.GetRenderDirtyObjects().empty());
    assert(!modelView.GetModel()
                .SetNodeVisibility("Digit7", false, HIKARI::GameProjectModelNodeVisibilityScope::Count)
                .Succeeded());

    HIKARI::ModelAsset nodeVisibilityModel{};
    nodeVisibilityModel.SetName("NodeVisibilityModel");
    nodeVisibilityModel.nodes.resize(4u);
    nodeVisibilityModel.nodes[0].name = "Digit7";
    nodeVisibilityModel.nodes[1].name = "DigitChild";
    nodeVisibilityModel.nodes[1].parent = 0;
    nodeVisibilityModel.nodes[2].name = "DigitGrandchild";
    nodeVisibilityModel.nodes[2].parent = 1;
    nodeVisibilityModel.nodes[3].name = "Other";
    modelComponent->SetModelAsset(&nodeVisibilityModel);
    assert(modelComponent->GetRuntimeNodeVisibilityOverrides().size() == 1u);
    std::vector<uint8_t> nodeVisibilityMask = modelComponent->ResolveRuntimeNodeVisibilityMask(nodeVisibilityModel);
    assert((nodeVisibilityMask == std::vector<uint8_t>{0u, 1u, 1u, 1u}));
    assert(world.GetRenderDirtyObjects().size() == 1u);
    world.AcknowledgeRenderDirtyObjects();

    assert(modelView.GetModel()
               .SetNodeVisibility("Digit7", false, HIKARI::GameProjectModelNodeVisibilityScope::NodeAndDescendants)
               .Succeeded());
    nodeVisibilityMask = modelComponent->ResolveRuntimeNodeVisibilityMask(nodeVisibilityModel);
    assert((nodeVisibilityMask == std::vector<uint8_t>{0u, 0u, 0u, 1u}));
    world.AcknowledgeRenderDirtyObjects();

    assert(modelView.GetModel()
               .SetNodeVisibility("DigitChild", true, HIKARI::GameProjectModelNodeVisibilityScope::NodeOnly)
               .Succeeded());
    nodeVisibilityMask = modelComponent->ResolveRuntimeNodeVisibilityMask(nodeVisibilityModel);
    assert((nodeVisibilityMask == std::vector<uint8_t>{0u, 1u, 0u, 1u}));
    world.AcknowledgeRenderDirtyObjects();

    assert(modelView.GetModel()
               .SetNodeVisibility("DigitChild", true, HIKARI::GameProjectModelNodeVisibilityScope::NodeAndDescendants)
               .Succeeded());
    nodeVisibilityMask = modelComponent->ResolveRuntimeNodeVisibilityMask(nodeVisibilityModel);
    assert((nodeVisibilityMask == std::vector<uint8_t>{0u, 1u, 1u, 1u}));
    world.AcknowledgeRenderDirtyObjects();

    assert(modelView.GetModel().ClearNodeVisibility("UnknownNode").Succeeded());
    assert(world.GetRenderDirtyObjects().empty());
    assert(modelView.GetModel().ClearNodeVisibility("Digit7").Succeeded());
    assert(modelComponent->GetRuntimeNodeVisibilityOverrides().size() == 1u);
    assert(world.GetRenderDirtyObjects().size() == 1u);
    world.AcknowledgeRenderDirtyObjects();
    assert(modelView.GetModel().ClearAllNodeVisibility().Succeeded());
    assert(modelComponent->GetRuntimeNodeVisibilityOverrides().empty());
    assert(world.GetRenderDirtyObjects().size() == 1u);
    world.AcknowledgeRenderDirtyObjects();
    assert(modelView.GetModel().ClearAllNodeVisibility().Succeeded());
    assert(world.GetRenderDirtyObjects().empty());
    HIKARI::GameProjectWorldTransformWrite modelWrite{};
    modelWrite.object = modelView.GetHandle();
    modelWrite.localTransform.position = {1.0f, 2.0f, 3.0f};
    HIKARI::GameProjectWorldResult modelWriteResult{};
    assert(worldView.SetLocalTransforms({&modelWrite, 1u}, {&modelWriteResult, 1u}));
    assert(modelWriteResult.Succeeded());
    assert(world.GetRenderDirtyObjects().size() == 1u);
    const uint64_t modelStableId = modelObject->GetRenderStableId();
    assert(world.DestroyObject(modelObject) == HIKARI::WorldMutationResult::Success);
    assert(world.GetRenderDirtyObjects().empty());
    assert(world.GetRemovedRenderObjectIds().size() == 1u);
    assert(world.GetRemovedRenderObjectIds().front() == modelStableId);
    world.AcknowledgeRenderDirtyObjects();

    HIKARI::GameObject* batchFirst = world.CreateObject("BatchTransformFirst");
    HIKARI::GameObject* batchSecond = world.CreateObject("BatchTransformSecond");
    assert(batchFirst != nullptr && batchSecond != nullptr);
    std::array<HIKARI::GameProjectWorldTransformWrite, 2u> writes{};
    std::array<HIKARI::GameProjectWorldResult, 2u> writeResults{};
    writes[0].object = HIKARI::GameProjectSdkAccess::MakeObjectView(*batchFirst).GetHandle();
    writes[1].object = HIKARI::GameProjectSdkAccess::MakeObjectView(*batchSecond).GetHandle();
    writes[0].localTransform.position.x = 3.0f;
    writes[1].localTransform.position.x = 4.0f;
    assert(worldView.SetLocalTransforms({writes.data(), 2u}, {writeResults.data(), 2u}));
    assert(writeResults[0].Succeeded() && writeResults[1].Succeeded());
    assert(world.GetRenderDirtyObjects().empty());
    writes[1].object = writes[0].object;
    writes[0].localTransform.position.x = 5.0f;
    writes[1].localTransform.position.x = 6.0f;
    assert(worldView.SetLocalTransforms({writes.data(), 2u}, {writeResults.data(), 2u}));
    HIKARI::GameProjectTransform lastWrite{};
    assert(worldView.FindObject(writes[0].object).GetLocalTransform(lastWrite));
    assert(lastWrite.position.x == 6.0f);
    writes[1].version = 2u;
    assert(!worldView.SetLocalTransforms({writes.data(), 2u}, {writeResults.data(), 2u}));
    assert(writeResults[0].Succeeded());
    assert(writeResults[1].code == HIKARI::GameProjectWorldResultCode::Unsupported);
    writes[1].version = HIKARI::kGameProjectWorldTransformWriteVersion;
    writes[1].reserved[0] = 1u;
    assert(!worldView.SetLocalTransforms({writes.data(), 2u}, {writeResults.data(), 2u}));
    assert(writeResults[1].code == HIKARI::GameProjectWorldResultCode::InvalidRequest);
    writes[1].reserved[0] = 0u;
    writes[1].localTransform.scale.x = 0.0f;
    assert(!worldView.SetLocalTransforms({writes.data(), 2u}, {writeResults.data(), 2u}));
    assert(writeResults[1].code == HIKARI::GameProjectWorldResultCode::InvalidTransform);
    assert(world.DestroyObject(batchFirst) == HIKARI::WorldMutationResult::Success);
    assert(world.DestroyObject(batchSecond) == HIKARI::WorldMutationResult::Success);

    HIKARI::GameObject* dirtyFirst = world.CreateObject("DirtyAdmissionFirst");
    HIKARI::GameObject* dirtySecond = world.CreateObject("DirtyAdmissionSecond");
    assert(dirtyFirst != nullptr);
    assert(dirtySecond != nullptr);
    assert(HIKARI::RuntimeObjectHandle::FromValue(dirtySecond->GetRuntimeHandle().ToValue()) ==
           dirtySecond->GetRuntimeHandle());
    assert(dirtyFirst->AddComponent<DirtyOnDetachComponent>() != nullptr);
    assert(dirtySecond->AddComponent<DirtyOnDetachComponent>() != nullptr);
    world.AcknowledgeAllRenderObjects();
    assert(world.GetRenderDirtyObjects().empty());

    const uint64_t dirtyRevisionBefore = dirtyFirst->GetRenderStateRevision();
    for (size_t index = 0u; index < 128u; ++index) {
        dirtyFirst->MarkRenderStateDirty();
    }
    assert(dirtyFirst->GetRenderStateRevision() == dirtyRevisionBefore + 128u);
    assert(world.GetRenderDirtyObjects().size() == 1u);
    assert(world.GetRenderDirtyObjects().front() == dirtyFirst);

    dirtySecond->MarkRenderStateDirty();
    assert(world.GetRenderDirtyObjects().size() == 2u);
    assert(world.DestroyObject(dirtyFirst) == HIKARI::WorldMutationResult::Success);
    assert(DirtyOnDetachComponent::queryVisitsDuringDetach == 1u);
    assert(world.GetRenderDirtyObjects().size() == 1u);
    assert(world.GetRenderDirtyObjects().front() == dirtySecond);
    dirtySecond->MarkRenderStateDirty();
    assert(world.GetRenderDirtyObjects().size() == 1u);

    world.AcknowledgeRenderDirtyObjects();
    assert(world.GetRenderDirtyObjects().empty());
    assert(!dirtySecond->IsRenderStateDirty());
    dirtySecond->MarkRenderStateDirty();
    assert(world.GetRenderDirtyObjects().size() == 1u);
    world.AcknowledgeRenderDirtyObjects();
    assert(world.GetRenderDirtyObjects().empty());
    assert(world.DestroyObject(dirtySecond) == HIKARI::WorldMutationResult::Success);
    world.AcknowledgeRenderDirtyObjects();

    world.MarkRenderObjectRemoved(0x1234u);
    world.MarkRenderObjectRemoved(0x1234u);
    assert(world.GetRemovedRenderObjectIds().size() == 1u);
    assert(world.GetRemovedRenderObjectIds().front() == 0x1234u);
    world.AcknowledgeRenderDirtyObjects();
    world.MarkRenderObjectRemoved(0x1234u);
    assert(world.GetRemovedRenderObjectIds().size() == 1u);
    world.AcknowledgeRenderDirtyObjects();

    HIKARI::GameObject* orderedA = world.CreateObject("OrderedA");
    HIKARI::GameObject* orderedB = world.CreateObject("OrderedB");
    HIKARI::GameObject* orderedC = world.CreateObject("OrderedC");
    HIKARI::GameObject* orderedD = world.CreateObject("OrderedD");
    assert(orderedA != nullptr);
    assert(orderedB != nullptr);
    assert(orderedC != nullptr);
    assert(orderedD != nullptr);
    const HIKARI::RuntimeObjectHandle orderedCHandle = orderedC->GetRuntimeHandle();
    const HIKARI::RuntimeObjectHandle orderedDHandle = orderedD->GetRuntimeHandle();
    auto collectObjectOrder = [&world]() {
        std::vector<std::string> names{};
        names.reserve(world.GetObjects().size());
        for (const auto& object : world.GetObjects()) {
            names.push_back(object->GetName());
        }
        return names;
    };
    assert((collectObjectOrder() == std::vector<std::string>{"OrderedA", "OrderedB", "OrderedC", "OrderedD"}));
    assert(world.DestroyObject(orderedB) == HIKARI::WorldMutationResult::Success);
    assert(world.FindObject(orderedCHandle) == orderedC);
    assert(world.FindObject(orderedDHandle) == orderedD);
    assert((collectObjectOrder() == std::vector<std::string>{"OrderedA", "OrderedC", "OrderedD"}));
    HIKARI::GameObject* orderedE = world.CreateObject("OrderedE");
    assert(orderedE != nullptr);
    assert((collectObjectOrder() == std::vector<std::string>{"OrderedA", "OrderedC", "OrderedD", "OrderedE"}));
    assert(world.DestroyObject(orderedA) == HIKARI::WorldMutationResult::Success);
    assert(world.DestroyObject(orderedC) == HIKARI::WorldMutationResult::Success);
    assert(world.DestroyObject(orderedD) == HIKARI::WorldMutationResult::Success);
    assert(world.DestroyObject(orderedE) == HIKARI::WorldMutationResult::Success);
    assert(world.GetObjects().empty());
    world.AcknowledgeRenderDirtyObjects();

    HIKARI::GameObject* iterationA = world.CreateObject("IterationA");
    HIKARI::GameObject* iterationB = world.CreateObject("IterationB");
    HIKARI::GameObject* iterationC = world.CreateObject("IterationC");
    assert(iterationA != nullptr);
    assert(iterationB != nullptr);
    assert(iterationC != nullptr);
    const HIKARI::RuntimeObjectHandle iterationBHandle = iterationB->GetRuntimeHandle();
    HIKARI::GameProjectWorldResult queuedIterationDestroy{};
    HIKARI::GameProjectWorldResult queuedIterationSpawn{};
    size_t iterationVisitCount = 0u;
    {
        const HIKARI::World::StructureReadScope structureRead = world.AcquireStructureReadScope();
        for (const auto& object : world.GetObjects()) {
            assert(object != nullptr);
            ++iterationVisitCount;
            if (object.get() != iterationA) {
                continue;
            }
            queuedIterationDestroy =
                worldView.DestroyObject(HIKARI::GameProjectSdkAccess::MakeObjectView(*iterationB).GetHandle());
            HIKARI::GameProjectSpawnObjectDesc iterationDesc{};
            std::snprintf(iterationDesc.name, HIKARI::kGameProjectObjectNameCapacity, "%s", "IterationD");
            queuedIterationSpawn = worldView.SpawnObject(iterationDesc);
        }
    }
    assert(iterationVisitCount == 3u);
    assert(queuedIterationDestroy.IsQueued());
    assert(queuedIterationSpawn.IsQueued());
    assert(world.FlushQueuedCommands(HIKARI::WorldCommandPhase::QueryEnd) == 2u);
    assert(world.FindObject(iterationBHandle) == nullptr);
    assert((collectObjectOrder() == std::vector<std::string>{"IterationA", "IterationC", "IterationD"}));
    assert(world.DestroyObject(iterationA) == HIKARI::WorldMutationResult::Success);
    assert(world.DestroyObject(iterationC) == HIKARI::WorldMutationResult::Success);
    assert(worldView.FindObject(queuedIterationSpawn.object).IsValid());
    assert(worldView.DestroyObject(queuedIterationSpawn.object).Succeeded());
    world.AcknowledgeRenderDirtyObjects();

    HIKARI::GameProjectSpawnObjectDesc victimDesc{};
    std::snprintf(victimDesc.name, HIKARI::kGameProjectObjectNameCapacity, "%s", "QueuedDestroyVictim");
    const HIKARI::GameProjectWorldResult victimSpawn = worldView.SpawnObject(victimDesc);
    assert(victimSpawn.Succeeded());
    const uint64_t objectCountBeforeQueue = worldView.GetObjectCount();

    HIKARI::GameProjectWorldResult queuedRoot{};
    HIKARI::GameProjectWorldResult queuedChild{};
    HIKARI::GameProjectWorldResult queuedDestroy{};
    {
        const HIKARI::World::StructureReadScope structureRead = world.AcquireStructureReadScope();
        HIKARI::GameProjectSpawnObjectDesc queuedRootDesc{};
        std::snprintf(queuedRootDesc.name, HIKARI::kGameProjectObjectNameCapacity, "%s", "QueuedRoot");
        queuedRoot = worldView.SpawnObject(queuedRootDesc);
        assert(queuedRoot.Accepted());
        assert(queuedRoot.IsQueued());
        assert(queuedRoot.GetCommandToken() != 0u);
        assert(!worldView.FindObject(queuedRoot.object).IsValid());

        HIKARI::GameProjectSpawnObjectDesc queuedChildDesc{};
        std::snprintf(queuedChildDesc.name, HIKARI::kGameProjectObjectNameCapacity, "%s", "QueuedChild");
        queuedChildDesc.parent = queuedRoot.object;
        queuedChild = worldView.SpawnObject(queuedChildDesc);
        assert(queuedChild.Accepted());
        assert(queuedChild.IsQueued());
        queuedDestroy = worldView.DestroyObject(victimSpawn.object);
        assert(queuedDestroy.Accepted());
        assert(queuedDestroy.IsQueued());
        assert(worldView.GetObjectCount() == objectCountBeforeQueue);
        assert(!worldView.CanMutateStructure());
    }
    assert(world.FlushQueuedCommands(HIKARI::WorldCommandPhase::QueryEnd) == 3u);
    const HIKARI::ObjectView queuedRootView = worldView.FindObject(queuedRoot.object);
    const HIKARI::ObjectView queuedChildView = worldView.FindObject(queuedChild.object);
    assert(queuedRootView.IsValid());
    assert(queuedChildView.IsValid());
    assert(queuedChildView.GetParent().GetHandle() == queuedRoot.object);
    assert(!worldView.FindObject(victimSpawn.object).IsValid());
    assert(worldView.GetCommandFailureCount() == 0u);
    assert(worldView.DestroyObject(queuedChild.object).Succeeded());
    assert(worldView.DestroyObject(queuedRoot.object).Succeeded());

    world.BeginFrame(2u);
    HIKARI::GameProjectWorldResult canceledSpawn{};
    HIKARI::GameProjectWorldResult cancelSubmission{};
    {
        const HIKARI::World::StructureReadScope structureRead = world.AcquireStructureReadScope();
        HIKARI::GameProjectSpawnObjectDesc canceledDesc{};
        std::snprintf(canceledDesc.name, HIKARI::kGameProjectObjectNameCapacity, "%s", "CanceledPendingSpawn");
        canceledSpawn = worldView.SpawnObject(canceledDesc);
        assert(canceledSpawn.IsQueued());
        cancelSubmission = worldView.DestroyObject(canceledSpawn.object);
        assert(cancelSubmission.IsQueued());
        assert(cancelSubmission.GetCommandToken() == canceledSpawn.GetCommandToken());
    }
    assert(world.FlushQueuedCommands(HIKARI::WorldCommandPhase::QueryEnd) == 1u);
    assert(!worldView.FindObject(canceledSpawn.object).IsValid());
    assert(worldView.GetCommandFailureCount() == 1u);
    HIKARI::GameProjectWorldCommandFailure canceledFailure{};
    assert(worldView.ReadCommandFailure(0u, canceledFailure));
    assert(canceledFailure.commandToken == canceledSpawn.GetCommandToken());
    assert(canceledFailure.kind == HIKARI::GameProjectWorldCommandKind::Spawn);
    assert(canceledFailure.code == HIKARI::GameProjectWorldResultCode::CommandCanceled);
    world.BeginFrame(3u);
    assert(worldView.GetCommandFailureCount() == 0u);

    HIKARI::GameProjectSpawnObjectDesc queueLimitDesc{};
    std::snprintf(queueLimitDesc.name, HIKARI::kGameProjectObjectNameCapacity, "%s", "QueueLimitObject");
    const HIKARI::GameProjectWorldResult queueLimitObject = worldView.SpawnObject(queueLimitDesc);
    assert(queueLimitObject.Succeeded());
    {
        const HIKARI::World::StructureReadScope structureRead = world.AcquireStructureReadScope();
        for (size_t index = 0u; index < HIKARI::kWorldCommandMaximumCapacity; ++index) {
            const HIKARI::GameProjectWorldResult result =
                worldView.SetParent(queueLimitObject.object, {}, HIKARI::GameProjectParentMode::KeepLocal);
            assert(result.IsQueued());
        }
        assert(worldView.SetParent(queueLimitObject.object, {}, HIKARI::GameProjectParentMode::KeepLocal).code ==
               HIKARI::GameProjectWorldResultCode::CommandQueueFull);
    }
    assert(world.FlushQueuedCommands(HIKARI::WorldCommandPhase::QueryEnd) == HIKARI::kWorldCommandMaximumCapacity);
    assert(worldView.GetCommandFailureCount() == 0u);
    assert(worldView.DestroyObject(queueLimitObject.object).Succeeded());

    world.BeginFrame(4u);
    HIKARI::GameProjectSpawnObjectDesc failureLimitDesc{};
    std::snprintf(failureLimitDesc.name, HIKARI::kGameProjectObjectNameCapacity, "%s", "FailureLimitObject");
    const HIKARI::GameProjectWorldResult failureLimitObject = worldView.SpawnObject(failureLimitDesc);
    assert(failureLimitObject.Succeeded());
    {
        const HIKARI::World::StructureReadScope structureRead = world.AcquireStructureReadScope();
        for (size_t index = 0u; index < HIKARI::kWorldCommandFailureCapacity + 2u; ++index) {
            assert(worldView.DestroyObject(failureLimitObject.object).IsQueued());
        }
    }
    assert(world.FlushQueuedCommands(HIKARI::WorldCommandPhase::QueryEnd) == HIKARI::kWorldCommandFailureCapacity + 2u);
    assert(worldView.GetCommandFailureCount() == HIKARI::kWorldCommandFailureCapacity);
    assert(worldView.GetCommandFailureOverflowCount() == 1u);
    world.BeginFrame(5u);
    assert(worldView.GetCommandFailureCount() == 0u);
    assert(worldView.GetCommandFailureOverflowCount() == 0u);

    HIKARI::GameProjectObjectHandle malformedParent{};
    malformedParent.world = worldView.GetHandle();
    assert(!malformedParent.IsNull());
    assert(!malformedParent.IsValid());
    HIKARI::GameProjectSpawnObjectDesc malformedParentDesc{};
    std::snprintf(malformedParentDesc.name, HIKARI::kGameProjectObjectNameCapacity, "%s", "MalformedParent");
    malformedParentDesc.parent = malformedParent;
    assert(worldView.SpawnObject(malformedParentDesc).code == HIKARI::GameProjectWorldResultCode::InvalidParent);
    malformedParent.value = 1u;
    assert(!malformedParent.IsValid());
    malformedParentDesc.parent = malformedParent;
    assert(worldView.SpawnObject(malformedParentDesc).code == HIKARI::GameProjectWorldResultCode::InvalidParent);

    HIKARI::GameObject* singularParent = world.CreateObject("SingularParent");
    assert(singularParent != nullptr);
    HIKARI::Transform3D singularTransform{};
    singularTransform.scale.x = 0.0f;
    (void)singularParent->SetLocalTransform(singularTransform);
    HIKARI::GameProjectSpawnObjectDesc singularChildDesc{};
    std::snprintf(singularChildDesc.name, HIKARI::kGameProjectObjectNameCapacity, "%s", "SingularChild");
    singularChildDesc.parent = HIKARI::GameProjectSdkAccess::MakeObjectView(*singularParent).GetHandle();
    const HIKARI::GameProjectWorldResult singularChildSpawn = worldView.SpawnObject(singularChildDesc);
    assert(singularChildSpawn.Succeeded());
    const HIKARI::ObjectView singularChild = worldView.FindObject(singularChildSpawn.object);
    HIKARI::GameProjectTransform singularChildLocalBefore{};
    assert(singularChild.GetLocalTransform(singularChildLocalBefore));
    HIKARI::GameProjectTransform requestedWorld{};
    requestedWorld.position.x = 3.0f;
    assert(singularChild.SetWorldTransform(requestedWorld).code ==
           HIKARI::GameProjectWorldResultCode::MutationRejected);
    HIKARI::GameProjectTransform singularChildLocalAfter{};
    assert(singularChild.GetLocalTransform(singularChildLocalAfter));
    assert(singularChild.GetParent().GetHandle() == singularChildDesc.parent);
    assert(singularChildLocalAfter.position.x == singularChildLocalBefore.position.x);
    assert(singularChildLocalAfter.position.y == singularChildLocalBefore.position.y);
    assert(singularChildLocalAfter.position.z == singularChildLocalBefore.position.z);

    HIKARI::GameProjectSpawnObjectDesc atomicChildDesc{};
    std::snprintf(atomicChildDesc.name, HIKARI::kGameProjectObjectNameCapacity, "%s", "AtomicReparentChild");
    atomicChildDesc.localTransform.position = {7.0f, 8.0f, 9.0f};
    const HIKARI::GameProjectWorldResult atomicChildSpawn = worldView.SpawnObject(atomicChildDesc);
    assert(atomicChildSpawn.Succeeded());
    const HIKARI::ObjectView atomicChild = worldView.FindObject(atomicChildSpawn.object);
    HIKARI::GameProjectTransform atomicLocalBefore{};
    assert(atomicChild.GetLocalTransform(atomicLocalBefore));
    assert(
        worldView.SetParent(atomicChildSpawn.object, singularChildDesc.parent, HIKARI::GameProjectParentMode::KeepWorld)
            .code == HIKARI::GameProjectWorldResultCode::MutationRejected);
    HIKARI::GameProjectTransform atomicLocalAfter{};
    assert(atomicChild.GetLocalTransform(atomicLocalAfter));
    assert(!atomicChild.GetParent().IsValid());
    assert(atomicLocalAfter.position.x == atomicLocalBefore.position.x);
    assert(atomicLocalAfter.position.y == atomicLocalBefore.position.y);
    assert(atomicLocalAfter.position.z == atomicLocalBefore.position.z);
    assert(worldView.DestroyObject(atomicChildSpawn.object).Succeeded());
    assert(worldView.DestroyObject(singularChildSpawn.object).Succeeded());
    assert(world.DestroyObject(singularParent) == HIKARI::WorldMutationResult::Success);

    HIKARI::GameObject* shearObject = world.CreateObject("ShearObject");
    assert(shearObject != nullptr);
    HIKARI::Transform3D shearTransform{};
    shearTransform.useExplicitMatrix = true;
    shearTransform.explicitMatrix = HIKARI::MATH::Mat4::Identity();
    shearTransform.explicitMatrix.m[1][0] = 0.5f;
    (void)shearObject->SetLocalTransform(shearTransform);
    HIKARI::GameProjectTransform rejectedShear{};
    assert(!HIKARI::GameProjectSdkAccess::MakeObjectView(*shearObject).GetWorldTransform(rejectedShear));
    assert(world.DestroyObject(shearObject) == HIKARI::WorldMutationResult::Success);

    HIKARI::GameProjectSpawnObjectDesc threadObjectDesc{};
    std::snprintf(threadObjectDesc.name, HIKARI::kGameProjectObjectNameCapacity, "%s", "ThreadBoundObject");
    const HIKARI::GameProjectWorldResult threadObjectSpawn = worldView.SpawnObject(threadObjectDesc);
    assert(threadObjectSpawn.Succeeded());
    const HIKARI::ObjectView threadObject = worldView.FindObject(threadObjectSpawn.object);
    assert(threadObject.IsValid());
    HIKARI::GameProjectTransform extremeRotation{};
    extremeRotation.rotation = {(std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)(),
                                (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)()};
    assert(threadObject.SetLocalTransform(extremeRotation).Succeeded());
    HIKARI::GameProjectTransform normalizedRotation{};
    assert(threadObject.GetLocalTransform(normalizedRotation));
    const float normalizedLength = std::sqrt(normalizedRotation.rotation.x * normalizedRotation.rotation.x +
                                             normalizedRotation.rotation.y * normalizedRotation.rotation.y +
                                             normalizedRotation.rotation.z * normalizedRotation.rotation.z +
                                             normalizedRotation.rotation.w * normalizedRotation.rotation.w);
    assert(std::isfinite(normalizedLength));
    assert(std::abs(normalizedLength - 1.0f) < 1.0e-4f);

    HIKARI::GameObject* rawThreadObject = world.CreateObject("RawThreadBoundObject");
    assert(rawThreadObject != nullptr);
    world.AcknowledgeRenderDirtyObjects();
    const uint64_t rawThreadRevision = rawThreadObject->GetRenderStateRevision();

    bool wrongThreadWorldRejected = false;
    bool wrongThreadObjectRejected = false;
    bool wrongThreadMutationRejected = false;
    bool wrongThreadBatchRejected = false;
    bool wrongThreadRawMutationRejected = false;
    bool wrongThreadReadScopeRejected = false;
    std::thread wrongThread([&]() {
        wrongThreadWorldRejected = !worldView.IsValid() && worldView.GetObjectCount() == 0u;
        wrongThreadObjectRejected = !threadObject.IsValid();
        HIKARI::GameProjectSpawnObjectDesc wrongThreadDesc{};
        std::snprintf(wrongThreadDesc.name, HIKARI::kGameProjectObjectNameCapacity, "%s", "WrongThread");
        wrongThreadMutationRejected =
            worldView.SpawnObject(wrongThreadDesc).code == HIKARI::GameProjectWorldResultCode::WrongThread;
        HIKARI::GameProjectWorldTransformWrite wrongThreadWrite{};
        wrongThreadWrite.object = threadObject.GetHandle();
        HIKARI::GameProjectWorldResult wrongThreadWriteResult{};
        wrongThreadBatchRejected =
            !worldView.SetLocalTransforms({&wrongThreadWrite, 1u}, {&wrongThreadWriteResult, 1u}) &&
            wrongThreadWriteResult.code == HIKARI::GameProjectWorldResultCode::WrongThread;
        rawThreadObject->SetName("WrongThreadName");
        const bool positionChanged = rawThreadObject->SetLocalPosition({7.0f, 8.0f, 9.0f});
        rawThreadObject->MarkRenderStateDirty();
        world.MarkRenderObjectRemoved(0x9876u);
        const HIKARI::World::StructureReadScope readScope = world.AcquireStructureReadScope();
        wrongThreadRawMutationRejected = !positionChanged && !rawThreadObject->SetParent(nullptr);
        wrongThreadReadScopeRejected = !readScope.IsValid() && world.GetObjects().empty() &&
                                       world.GetRenderDirtyObjects().empty() &&
                                       world.GetRemovedRenderObjectIds().empty();
    });
    wrongThread.join();
    assert(wrongThreadWorldRejected);
    assert(wrongThreadObjectRejected);
    assert(wrongThreadMutationRejected);
    assert(wrongThreadBatchRejected);
    assert(wrongThreadRawMutationRejected);
    assert(wrongThreadReadScopeRejected);
    assert(worldView.IsValid());
    assert(threadObject.IsValid());
    assert(rawThreadObject->GetName() == "RawThreadBoundObject");
    assert(rawThreadObject->GetRenderStateRevision() == rawThreadRevision);
    assert(!rawThreadObject->IsRenderStateDirty());
    assert(world.GetRenderDirtyObjects().empty());
    assert(world.GetRemovedRenderObjectIds().empty());
    assert(world.DestroyObject(rawThreadObject) == HIKARI::WorldMutationResult::Success);
    assert(worldView.DestroyObject(threadObjectSpawn.object).Succeeded());

    HIKARI::GameObject* clearLiveObject = world.CreateObject("ClearLiveObject");
    assert(clearLiveObject != nullptr);
    assert(clearLiveObject->AddComponent<DirtyOnDetachComponent>() != nullptr);
    const uint64_t clearLiveStableId = clearLiveObject->GetRenderStableId();
    HIKARI::GameObject* clearSimulationObject = world.CreateObject("ClearSimulationObject");
    assert(clearSimulationObject != nullptr);
    const uint64_t clearSimulationStableId = clearSimulationObject->GetRenderStableId();
    HIKARI::GameProjectWorldResult clearedPendingSpawn{};
    {
        const HIKARI::World::StructureReadScope structureRead = world.AcquireStructureReadScope();
        HIKARI::GameProjectSpawnObjectDesc pendingDesc{};
        std::snprintf(pendingDesc.name, HIKARI::kGameProjectObjectNameCapacity, "%s", "CanceledByWorldClear");
        clearedPendingSpawn = worldView.SpawnObject(pendingDesc);
        assert(clearedPendingSpawn.IsQueued());
    }
    world.Clear();
    assert(worldView.IsValid());
    assert(worldView.GetObjectCount() == 0u);
    assert(world.GetRemovedRenderObjectIds().size() == 1u);
    assert(world.GetRemovedRenderObjectIds().front() == clearLiveStableId);
    assert(world.GetRemovedRenderObjectIds().front() != clearSimulationStableId);
    assert(!worldView.FindObject(clearedPendingSpawn.object).IsValid());
    assert(world.FlushQueuedCommands(HIKARI::WorldCommandPhase::QueryEnd) == 0u);
    assert(worldView.GetCommandFailureCount() == 1u);
    HIKARI::GameProjectWorldCommandFailure clearFailure{};
    assert(worldView.ReadCommandFailure(0u, clearFailure));
    assert(clearFailure.commandToken == clearedPendingSpawn.GetCommandToken());
    assert(clearFailure.code == HIKARI::GameProjectWorldResultCode::CommandCanceled);
    assert(clearFailure.phase == HIKARI::GameProjectWorldCommandPhase::WorldClear);
    world.AcknowledgeRenderDirtyObjects();
    assert(world.GetRemovedRenderObjectIds().empty());

    HIKARI::GameObject* afterClearA = world.CreateObject("AfterClearA");
    HIKARI::GameObject* afterClearB = world.CreateObject("AfterClearB");
    assert(afterClearA != nullptr);
    assert(afterClearB != nullptr);
    std::vector<std::string> afterClearOrder{};
    for (const auto& object : world.GetObjects()) {
        afterClearOrder.push_back(object->GetName());
    }
    assert((afterClearOrder == std::vector<std::string>{"AfterClearA", "AfterClearB"}));
    assert(world.DestroyObject(afterClearA) == HIKARI::WorldMutationResult::Success);
    assert(world.DestroyObject(afterClearB) == HIKARI::WorldMutationResult::Success);
    world.AcknowledgeRenderDirtyObjects();

    HIKARI::WorldView expiredWorld{};
    HIKARI::ObjectView expiredObject{};
    HIKARI::GameProjectObjectHandle expiredHandle{};
    {
        auto temporaryWorld = std::make_unique<HIKARI::World>();
        expiredWorld = HIKARI::GameProjectSdkAccess::MakeWorldView(*temporaryWorld);
        HIKARI::GameProjectSpawnObjectDesc temporaryDesc{};
        std::snprintf(temporaryDesc.name, HIKARI::kGameProjectObjectNameCapacity, "%s", "TemporaryObject");
        const HIKARI::GameProjectWorldResult temporarySpawn = expiredWorld.SpawnObject(temporaryDesc);
        assert(temporarySpawn.Succeeded());
        assert(expiredWorld.FindObject(temporarySpawn.object).GetSceneObjectId() != 0u);
        expiredHandle = temporarySpawn.object;
        expiredObject = expiredWorld.FindObject(expiredHandle);
        assert(expiredWorld.IsValid());
        assert(expiredObject.IsValid());
    }
    auto replacementWorld = std::make_unique<HIKARI::World>();
    const HIKARI::WorldView replacementWorldView = HIKARI::GameProjectSdkAccess::MakeWorldView(*replacementWorld);
    assert(replacementWorldView.GetHandle() != expiredWorld.GetHandle());
    assert(!expiredWorld.IsValid());
    assert(!expiredObject.IsValid());
    assert(!replacementWorldView.FindObject(expiredHandle).IsValid());

    assert(world.BindComponentRegistry(&componentRegistry));
    HIKARI::GameObject* closingPatchObject = world.CreateObject("ClosingWorldSpritePatch");
    assert(closingPatchObject != nullptr);
    assert(closingPatchObject->AddComponent<HIKARI::WorldSpriteComponent>() != nullptr);
    HIKARI::GameProjectWorldResultCode closingPatchResult = HIKARI::GameProjectWorldResultCode::InternalError;
    ClosingWorldSpritePatchProbe* closingPatchProbe = closingPatchObject->AddComponent<ClosingWorldSpritePatchProbe>();
    assert(closingPatchProbe != nullptr);
    closingPatchProbe->Configure(HIKARI::GameProjectSdkAccess::MakeObjectView(*closingPatchObject),
                                 &closingPatchResult);
    world.Clear();
    assert(closingPatchResult == HIKARI::GameProjectWorldResultCode::WorldClosing);
    // Frame submissions are bounded, atomically validated and tied to live owner generations.
    {
        HIKARI::LocalFogService fog;
        HIKARI::World fogWorld;
        assert(fogWorld.Services().Register(fog));
        auto* owner = fogWorld.CreateObject("FogOwner");
        const auto ownerHandle = HIKARI::GameProjectSdkAccess::MakeObjectView(*owner).GetHandle();
        const auto fogView = HIKARI::GameProjectSdkAccess::MakeWorldView(fogWorld).GetLocalFog();
        HIKARI::GameProjectLocalFogVolume volume{};
        volume.worldTransform.position = {2, 3, 4};
        volume.worldTransform.scale = {8, 4, 2};
        const uint64_t frameIndex = HIKARI::TIME::GetFrameContext().frameIndex;
        assert(fogView.Submit(ownerHandle, 1, volume).Succeeded());
        auto projected = fog.PrepareFrame(fogWorld, frameIndex);
        assert(projected.size() == 1);
        const auto center = projected[0].worldToBox.TransformPoint({2, 3, 4, 1});
        assert(std::abs(center.x) < 1e-5f && std::abs(center.y) < 1e-5f && std::abs(center.z) < 1e-5f);
        auto* parent = fogWorld.CreateObject("FogParent");
        parent->SetDocumentId({99});
        owner->SetDocumentId({100});
        owner->SetParent(parent);
        HIKARI::SceneDocument fogDocument;
        HIKARI::SceneObjectData disabledParent;
        disabledParent.id = {99};
        disabledParent.enabled = false;
        fogDocument.objects.push_back(disabledParent);
        assert(fog.PrepareFrame(fogWorld, frameIndex, &fogDocument, 1).empty());
        fogDocument.objects[0].enabled = true;
        assert(fog.PrepareFrame(fogWorld, frameIndex, &fogDocument, 2).size() == 1);
        volume.density = std::numeric_limits<float>::quiet_NaN();
        assert(fogView.Submit(ownerHandle, 1, volume).code == HIKARI::GameProjectWorldResultCode::InvalidRequest);
        assert(fog.PrepareFrame(fogWorld, frameIndex)[0].colorDensity.w == 0.1f);
        volume.density = 0.25f;
        assert(fogView.Submit(ownerHandle, 1, volume).Succeeded());
        assert(fog.PrepareFrame(fogWorld, frameIndex)[0].colorDensity.w == 0.25f);
        for (uint64_t id = 2; id <= HIKARI::kGameProjectLocalFogMaximumVolumes; ++id)
            assert(fogView.Submit(ownerHandle, id, volume).Succeeded());
        assert(fogView.Submit(ownerHandle, 33, volume).code == HIKARI::GameProjectWorldResultCode::MutationRejected);
        assert(fogView.Remove(ownerHandle, 1).Succeeded());
        assert(fogView.Remove(ownerHandle, 1).Succeeded());
        assert(fog.PrepareFrame(fogWorld, frameIndex).size() == 31);
        assert(fog.PrepareFrame(fogWorld, frameIndex + 1).empty());
        assert(fogView.Submit(ownerHandle, 1, volume).Succeeded());
        fogWorld.DestroyObject(owner);
        assert(fog.PrepareFrame(fogWorld, frameIndex).empty());
        assert(fogView.Submit(ownerHandle, 1, volume).code == HIKARI::GameProjectWorldResultCode::InvalidObject);
    }
    // 環境の時刻・天気・描画の様式は実行中の World だけを変える。不正な日付と版の違う存档は拒否する。
    {
        using Code = HIKARI::GameProjectWorldResultCode;
        assert(HIKARI::CONTENT::GetEngineContentContext().Open(HIKARI_CONTRACT_ENGINE_CONTENT_ROOT));
        HIKARI::ENVIRONMENT::EnvironmentAssetStore store;
        store.Configure(nullptr);
        HIKARI::ENVIRONMENT::EnvironmentRuntime runtime;
        runtime.Configure(&store);
        runtime.Reset({});
        HIKARI::World environmentWorld;
        const auto environment = HIKARI::GameProjectSdkAccess::MakeWorldView(environmentWorld).GetEnvironment();
        HIKARI::GameProjectEnvironmentState state{};
        assert(environment.GetState(state).code == Code::Unsupported && environment.GetWeatherId().empty());
        assert(environmentWorld.Services().Register(runtime));
        assert(environment.SetTime({2026, 2, 30, 12.0}).code == Code::InvalidRequest);
        assert(environment.SetTime({2026, 6, 21, 21.5}).Succeeded());
        assert(environment.GetState(state).Succeeded() && state.time.localHours == 21.5 &&
               state.sunIlluminance == 0.0f);
        assert(environment.RequestWeather("engine.weather.storm", 0.0f).Succeeded() &&
               environment.GetWeatherId() == "engine.weather.storm");
        assert(environment.RequestWeather("engine.weather.missing", 1.0f).code == Code::InvalidRequest);
        // 描画の様式は様式の store が読める ID だけを受け付け、存档と一緒に戻る。
        assert(environment.GetRenderStyleId() == HIKARI::RENDERSTYLE::kDefaultRenderStyleId);
        assert(environment.SetRenderStyle("engine.style.manga").code == Code::Unsupported);
        HIKARI::RENDERSTYLE::RenderStyleAssetStore styles;
        styles.Configure(nullptr);
        assert(environmentWorld.Services().Register(styles));
        assert(environment.SetRenderStyle("engine.style.missing").code == Code::InvalidRequest);
        assert(environment.SetRenderStyle("engine.style.manga").Succeeded() &&
               environment.GetRenderStyleId() == "engine.style.manga");
        HIKARI::GameProjectEnvironmentSnapshot snapshot{};
        assert(environment.CaptureSnapshot(snapshot).Succeeded());
        assert(environment.SetTimeScale(60.0).Succeeded() && environment.SetTime({2027, 1, 1, 6.0}).Succeeded());
        assert(environment.SetRenderStyle("engine.style.pixel").Succeeded());
        assert(environment.RestoreSnapshot(snapshot).Succeeded() && environment.GetState(state).Succeeded() &&
               state.time.year == 2026 && state.timeScale == 0.0 &&
               environment.GetRenderStyleId() == "engine.style.manga");
        snapshot.layoutVersion += 1u;
        assert(environment.RestoreSnapshot(snapshot).code == Code::InvalidRequest);
        // 色調整の置き換えは範囲へ丸め、読めない LUT は拒否し、解除でプロファイルの値へ戻る。
        HIKARI::GameProjectColorGrading grading{};
        grading.shadowsTint = {3.0f, 1.0f, 0.5f};
        assert(environment.SetColorGradingOverride(grading).Succeeded());
        HIKARI::GameProjectColorGrading effective{};
        assert(environment.GetColorGrading(effective).Succeeded() && effective.shadowsTint.x == 2.0f &&
               effective.shadowsTint.z == 0.5f && effective.lutAssetGuid[0] == '\0');
        std::memcpy(grading.lutAssetGuid, "0123456789abcdef0123456789abcdef", 33u);
        assert(environment.SetColorGradingOverride(grading).code == Code::InvalidRequest);
        assert(environment.GetColorGrading(effective).Succeeded() && effective.shadowsTint.x == 2.0f);
        assert(environment.ClearColorGradingOverride().Succeeded() &&
               environment.GetColorGrading(effective).Succeeded() && effective.shadowsTint.x == 1.0f);
        effective.version = 0u;
        assert(environment.GetColorGrading(effective).code == Code::InvalidRequest);
    }
    return 0;
}
