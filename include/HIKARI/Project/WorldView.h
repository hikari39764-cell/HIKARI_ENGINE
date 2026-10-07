#pragma once

#include "HIKARI/Project/ComponentViews.h"
#include "HIKARI/Project/PrefabView.h"
#include "HIKARI/Project/UiCanvasView.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/LightTypes.h"
#include "HIKARI/Project/LocalFogView.h"
#include "HIKARI/Project/MaterialFxView.h"
#include "HIKARI/Project/SpriteAnimationView.h"
#include "HIKARI/Project/SpriteMaterialView.h"
#include "HIKARI/Project/ValueTypes.h"
#include "HIKARI/Project/WorldTypes.h"
#include "HIKARI/Project/WorldMassTypes.h"
#include "HIKARI/Project/WorldSpriteTypes.h"
#include "HIKARI/Project/WorldTextTypes.h"
#include "HIKARI/Runtime/ComponentTypeId.h"

namespace HIKARI {
class AnimationView;
class AudioView;
class CameraView;
class DialogueView;
class DebugView;
class DisplaySettingsView;
class IGameComponent;
class IGameProjectSession;
class InputView;
class MotionView;
class NavigationView;
class PhysicsQueryView;
class ProjectDataView;
class PresentationModelView;
class PresentationVfxView;
class PrefabView;
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

// 各型について一つの静的変数のアドレスを型の印とする（RTTI 不要、モジュール内で一意）。
template <class Component>
const void* GameComponentTypeToken() noexcept {
    static constexpr char token = 0;
    return &token;
}

// Non-owning identity view. It may be retained, does not extend World
// lifetime, resolves World + object generations on every call, and becomes
// invalid after either lifetime ends. Use only on the owning runtime thread.
class ObjectView {
public:
    ObjectView() = default;

    HIKARI_GAME_PROJECT_SDK_API bool IsValid() const noexcept;
    GameProjectObjectHandle GetHandle() const noexcept { return handle_; }
    // Returned text is borrowed and remains valid only until the object is
    // renamed or destroyed.
    HIKARI_GAME_PROJECT_SDK_API std::string_view GetName() const noexcept;
    // Identity used to resolve objects inside this World. Authored objects
    // retain their document ID; runtime-spawned objects receive an ephemeral
    // ID that is not written back to the Scene document. Zero means invalid.
    HIKARI_GAME_PROJECT_SDK_API uint64_t GetSceneObjectId() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool GetLocalTransform(GameProjectTransform& outTransform) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool GetWorldTransform(GameProjectTransform& outTransform) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    SetLocalTransform(const GameProjectTransform& transform) const noexcept;
    // Writes the authoritative pose. For script-driven Kinematic motion, write in
    // FixedUpdate from a system ordered before PhysicsSystem. Physics synchronizes
    // the same tick; rendering interpolates completed poses without changing these
    // coordinates or enabling character collision solving. Use MotionView::RequestTeleport
    // for discontinuous Kinematic relocation (no interpolation across the jump).
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    SetWorldTransform(const GameProjectTransform& transform) const noexcept;
    // 組み込みコンポーネントの実行時操作。対象コンポーネントが無い場合、各操作は Unsupported を返す。
    HIKARI_GAME_PROJECT_SDK_API LightView GetLight() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API ModelView GetModel() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API WorldSpriteView GetWorldSprite() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API WorldTextView GetWorldText() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API SpriteAnimationView GetSpriteAnimation() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API SpriteMaterialView GetSpriteMaterial() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API MaterialFxView GetMaterialFx() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API ObjectView GetParent() const noexcept;

private:
    friend struct GameProjectSdkAccess;

    explicit ObjectView(GameProjectObjectHandle handle) noexcept : handle_(handle) {}

    GameProjectObjectHandle handle_{};
};

// Non-owning identity view. It may be retained and becomes invalid when
// the World is destroyed. The current SDK is single-writer: all access and
// mutation commands run on the owning runtime thread.
class WorldView {
public:
    WorldView() = default;
    HIKARI_GAME_PROJECT_SDK_API DebugView GetDebug() const noexcept;

    HIKARI_GAME_PROJECT_SDK_API bool IsValid() const noexcept;
    GameProjectWorldHandle GetHandle() const noexcept { return world_; }

