#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "Assets/Models/HIKARI_ModelManager.h"
#include "RenderContracts/Asset/HIKARI_AssetRegistry.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"

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

// 世界の物体ごとの名前・親・局所変換・型ごとの部品属性（同じ型の中の並びは保つ）。
nlohmann::json DescribeWorld(const HIKARI::World& world) {
    nlohmann::json objects = nlohmann::json::object();
    for (const auto& object : world.GetObjects()) {
        const HIKARI::Transform3D& local = object->GetTransform();
        nlohmann::json entry{{"name", object->GetName()},
                             {"parent", object->GetParent() ? object->GetParent()->GetDocumentId().value : 0u},
                             {"position", {local.position.x, local.position.y, local.position.z}},
                             {"rotation", {local.rotation.x, local.rotation.y, local.rotation.z, local.rotation.w}},
                             {"scale", {local.scale.x, local.scale.y, local.scale.z}},
                             {"components", nlohmann::json::object()}};
        for (const auto& component : object->GetComponents()) {
            nlohmann::json properties{};
            component->Serialize(properties);
            entry["components"][std::string(component->GetTypeName())].push_back(std::move(properties));
        }
        objects[std::to_string(object->GetDocumentId().value)] = std::move(entry);
    }
    return objects;
}

nlohmann::json DescribeRebuiltWorld(const HIKARI::SceneDocument& document, const HIKARI::ComponentRegistry& components,
                                    const HIKARI::AssetRegistry& assets, HIKARI::ModelManager& models) {
    HIKARI::World world{};
    assert(world.BindComponentRegistry(&components));
    std::vector<const HIKARI::SceneObjectData*> objects{};
    for (const HIKARI::SceneObjectData& object : document.objects) {
        objects.push_back(&object);
    }
    assert(HIKARI::SceneRuntimeBuilder{}.AddObjectsFromDocument(objects, world, assets, components, models));
    return DescribeWorld(world);
}

