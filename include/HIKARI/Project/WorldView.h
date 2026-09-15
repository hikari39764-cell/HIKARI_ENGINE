#pragma once

#include "HIKARI/Project/UiCanvasView.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/LightTypes.h"
#include "HIKARI/Project/MaterialFxView.h"
#include "HIKARI/Project/ValueTypes.h"
#include "HIKARI/Project/WorldTypes.h"
#include "HIKARI/Project/WorldMassTypes.h"
#include "HIKARI/Project/WorldSpriteTypes.h"
#include "HIKARI/Project/WorldTextTypes.h"
#include "HIKARI/Runtime/ComponentTypeId.h"

namespace HIKARI {
    struct GameProjectModelAppearance;

    class AnimationView;
    class AudioView;
    class CameraView;
    class DialogueView;
    class DisplaySettingsView;
    class IGameComponent;
    class InputView;
    class MotionView;
    class NavigationView;
    class PhysicsQueryView;
    class PresentationModelView;
    class PresentationVfxView;
    class RuntimeSessionView;
    class SaveGameView;
    class SceneCaptureView;
    class SceneFlowView;
    class SequenceView;
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
        // Reads and updates the engine LightComponent attached to this object.
        // Initialize outState size/version. Unsupported headers return false unchanged;
        // valid headers receive only their negotiated prefix, neutral on lookup failure.
        // Runtime patches are immediate, owner-thread only, and never persist
        // into the authored Scene document.
        HIKARI_GAME_PROJECT_SDK_API bool TryGetRuntimeLightState(
            GameProjectLightState& outState) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
            ApplyRuntimeLightPatch(
                const GameProjectLightPatch& patch) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool TryGetRuntimeWorldSpriteState(
            GameProjectWorldSpriteState& outState) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SetRuntimeWorldSpriteTexture(
            std::string_view textureAssetGuid) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
            ApplyRuntimeWorldSpritePatch(
                const GameProjectWorldSpritePatch& patch) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API MaterialFxView
            GetMaterialFx() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool TryGetRuntimeModelAppearance(GameProjectModelAppearance&) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool ApplyRuntimeModelAppearance(const GameProjectModelAppearance&) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool TryGetRuntimeWorldTextState(
            GameProjectWorldTextState& outState) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
            ApplyRuntimeWorldTextPatch(
                const GameProjectWorldTextPatch& patch) const noexcept;
        // UTF-8 is copied during this owner-thread call. Empty font keeps the
        // authored font. No component is created and no Scene is persisted.
        HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
            SetRuntimeWorldTextContent(std::string_view text,
                std::string_view fontAssetPath = {}) const noexcept;
        // Updates an existing engine-owned runtime material override. This is
        // intended for authored procedural/model presentation objects; it
        // never creates a material, changes the source asset, or persists the
        // value back into the Scene document. Reapply after a World rebuild.
        HIKARI_GAME_PROJECT_SDK_API bool SetRuntimeMaterialColorOverride(
            uint32_t materialIndex,
            GameProjectFloat3 baseColor,
            float opacity,
            GameProjectFloat3 emissiveColor,
            float emissiveStrength) const noexcept;
        // Runtime-only render visibility for the first case-sensitive,
        // exact-name model node. The name is limited to
        // kGameProjectModelNodeNameMaximumBytes. The override may be authored
        // before the model asset is bound; an unmatched name remains stored
        // but has no render effect. visible must be 0 or 1.
        // This skips that node's render surfaces without changing animation,
        // collision, transforms, or the source model asset. A descendant's
        // explicit override wins over an inherited ancestor override. The
        // state is not serialized; reapply it after a World rebuild.
        HIKARI_GAME_PROJECT_SDK_API bool SetModelNodeVisibility(
            std::string_view nodeName,
            uint8_t visible,
            GameProjectModelNodeVisibilityScope scope =
                GameProjectModelNodeVisibilityScope::NodeAndDescendants)
            const noexcept;
        // Clearing restores inherited/default visibility and is idempotent.
        HIKARI_GAME_PROJECT_SDK_API bool ClearModelNodeVisibilityOverride(
            std::string_view nodeName) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool ClearModelNodeVisibilityOverrides()
            const noexcept;
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
        // Total live identity count, including GameObject-backed and mass
        // objects. Mass handles are accessed through the mass query API rather
        // than ObjectView.
        HIKARI_GAME_PROJECT_SDK_API uint64_t GetObjectCount() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool CanMutateStructure() const noexcept;
        // SDK 2.5 additive non-structural batch. The World is resolved once,
        // then each valid item is applied immediately in input order. Items
        // are independent: false means at least one item failed or the batch
        // shape was invalid; successful items are not rolled back. Duplicate
        // handles are allowed and the later transform wins. Arrays are
        // borrowed only for this call and count is capped by
        // kGameProjectWorldMaximumTransformWriteBatchCount.
        HIKARI_GAME_PROJECT_SDK_API bool SetLocalTransforms(
            const GameProjectWorldTransformWrite* writes,
            uint32_t writeCount,
            GameProjectWorldResult* outResults,
            uint32_t resultCount) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API uint64_t GetMassObjectCount(
            ComponentTypeId typeId) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SpawnMassObjects(
            const GameProjectWorldMassSpawnBatchDesc& batch,
            GameProjectWorldResult* outResults,
            uint32_t resultCount) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool DestroyMassObjects(
            const GameProjectObjectHandle* objects,
            uint32_t objectCount,
            GameProjectWorldResult* outResults,
            uint32_t resultCount) const noexcept;
        // Valid queries flush QueryEnd after the callback scope closes. Rows,
        // pointers and POD bytes are borrowed only for the callback.
        HIKARI_GAME_PROJECT_SDK_API GameProjectWorldQueryResult
            QueryMassObjects(
                ComponentTypeId typeId,
                void* context,
                GameProjectWorldMassCallback callback) const noexcept;
        template<class Data, class Visitor>
        GameProjectWorldQueryResult ForEachMassObject(
            ComponentTypeId typeId,
            Visitor&& visitor) const;
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
        HIKARI_GAME_PROJECT_SDK_API DisplaySettingsView GetDisplaySettings() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API UiCanvasView GetUiCanvases() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API SequenceView
            GetSequence() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API SceneCaptureView
            GetSceneCapture() const noexcept;
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
