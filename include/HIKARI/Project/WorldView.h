#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

#include "HIKARI/Project/ActionViews.h"
#include "HIKARI/Project/AudioView.h"
#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/GameFlowViews.h"
#include "HIKARI/Project/NavigationView.h"
#include "HIKARI/Project/PresentationViews.h"
#include "HIKARI/Project/RuntimeActivityTypes.h"
#include "HIKARI/Project/TerrainView.h"
#include "HIKARI/Project/UiView.h"
#include "HIKARI/Project/ValueTypes.h"
#include "HIKARI/Project/VfxView.h"

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
        HIKARI_GAME_PROJECT_SDK_API bool GetWorldTransform(
            GameProjectTransform& outTransform) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SetLocalPosition(
            GameProjectFloat3 position) const noexcept;
        // Changes only runtime scheduling. Authored enable/visibility values
        // are preserved and become effective again when activity is Active.
        HIKARI_GAME_PROJECT_SDK_API bool SetRuntimeActivity(
            GameProjectRuntimeActivity activity) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API GameProjectRuntimeActivity
            GetRuntimeActivity() const noexcept;
        // Authoring composition hook for project-owned preset components.
        // Values are persisted by the built-in ModelComponent; the project
        // never owns renderer resources or material instances directly.
        HIKARI_GAME_PROJECT_SDK_API bool SetModelAssetGuid(
            std::string_view assetGuid) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SetModelMaterialSetAssetGuid(
            std::string_view materialSetAssetGuid) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool UseImportedModelMaterials()
            const noexcept;
        // Reads the first built-in ColliderComponent as one local-space shape.
        // The caller owns the output value; the view exposes no Physics state.
        HIKARI_GAME_PROJECT_SDK_API bool GetPrimaryColliderShape(
            GameProjectPhysicsShape& outShape) const noexcept;
        // Applies a project composition preset only while the first Collider
        // is still the untouched engine Box default. Authored collider edits
        // are never overwritten by later runtime attachment.
        HIKARI_GAME_PROJECT_SDK_API bool ConfigureDefaultCollider(
            const GameProjectPhysicsShape& shape) const noexcept;

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
        HIKARI_GAME_PROJECT_SDK_API AudioView
            GetAudio() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API AnimationView
            GetAnimation() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API MotionView
            GetMotion() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API PhysicsQueryView
            GetPhysicsQueries() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API NavigationView
            GetNavigation() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API TerrainView
            GetTerrain() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API CameraView
            GetCamera() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API TimeDilationView
            GetTimeDilation() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API PresentationModelView
            GetPresentationModels() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API PresentationVfxView
            GetPresentationVfx() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API VfxView
            GetVfx() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API UiView
            GetUi() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API SceneFlowView
            GetSceneFlow() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API DialogueView
            GetDialogue() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API SaveGameView
            GetSaveGame() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API RuntimeSessionView
            GetRuntimeSession() const noexcept;

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
