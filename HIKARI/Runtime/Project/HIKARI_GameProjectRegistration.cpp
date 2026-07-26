#include "HIKARI/Project/RuntimeRegistration.h"

#include <exception>
#include <memory>
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

    template<class Callback>
    void InvokeProjectCallback(
        std::string_view owner,
        std::string_view callback,
        Callback&& value) noexcept {
        try {
            std::forward<Callback>(value)();
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

    GameProjectComponentAdapter::GameProjectComponentAdapter(
        std::string typeName,
        IGameComponent* component,
        DestroyGameComponentFn destroy) noexcept
        : typeName_(std::move(typeName)),
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
        const ObjectView owner =
            GameProjectSdkAccess::MakeObjectView(*GetOwner());
        InvokeProjectCallback(
            typeName_,
            "OnAttach",
            [this, &owner]() {
                component_->OnAttach(owner);
            });
    }

    void GameProjectComponentAdapter::Update(
        float deltaTime) {
        if (component_ == nullptr) {
            return;
        }
        InvokeProjectCallback(
            typeName_,
            "Update",
            [this, deltaTime]() {
                component_->Update(deltaTime);
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
        const CreateGameComponentFn create = descriptor.create;
        const DestroyGameComponentFn destroy = descriptor.destroy;

        ComponentTypeInfo info{};
        info.typeName = typeName;
        info.factory =
            [typeName, create, destroy]()
                -> std::unique_ptr<IComponent> {
                IGameComponent* component = create();
                if (component == nullptr) {
                    return {};
                }
                return std::make_unique<
                    GameProjectComponentAdapter>(
                        typeName,
                        component,
                        destroy);
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
