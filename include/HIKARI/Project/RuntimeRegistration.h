#pragma once

#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "HIKARI/Project/GameComponent.h"
#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/GameSystem.h"
#include "HIKARI/Project/MassSystem.h"
#include "HIKARI/Project/WorldTypes.h"
#include "HIKARI/Project/WorldMassTypes.h"
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
            ConfigureSystemDescriptor<System>(descriptor);
            return RegisterSystemRaw(descriptor);
        }

        // Opt-in Mass partition system. The schedule phase's ordinary
        // IGameSystem callback is replaced by this const partition kernel;
        // every other callback remains owner-thread serial.
        template<class System>
        bool RegisterParallelMassSystem(
            GameSystemDescriptor descriptor,
            const GameProjectMassSystemScheduleDesc& schedule) {
            ConfigureSystemDescriptor<System>(descriptor);
            using CallbackResult = std::invoke_result_t<
                decltype(&System::ExecuteMassPartition),
                const System*,
                const GameProjectMassPartitionInput&,
                GameProjectMassPartitionOutput&,
                const GameProjectMassCommandWriter&,
                const FrameContext&>;
            static_assert(std::is_void_v<CallbackResult> ||
                std::is_same_v<CallbackResult, bool>);
            const ExecuteGameProjectMassPartitionFn execute =
                [](const IGameSystem* system,
                    const GameProjectMassPartitionInput* input,
                    GameProjectMassPartitionOutput* output,
                    const GameProjectMassCommandWriter* commands,
                    const FrameContext* frame) noexcept {
                    if (system == nullptr || input == nullptr ||
                        output == nullptr || commands == nullptr ||
                        frame == nullptr) {
                        return false;
                    }
                    try {
                        const auto& typed =
                            *static_cast<const System*>(system);
                        if constexpr (std::is_void_v<CallbackResult>) {
                            typed.ExecuteMassPartition(
                                *input,
                                *output,
                                *commands,
                                *frame);
                            return true;
                        }
                        else {
                            return typed.ExecuteMassPartition(
                                *input,
                                *output,
                                *commands,
                                *frame);
                        }
                    }
                    catch (...) {
                        return false;
                    }
                };
            return RegisterParallelMassSystemRaw(
                descriptor,
                schedule,
                execute);
        }

        template<class Data>
        bool RegisterMassData(
            ComponentTypeId typeId,
            uint64_t layoutHash,
            uint32_t schemaVersion = 1u) {
            static_assert(std::is_standard_layout_v<Data>);
            static_assert(std::is_trivially_copyable_v<Data>);
            static_assert(std::is_default_constructible_v<Data>);
            static_assert(sizeof(Data) <=
                kGameProjectWorldMaximumMassPodBytes);
            static_assert(alignof(Data) <= alignof(std::max_align_t));
            GameProjectMassDataTypeDesc descriptor{};
            descriptor.typeId = typeId;
            descriptor.layoutHash = layoutHash;
            descriptor.schemaVersion = schemaVersion;
            descriptor.dataSize = sizeof(Data);
            descriptor.dataAlignment = alignof(Data);
            return RegisterMassDataRaw(descriptor);
        }

        // Registers one schema-level render binding. This is independent of
        // entity count and never creates per-entity model/component records.
        HIKARI_GAME_PROJECT_SDK_API bool RegisterMassRenderProxy(
            const GameProjectWorldMassRenderProxyDesc& descriptor);

        HIKARI_GAME_PROJECT_SDK_API bool BindComponentSystem(
            ComponentSystemBinding binding);

    private:
        friend struct RuntimeFeatureContextAccess;

        template<class System>
        static void ConfigureSystemDescriptor(
            GameSystemDescriptor& descriptor) {
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
        }

        RuntimeFeatureContext(
            void* componentRegistry,
            void* massSchemaRegistry,
            void* systemTypeRegistry,
            void* componentSystemPolicy) noexcept
            : componentRegistry_(componentRegistry),
              massSchemaRegistry_(massSchemaRegistry),
              systemTypeRegistry_(systemTypeRegistry),
              componentSystemPolicy_(componentSystemPolicy) {}

        HIKARI_GAME_PROJECT_SDK_API bool RegisterComponentRaw(
            GameComponentDescriptor descriptor);
        HIKARI_GAME_PROJECT_SDK_API bool RegisterSystemRaw(
            GameSystemDescriptor descriptor);
        HIKARI_GAME_PROJECT_SDK_API bool RegisterParallelMassSystemRaw(
            GameSystemDescriptor descriptor,
            const GameProjectMassSystemScheduleDesc& schedule,
            ExecuteGameProjectMassPartitionFn execute);
        HIKARI_GAME_PROJECT_SDK_API bool RegisterMassDataRaw(
            const GameProjectMassDataTypeDesc& descriptor);

        void* componentRegistry_ = nullptr;
        void* massSchemaRegistry_ = nullptr;
        void* systemTypeRegistry_ = nullptr;
        void* componentSystemPolicy_ = nullptr;
    };

} // namespace HIKARI