// 編集をその場で世界へ当て続けた結果が、同じ document から作り直した世界と項目ごとに一致する。最後に document を
// 最初の状態へ戻して（全部の取り消しに当たる）当てると、最初に作った世界と一致する。
void InPlaceEditsMatchRebuild(const HIKARI::ComponentRegistry& components, const std::filesystem::path& scratch) {
    constexpr std::array<std::string_view, 8> kPool{
        "LightComponent", "CameraComponent",          "SpawnPointComponent", "WorldTextComponent",
        "DecalComponent", "ReflectionProbeComponent", "ModelComponent",      "WorldSpriteComponent"};
    const HIKARI::AssetRegistry assets{};
    HIKARI::ModelManager models{};
    const HIKARI::SceneRuntimeBuilder builder{};
    std::mt19937 random{20261108u};
    const auto pick = [&](size_t count) { return static_cast<size_t>(random() % count); };
    const auto defaults = [&](std::string_view type) {
        nlohmann::json properties{};
        components.Find(std::string(type))->factory()->Serialize(properties);
        return properties;
    };

    HIKARI::SceneDocument document{};
    uint64_t nextId = 1u;
    const auto newObject = [&]() {
        HIKARI::SceneObjectData object{};
        object.id = {nextId++};
        object.name = "Object" + std::to_string(object.id.value);
        object.transform.position = {static_cast<float>(pick(50)), 0.0f, static_cast<float>(pick(50))};
        for (size_t count = pick(3); count-- > 0u;) {
            const std::string_view type = kPool[pick(kPool.size())];
            object.components.push_back({std::string(type), defaults(type)});
        }
        if (!document.objects.empty() && pick(3) == 0u) {
            object.parent = document.objects[pick(document.objects.size())].id;
        }
        document.objects.push_back(std::move(object));
    };
    for (int index = 0; index < 40; ++index) {
        newObject();
    }

    HIKARI::World world{};
    assert(world.BindComponentRegistry(&components));
    {
        std::vector<const HIKARI::SceneObjectData*> objects{};
        for (const HIKARI::SceneObjectData& object : document.objects) {
            objects.push_back(&object);
        }
        assert(builder.AddObjectsFromDocument(objects, world, assets, components, models));
    }
    const HIKARI::SceneDocument initialDocument = document;
    const nlohmann::json initialWorld = DescribeWorld(world);
    std::unordered_set<uint64_t> touched{};

    const auto isAncestor = [&](HIKARI::SceneObjectId ancestor, HIKARI::SceneObjectId object) {
        for (std::optional<HIKARI::SceneObjectId> cursor = object; cursor.has_value();) {
            if (cursor->value == ancestor.value) {
                return true;
            }
            const auto found = std::find_if(document.objects.begin(), document.objects.end(),
                                            [&](const HIKARI::SceneObjectData& data) { return data.id == *cursor; });
            cursor = found != document.objects.end() ? found->parent : std::nullopt;
        }
        return false;
    };
    // 数値か真偽の葉を一つ選んで変え、部品に読ませて書き戻した値（正規化済み）を返す。読めない値なら元のまま。
    const auto mutate = [&](const HIKARI::SceneComponentData& component) {
        std::vector<nlohmann::json::json_pointer> leaves{};
        const nlohmann::json flat = component.properties.flatten();
        for (const auto& item : flat.items()) {
            if (item.value().is_number() || item.value().is_boolean()) {
                leaves.emplace_back(item.key());
            }
        }
        if (leaves.empty()) {
            return component.properties;
        }
        nlohmann::json edited = component.properties;
        nlohmann::json& leaf = edited[leaves[pick(leaves.size())]];
        if (leaf.is_boolean()) {
            leaf = !leaf.get<bool>();
        } else if (leaf.is_number_float()) {
            leaf = leaf.get<double>() + 0.25 * static_cast<double>(1 + pick(4));
        } else {
            leaf = leaf.get<int64_t>() + 1;
        }
        std::unique_ptr<HIKARI::IComponent> probe = components.Find(component.type)->factory();
        std::string diagnostic{};
        if (!probe->TryDeserialize(edited, diagnostic)) {
            return component.properties;
        }
        nlohmann::json normalized{};
        probe->Serialize(normalized);
        return normalized;
    };

    constexpr int kEdits = 1000;
    for (int step = 0; step < kEdits; ++step) {
        HIKARI::SceneObjectChanges requested{};
        HIKARI::SceneObjectData& target = document.objects[pick(document.objects.size())];
        switch (pick(11)) {
        case 0:
            target.name += "_";
            requested.changed.push_back(target.id);
            break;
        case 1:
            target.transform.position.y += 1.0f;
            target.transform.rotationEulerDeg.y += 15.0f;
            requested.changed.push_back(target.id);
            break;
        case 2: {
            const HIKARI::SceneObjectId parent = document.objects[pick(document.objects.size())].id;
            if (pick(4) == 0u) {
                target.parent.reset();
            } else if (!isAncestor(target.id, parent)) {
                target.parent = parent;
            }
            requested.changed.push_back(target.id);
            break;
        }
        case 3: {
            const std::string_view type = kPool[pick(kPool.size())];
            target.components.insert(target.components.begin() +
                                         static_cast<std::ptrdiff_t>(pick(target.components.size() + 1u)),
                                     {std::string(type), defaults(type)});
            requested.changed.push_back(target.id);
            break;
        }
        case 4:
            if (!target.components.empty()) {
                target.components.erase(target.components.begin() +
                                        static_cast<std::ptrdiff_t>(pick(target.components.size())));
            }
            requested.changed.push_back(target.id);
            break;
        case 5:
        case 6:
        case 7:
            if (!target.components.empty()) {
                HIKARI::SceneComponentData& component = target.components[pick(target.components.size())];
                component.properties = mutate(component);
            }
            requested.changed.push_back(target.id);
            break;
        case 8:
        case 9:
            newObject();
            requested.added.push_back(document.objects.back().id);
            break;
        default: {
            // 物体と子孫を消す（エディタの削除と同じ）。
            const HIKARI::SceneObjectId root = target.id;
            for (const HIKARI::SceneObjectData& object : document.objects) {
                if (isAncestor(root, object.id)) {
                    requested.removed.push_back(object.id);
                }
            }
            std::erase_if(document.objects, [&](const HIKARI::SceneObjectData& object) {
                return std::find(requested.removed.begin(), requested.removed.end(), object.id) !=
                       requested.removed.end();
            });
            if (document.objects.empty()) {
                newObject();
                requested.added.push_back(document.objects.back().id);
            }
            break;
        }
        }
        for (const std::vector<HIKARI::SceneObjectId>* ids :
             {&requested.added, &requested.changed, &requested.removed}) {
            for (HIKARI::SceneObjectId id : *ids) {
                touched.insert(id.value);
            }
        }
        const HIKARI::SceneObjectChanges changes = builder.ClassifyObjectChanges(document, requested, world);
        std::vector<HIKARI::RuntimeObjectHandle> handles{};
        for (HIKARI::SceneObjectId id : changes.changed) {
            handles.push_back(world.FindObject(id)->GetRuntimeHandle());
        }
        assert(builder.ApplyObjectChanges(document, changes, world, assets, components, models));
        // 変更分は壊さずに直す：句柄が変わらない。
        for (size_t index = 0u; index < handles.size(); ++index) {
            assert(world.FindObject(changes.changed[index])->GetRuntimeHandle() == handles[index]);
        }
        if ((step + 1) % 250 == 0) {
            const bool same = DescribeWorld(world) == DescribeRebuiltWorld(document, components, assets, models);
            if (!same) {
                std::cerr << "in-place edits diverged from rebuild after " << (step + 1) << " edits\n";
            }
            assert(same);
        }
    }

    // 保存して読み直してもう一度保存すると、一度目と同じ内容になる。
    const HIKARI::SceneSerializer serializer{};
    std::filesystem::create_directories(scratch);
    const std::filesystem::path first = scratch / "first.hscene";
    const std::filesystem::path second = scratch / "second.hscene";
    HIKARI::SceneDocument reloaded{};
    assert(serializer.SaveToFile(first.string(), document) && serializer.LoadFromFile(first.string(), reloaded) &&
           serializer.SaveToFile(second.string(), reloaded));
    const auto readBytes = [](const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };
    const std::string savedOnce = readBytes(first);
    assert(!savedOnce.empty() && savedOnce == readBytes(second));
    std::filesystem::remove_all(scratch);

    // 全部の取り消し：document を最初へ戻し、触れた全物体を一つの差分として当てる。
    const size_t editedObjects = document.objects.size();
    document = initialDocument;
    HIKARI::SceneObjectChanges undoAll{};
    for (uint64_t id : touched) {
        undoAll.changed.push_back({id});
    }
    assert(builder.ApplyObjectChanges(document, builder.ClassifyObjectChanges(document, undoAll, world), world, assets,
                                      components, models));
    assert(DescribeWorld(world) == initialWorld);
    std::cout << "in-place edits: " << kEdits << " edits match rebuild, " << touched.size()
              << " touched objects restored, " << editedObjects << " objects before undo, second save identical ("
              << savedOnce.size() << " bytes)\n";
}

} // namespace

