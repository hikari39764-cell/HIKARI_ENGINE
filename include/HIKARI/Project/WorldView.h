#pragma once

#include "HIKARI/Project/ComponentViews.h"
#include "HIKARI/Project/PrefabView.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "HIKARI/Project/FluidView.h"
#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/LightTypes.h"
#include "HIKARI/Project/LocalFogView.h"
#include "HIKARI/Project/MaterialFxView.h"
#include "HIKARI/Project/SpriteAnimationView.h"
#include "HIKARI/Project/SpriteMaterialView.h"
#include "HIKARI/Project/ValueTypes.h"
#include "HIKARI/Project/WorldTypes.h"
#include "HIKARI/Project/WorldInstanceTypes.h"
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
class EnvironmentView;
class DisplaySettingsView;
class IGameComponent;
class INavigationGeometrySource;
class IRenderSurfaceExtension;
class IWaterSource;
struct GameProjectOcean;
struct GameProjectWaterSample;
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

// ObjectView は、ゲームオブジェクトの非所有参照ビューを表す。
// オブジェクトの識別子、位置変換、名前、及び組み込みコンポーネントへのアクセスを提供する。
// 実体の所有権は World 側にあり、ここでは参照と操作 API を扱う。
class ObjectView {
public:
    ObjectView() = default;

    HIKARI_GAME_PROJECT_SDK_API bool IsValid() const noexcept;
    GameProjectObjectHandle GetHandle() const noexcept { return handle_; }
    HIKARI_GAME_PROJECT_SDK_API std::string_view GetName() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API uint64_t GetSceneObjectId() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool GetLocalTransform(GameProjectTransform& outTransform) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool GetWorldTransform(GameProjectTransform& outTransform) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    SetLocalTransform(const GameProjectTransform& transform) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    SetWorldTransform(const GameProjectTransform& transform) const noexcept;
    // 組み込みコンポーネントの実行時操作。対象コンポーネントが無い場合、各操作は Unsupported を返す。
    HIKARI_GAME_PROJECT_SDK_API LightView GetLight() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API DecalView GetDecal() const noexcept;
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

// WorldView はワールド全体の操作窓口を表す。
// オブジェクトの生成・削除、変換、親子関係、変数・イベントの管理、
// そして入力・音声・物理・UI などの実行時サブシステムへのアクセスを提供する。
class WorldView {
public:
    WorldView() = default;
    // デバッグ情報や開発用の診断データへアクセスする。
    HIKARI_GAME_PROJECT_SDK_API DebugView GetDebug() const noexcept;

    // ワールドが有効なハンドルを保持しているかを確認する。
    HIKARI_GAME_PROJECT_SDK_API bool IsValid() const noexcept;
    GameProjectWorldHandle GetHandle() const noexcept { return world_; }

    // 動かない高さ場の当たりを足す。sampleCount は辺の標本数（2^n + 1）、heights は sampleCount² の高さ（m）を z 行優先で。
    // (originX, originZ) が標本 (0, 0) の世界位置。物理の動いている世界（Play）でだけ作れる。戻りは外す時の番号、失敗は 0。
    HIKARI_GAME_PROJECT_SDK_API uint64_t AddStaticHeightField(GameProjectObjectHandle owner, float originX,
                                                              float originZ, float spacingMeters, uint32_t sampleCount,
                                                              std::span<const float> heights) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API void RemoveStaticHeightField(uint64_t body) const noexcept;
    // モジュールの幾何を道案内の入力へ足す。外すまで source を生かしておく。
    HIKARI_GAME_PROJECT_SDK_API bool AddNavigationGeometrySource(INavigationGeometrySource& source) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API void RemoveNavigationGeometrySource(INavigationGeometrySource& source) const noexcept;
    // モジュールの水（河や湖）を水の問い合わせへ足す。外すまで source を生かしておく（HIKARI/Project/WaterSource.h）。
    HIKARI_GAME_PROJECT_SDK_API bool AddWaterSource(IWaterSource& source) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API void RemoveWaterSource(IWaterSource& source) const noexcept;
    // 一点の水：水面の高さ、流速、深さ。引擎の水面とモジュールの水のうち position の高さに一番近い水面。
    HIKARI_GAME_PROJECT_SDK_API bool SampleWater(GameProjectFloat3 position,
                                                 GameProjectWaterSample& out) const noexcept;
    // 場面の海（相机を追う水面）。無ければ false。
    HIKARI_GAME_PROJECT_SDK_API bool FindOcean(GameProjectOcean& out) const noexcept;
    // モジュールの面を登録する。OnWorldDetached までに外すこと。
    HIKARI_GAME_PROJECT_SDK_API bool AddRenderSurfaceExtension(IRenderSurfaceExtension& extension) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API void RemoveRenderSurfaceExtension(IRenderSurfaceExtension& extension) const noexcept;

