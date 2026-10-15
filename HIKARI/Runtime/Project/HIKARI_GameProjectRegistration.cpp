#include "HIKARI/Project/RuntimeRegistration.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Core/HIKARI_Logger.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/Features/HIKARI_RuntimeFeatureContextAccess.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectBindingConversion.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_WorldMassStore.h"

namespace HIKARI {
namespace {

std::vector<std::string> CopyNames(std::span<const std::string_view> names) {
    std::vector<std::string> result;
    result.reserve(names.size());
    for (const std::string_view name : names) {
        if (!name.empty()) {
            result.emplace_back(name);
        }
    }
    return result;
}

std::string MakeGameComponentBindingWriterId() {
    static std::atomic_uint64_t nextWriterId{1u};
    return "game-component/" + std::to_string(nextWriterId.fetch_add(1u, std::memory_order_relaxed));
}

template <class Callback>
bool InvokeProjectCallback(std::string_view owner, std::string_view callback, Callback&& value) noexcept {
    try {
        std::forward<Callback>(value)();
        return true;
    } catch (const std::exception& error) {
        HIKARI_LOG_ERROR("[GameProjectSdk] " + std::string(owner) + " " + std::string(callback) +
                         " failed: " + error.what());
    } catch (...) {
        HIKARI_LOG_ERROR("[GameProjectSdk] " + std::string(owner) + " " + std::string(callback) +
                         " failed with an unknown exception.");
    }
    return false;
}

BINDING::RuntimeBindingStore* FindObjectBindingStore(const WorldView& world, const GameObject* owner) noexcept {
    World* runtimeWorld = GameProjectSdkAccess::GetWorld(world);
    return runtimeWorld != nullptr && owner != nullptr && owner->GetWorld() == runtimeWorld
               ? runtimeWorld->Services().Find<BINDING::RuntimeBindingStore>()
               : nullptr;
}

} // namespace

GameComponentContext GameProjectSdkAccess::MakeGameComponentContext(GameProjectComponentAdapter& adapter,
                                                                    GameObject& object, World& world) noexcept {
    return GameComponentContext(MakeObjectView(object), MakeWorldView(world), &adapter);
}

bool GameComponentContext::WriteVariable(std::string_view variableId,
                                         const GameProjectBindingValue& value) const noexcept {
    auto* adapter = static_cast<GameProjectComponentAdapter*>(bindingOwner_);
    return adapter != nullptr && adapter->WriteVariable(world_, variableId, value);
}

bool GameComponentContext::ReleaseVariable(std::string_view variableId) const noexcept {
    auto* adapter = static_cast<GameProjectComponentAdapter*>(bindingOwner_);
    return adapter != nullptr && adapter->ReleaseVariable(world_, variableId);
}

bool GameComponentContext::WriteObjectVariable(std::string_view variableId,
                                               const GameProjectBindingValue& value) const noexcept {
    auto* adapter = static_cast<GameProjectComponentAdapter*>(bindingOwner_);
    return adapter != nullptr && adapter->WriteObjectVariable(world_, variableId, value);
}

bool GameComponentContext::ReleaseObjectVariable(std::string_view variableId) const noexcept {
    auto* adapter = static_cast<GameProjectComponentAdapter*>(bindingOwner_);
    return adapter != nullptr && adapter->ReleaseObjectVariable(world_, variableId);
}

GameProjectComponentAdapter::GameProjectComponentAdapter(std::string typeName, IGameComponent* component,
                                                         DestroyGameComponentFn destroy)
    : typeName_(std::move(typeName)), bindingWriterId_(MakeGameComponentBindingWriterId()), component_(component),
      destroy_(destroy) {}

GameProjectComponentAdapter::~GameProjectComponentAdapter() {
    if (component_ != nullptr && destroy_ != nullptr) {
        destroy_(component_);
    }
    component_ = nullptr;
}

std::string_view GameProjectComponentAdapter::GetTypeName() const { return typeName_; }

void GameProjectComponentAdapter::OnAttach() {
    if (component_ == nullptr || GetOwner() == nullptr) {
        return;
    }
    GameObject& owner = *GetOwner();
    World* world = owner.GetWorld();
    if (world == nullptr) {
        // World 外の一時オブジェクト（Inspector の既定値作成など）ではゲーム処理を走らせない。
        return;
    }
    GameComponentContext context = GameProjectSdkAccess::MakeGameComponentContext(*this, owner, *world);
    if (!InvokeProjectCallback(typeName_, "OnAttach", [this, &context]() { component_->OnAttach(context); })) {
        ReleaseAllVariables(context.GetWorld());
        throw std::runtime_error("Game project component OnAttach failed.");
    }
}

void GameProjectComponentAdapter::OnDetach() {
    if (component_ == nullptr || GetOwner() == nullptr) {
        return;
    }
    GameObject& owner = *GetOwner();
    World* world = owner.GetWorld();
    if (world == nullptr) {
        return;
    }
    GameComponentContext context = GameProjectSdkAccess::MakeGameComponentContext(*this, owner, *world);
    InvokeProjectCallback(typeName_, "OnDetach", [this, &context]() { component_->OnDetach(context); });
    ReleaseAllVariables(context.GetWorld());
}

void GameProjectComponentAdapter::Update(float deltaTime) {
    if (component_ == nullptr || GetOwner() == nullptr) {
        return;
    }
    GameObject& owner = *GetOwner();
    World* world = owner.GetWorld();
    if (world == nullptr) {
        return;
    }
    GameComponentContext context = GameProjectSdkAccess::MakeGameComponentContext(*this, owner, *world);
    InvokeProjectCallback(typeName_, "Update",
                          [this, &context, deltaTime]() { component_->Update(context, deltaTime); });
}

void GameProjectComponentAdapter::Serialize(nlohmann::json& out) const {
    out = nlohmann::json::object();
    if (component_ == nullptr) {
        return;
    }
    PropertyFields fields = GameProjectSdkAccess::MakePropertyFields(PropertyFieldsMode::Save, nullptr, &out, nullptr);
    InvokeProjectCallback(typeName_, "DescribeProperties",
                          [this, &fields]() { component_->DescribeProperties(fields); });
}

void GameProjectComponentAdapter::Deserialize(const nlohmann::json& in) {
    std::string diagnostic{};
    (void)TryDeserialize(in, diagnostic);
}

bool GameProjectComponentAdapter::TryDeserialize(const nlohmann::json& in, std::string& outDiagnostic) {
    if (component_ == nullptr) {
        outDiagnostic = "GameProject component instance is unavailable.";
        return false;
    }
    PropertyFields fields = GameProjectSdkAccess::MakePropertyFields(PropertyFieldsMode::Load, &in, nullptr, nullptr);
    const bool loaded = InvokeProjectCallback(typeName_, "DescribeProperties",
                                              [this, &fields]() { component_->DescribeProperties(fields); });
    outDiagnostic = loaded ? std::string{} : "GameProject DescribeProperties failed while loading; see the engine log.";
    return loaded;
}

void GameProjectComponentAdapter::BuildInspector(IInspectorBuilder& builder) {
    if (component_ == nullptr) {
        return;
    }
    PropertyFields fields =
        GameProjectSdkAccess::MakePropertyFields(PropertyFieldsMode::Inspect, nullptr, nullptr, &builder);
    InvokeProjectCallback(typeName_, "DescribeProperties",
                          [this, &fields]() { component_->DescribeProperties(fields); });
}

IGameComponent* GameProjectComponentAdapter::GetGameComponent() noexcept { return component_; }

bool GameProjectComponentAdapter::WriteVariable(const WorldView& world, std::string_view variableId,
                                                const GameProjectBindingValue& value) noexcept {
    if (variableId.empty() || bindingWriterId_.empty()) {
        return false;
    }
    try {
        const auto [written, inserted] = writtenVariableIds_.emplace(variableId);
        if (world.WriteVariable(variableId, value, bindingWriterId_)) {
            return true;
        }
        if (inserted) {
            writtenVariableIds_.erase(written);
        }
        return false;
    } catch (...) {
        return false;
    }
}

bool GameProjectComponentAdapter::ReleaseVariable(const WorldView& world, std::string_view variableId) noexcept {
    for (auto written = writtenVariableIds_.begin(); written != writtenVariableIds_.end(); ++written) {
        if (*written != variableId) {
            continue;
        }
        if (!world.ReleaseVariableWriter(*written, bindingWriterId_)) {
            return false;
        }
        writtenVariableIds_.erase(written);
        return true;
    }
    return false;
}

bool GameProjectComponentAdapter::WriteObjectVariable(const WorldView& world, std::string_view variableId,
                                                      const GameProjectBindingValue& value) noexcept {
    GameObject* owner = GetOwner();
    BINDING::RuntimeBindingStore* store = FindObjectBindingStore(world, owner);
    if (variableId.empty() || bindingWriterId_.empty() || owner == nullptr || store == nullptr) {
        return false;
    }
    try {
        const auto [written, inserted] = writtenObjectVariableIds_.emplace(variableId);
        if (store->WriteObjectVariable(owner->GetRuntimeHandle(), variableId, PROJECT::DETAIL::ToRuntimeValue(value),
                                       bindingWriterId_)) {
            return true;
        }
        if (inserted) {
            writtenObjectVariableIds_.erase(written);
        }
        return false;
    } catch (...) {
        return false;
    }
}

bool GameProjectComponentAdapter::ReleaseObjectVariable(const WorldView& world, std::string_view variableId) noexcept {
    GameObject* owner = GetOwner();
    BINDING::RuntimeBindingStore* store = FindObjectBindingStore(world, owner);
    if (owner == nullptr || store == nullptr) {
        return false;
    }
    for (auto written = writtenObjectVariableIds_.begin(); written != writtenObjectVariableIds_.end(); ++written) {
        if (*written != variableId) {
            continue;
        }
        if (!store->ReleaseObjectVariableWriter(owner->GetRuntimeHandle(), *written, bindingWriterId_)) {
            return false;
        }
        writtenObjectVariableIds_.erase(written);
        return true;
    }
    return false;
}

void GameProjectComponentAdapter::ReleaseAllVariables(const WorldView& world) noexcept {
    for (const std::string& variableId : writtenVariableIds_) {
        (void)world.ReleaseVariableWriter(variableId, bindingWriterId_);
    }
    writtenVariableIds_.clear();
    if (GameObject* owner = GetOwner()) {
        if (BINDING::RuntimeBindingStore* store = FindObjectBindingStore(world, owner); store != nullptr) {
            store->ReleaseObjectVariableWriters(owner->GetRuntimeHandle(), bindingWriterId_);
        }
    }
    writtenObjectVariableIds_.clear();
}

GameProjectSystemAdapter::GameProjectSystemAdapter(std::string systemId, IGameSystem* system,
                                                   DestroyGameSystemFn destroy,
                                                   GameProjectSystemExtensions extensions) noexcept
    : systemId_(std::move(systemId)), system_(system), destroy_(destroy), extensions_(extensions) {}

GameProjectSystemAdapter::~GameProjectSystemAdapter() {
    if (system_ != nullptr && destroy_ != nullptr) {
        destroy_(system_);
    }
    system_ = nullptr;
}

std::string_view GameProjectSystemAdapter::GetName() const { return systemId_; }

const GameProjectMassSystemScheduleDesc& GameProjectSystemAdapter::GetMassSystemSchedule() const noexcept {
    return extensions_.massSchedule;
}

bool GameProjectSystemAdapter::ExecuteMassPartition(const GameProjectMassPartitionInput& input,
                                                    GameProjectMassPartitionOutput& output,
                                                    const GameProjectMassCommandWriter& commands,
                                                    const FrameContext& frame) const noexcept {
    return system_ != nullptr && extensions_.executeMass != nullptr &&
           extensions_.executeMass(system_, &input, &output, &commands, &frame);
}

const GameProjectRenderObserverDesc& GameProjectSystemAdapter::GetRenderObserverDesc() const noexcept {
    return extensions_.renderObserver;
}

bool GameProjectSystemAdapter::ObserveRenderFrame(const GameProjectRenderFrameView& frame) const noexcept {
    return system_ != nullptr && extensions_.observeRender != nullptr && extensions_.observeRender(system_, &frame);
}

const GameProjectRenderPassDesc& GameProjectSystemAdapter::GetRenderPassDesc() const noexcept {
    return extensions_.renderPass;
}

bool GameProjectSystemAdapter::ExecuteRenderPass(const GameProjectRenderPassContext& context) const noexcept {
    return system_ != nullptr && extensions_.executeRenderPass != nullptr &&
           extensions_.executeRenderPass(system_, &context);
}

#define HIKARI_INVOKE_GAME_SYSTEM(Name, Method)                                                                        \
    void GameProjectSystemAdapter::Name(World& world, const FrameContext& frame) {                                     \
        if (system_ == nullptr) {                                                                                      \
            return;                                                                                                    \
        }                                                                                                              \
        WorldView view = GameProjectSdkAccess::MakeWorldView(world);                                                   \
        InvokeProjectCallback(systemId_, #Method, [this, &view, &frame]() { system_->Method(view, frame); });          \
    }

void GameProjectSystemAdapter::OnWorldAttached(World& world) {
    if (system_ == nullptr) {
        return;
    }
    WorldView view = GameProjectSdkAccess::MakeWorldView(world);
    InvokeProjectCallback(systemId_, "OnWorldAttached", [this, &view]() { system_->OnWorldAttached(view); });
}

void GameProjectSystemAdapter::OnWorldDetached(World& world) {
    if (system_ == nullptr) {
        return;
    }
    WorldView view = GameProjectSdkAccess::MakeWorldView(world);
    InvokeProjectCallback(systemId_, "OnWorldDetached", [this, &view]() { system_->OnWorldDetached(view); });
}

HIKARI_INVOKE_GAME_SYSTEM(PreUpdate, PreUpdate)
HIKARI_INVOKE_GAME_SYSTEM(PreFixedUpdate, PreFixedUpdate)
HIKARI_INVOKE_GAME_SYSTEM(FixedUpdate, FixedUpdate)
HIKARI_INVOKE_GAME_SYSTEM(PostFixedUpdate, PostFixedUpdate)
HIKARI_INVOKE_GAME_SYSTEM(Update, Update)
HIKARI_INVOKE_GAME_SYSTEM(LateUpdate, LateUpdate)
HIKARI_INVOKE_GAME_SYSTEM(PreRender, PreRender)
HIKARI_INVOKE_GAME_SYSTEM(Render, Render)
HIKARI_INVOKE_GAME_SYSTEM(PostRender, PostRender)

#undef HIKARI_INVOKE_GAME_SYSTEM

bool RuntimeFeatureContext::RegisterComponentRaw(GameComponentDescriptor descriptor) {
    if (componentRegistry_ == nullptr || descriptor.typeName.empty() || descriptor.create == nullptr ||
        descriptor.destroy == nullptr) {
        return false;
    }

    const std::string typeName(descriptor.typeName);
    const ComponentTypeId computedTypeId = MakeComponentTypeId(typeName);
    if (!computedTypeId.IsValid()) {
        return false;
    }
    const CreateGameComponentFn create = descriptor.create;
    const DestroyGameComponentFn destroy = descriptor.destroy;

    ComponentTypeInfo info{};
    info.typeName = typeName;
    info.typeId = computedTypeId;
    info.factory = [typeName, create, destroy]() -> std::unique_ptr<IComponent> {
        IGameComponent* component = create();
        if (component == nullptr) {
            return {};
        }
        try {
            return std::make_unique<GameProjectComponentAdapter>(typeName, component, destroy);
        } catch (...) {
            destroy(component);
            return {};
        }
    };
    info.requiredComponents = CopyNames(descriptor.requiredComponents);
    info.optionalComponents = CopyNames(descriptor.optionalComponents);
    info.incompatibleComponents = CopyNames(descriptor.incompatibleComponents);
    info.allowMultiple = descriptor.allowMultiple;
    info.registrationSource = ComponentRegistrationSource::GameProject;
    info.runtimeApplyPolicy = descriptor.applyPolicy == GameComponentApplyPolicy::OnEditCommit
                                  ? ComponentRuntimeApplyPolicy::OnEditCommit
                                  : ComponentRuntimeApplyPolicy::Immediate;
    info.initializeDefaults = [create, destroy](const SceneObjectData&, nlohmann::json& properties) {
        IGameComponent* component = create();
        if (component == nullptr) {
            properties = nlohmann::json::object();
            return;
        }
        properties = nlohmann::json::object();
        PropertyFields fields =
            GameProjectSdkAccess::MakePropertyFields(PropertyFieldsMode::Save, nullptr, &properties, nullptr);
        InvokeProjectCallback("GameProjectComponent", "SaveDefaults",
                              [component, &fields]() { component->DescribeProperties(fields); });
        destroy(component);
    };
    info.presentation.displayName = descriptor.displayName.empty() ? typeName : std::string(descriptor.displayName);
    info.presentation.category = descriptor.category.empty() ? "Game" : std::string(descriptor.category);
    info.presentation.description = std::string(descriptor.description);
    info.presentation.featureId = std::string(featureId_);
    info.gameProjectTypeToken = descriptor.typeToken;
    return RuntimeFeatureContextAccess::Components(*this).Register(std::move(info));
}

namespace {

bool RegisterGameSystemType(SystemTypeRegistry& registry, std::string_view featureId, GameSystemDescriptor descriptor,
                            GameProjectSystemExtensions extensions) {
    if (descriptor.systemId.empty() || descriptor.create == nullptr || descriptor.destroy == nullptr) {
        return false;
    }

    const std::string systemId(descriptor.systemId);
    const CreateGameSystemFn create = descriptor.create;
    const DestroyGameSystemFn destroy = descriptor.destroy;

    nlohmann::json defaultSettings = nlohmann::json::object();
    if (IGameSystem* defaults = create()) {
        PropertyFields fields =
            GameProjectSdkAccess::MakePropertyFields(PropertyFieldsMode::Save, nullptr, &defaultSettings, nullptr);
        InvokeProjectCallback(systemId, "DescribeSettings",
                              [defaults, &fields]() { defaults->DescribeSettings(fields); });
        destroy(defaults);
    }

    SystemTypeInfo info{};
    info.systemId = systemId;
    info.factory = [systemId, create, destroy, extensions](const nlohmann::json& settings) -> std::unique_ptr<ISystem> {
        IGameSystem* system = create();
        if (system == nullptr) {
            return {};
        }
        PropertyFields fields =
            GameProjectSdkAccess::MakePropertyFields(PropertyFieldsMode::Load, &settings, nullptr, nullptr);
        bool loaded = true;
        try {
            system->DescribeSettings(fields);
        } catch (...) {
            loaded = false;
        }
        if (!loaded) {
            destroy(system);
            return {};
        }
        return std::make_unique<GameProjectSystemAdapter>(systemId, system, destroy, extensions);
    };
    info.displayName = descriptor.displayName.empty() ? systemId : std::string(descriptor.displayName);
    info.featureId = std::string(featureId);
    info.settings.defaultSettings = std::move(defaultSettings);
    info.hasParallelMassSchedule = extensions.executeMass != nullptr;
    info.parallelMassSchedule = extensions.massSchedule;
    info.hasRenderObserver = extensions.observeRender != nullptr;
    info.renderObserver = extensions.renderObserver;
    info.hasRenderPass = extensions.executeRenderPass != nullptr;
    info.renderPass = extensions.renderPass;
    return registry.Register(std::move(info));
}

} // namespace

bool RuntimeFeatureContext::RegisterSystemRaw(GameSystemDescriptor descriptor) {
    return systemTypeRegistry_ != nullptr &&
           RegisterGameSystemType(RuntimeFeatureContextAccess::Systems(*this), featureId_, descriptor,
                                  GameProjectSystemExtensions{});
}

bool RuntimeFeatureContext::RegisterParallelMassSystemRaw(GameSystemDescriptor descriptor,
                                                          const GameProjectMassSystemScheduleDesc& schedule,
                                                          ExecuteGameProjectMassPartitionFn execute) {
    GameProjectSystemExtensions extensions{};
    extensions.massSchedule = schedule;
    extensions.executeMass = execute;
    return systemTypeRegistry_ != nullptr && massSchemaRegistry_ != nullptr && execute != nullptr &&
           IsValidParallelMassSystemSchedule(schedule) &&
           RuntimeFeatureContextAccess::MassSchemas(*this).Find(schedule.access.typeId) != nullptr &&
           RegisterGameSystemType(RuntimeFeatureContextAccess::Systems(*this), featureId_, descriptor, extensions);
}

bool RuntimeFeatureContext::RegisterRenderObserverSystemRaw(GameSystemDescriptor descriptor,
                                                            const GameProjectRenderObserverDesc& observer,
                                                            ObserveGameProjectRenderFrameFn observe) {
    GameProjectSystemExtensions extensions{};
    extensions.renderObserver = observer;
    extensions.observeRender = observe;
    return systemTypeRegistry_ != nullptr && observe != nullptr && IsValidRenderObserverDesc(observer) &&
           RegisterGameSystemType(RuntimeFeatureContextAccess::Systems(*this), featureId_, descriptor, extensions);
}

bool RuntimeFeatureContext::RegisterRenderPassSystemRaw(GameSystemDescriptor descriptor,
                                                        const GameProjectRenderPassDesc& pass,
                                                        ExecuteGameProjectRenderPassFn execute) {
    GameProjectSystemExtensions extensions{};
    extensions.renderPass = pass;
    extensions.executeRenderPass = execute;
    return systemTypeRegistry_ != nullptr && execute != nullptr && IsValidRenderPassDesc(pass) &&
           RegisterGameSystemType(RuntimeFeatureContextAccess::Systems(*this), featureId_, descriptor, extensions);
}

bool RuntimeFeatureContext::RegisterMassDataRaw(const GameProjectMassDataTypeDesc& descriptor) {
    if (massSchemaRegistry_ == nullptr || descriptor.structSize != kGameProjectMassDataTypeDescV1Size ||
        descriptor.version != kGameProjectWorldMassVersion || descriptor.reserved != 0u) {
        return false;
    }
    return RuntimeFeatureContextAccess::MassSchemas(*this).Register(
        WorldMassSchema{descriptor.typeId, descriptor.layoutHash, descriptor.schemaVersion, descriptor.dataSize,
                        descriptor.dataAlignment});
}

bool RuntimeFeatureContext::RegisterMassRenderProxy(const GameProjectWorldMassRenderProxyDesc& descriptor) {
    return massSchemaRegistry_ != nullptr &&
           RuntimeFeatureContextAccess::MassSchemas(*this).RegisterRenderProxy(descriptor);
}

bool RuntimeFeatureContext::BindComponentSystem(ComponentSystemBinding binding) {
    if (componentSystemPolicy_ == nullptr) {
        return false;
    }
    return RuntimeFeatureContextAccess::Policy(*this).Register(ComponentSystemRule{
        std::string(binding.componentType), std::string(binding.systemId), binding.defaultExecutionOrder});
}

} // namespace HIKARI
