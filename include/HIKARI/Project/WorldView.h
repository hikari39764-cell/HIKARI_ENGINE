#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/ValueTypes.h"
#include "HIKARI/Project/WorldTypes.h"
#include "HIKARI/Runtime/ComponentTypeId.h"

namespace HIKARI {

    class AnimationView;
    class AudioView;
    class CameraView;
    class DialogueView;
    class IGameComponent;
    class InputView;
    class MotionView;
    class NavigationView;
    class PhysicsQueryView;
    class PresentationModelView;
    class PresentationVfxView;
    class RuntimeSessionView;
    class SaveGameView;
    class SceneFlowView;
    class TerrainView;
    class TimeDilationView;
    class UiView;
    class VfxView;
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
        // Admission-atomic queued batch. Either every item is accepted in
        // input order or none is accepted. Count is capped by
        // kGameProjectWorldMaximumSpawnBatchCount. Batch v1 accepts only
        // already-live parents; same-batch parent references are unsupported.
        // false means zero items were admitted and outResults contains the
        // rejection result for every addressable input item.
        HIKARI_GAME_PROJECT_SDK_API bool SpawnObjects(
            std::span<const GameProjectSpawnObjectDesc> objects,
            std::span<GameProjectWorldResult> outResults) const noexcept;
        // SDK 2.4 additive batch. Descriptor arrays and JSON bytes are borrowed
        // only for this call; accepted data is deep-copied before return. The
        // batch admission is all-or-none, while flush-time failure rolls back
        // only the failed object and its structural commands. Project-defined
        // event/variable/external side effects are not transactionally undone.
        HIKARI_GAME_PROJECT_SDK_API bool SpawnObjectsWithComponents(
            const GameProjectSpawnObjectComponentsDesc* objects,
            uint32_t objectCount,
            GameProjectWorldResult* outResults,
            uint32_t resultCount) const noexcept;
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
        HIKARI_GAME_PROJECT_SDK_API InputView GetInput() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API AudioView GetAudio() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API AnimationView
            GetAnimation() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API MotionView GetMotion() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API PhysicsQueryView
            GetPhysicsQueries() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API NavigationView
            GetNavigation() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API TerrainView GetTerrain() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API CameraView GetCamera() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API TimeDilationView
            GetTimeDilation() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API PresentationModelView
            GetPresentationModels() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API PresentationVfxView
            GetPresentationVfx() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API VfxView GetVfx() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API UiView GetUi() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API SceneFlowView
            GetSceneFlow() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API DialogueView
            GetDialogue() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API SaveGameView
            GetSaveGame() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API RuntimeSessionView
            GetRuntimeSession() const noexcept;
        // Additive C++ SDK export; this is not the module's extern-C ABI table.
        // Invalid queries invoke no callback and do not flush commands.
        // Valid queries attempt QueryEnd after traversal. When nested in an
        // enclosing World read scope, that owner's deterministic phase boundary
        // performs the flush. Failures use the existing command-failure readers.
        HIKARI_GAME_PROJECT_SDK_API GameProjectWorldQueryResult
            QueryComponents(
                const GameProjectWorldQueryDesc& desc,
                void* context,
                GameProjectWorldQueryCallback callback) const noexcept;

        template<class... Components, class Visitor>
        // Definition is opt-in through WorldQuery.h to keep this base view light.
        // typeIds must address at least sizeof...(Components) contiguous IDs.
        GameProjectWorldQueryResult ForEachComponents(
            const ComponentTypeId* typeIds,
            GameProjectWorldQueryAnchor anchor,
            Visitor&& visitor) const;

        template<class Component, class Visitor>
        // Visits the World-owned typed index in stable attachment order.
        // Removing an item does not reorder the remaining items.
        bool ForEachComponent(
            ComponentTypeId typeId,
            Visitor&& visitor) const {

            static_assert(
                std::is_base_of_v<IGameComponent, Component>);
            using Callback = std::decay_t<Visitor>;
            Callback callback = std::forward<Visitor>(visitor);
            return VisitComponents(
                typeId,
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
            ComponentTypeId typeId,
            void* context,
            VisitorFn visitor) const noexcept;

        GameProjectWorldHandle world_{};
    };

} // namespace HIKARI
