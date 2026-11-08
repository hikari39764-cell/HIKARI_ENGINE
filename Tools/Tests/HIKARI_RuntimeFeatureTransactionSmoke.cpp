#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "HIKARI/Project/GameComponent.h"
#include "HIKARI/Project/GameSystem.h"
#include "HIKARI/Project/RuntimeFeature.h"
#include "HIKARI/Scene/Features/HIKARI_RuntimeFeatureContextAccess.h"
#include "Scene/Features/HIKARI_BuiltInRuntimeFeatures.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/Features/HIKARI_RuntimeFeatureCatalog.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SystemScheduler.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_WorldMassStore.h"
#include "HIKARI/Project/UiTypes.h"
#include "Tools/Tests/Internal/HIKARI_ContractAssert.h"

namespace {

struct ProbeRenderCommands {
    uint32_t clearCount = 0u;
    uint32_t drawCount = 0u;
    bool failDraw = false;
};

bool ProbeClearWorkingColor(void* context, uint64_t, float, float, float, float) noexcept {
    if (context == nullptr) {
        return false;
    }
    ++static_cast<ProbeRenderCommands*>(context)->clearCount;
    return true;
}

bool ProbeDrawFullscreen(void* context, uint64_t, const HIKARI::GameProjectFullscreenDrawDesc* draw) noexcept {
    if (context == nullptr || draw == nullptr || !draw->IsValid()) {
        return false;
    }
    auto& commands = *static_cast<ProbeRenderCommands*>(context);
    ++commands.drawCount;
    return !commands.failDraw;
}

constexpr std::string_view kFailedFeatureId = "Test.Feature.Failed";
constexpr std::string_view kThrowingFeatureId = "Test.Feature.Throwing";
constexpr std::string_view kComponentType = "Test.TransactionalComponent";
constexpr std::string_view kSystemId = "Test.TransactionalSystem";
constexpr std::string_view kObserverSystemId = "Test.TransactionalRenderObserver";
constexpr std::string_view kRenderPassSystemId = "Test.TransactionalRenderPass";
constexpr HIKARI::ComponentTypeId kMassType = HIKARI::MakeComponentTypeId("Test.TransactionalMassData");
constexpr uint64_t kMassLayoutHash = HIKARI::MakeComponentTypeId("Test.TransactionalMassData.Layout.v1").value;
constexpr HIKARI::ComponentTypeId kBaselineMassType = HIKARI::MakeComponentTypeId("Test.BaselineMassData");
constexpr uint64_t kBaselineMassLayoutHash = HIKARI::MakeComponentTypeId("Test.BaselineMassData.Layout.v1").value;

class TransactionalComponent final : public HIKARI::IGameComponent {};

class TransactionalSystem final : public HIKARI::IGameSystem {
public:
    bool ExecuteMassPartition(const HIKARI::GameProjectMassPartitionInput&, HIKARI::GameProjectMassPartitionOutput&,
                              const HIKARI::GameProjectMassCommandWriter&, const HIKARI::FrameContext&) const noexcept {
        return true;
    }

    bool ObserveRenderFrame(const HIKARI::GameProjectRenderFrameView&) const noexcept { return true; }