    HIKARI_GAME_PROJECT_SDK_API ObjectView FindObject(GameProjectObjectHandle handle) const noexcept;
    // Resolves an authored reference in this World only. Returns an invalid
    // view for zero/missing IDs, expired Worlds, or non-owner threads.
    // Resolve again after scene replacement; never serialize runtime handles.
    HIKARI_GAME_PROJECT_SDK_API ObjectView FindSceneObject(uint64_t sceneObjectId) const noexcept;
    // Total live identity count, including GameObject-backed and mass
    // objects. Mass handles are accessed through the mass query API rather
    // than ObjectView.
    HIKARI_GAME_PROJECT_SDK_API uint64_t GetObjectCount() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool CanMutateStructure() const noexcept;
    // Non-structural batch. The World is resolved once,
    // then each valid item is applied immediately in input order. Items
    // are independent: false means at least one item failed or the batch
    // shape was invalid; successful items are not rolled back. Duplicate
    // handles are allowed and the later transform wins. Arrays are
    // borrowed only for this call and count is capped by
    // kGameProjectWorldMaximumTransformWriteBatchCount.
    HIKARI_GAME_PROJECT_SDK_API bool SetLocalTransforms(std::span<const GameProjectWorldTransformWrite> writes,
                                                        std::span<GameProjectWorldResult> outResults) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API uint64_t GetMassObjectCount(ComponentTypeId typeId) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool SpawnMassObjects(const GameProjectWorldMassSpawnBatchDesc& batch,
                                                      std::span<GameProjectWorldResult> outResults) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool DestroyMassObjects(std::span<const GameProjectObjectHandle> objects,
                                                        std::span<GameProjectWorldResult> outResults) const noexcept;
    // Valid queries flush QueryEnd after the callback scope closes. Rows,
    // pointers and POD bytes are borrowed only for the callback.
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldQueryResult
    QueryMassObjects(ComponentTypeId typeId, void* context, GameProjectWorldMassCallback callback) const noexcept;
    template <class Data, class Visitor>
    GameProjectWorldQueryResult ForEachMassObject(ComponentTypeId typeId, Visitor&& visitor) const;
    // Structural commands apply immediately only outside protected World
    // traversal. Calls made inside one return Queued and are applied by the
    // owning Runtime at the next deterministic command boundary. A queued
    // Spawn returns its reserved handle, but FindObject remains invalid
    // until that command materializes successfully.
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    SpawnObject(const GameProjectSpawnObjectDesc& desc) const noexcept;
    // Admission-atomic queued batch. Either every item is accepted in
    // input order or none is accepted. Count is capped by
    // kGameProjectWorldMaximumSpawnBatchCount. Batch v1 accepts only
    // already-live parents; same-batch parent references are unsupported.
    // false means zero items were admitted and outResults contains the
    // rejection result for every addressable input item.
    HIKARI_GAME_PROJECT_SDK_API bool SpawnObjects(std::span<const GameProjectSpawnObjectDesc> objects,
                                                  std::span<GameProjectWorldResult> outResults) const noexcept;
    // Descriptor arrays and JSON bytes are borrowed
    // only for this call; accepted data is deep-copied before return. The
    // batch admission is all-or-none, while flush-time failure rolls back
    // only the failed object and its structural commands. Project-defined
    // event/variable/external side effects are not transactionally undone.
    HIKARI_GAME_PROJECT_SDK_API bool SpawnObjectsWithComponents(
        std::span<const GameProjectSpawnObjectComponentsDesc> objects,
        std::span<GameProjectWorldResult> outResults) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult DestroyObject(GameProjectObjectHandle object) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    SetParent(GameProjectObjectHandle object, GameProjectObjectHandle parent,
              GameProjectParentMode mode = GameProjectParentMode::KeepLocal) const noexcept;
    // Flush-time failures remain readable until the next BeginFrame.
    HIKARI_GAME_PROJECT_SDK_API uint32_t GetCommandFailureCount() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool ReadCommandFailure(uint32_t index,
                                                        GameProjectWorldCommandFailure& outFailure) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API uint32_t GetCommandFailureOverflowCount() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool ReadVariable(std::string_view variableId,
                                                  GameProjectBindingValue& outValue) const noexcept;
    // Reads this object's variable scope, returning its catalog default until
    // a component on that object writes the variable.
    HIKARI_GAME_PROJECT_SDK_API bool ReadObjectVariable(GameProjectObjectHandle object, std::string_view variableId,
                                                        GameProjectBindingValue& outValue) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool WriteVariable(std::string_view variableId, const GameProjectBindingValue& value,
                                                   std::string_view writerId) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool ReleaseVariableWriter(std::string_view variableId,
                                                           std::string_view writerId) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool EmitEvent(std::string_view eventId,
                                               GameProjectObjectHandle sourceObjectHandle = {}) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool EmitEvent(std::string_view eventId, GameProjectObjectHandle sourceObjectHandle,
                                               const GameProjectBindingValue& payload) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API uint32_t GetEventCount(std::string_view eventId) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool ReadEvent(std::string_view eventId, uint32_t index,
                                               GameProjectEventRecord& outEvent) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API InputView GetInput() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API AudioView GetAudio() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API AnimationView GetAnimation() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API MotionView GetMotion() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API PhysicsQueryView GetPhysicsQueries() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API NavigationView GetNavigation() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API TerrainView GetTerrain() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API LocalFogView GetLocalFog() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API CameraView GetCamera() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API TimeDilationView GetTimeDilation() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API PresentationModelView GetPresentationModels() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API PresentationVfxView GetPresentationVfx() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API VfxView GetVfx() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API UiView GetUi() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API DisplaySettingsView GetDisplaySettings() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API UiCanvasView GetUiCanvases() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API SequenceView GetSequence() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API PrefabView GetPrefabs() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API SceneCaptureView GetSceneCapture() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API SceneFlowView GetSceneFlow() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API DialogueView GetDialogue() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API SaveGameView GetSaveGame() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API ProjectDataView GetProjectData() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API RuntimeSessionView GetRuntimeSession() const noexcept;
    // Borrowed project state for the active Play session; null outside Play.
    // Valid only on the World owner thread and until that session is stopped.
    HIKARI_GAME_PROJECT_SDK_API IGameProjectSession* GetGameSession() const noexcept;
    // Additive C++ SDK export; this is not the module's extern-C ABI table.
    // Invalid queries invoke no callback and do not flush commands.
    // Valid queries attempt QueryEnd after traversal. When nested in an
    // enclosing World read scope, that owner's deterministic phase boundary
    // performs the flush. Failures use the existing command-failure readers.
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldQueryResult QueryComponents(
        const GameProjectWorldQueryDesc& desc, void* context, GameProjectWorldQueryCallback callback) const noexcept;