int main(int, char** argv) {
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
        // 全ての組み込み部品：既定値を書いて読み直して書くと同じ JSON。空の object を読むと既定値になる
        // （保存で既定値の項目を省けることの前提）。
        {
            HIKARI::ComponentRegistry allComponents{};
            HIKARI::WorldMassSchemaRegistry allMassSchemas{};
            HIKARI::SystemTypeRegistry allSystems{};
            HIKARI::ComponentSystemPolicy allPolicy{};
            HIKARI::RuntimeFeatureContext allContext =
                HIKARI::RuntimeFeatureContextAccess::Create(allComponents, allMassSchemas, allSystems, allPolicy);
            HIKARI::RuntimeFeatureCatalog everything = HIKARI::CreateBuiltInRuntimeFeatureCatalog();
            assert(everything.RegisterEnabled(allContext, everything.GetFeatureIds()).success);
            size_t checked = 0u;
            size_t failures = 0u;
            for (const HIKARI::ComponentTypeInfo* info : allComponents.GetTypeInfos()) {
                std::unique_ptr<HIKARI::IComponent> original = info->factory();
                nlohmann::json written{};
                original->Serialize(written);
                std::unique_ptr<HIKARI::IComponent> reread = info->factory();
                std::string diagnostic{};
                const bool readBack = reread->TryDeserialize(written, diagnostic);
                nlohmann::json rewritten{};
                reread->Serialize(rewritten);
                std::unique_ptr<HIKARI::IComponent> empty = info->factory();
                const bool readEmpty = empty->TryDeserialize(nlohmann::json::object(), diagnostic);
                nlohmann::json emptyWritten{};
                empty->Serialize(emptyWritten);
                const char* failure = !readBack                 ? "read back"
                                      : rewritten != written    ? "rewrite differs"
                                      : !readEmpty              ? "empty object rejected"
                                      : emptyWritten != written ? "empty object is not the default"
                                                                : nullptr;
                if (failure != nullptr) {
                    std::cerr << "component round trip failed: " << info->typeName << " (" << failure << ") "
                              << diagnostic << "\n  default: " << written.dump()
                              << "\n  reread:  " << (rewritten != written ? rewritten : emptyWritten).dump() << '\n';
                    ++failures;
                }
                ++checked;
            }
            std::cout << "component round trip: " << checked << " built-in component types, " << failures
                      << " failed\n";
            assert(checked > 20u && failures == 0u);
            InPlaceEditsMatchRebuild(allComponents,
                                     std::filesystem::path(argv[0]).parent_path() / "runtime-feature-in-place-edits");
        }
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