    bool ExecuteRenderPass(const HIKARI::GameProjectRenderPassContext& context) const noexcept {
        HIKARI::GameProjectFullscreenDrawDesc draw{};
        if (!draw.shader.SetAssetGuid("0123456789abcdef0123456789abcdef") || !draw.shader.SetPixelEntryId("MainPS")) {
            return false;
        }
        draw.inputSemantic = HIKARI::GameProjectRenderSurfaceSemantic::SceneColorOpaque;
        draw.inputPhase = HIKARI::GameProjectRenderSurfacePhase::Opaque;
        return context.encoder.ClearWorkingColor(0.1f, 0.2f, 0.3f, 1.0f) && context.encoder.DrawFullscreen(draw);
    }
};

struct TransactionalMassData {
    uint32_t value = 0u;
};

bool RegisterTransactionalPayload(HIKARI::RuntimeFeatureContext& context) {
    HIKARI::GameComponentDescriptor component{};
    component.typeName = kComponentType;
    if (!context.RegisterComponent<TransactionalComponent>(component)) {
        return false;
    }

    if (!context.RegisterMassData<TransactionalMassData>(kMassType, kMassLayoutHash)) {
        return false;
    }
    HIKARI::GameProjectWorldMassRenderProxyDesc renderProxy{};
    renderProxy.typeId = kMassType;
    renderProxy.layoutHash = kMassLayoutHash;
    renderProxy.geometry = HIKARI::GameProjectWorldMassRenderGeometry::BuiltInBox;
    if (!context.RegisterMassRenderProxy(renderProxy)) {
        return false;
    }
    HIKARI::GameSystemDescriptor system{};
    system.systemId = kSystemId;
    HIKARI::GameProjectMassSystemScheduleDesc schedule{};
    schedule.access.typeId = kMassType;
    schedule.access.readMask = static_cast<uint32_t>(HIKARI::GameProjectMassSystemField::ProjectData);
    schedule.phase = HIKARI::GameProjectMassSystemPhase::Update;
    schedule.partitionSize = 256u;
    if (!context.RegisterParallelMassSystem<TransactionalSystem>(system, schedule)) {
        return false;
    }
    HIKARI::GameSystemDescriptor observerSystem{};
    observerSystem.systemId = kObserverSystemId;
    HIKARI::GameProjectRenderObserverDesc observer{};
    if (!context.RegisterRenderObserverSystem<TransactionalSystem>(observerSystem, observer)) {
        return false;
    }
    HIKARI::GameSystemDescriptor renderPassSystem{};
    renderPassSystem.systemId = kRenderPassSystemId;
    HIKARI::GameProjectRenderPassDesc renderPass{};
    renderPass.target = HIKARI::GameProjectRenderPassTarget::WorkingSceneColor;
    renderPass.flags = HIKARI::GameProjectRenderPassFlag::ShaderCommands;
    renderPass.resourceUseCount = 1u;
    renderPass.resources[0].semantic = HIKARI::GameProjectRenderSurfaceSemantic::SceneColorOpaque;
    renderPass.resources[0].phase = HIKARI::GameProjectRenderSurfacePhase::Opaque;
    renderPass.resources[0].access = HIKARI::GameProjectRenderSurfaceAccess::Sampled;
    if (!renderPass.SetStableId("Project.Test.Transactional") ||
        !context.RegisterRenderPassSystem<TransactionalSystem>(renderPassSystem, renderPass)) {
        return false;
    }
    return context.BindComponentSystem({kComponentType, kSystemId, 10});
}

class FailedFeature final : public HIKARI::IRuntimeFeature {
public:
    std::string_view GetFeatureId() const noexcept override { return kFailedFeatureId; }
    std::string_view GetDisplayName() const noexcept override { return "Failed Feature"; }
    std::string_view GetDescription() const noexcept override { return "Registers a payload and then rejects it."; }
    bool Register(HIKARI::RuntimeFeatureContext& context) override {
        assert(RegisterTransactionalPayload(context));
        return false;
    }
};

class ThrowingFeature final : public HIKARI::IRuntimeFeature {
public:
    std::string_view GetFeatureId() const noexcept override { return kThrowingFeatureId; }
    std::string_view GetDisplayName() const noexcept override { return "Throwing Feature"; }
    std::string_view GetDescription() const noexcept override { return "Registers a payload and then throws."; }
    bool Register(HIKARI::RuntimeFeatureContext& context) override {
        assert(RegisterTransactionalPayload(context));
        throw std::runtime_error("intentional feature failure");
    }
};

void AssertRegistryClean(const HIKARI::ComponentRegistry& components,
                         const HIKARI::WorldMassSchemaRegistry& massSchemas, const HIKARI::SystemTypeRegistry& systems,
                         const HIKARI::ComponentSystemPolicy& policy) {
    assert(components.Find(kComponentType) == nullptr);
    assert(components.Find(HIKARI::MakeComponentTypeId(kComponentType)) == nullptr);
    assert(massSchemas.Find(kMassType) == nullptr);
    assert(massSchemas.FindRenderProxy(kMassType) == nullptr);
    assert(massSchemas.Find(kBaselineMassType) != nullptr);
    assert(systems.Find(kSystemId) == nullptr);
    assert(systems.Find(kObserverSystemId) == nullptr);
    assert(systems.Find(kRenderPassSystemId) == nullptr);
    assert(policy.GetRules().empty());
}

} // namespace