    HIKARI_GAME_PROJECT_SDK_API ObjectView FindObject(GameProjectObjectHandle handle) const noexcept;

    HIKARI_GAME_PROJECT_SDK_API ObjectView FindSceneObject(uint64_t sceneObjectId) const noexcept;

    HIKARI_GAME_PROJECT_SDK_API uint64_t GetObjectCount() const noexcept;
    // 通常オブジェクト（Mass を除く）のハンドルを格納し、書いた数を返す。所有スレッド上の一時スナップショット。
    HIKARI_GAME_PROJECT_SDK_API uint64_t
    CopyObjectHandles(std::span<GameProjectObjectHandle> destination) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool CanMutateStructure() const noexcept;

    // 複数オブジェクトのローカル変換をまとめて更新する。
    HIKARI_GAME_PROJECT_SDK_API bool SetLocalTransforms(std::span<const GameProjectWorldTransformWrite> writes,
                                                        std::span<GameProjectWorldResult> outResults) const noexcept;
    // 指定した Mass コンポーネント型のオブジェクト数を取得する。
    HIKARI_GAME_PROJECT_SDK_API uint64_t GetMassObjectCount(ComponentTypeId typeId) const noexcept;
    // Mass オブジェクトを一括生成する。
    HIKARI_GAME_PROJECT_SDK_API bool SpawnMassObjects(const GameProjectWorldMassSpawnBatchDesc& batch,
                                                      std::span<GameProjectWorldResult> outResults) const noexcept;
    // Mass オブジェクトを一括破棄する。
    HIKARI_GAME_PROJECT_SDK_API bool DestroyMassObjects(std::span<const GameProjectObjectHandle> objects,
                                                        std::span<GameProjectWorldResult> outResults) const noexcept;
    // Mass オブジェクトを走査してクエリを実行する。
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldQueryResult
    QueryMassObjects(ComponentTypeId typeId, void* context, GameProjectWorldMassCallback callback) const noexcept;
    template <class Data, class Visitor>
    GameProjectWorldQueryResult ForEachMassObject(ComponentTypeId typeId, Visitor&& visitor) const;
    // 実例集を足す（同じ名前があれば定義を置き換える）。ID は MakeComponentTypeId(desc.name)。
    HIKARI_GAME_PROJECT_SDK_API bool AddInstanceSet(const GameProjectWorldInstanceSetDesc& desc) const noexcept;
    // 実例集をその個体ごと消す。
    HIKARI_GAME_PROJECT_SDK_API bool RemoveInstanceSet(ComponentTypeId setId) const noexcept;
    // 実例集の ID を足した順に書き、全部の数を返す（outIds が短ければ書けた分だけ）。
    HIKARI_GAME_PROJECT_SDK_API uint32_t GetInstanceSetIds(std::span<ComponentTypeId> outIds) const noexcept;
    // 実例集の定義。無ければ nullptr。
    HIKARI_GAME_PROJECT_SDK_API const GameProjectWorldInstanceSetDesc* FindInstanceSet(
        ComponentTypeId setId) const noexcept;
    // 一格の個体を丸ごと置き換える。格子は WorldCellAt(x, z, 256) で求める。
    HIKARI_GAME_PROJECT_SDK_API bool ReplaceInstanceCell(
        ComponentTypeId setId, int32_t cellX, int32_t cellZ,
        std::span<const GameProjectWorldInstance> instances) const noexcept;
    // 一格の個体。次にその実例集を変えるまで有効。
    HIKARI_GAME_PROJECT_SDK_API std::span<const GameProjectWorldInstance> ReadInstanceCell(
        ComponentTypeId setId, int32_t cellX, int32_t cellZ) const noexcept;
    // 実例集の個体数。
    HIKARI_GAME_PROJECT_SDK_API uint64_t GetInstanceCount(ComponentTypeId setId) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    SpawnObject(const GameProjectSpawnObjectDesc& desc) const noexcept;
    // 複数のオブジェクトをまとめて生成する。
    HIKARI_GAME_PROJECT_SDK_API bool SpawnObjects(std::span<const GameProjectSpawnObjectDesc> objects,
                                                  std::span<GameProjectWorldResult> outResults) const noexcept;

    // 事前定義済みのコンポーネント構成で複数オブジェクトを生成する。
    HIKARI_GAME_PROJECT_SDK_API bool SpawnObjectsWithComponents(
        std::span<const GameProjectSpawnObjectComponentsDesc> objects,
        std::span<GameProjectWorldResult> outResults) const noexcept;
    // 単一オブジェクトを破棄する。
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult DestroyObject(GameProjectObjectHandle object) const noexcept;
    // オブジェクトの親子関係を更新する。親が変わる場合のローカル変換維持方法を mode で制御する。
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    SetParent(GameProjectObjectHandle object, GameProjectObjectHandle parent,
              GameProjectParentMode mode = GameProjectParentMode::KeepLocal) const noexcept;