    template <class... Components, class Visitor>
    // Definition is opt-in through WorldQuery.h to keep this base view light.
    // typeIds must address at least sizeof...(Components) contiguous IDs.
    GameProjectWorldQueryResult ForEachComponents(const ComponentTypeId* typeIds, GameProjectWorldQueryAnchor anchor,
                                                  Visitor&& visitor) const;

    template <class Component, class Visitor>
    // Visits the World-owned typed index in stable attachment order.
    // Removing an item does not reorder the remaining items.
    bool ForEachComponent(ComponentTypeId typeId, Visitor&& visitor) const {
        static_assert(std::is_base_of_v<IGameComponent, Component>);
        using Callback = std::decay_t<Visitor>;
        Callback callback = std::forward<Visitor>(visitor);
        // 型の印が登録と一致する場合だけ走査されるため、要素ごとの dynamic_cast は不要。
        return VisitComponents(typeId, GameComponentTypeToken<Component>(), &callback,
                               [](void* context, ObjectView object, IGameComponent* component) noexcept {
                                   try {
                                       (*static_cast<Callback*>(context))(object, *static_cast<Component*>(component));
                                       return true;
                                   } catch (...) {
                                       return false;
                                   }
                               });
    }

private:
    using VisitorFn = bool (*)(void* context, ObjectView object, IGameComponent* component) noexcept;

    friend struct GameProjectSdkAccess;

    explicit WorldView(GameProjectWorldHandle world) noexcept : world_(world) {}

    HIKARI_GAME_PROJECT_SDK_API bool VisitComponents(ComponentTypeId typeId, const void* typeToken, void* context,
                                                     VisitorFn visitor) const noexcept;

    GameProjectWorldHandle world_{};
};

} // namespace HIKARI
