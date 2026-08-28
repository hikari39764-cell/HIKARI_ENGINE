#pragma once

#include <span>
#include <string_view>
#include <type_traits>

#include "HIKARI/Project/GameComponent.h"
#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/GameSystem.h"
#include "HIKARI/Runtime/ComponentTypeId.h"

namespace HIKARI {

    struct RuntimeFeatureContextAccess;

    enum class GameComponentApplyPolicy {
        Immediate,
        OnEditCommit,
    };

    using CreateGameComponentFn =
        IGameComponent* (*)() noexcept;
    using DestroyGameComponentFn =
        void (*)(IGameComponent*) noexcept;
    using CreateGameSystemFn =
        IGameSystem* (*)() noexcept;
    using DestroyGameSystemFn =
        void (*)(IGameSystem*) noexcept;

    struct GameComponentDescriptor {
        std::string_view typeName{};
        ComponentTypeId typeId{};
        std::span<const std::string_view> requiredComponents{};
        std::span<const std::string_view> optionalComponents{};
        std::span<const std::string_view> incompatibleComponents{};
        bool allowMultiple = false;
        GameComponentApplyPolicy applyPolicy =
            GameComponentApplyPolicy::Immediate;
        std::string_view displayName{};
        std::string_view category{ "Game" };
        std::string_view description{};
        std::string_view featureId{};

        CreateGameComponentFn create = nullptr;
        DestroyGameComponentFn destroy = nullptr;
    };

    struct GameSystemDescriptor {
        std::string_view systemId{};
        std::string_view displayName{};
        std::string_view featureId{};

        CreateGameSystemFn create = nullptr;
        DestroyGameSystemFn destroy = nullptr;
    };

    struct ComponentSystemBinding {
        std::string_view componentType{};
        std::string_view systemId{};
        int defaultExecutionOrder = 0;
    };

    class RuntimeFeatureContext {
    public:
        template<class Component>
        bool RegisterComponent(GameComponentDescriptor descriptor) {
            static_assert(
                std::is_base_of_v<IGameComponent, Component>);
            static_assert(
                std::is_default_constructible_v<Component>);
            descriptor.create = []() noexcept -> IGameComponent* {
                try {
                    return new Component();
                }
                catch (...) {
                    return nullptr;
                }
            };
            descriptor.destroy =
                [](IGameComponent* component) noexcept {
                    delete static_cast<Component*>(component);
                };
            return RegisterComponentRaw(descriptor);
        }

        template<class System>
        bool RegisterSystem(GameSystemDescriptor descriptor) {
            static_assert(
                std::is_base_of_v<IGameSystem, System>);
            static_assert(
                std::is_default_constructible_v<System>);
            descriptor.create = []() noexcept -> IGameSystem* {
                try {
                    return new System();
                }
                catch (...) {
                    return nullptr;
                }
            };
            descriptor.destroy =
                [](IGameSystem* system) noexcept {
                    delete static_cast<System*>(system);
                };
            return RegisterSystemRaw(descriptor);
        }

        HIKARI_GAME_PROJECT_SDK_API bool BindComponentSystem(
            ComponentSystemBinding binding);

    private:
        friend struct RuntimeFeatureContextAccess;

        RuntimeFeatureContext(
            void* componentRegistry,
            void* systemTypeRegistry,
            void* componentSystemPolicy) noexcept
            : componentRegistry_(componentRegistry),
              systemTypeRegistry_(systemTypeRegistry),
              componentSystemPolicy_(componentSystemPolicy) {}

        HIKARI_GAME_PROJECT_SDK_API bool RegisterComponentRaw(
            GameComponentDescriptor descriptor);
        HIKARI_GAME_PROJECT_SDK_API bool RegisterSystemRaw(
            GameSystemDescriptor descriptor);

        void* componentRegistry_ = nullptr;
        void* systemTypeRegistry_ = nullptr;
        void* componentSystemPolicy_ = nullptr;
    };

} // namespace HIKARI
