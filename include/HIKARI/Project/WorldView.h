#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/ValueTypes.h"
#include "HIKARI/Project/WorldTypes.h"

namespace HIKARI {

    class IGameComponent;
    struct GameProjectSdkAccess;

    // Non-owning identity view. It may be retained, does not extend World
    // lifetime, resolves World + object generations on every call, and becomes
    // invalid after either lifetime ends. Use only on the owning runtime thread.
    class ObjectView {
    public:
        ObjectView() = default;

        HIKARI_GAME_PROJECT_SDK_API bool IsValid() const noexcept;
        GameProjectObjectHandle GetHandle() const noexcept {
            return handle_;
        }
        // Returned text is borrowed and remains valid only until the object is
        // renamed or destroyed.
        HIKARI_GAME_PROJECT_SDK_API std::string_view
            GetName() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool GetLocalTransform(
            GameProjectTransform& outTransform) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool GetWorldTransform(
            GameProjectTransform& outTransform) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult SetLocalTransform(
            const GameProjectTransform& transform) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult SetWorldTransform(
            const GameProjectTransform& transform) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API ObjectView GetParent() const noexcept;

    private:
        friend struct GameProjectSdkAccess;

        explicit ObjectView(
            GameProjectObjectHandle handle) noexcept
            : handle_(handle) {}

        GameProjectObjectHandle handle_{};
    };

    // Non-owning identity view. It may be retained and becomes invalid when
    // the World is destroyed. The current SDK is single-writer: all access and
    // mutation commands run on the owning runtime thread.
    class WorldView {
    public:
        WorldView() = default;

        HIKARI_GAME_PROJECT_SDK_API bool IsValid() const noexcept;
        GameProjectWorldHandle GetHandle() const noexcept {
            return world_;
        }

        HIKARI_GAME_PROJECT_SDK_API ObjectView FindObject(
            GameProjectObjectHandle handle) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API uint64_t GetObjectCount() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool CanMutateStructure() const noexcept;
        // Structural commands apply immediately only outside protected World
        // traversal. Calls made inside one return Queued and are applied by the
        // owning Runtime at the next deterministic command boundary. A queued
        // Spawn returns its reserved handle, but FindObject remains invalid
        // until that command materializes successfully.
        HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult SpawnObject(
            const GameProjectSpawnObjectDesc& desc) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult DestroyObject(
            GameProjectObjectHandle object) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult SetParent(
            GameProjectObjectHandle object,
            GameProjectObjectHandle parent,
            GameProjectParentMode mode =
                GameProjectParentMode::KeepLocal) const noexcept;
        // Flush-time failures remain readable until the next BeginFrame.
        HIKARI_GAME_PROJECT_SDK_API uint32_t
            GetCommandFailureCount() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool ReadCommandFailure(
            uint32_t index,
            GameProjectWorldCommandFailure& outFailure) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API uint32_t
            GetCommandFailureOverflowCount() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool ReadVariable(
            std::string_view variableId,
            GameProjectBindingValue& outValue) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool WriteVariable(
            std::string_view variableId,
            const GameProjectBindingValue& value,
            std::string_view writerId) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool ReleaseVariableWriter(
            std::string_view variableId,
            std::string_view writerId) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool EmitEvent(
            std::string_view eventId,
            GameProjectObjectHandle sourceObjectHandle = {}) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool EmitEvent(
            std::string_view eventId,
            GameProjectObjectHandle sourceObjectHandle,
            const GameProjectBindingValue& payload) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API uint32_t GetEventCount(
            std::string_view eventId) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool ReadEvent(
            std::string_view eventId,
            uint32_t index,
            GameProjectEventRecord& outEvent) const noexcept;

        template<class Component, class Visitor>
        bool ForEachComponent(
            std::string_view typeName,
            Visitor&& visitor) const {

            static_assert(
                std::is_base_of_v<IGameComponent, Component>);
            using Callback = std::decay_t<Visitor>;
            Callback callback = std::forward<Visitor>(visitor);
            return VisitComponents(
                typeName,
                &callback,
                [](void* context,
                    ObjectView object,
                    IGameComponent* component) noexcept {
                    try {
                        Component* typed =
                            dynamic_cast<Component*>(component);
                        if (typed == nullptr) {
                            return false;
                        }
                        (*static_cast<Callback*>(context))(
                            object,
                            *typed);
                        return true;
                    }
                    catch (...) {
                        return false;
                    }
                });
        }

    private:
        using VisitorFn = bool (*)(
            void* context,
            ObjectView object,
            IGameComponent* component) noexcept;

        friend struct GameProjectSdkAccess;

        explicit WorldView(GameProjectWorldHandle world) noexcept
            : world_(world) {}

        HIKARI_GAME_PROJECT_SDK_API bool VisitComponents(
            std::string_view typeName,
            void* context,
            VisitorFn visitor) const noexcept;

        GameProjectWorldHandle world_{};
    };

} // namespace HIKARI