int main() {
    {
        HIKARI::RuntimeFeatureCatalog builtIn = HIKARI::CreateBuiltInRuntimeFeatureCatalog();
        assert(builtIn.FindOwningFeatureForComponent("WaterSurfaceComponent") == HIKARI::RuntimeFeatureIds::Water);
        assert(builtIn.FindOwningFeatureForComponent("LightComponent") == HIKARI::RuntimeFeatureIds::Lighting);
        assert(builtIn.FindOwningFeatureForSystem("WaterSurfaceSystem") == HIKARI::RuntimeFeatureIds::Water);
        assert(builtIn.FindOwningFeatureForComponent("BuoyancyComponent") == HIKARI::RuntimeFeatureIds::Buoyancy);
        assert(builtIn.FindOwningFeatureForComponent("TerrainSurfaceComponent") == HIKARI::RuntimeFeatureIds::Terrain);
        assert(builtIn.FindOwningFeatureForComponent("TerrainSurfaceFollowerComponent") ==
               HIKARI::RuntimeFeatureIds::SurfaceInteraction);
        assert(builtIn.FindOwningFeatureForSystem("TerrainInteractionSourceSystem") ==
               HIKARI::RuntimeFeatureIds::SurfaceInteraction);
        assert(builtIn.FindOwningFeatureForSystem("TerrainSurfaceSystem") ==
               HIKARI::RuntimeFeatureIds::SurfaceInteraction);
        const auto resolution = builtIn.ResolveEnabledFeatures({std::string(HIKARI::RuntimeFeatureIds::Buoyancy)});
        assert(resolution.success);
        assert((resolution.resolvedFeatures ==
                std::vector<std::string>{std::string(HIKARI::RuntimeFeatureIds::Rendering),
                                         std::string(HIKARI::RuntimeFeatureIds::Water),
                                         std::string(HIKARI::RuntimeFeatureIds::Physics),
                                         std::string(HIKARI::RuntimeFeatureIds::Buoyancy)}));
        const auto surfaceResolution =
            builtIn.ResolveEnabledFeatures({std::string(HIKARI::RuntimeFeatureIds::SurfaceInteraction)});
        assert(surfaceResolution.success);
        assert((surfaceResolution.resolvedFeatures ==
                std::vector<std::string>{std::string(HIKARI::RuntimeFeatureIds::Rendering),
                                         std::string(HIKARI::RuntimeFeatureIds::Terrain),
                                         std::string(HIKARI::RuntimeFeatureIds::Physics),
                                         std::string(HIKARI::RuntimeFeatureIds::SurfaceInteraction)}));
        const auto navigationResolution =
            builtIn.ResolveEnabledFeatures({std::string(HIKARI::RuntimeFeatureIds::Navigation)});
        assert(navigationResolution.success);
        assert((navigationResolution.resolvedFeatures ==
                std::vector<std::string>{std::string(HIKARI::RuntimeFeatureIds::Navigation)}));
        HIKARI::ComponentRegistry navigationComponents{};
        HIKARI::WorldMassSchemaRegistry navigationMassSchemas{};
        HIKARI::SystemTypeRegistry navigationSystems{};
        HIKARI::ComponentSystemPolicy navigationPolicy{};
        HIKARI::RuntimeFeatureContext navigationContext = HIKARI::RuntimeFeatureContextAccess::Create(
            navigationComponents, navigationMassSchemas, navigationSystems, navigationPolicy);
        const HIKARI::RuntimeFeatureInstallReport navigationInstall =
            builtIn.RegisterEnabled(navigationContext, {std::string(HIKARI::RuntimeFeatureIds::Navigation)});
        assert(navigationInstall.success);
        assert((navigationInstall.installedFeatures ==
                std::vector<std::string>{std::string(HIKARI::RuntimeFeatureIds::Navigation)}));
        assert(navigationComponents.GetTypeNames().empty());
        assert(navigationSystems.GetTypeNames().empty());
        assert(navigationPolicy.GetRules().empty());
        assert(builtIn.CreateDefaultSceneSystems().empty());
        const auto actionPresentationResolution =
            builtIn.ResolveEnabledFeatures({std::string(HIKARI::RuntimeFeatureIds::ActionPresentation)});
        assert(actionPresentationResolution.success);
        assert((actionPresentationResolution.resolvedFeatures ==
                std::vector<std::string>{std::string(HIKARI::RuntimeFeatureIds::Rendering),
                                         std::string(HIKARI::RuntimeFeatureIds::Vfx),
                                         std::string(HIKARI::RuntimeFeatureIds::ActionPresentation)}));
        const auto animationResolution =
            builtIn.ResolveEnabledFeatures({std::string(HIKARI::RuntimeFeatureIds::Animation)});
        assert(animationResolution.success);
        assert((animationResolution.resolvedFeatures ==
                std::vector<std::string>{std::string(HIKARI::RuntimeFeatureIds::Rendering),
                                         std::string(HIKARI::RuntimeFeatureIds::Animation)}));
        // 旧 UI の物体層は削除済み。"UI" はもう組み込みの機能ではない。
        assert(!builtIn.ResolveEnabledFeatures({"UI"}).success);
        // 直接描画の HUD は参照解像度へ letterbox で合わせる。
        HIKARI::GameProjectUiCanvasFit fit{};
        assert(HIKARI::ResolveGameProjectUiCanvasFit(1920u, 1200u, 1920.0f, 1080.0f, fit));
        assert(fit.scale == 1.0f);
        assert(fit.offsetY == 60.0f);
    }

    {
        HIKARI::ComponentRegistry scheduledComponents{};
        HIKARI::WorldMassSchemaRegistry scheduledMassSchemas{};
        HIKARI::SystemTypeRegistry scheduledSystems{};
        HIKARI::ComponentSystemPolicy scheduledPolicy{};
        HIKARI::RuntimeFeatureContext scheduledContext = HIKARI::RuntimeFeatureContextAccess::Create(
            scheduledComponents, scheduledMassSchemas, scheduledSystems, scheduledPolicy);
        assert(RegisterTransactionalPayload(scheduledContext));
        const HIKARI::SystemTypeInfo* scheduled = scheduledSystems.Find(kSystemId);
        assert(scheduled != nullptr);
        assert(scheduled->hasParallelMassSchedule);
        assert(scheduled->parallelMassSchedule.access.typeId == kMassType);
        assert(scheduled->parallelMassSchedule.partitionSize == 256u);
        const HIKARI::SystemTypeInfo* observer = scheduledSystems.Find(kObserverSystemId);
        assert(observer != nullptr);
        assert(observer->hasRenderObserver);
        assert(observer->renderObserver.stage == HIKARI::GameProjectRenderObserverStage::PostOpaque);
        const HIKARI::SystemTypeInfo* renderPass = scheduledSystems.Find(kRenderPassSystemId);
        assert(renderPass != nullptr);
        assert(renderPass->hasRenderPass);
        assert(std::string_view(renderPass->renderPass.stableId) == "Project.Test.Transactional");
        HIKARI::GameSystemDescriptor duplicatePassSystem{};
        duplicatePassSystem.systemId = "Test.DuplicateRenderPass";
        HIKARI::GameProjectRenderPassDesc duplicatePass{};
        assert(duplicatePass.SetStableId("Project.Test.Transactional"));
        duplicatePass.target = HIKARI::GameProjectRenderPassTarget::WorkingSceneColor;
        assert(!scheduledContext.RegisterRenderPassSystem<TransactionalSystem>(duplicatePassSystem, duplicatePass));
        assert(scheduledSystems.Find(duplicatePassSystem.systemId) == nullptr);

        HIKARI::SystemScheduler scheduler{};
        assert(scheduler.AddSystem(std::string(kRenderPassSystemId), 0,
                                   scheduledSystems.Create(kRenderPassSystemId, nlohmann::json::object())));
        HIKARI::World world{};
        scheduler.AttachWorld(world);
        uint32_t renderPassCount = 0u;
        assert(scheduler.GetRenderPassCount(renderPassCount));
        assert(renderPassCount == 1u);
        HIKARI::GameProjectRenderPassDesc activePass{};
        assert(scheduler.DescribeRenderPass(0u, activePass));
        HIKARI::GameProjectRenderCameraFrame camera{};
        camera.renderWidth = camera.renderHeight = 1u;
        camera.outputWidth = camera.outputHeight = 1u;
        HIKARI::GameProjectRenderFrameView frame{};
        frame.frame = {1u, 1u, 1u};
        frame.viewGeneration = 1u;
        frame.worldIdentity = world.GetRuntimeIdentity();
        frame.camera = &camera;
        ProbeRenderCommands renderCommands{};
        HIKARI::GameProjectRenderPassContext passContext{};
        passContext.frame = &frame;
        passContext.encoder.version = HIKARI::kGameProjectRenderCommandEncoderVersion;
        passContext.encoder.context = &renderCommands;
        passContext.encoder.commandGeneration = 1u;
        passContext.encoder.clearWorkingColor = &ProbeClearWorkingColor;
        passContext.encoder.drawFullscreen = &ProbeDrawFullscreen;
        passContext.encoder.capabilityMask =
            static_cast<uint32_t>(HIKARI::GameProjectRenderCommandCapability::ClearWorkingColor) |
            static_cast<uint32_t>(HIKARI::GameProjectRenderCommandCapability::ShaderCommands);
        bool callbackEntered = false;
        assert(scheduler.ExecuteRenderPass(activePass.stableId, passContext, &callbackEntered));
        assert(callbackEntered);
        assert(renderCommands.clearCount == 1u);
        assert(renderCommands.drawCount == 1u);
        renderCommands.failDraw = true;
        callbackEntered = false;
        assert(!scheduler.ExecuteRenderPass(activePass.stableId, passContext, &callbackEntered));
        assert(callbackEntered);
        renderCommands.failDraw = false;
        callbackEntered = true;
        assert(!scheduler.ExecuteRenderPass("Project.Test.Missing", passContext, &callbackEntered));
        assert(!callbackEntered);
        assert(scheduler.GetRenderPassFailureCount() == 2u);
        scheduler.DetachWorld(world);
    }

    HIKARI::ComponentRegistry components{};
    HIKARI::WorldMassSchemaRegistry massSchemas{};
    assert(
        massSchemas.Register(HIKARI::WorldMassSchema{kBaselineMassType, kBaselineMassLayoutHash, 1u,
                                                     sizeof(TransactionalMassData), alignof(TransactionalMassData)}));
    HIKARI::SystemTypeRegistry systems{};
    HIKARI::ComponentSystemPolicy policy{};
    HIKARI::RuntimeFeatureContext context =
        HIKARI::RuntimeFeatureContextAccess::Create(components, massSchemas, systems, policy);
    HIKARI::GameProjectWorldMassRenderProxyDesc prematureProxy{};
    prematureProxy.typeId = kMassType;
    prematureProxy.layoutHash = kMassLayoutHash;
    prematureProxy.geometry = HIKARI::GameProjectWorldMassRenderGeometry::BuiltInBox;
    assert(!context.RegisterMassRenderProxy(prematureProxy));
    HIKARI::GameProjectWorldMassRenderProxyDesc invalidProxy{};
    invalidProxy.typeId = kBaselineMassType;
    invalidProxy.layoutHash = kBaselineMassLayoutHash;
    invalidProxy.geometry = HIKARI::GameProjectWorldMassRenderGeometry::BuiltInBox;
    invalidProxy.reserved[0] = 1u;
    assert(!context.RegisterMassRenderProxy(invalidProxy));
    HIKARI::GameSystemDescriptor invalidParallelSystem{};
    invalidParallelSystem.systemId = "Test.InvalidParallelSystem";
    HIKARI::GameProjectMassSystemScheduleDesc invalidSchedule{};
    invalidSchedule.access.typeId = kBaselineMassType;
    invalidSchedule.access.readMask = static_cast<uint32_t>(HIKARI::GameProjectMassSystemField::ProjectData);
    invalidSchedule.reserved[0] = 1u;
    assert(!context.RegisterParallelMassSystem<TransactionalSystem>(invalidParallelSystem, invalidSchedule));
    assert(systems.Find(invalidParallelSystem.systemId) == nullptr);
    HIKARI::GameSystemDescriptor invalidObserverSystem{};
    invalidObserverSystem.systemId = "Test.InvalidRenderObserver";
    HIKARI::GameProjectRenderObserverDesc invalidObserver{};
    invalidObserver.reserved[0] = 1u;
    assert(!context.RegisterRenderObserverSystem<TransactionalSystem>(invalidObserverSystem, invalidObserver));
    assert(systems.Find(invalidObserverSystem.systemId) == nullptr);
    HIKARI::GameSystemDescriptor invalidRenderPassSystem{};
    invalidRenderPassSystem.systemId = "Test.InvalidRenderPass";
    HIKARI::GameProjectRenderPassDesc invalidRenderPass{};
    assert(invalidRenderPass.SetStableId("Project.Test.Invalid"));
    invalidRenderPass.target = HIKARI::GameProjectRenderPassTarget::WorkingSceneColor;
    invalidRenderPass.reserved[0] = 1u;
    assert(!context.RegisterRenderPassSystem<TransactionalSystem>(invalidRenderPassSystem, invalidRenderPass));
    assert(systems.Find(invalidRenderPassSystem.systemId) == nullptr);

    HIKARI::RuntimeFeatureCatalog catalog{};
    assert(catalog.Add(std::make_unique<FailedFeature>()));
    assert(catalog.Add(std::make_unique<ThrowingFeature>()));

    const HIKARI::RuntimeFeatureInstallReport rejected =
        catalog.RegisterEnabled(context, {std::string(kFailedFeatureId)});
    assert(!rejected.success);
    assert(rejected.failedFeatures.size() == 1u);
    AssertRegistryClean(components, massSchemas, systems, policy);

    const HIKARI::RuntimeFeatureInstallReport threw =
        catalog.RegisterEnabled(context, {std::string(kThrowingFeatureId)});
    assert(!threw.success);
    assert(threw.failedFeatures.size() == 1u);
    AssertRegistryClean(components, massSchemas, systems, policy);
    return 0;
}
