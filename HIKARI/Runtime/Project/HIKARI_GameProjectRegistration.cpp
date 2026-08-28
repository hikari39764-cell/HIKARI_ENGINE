#include "HIKARI/Project/RuntimeRegistration.h"

#include <atomic>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Core/HIKARI_Logger.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
namespace {

    std::vector<std::string> CopyNames(
        std::span<const std::string_view> names) {
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
        static std::atomic_uint64_t nextWriterId{ 1u };
        return "game-component/" + std::to_string(
            nextWriterId.fetch_add(1u, std::memory_order_relaxed));
    }

    template<class Callback>
    bool InvokeProjectCallback(
        std::string_view owner,
        std::string_view callback,
        Callback&& value) noexcept {
        try {
            std::forward<Callback>(value)();
            return true;
        }
        catch (const std::exception& error) {
            HIKARI_LOG_ERROR(
                "[GameProjectSdk] " + std::string(owner) +
                " " + std::string(callback) +
                " failed: " + error.what());
        }
        catch (...) {
            HIKARI_LOG_ERROR(
                "[GameProjectSdk] " + std::string(owner) +
                " " + std::string(callback) +
                " failed with an unknown exception.");
        }
        return false;
    }

} // namespace

    RuntimeFeatureContext RuntimeFeatureContextAccess::Create(
        ComponentRegistry& componentRegistry,
        SystemTypeRegistry& systemTypeRegistry,
        ComponentSystemPolicy& componentSystemPolicy) noexcept {
        return RuntimeFeatureContext(
            &componentRegistry,
            &systemTypeRegistry,
            &componentSystemPolicy);
    }

    ComponentRegistry& RuntimeFeatureContextAccess::Components(
        RuntimeFeatureContext& context) noexcept {
        return *static_cast<ComponentRegistry*>(
            context.componentRegistry_);
    }

    SystemTypeRegistry& RuntimeFeatureContextAccess::Systems(
        RuntimeFeatureContext& context) noexcept {
        return *static_cast<SystemTypeRegistry*>(
            context.systemTypeRegistry_);
    }

    ComponentSystemPolicy& RuntimeFeatureContextAccess::Policy(
        RuntimeFeatureContext& context) noexcept {
        return *static_cast<ComponentSystemPolicy*>(
            context.componentSystemPolicy_);
    }

    GameComponentContext
        GameProjectSdkAccess::MakeGameComponentContext(
            GameProjectComponentAdapter& adapter,
            GameObject& object,
            World& world) noexcept {
        return GameComponentContext(
            MakeObjectView(object),
            MakeWorldView(world),
            &adapter);
    }

    bool GameComponentContext::WriteVariable(
        std::string_view variableId,
        const GameProjectBindingValue& value) const noexcept {
        auto* adapter = static_cast<GameProjectComponentAdapter*>(
            bindingOwner_);
        return adapter != nullptr && adapter->WriteVariable(
            world_, variableId, value);
    }

    bool GameComponentContext::ReleaseVariable(
        std::string_view variableId) const noexcept {
        auto* adapter = static_cast<GameProjectComponentAdapter*>(
            bindingOwner_);
        return adapter != nullptr && adapter->ReleaseVariable(
            world_, variableId);
    }

    GameProjectComponentAdapter::GameProjectComponentAdapter(
        std::string typeName,
        IGameComponent* component,
        DestroyGameComponentFn destroy)
        : typeName_(std::move(typeName)),
          bindingWriterId_(MakeGameComponentBindingWriterId()),
          component_(component),
          destroy_(destroy) {
    }

    GameProjectComponentAdapter::~GameProjectComponentAdapter() {
        if (component_ != nullptr && destroy_ != nullptr) {
            destroy_(component_);
        }
        component_ = nullptr;
    }

    std::string_view
        GameProjectComponentAdapter::GetTypeName() const {
        return typeName_;
    }

    void GameProjectComponentAdapter::OnAttach() {
        if (component_ == nullptr || GetOwner() == nullptr) {
            return;
        }
        GameObject& owner = *GetOwner();
        World* world = owner.GetWorld();
        if (world == nullptr) {
            const ObjectView ownerView =
                GameProjectSdkAccess::MakeObjectView(owner);
            if (!InvokeProjectCallback(
                typeName_,
                "OnAttach",
                [this, &ownerView]() {
                    component_->OnAttach(ownerView);
                })) {
                throw std::runtime_error(
                    "Game project component OnAttach failed.");
            }
            return;
        }
        GameComponentContext context =
            GameProjectSdkAccess::MakeGameComponentContext(
                *this, owner, *world);
        if (!InvokeProjectCallback(
            typeName_,
            "OnAttach",
            [this, &context]() {
                component_->OnAttach(context);
            })) {
            ReleaseAllVariables(context.GetWorld());
            throw std::runtime_error(
                "Game project component OnAttach failed.");
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
        GameComponentContext context =
            GameProjectSdkAccess::MakeGameComponentContext(
                *this, owner, *world);
        InvokeProjectCallback(
            typeName_,
            "OnDetach",
            [this, &context]() {
                component_->OnDetach(context);
            });
        ReleaseAllVariables(context.GetWorld());
    }

    void GameProjectComponentAdapter::Update(
        float deltaTime) {
        if (component_ == nullptr || GetOwner() == nullptr) {
            return;
        }
        GameObject& owner = *GetOwner();
        World* world = owner.GetWorld();
        if (world == nullptr) {
            InvokeProjectCallback(
                typeName_,
                "Update",
                [this, deltaTime]() {
                    component_->Update(deltaTime);
                });
            return;
        }
        GameComponentContext context =
            GameProjectSdkAccess::MakeGameComponentContext(
                *this, owner, *world);
        InvokeProjectCallback(
            typeName_,
            "Update",
            [this, &context, deltaTime]() {
                component_->Update(context, deltaTime);
            });
    }

    void GameProjectComponentAdapter::Serialize(
        nlohmann::json& out) const {
        out = nlohmann::json::object();
        if (component_ == nullptr) {
            return;
        }
        PropertyWriter writer =
            GameProjectSdkAccess::MakePropertyWriter(&out);
        InvokeProjectCallback(
            typeName_,
            "SaveProperties",
            [this, &writer]() {
                component_->SaveProperties(writer);
            });
    }

    void GameProjectComponentAdapter::Deserialize(
        const nlohmann::json& in) {
        if (component_ == nullptr) {
            return;
        }
        const PropertyReader reader =
            GameProjectSdkAccess::MakePropertyReader(&in);
        InvokeProjectCallback(
            typeName_,
            "LoadProperties",
            [this, &reader]() {
                component_->LoadProperties(reader);
            });
    }

    void GameProjectComponentAdapter::BuildInspector(
        IInspectorBuilder& builder) {
        if (component_ == nullptr) {
            return;
        }
        GameProjectInspector inspector =
            GameProjectSdkAccess::MakeInspector(builder);
        InvokeProjectCallback(
            typeName_,
            "BuildInspector",
            [this, &inspector]() {
                component_->BuildInspector(inspector);
            });
    }

    IGameComponent*
        GameProjectComponentAdapter::GetGameComponent() noexcept {
        return component_;
    }

    bool GameProjectComponentAdapter::WriteVariable(
        const WorldView& world,
        std::string_view variableId,
        const GameProjectBindingValue& value) noexcept {
        if (variableId.empty() || bindingWriterId_.empty()) {
            return false;
        }
        try {
            const auto [written, inserted] =
                writtenVariableIds_.emplace(variableId);
            if (world.WriteVariable(
                    variableId, value, bindingWriterId_)) {
                return true;
            }
            if (inserted) {
                writtenVariableIds_.erase(written);
            }
            return false;
        }
        catch (...) {
            return false;
        }
    }

    bool GameProjectComponentAdapter::ReleaseVariable(
        const WorldView& world,
        std::string_view variableId) noexcept {
        for (auto written = writtenVariableIds_.begin();
            written != writtenVariableIds_.end();
            ++written) {
            if (*written != variableId) {
                continue;
            }
            if (!world.ReleaseVariableWriter(
                    *written, bindingWriterId_)) {
                return false;
            }
            writtenVariableIds_.erase(written);
            return true;
        }
        return false;
    }

    void GameProjectComponentAdapter::ReleaseAllVariables(
        const WorldView& world) noexcept {
        for (const std::string& variableId : writtenVariableIds_) {
            (void)world.ReleaseVariableWriter(
                variableId, bindingWriterId_);
        }
        writtenVariableIds_.clear();
    }

    GameProjectSystemAdapter::GameProjectSystemAdapter(
        std::string systemId,
        IGameSystem* system,
        DestroyGameSystemFn destroy) noexcept
        : systemId_(std::move(systemId)),
          system_(system),
          destroy_(destroy) {
    }

    GameProjectSystemAdapter::~GameProjectSystemAdapter() {
        if (system_ != nullptr && destroy_ != nullptr) {
            destroy_(system_);
        }
        system_ = nullptr;
    }

    std::string_view GameProjectSystemAdapter::GetName() const {
        return systemId_;
    }

#define HIKARI_INVOKE_GAME_SYSTEM(Name, Method)                         \
    void GameProjectSystemAdapter::Name(                               \
        World& world,                                                   \
        const FrameContext& frame) {                                    \
        if (system_ == nullptr) {                                       \
            return;                                                     \
        }                                                               \
        WorldView view = GameProjectSdkAccess::MakeWorldView(world);    \
        InvokeProjectCallback(                                         \
            systemId_,                                                  \
            #Method,                                                    \
            [this, &view, &frame]() {                                   \
                system_->Method(view, frame);                           \
            });                                                         \
    }

    void GameProjectSystemAdapter::OnWorldAttached(
        World& world) {
        if (system_ == nullptr) {
            return;
        }
        WorldView view =
            GameProjectSdkAccess::MakeWorldView(world);
        InvokeProjectCallback(
            systemId_,
            "OnWorldAttached",
            [this, &view]() {
                system_->OnWorldAttached(view);
            });
    }

    void GameProjectSystemAdapter::OnWorldDetached(
        World& world) {
        if (system_ == nullptr) {
            return;
        }
        WorldView view =
            GameProjectSdkAccess::MakeWorldView(world);
        InvokeProjectCallback(
            systemId_,
            "OnWorldDetached",
            [this, &view]() {
                system_->OnWorldDetached(view);
            });
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

    bool RuntimeFeatureContext::RegisterComponentRaw(
        GameComponentDescriptor descriptor) {
        if (componentRegistry_ == nullptr ||
            descriptor.typeName.empty() ||
            descriptor.create == nullptr ||
            descriptor.destroy == nullptr) {
            return false;
        }

        const std::string typeName(descriptor.typeName);
        const ComponentTypeId computedTypeId =
            MakeComponentTypeId(typeName);
        if (!computedTypeId.IsValid() ||
            (descriptor.typeId.IsValid() &&
             descriptor.typeId != computedTypeId)) {
            return false;
        }
        const CreateGameComponentFn create = descriptor.create;
        const DestroyGameComponentFn destroy = descriptor.destroy;

        ComponentTypeInfo info{};
        info.typeName = typeName;
        info.typeId = computedTypeId;
        info.factory =
            [typeName, create, destroy]()
                -> std::unique_ptr<IComponent> {
                IGameComponent* component = create();
                if (component == nullptr) {
                    return {};
                }
                try {
                    return std::make_unique<
                        GameProjectComponentAdapter>(
                            typeName,
                            component,
                            destroy);
                }
                catch (...) {
                    destroy(component);
                    return {};
                }
            };
        info.requiredComponents =
            CopyNames(descriptor.requiredComponents);
        info.optionalComponents =
            CopyNames(descriptor.optionalComponents);
        info.incompatibleComponents =
            CopyNames(descriptor.incompatibleComponents);
        info.allowMultiple = descriptor.allowMultiple;
        info.runtimeApplyPolicy =
            descriptor.applyPolicy ==
                GameComponentApplyPolicy::OnEditCommit
            ? ComponentRuntimeApplyPolicy::OnEditCommit
            : ComponentRuntimeApplyPolicy::Immediate;
        info.initializeDefaults =
            [create, destroy](
                const SceneObjectData&,
                nlohmann::json& properties) {
                IGameComponent* component = create();
                if (component == nullptr) {
                    properties = nlohmann::json::object();
                    return;
                }
                properties = nlohmann::json::object();
                PropertyWriter writer =
                    GameProjectSdkAccess::MakePropertyWriter(
                        &properties);
                InvokeProjectCallback(
                    "GameProjectComponent",
                    "SaveDefaults",
                    [component, &writer]() {
                        component->SaveProperties(writer);
                    });
                destroy(component);
            };
        info.presentation.displayName =
            descriptor.displayName.empty()
            ? typeName
            : std::string(descriptor.displayName);
        info.presentation.category =
            descriptor.category.empty()
            ? "Game"
            : std::string(descriptor.category);
        info.presentation.description =
            std::string(descriptor.description);
        info.presentation.featureId =
            std::string(descriptor.featureId);
        return RuntimeFeatureContextAccess::Components(*this).
            Register(std::move(info));
    }

    bool RuntimeFeatureContext::RegisterSystemRaw(
        GameSystemDescriptor descriptor) {
        if (systemTypeRegistry_ == nullptr ||
            descriptor.systemId.empty() ||
            descriptor.create == nullptr ||
            descriptor.destroy == nullptr) {
            return false;
        }

        const std::string systemId(descriptor.systemId);
        const CreateGameSystemFn create = descriptor.create;
        const DestroyGameSystemFn destroy = descriptor.destroy;

        nlohmann::json defaultSettings =
            nlohmann::json::object();
        if (IGameSystem* defaults = create()) {
            PropertyWriter writer =
                GameProjectSdkAccess::MakePropertyWriter(
                    &defaultSettings);
            InvokeProjectCallback(
                systemId,
                "SaveSettings",
                [defaults, &writer]() {
                    defaults->SaveSettings(writer);
                });
            destroy(defaults);
        }

        SystemTypeInfo info{};
        info.systemId = systemId;
        info.factory =
            [systemId, create, destroy](
                const nlohmann::json& settings)
                -> std::unique_ptr<ISystem> {
                IGameSystem* system = create();
                if (system == nullptr) {
                    return {};
                }
                const PropertyReader reader =
                    GameProjectSdkAccess::MakePropertyReader(
                        &settings);
                bool loaded = true;
                try {
                    system->LoadSettings(reader);
                }
                catch (...) {
                    loaded = false;
                }
                if (!loaded) {
                    destroy(system);
                    return {};
                }
                return std::make_unique<
                    GameProjectSystemAdapter>(
                        systemId,
                        system,
                        destroy);
            };
        info.displayName =
            descriptor.displayName.empty()
            ? systemId
            : std::string(descriptor.displayName);
        info.featureId =
            std::string(descriptor.featureId);
        info.settings.defaultSettings =
            std::move(defaultSettings);
        return RuntimeFeatureContextAccess::Systems(*this).
            Register(std::move(info));
    }

    bool RuntimeFeatureContext::BindComponentSystem(
        ComponentSystemBinding binding) {
        if (componentSystemPolicy_ == nullptr) {
            return false;
        }
        return RuntimeFeatureContextAccess::Policy(*this).
            Register(ComponentSystemRule{
                std::string(binding.componentType),
                std::string(binding.systemId),
                binding.defaultExecutionOrder
            });
    }

} // namespace HIKARI
