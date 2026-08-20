#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

#include "HIKARI/Project/ActionViews.h"
#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

    class IGameComponent;
    struct GameProjectSdkAccess;

    class ObjectView {
    public:
        ObjectView() = default;

        bool IsValid() const noexcept {
            return object_ != nullptr;
        }

        HIKARI_GAME_PROJECT_SDK_API uint64_t GetHandle() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API std::string_view
            GetName() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API GameProjectFloat3
            GetLocalPosition() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SetLocalPosition(
            GameProjectFloat3 position) const noexcept;

    private:
        friend struct GameProjectSdkAccess;

        explicit ObjectView(void* object) noexcept
            : object_(object) {}

        void* object_ = nullptr;
    };

    class WorldView {
    public:
        WorldView() = default;

        bool IsValid() const noexcept {
            return world_ != nullptr;
        }

        HIKARI_GAME_PROJECT_SDK_API ObjectView FindObject(
            uint64_t handle) const noexcept;
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
            uint64_t sourceObjectHandle = 0) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool EmitEvent(
            std::string_view eventId,
            uint64_t sourceObjectHandle,
            const GameProjectBindingValue& payload) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API uint32_t GetEventCount(
            std::string_view eventId) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool ReadEvent(
            std::string_view eventId,
            uint32_t index,
            GameProjectEventRecord& outEvent) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API InputView
            GetInput() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API AnimationView
            GetAnimation() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API MotionView
            GetMotion() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API PhysicsQueryView
            GetPhysicsQueries() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API CameraView
            GetCamera() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API TimeDilationView
            GetTimeDilation() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API PresentationVfxView
            GetPresentationVfx() const noexcept;

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
                        (*static_cast<Callback*>(context))(
                            object,
                            *static_cast<Component*>(component));
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

        explicit WorldView(void* world) noexcept
            : world_(world) {}

        HIKARI_GAME_PROJECT_SDK_API bool VisitComponents(
            std::string_view typeName,
            void* context,
            VisitorFn visitor) const noexcept;

        void* world_ = nullptr;
    };

} // namespace HIKARI