    // 直近のコマンド失敗情報を取得するためのカウンタとリーダー。
    HIKARI_GAME_PROJECT_SDK_API uint32_t GetCommandFailureCount() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool ReadCommandFailure(uint32_t index,
                                                        GameProjectWorldCommandFailure& outFailure) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API uint32_t GetCommandFailureOverflowCount() const noexcept;
    // ワールド全体で共有される変数を読み取る。
    HIKARI_GAME_PROJECT_SDK_API bool ReadVariable(std::string_view variableId,
                                                  GameProjectBindingValue& outValue) const noexcept;
    // 特定オブジェクトに紐づく変数を読み取る。
    HIKARI_GAME_PROJECT_SDK_API bool ReadObjectVariable(GameProjectObjectHandle object, std::string_view variableId,
                                                        GameProjectBindingValue& outValue) const noexcept;
    // ワールド変数へ値を書き込む。writerId で更新元を区別する。
    HIKARI_GAME_PROJECT_SDK_API bool WriteVariable(std::string_view variableId, const GameProjectBindingValue& value,
                                                   std::string_view writerId) const noexcept;
    // 特定 writer による変数更新の所有権を解放する。
    HIKARI_GAME_PROJECT_SDK_API bool ReleaseVariableWriter(std::string_view variableId,
                                                           std::string_view writerId) const noexcept;
    // イベントを発火する。ソースオブジェクトを明示しない場合は未指定として扱う。
    HIKARI_GAME_PROJECT_SDK_API bool EmitEvent(std::string_view eventId,
                                               GameProjectObjectHandle sourceObjectHandle = {}) const noexcept;
    // payload を伴うイベントを発火する。
    HIKARI_GAME_PROJECT_SDK_API bool EmitEvent(std::string_view eventId, GameProjectObjectHandle sourceObjectHandle,
                                               const GameProjectBindingValue& payload) const noexcept;
    // 指定イベントの発火回数を取得する。
    HIKARI_GAME_PROJECT_SDK_API uint32_t GetEventCount(std::string_view eventId) const noexcept;
    // 指定イベントの履歴を index で読み取る。
    HIKARI_GAME_PROJECT_SDK_API bool ReadEvent(std::string_view eventId, uint32_t index,
                                               GameProjectEventRecord& outEvent) const noexcept;
    // 入力や制御系の実行時サブシステムへのアクセス。
    HIKARI_GAME_PROJECT_SDK_API InputView GetInput() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API AudioView GetAudio() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API AnimationView GetAnimation() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API MotionView GetMotion() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API PhysicsQueryView GetPhysicsQueries() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API NavigationView GetNavigation() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API TerrainView GetTerrain() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API LocalFogView GetLocalFog() const noexcept;
    // VFX の流体烟雾を押す障碍物・冲量・力（毎フレーム提出）。
    HIKARI_GAME_PROJECT_SDK_API FluidView GetFluid() const noexcept;
    // シーンの時刻・天気（実行時だけの変更）。
    HIKARI_GAME_PROJECT_SDK_API EnvironmentView GetEnvironment() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API CameraView GetCamera() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API TimeDilationView GetTimeDilation() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API PresentationModelView GetPresentationModels() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API PresentationVfxView GetPresentationVfx() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API VfxView GetVfx() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API UiView GetUi() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API DisplaySettingsView GetDisplaySettings() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API SequenceView GetSequence() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API PrefabView GetPrefabs() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API SceneCaptureView GetSceneCapture() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API SceneFlowView GetSceneFlow() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API DialogueView GetDialogue() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API SaveGameView GetSaveGame() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API ProjectDataView GetProjectData() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API RuntimeSessionView GetRuntimeSession() const noexcept;

    // 現在のワールドを所有するゲームセッションへのアクセスを返す。
    HIKARI_GAME_PROJECT_SDK_API IGameProjectSession* GetGameSession() const noexcept;
    // 条件付きクエリでコンポーネントを走査し、コールバックで各結果を処理する。
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldQueryResult QueryComponents(
        const GameProjectWorldQueryDesc& desc, void* context, GameProjectWorldQueryCallback callback) const noexcept;

    // 指定した型 ID の一覧に対して各コンポーネントを走査する。
    template <class... Components, class Visitor>

    GameProjectWorldQueryResult ForEachComponents(const ComponentTypeId* typeIds, GameProjectWorldQueryAnchor anchor,
                                                  Visitor&& visitor) const;

    // 型安全に特定のゲームコンポーネントをイテレートする。
    template <class Component, class Visitor>

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
