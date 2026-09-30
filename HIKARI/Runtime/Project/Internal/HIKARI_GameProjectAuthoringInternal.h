#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>

#include <json.hpp>

#include "HIKARI/Project/GameComponent.h"
#include "HIKARI/Project/GameSystem.h"
#include "HIKARI/Project/RuntimeRegistration.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {

class AnimationView;
class AudioView;
class CameraView;
class ComponentRegistry;
class ComponentSystemPolicy;
class DialogueView;
class GameProjectComponentAdapter;
class GameObject;
class InputView;
class IInspectorBuilder;
class MotionView;
class NavigationView;
class PhysicsQueryView;
class PrefabView;
class PresentationModelView;
class PresentationVfxView;
class RuntimeSessionView;
class SaveGameView;
class SceneCaptureView;
class SequenceView;
class SceneFlowView;
class SystemTypeRegistry;
class TerrainView;
class TimeDilationView;
class UiView;
class VfxView;
class World;
class WorldMassSchemaRegistry;
struct WorldMassMutableRow;
enum class RuntimeWorldAccessResult : uint8_t;
enum class WorldComponentQueryAnchor : uint8_t;
enum class WorldComponentQueryCardinality : uint8_t;

struct RuntimeFeatureContextAccess {
    static RuntimeFeatureContext Create(ComponentRegistry& componentRegistry,
                                        WorldMassSchemaRegistry& massSchemaRegistry,
                                        SystemTypeRegistry& systemTypeRegistry,
                                        ComponentSystemPolicy& componentSystemPolicy) noexcept;

    static ComponentRegistry& Components(RuntimeFeatureContext& context) noexcept;
    static WorldMassSchemaRegistry& MassSchemas(RuntimeFeatureContext& context) noexcept;
    static SystemTypeRegistry& Systems(RuntimeFeatureContext& context) noexcept;
    static ComponentSystemPolicy& Policy(RuntimeFeatureContext& context) noexcept;
};

struct GameProjectSdkAccess {
    using IndexedComponentVisitorFn = bool (*)(void* context, RuntimeObjectHandle object, IComponent* const* components,
                                               uint32_t componentCount) noexcept;
    using MassObjectVisitorFn = bool (*)(void* context, WorldMassMutableRow& row) noexcept;

    static PropertyReader MakePropertyReader(const void* properties) noexcept;
    static PropertyWriter MakePropertyWriter(void* properties) noexcept;
    static GameProjectInspector MakeInspector(IInspectorBuilder& builder) noexcept;
    static ObjectView MakeObjectView(GameObject& object) noexcept;
    static WorldView MakeWorldView(World& world) noexcept;
    static InputView MakeInputView(World& world) noexcept;
    static AudioView MakeAudioView(World& world) noexcept;
    static AnimationView MakeAnimationView(World& world) noexcept;
    static MotionView MakeMotionView(World& world) noexcept;
    static PhysicsQueryView MakePhysicsQueryView(World& world) noexcept;
    static NavigationView MakeNavigationView(World& world) noexcept;
    static TerrainView MakeTerrainView(World& world) noexcept;
    static CameraView MakeCameraView(World& world) noexcept;
    static TimeDilationView MakeTimeDilationView(World& world) noexcept;
    static PresentationModelView MakePresentationModelView(World& world) noexcept;
    static PresentationVfxView MakePresentationVfxView(World& world) noexcept;
    static VfxView MakeVfxView(World& world) noexcept;
    static UiView MakeUiView(World& world) noexcept;
    static DebugView MakeDebugView(World& world) noexcept;
    static SceneFlowView MakeSceneFlowView(World& world) noexcept;
    static DialogueView MakeDialogueView(World& world) noexcept;
    static SaveGameView MakeSaveGameView(World& world) noexcept;
    static RuntimeSessionView MakeRuntimeSessionView(World& world) noexcept;
    static SceneCaptureView MakeSceneCaptureView(World& world) noexcept;
    static SequenceView MakeSequenceView(World& world) noexcept;
    static PrefabView MakePrefabView(World& world) noexcept;
    static GameComponentContext MakeGameComponentContext(GameProjectComponentAdapter& adapter, GameObject& object,
                                                         World& world) noexcept;
    static GameObject* GetObject(const ObjectView& view, RuntimeWorldAccessResult* outAccess = nullptr) noexcept;
    static World* GetWorld(const WorldView& view, RuntimeWorldAccessResult* outAccess = nullptr) noexcept;
    static World* GetWorld(const PresentationModelView& view) noexcept;
    static bool IsWorldClosing(const World& world) noexcept;
    static bool VisitIndexedComponents(World& world, std::span<const ComponentTypeId> typeIds,
                                       WorldComponentQueryCardinality cardinality, WorldComponentQueryAnchor anchor,
                                       void* context, IndexedComponentVisitorFn visitor) noexcept;
    static bool VisitMassObjects(World& world, ComponentTypeId typeId, void* context,
                                 MassObjectVisitorFn visitor) noexcept;
};

class GameProjectComponentAdapter final : public IComponent {
public:
    GameProjectComponentAdapter(std::string typeName, IGameComponent* component, DestroyGameComponentFn destroy);
    ~GameProjectComponentAdapter() override;

    std::string_view GetTypeName() const override;
    void OnAttach() override;
    void OnDetach() override;
    void Update(float deltaTime) override;
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    bool TryDeserialize(const nlohmann::json& in, std::string& outDiagnostic) override;
    void BuildInspector(IInspectorBuilder& builder) override;

    IGameComponent* GetGameComponent() noexcept;
    bool WriteVariable(const WorldView& world, std::string_view variableId,
                       const GameProjectBindingValue& value) noexcept;
    bool ReleaseVariable(const WorldView& world, std::string_view variableId) noexcept;
    bool WriteObjectVariable(const WorldView& world, std::string_view variableId,
                             const GameProjectBindingValue& value) noexcept;
    bool ReleaseObjectVariable(const WorldView& world, std::string_view variableId) noexcept;

private:
    void ReleaseAllVariables(const WorldView& world) noexcept;

    std::string typeName_{};
    std::string bindingWriterId_{};
    std::unordered_set<std::string> writtenVariableIds_{};
    std::unordered_set<std::string> writtenObjectVariableIds_{};
    IGameComponent* component_ = nullptr;
    DestroyGameComponentFn destroy_ = nullptr;
};

struct GameProjectSystemExtensions {
    GameProjectMassSystemScheduleDesc massSchedule{};
    ExecuteGameProjectMassPartitionFn executeMass = nullptr;
    GameProjectRenderObserverDesc renderObserver{};
    ObserveGameProjectRenderFrameFn observeRender = nullptr;
    GameProjectRenderPassDesc renderPass{};
    ExecuteGameProjectRenderPassFn executeRenderPass = nullptr;

    GameProjectSystemExtensions() noexcept {
        renderObserver.structSize = 0u;
        renderObserver.version = 0u;
        renderObserver.stage = GameProjectRenderObserverStage::Count;
        renderPass.structSize = 0u;
        renderPass.version = 0u;
        renderPass.anchor = GameProjectRenderPassAnchor::Count;
        renderPass.target = GameProjectRenderPassTarget::Count;
    }
};

class GameProjectSystemAdapter final : public ISystem,
                                       public IMassWorkerSystem,
                                       public IRenderFrameObserverSystem,
                                       public IRenderFramePassSystem {
public:
    GameProjectSystemAdapter(std::string systemId, IGameSystem* system, DestroyGameSystemFn destroy,
                             GameProjectSystemExtensions extensions = {}) noexcept;
    ~GameProjectSystemAdapter() override;

    std::string_view GetName() const override;
    void OnWorldAttached(World& world) override;
    void OnWorldDetached(World& world) override;
    void PreUpdate(World& world, const FrameContext& frame) override;
    void PreFixedUpdate(World& world, const FrameContext& frame) override;
    void FixedUpdate(World& world, const FrameContext& frame) override;
    void PostFixedUpdate(World& world, const FrameContext& frame) override;
    void Update(World& world, const FrameContext& frame) override;
    void LateUpdate(World& world, const FrameContext& frame) override;
    void PreRender(World& world, const FrameContext& frame) override;
    void Render(World& world, const FrameContext& frame) override;
    void PostRender(World& world, const FrameContext& frame) override;
    const GameProjectMassSystemScheduleDesc& GetMassSystemSchedule() const noexcept override;
    bool ExecuteMassPartition(const GameProjectMassPartitionInput& input, GameProjectMassPartitionOutput& output,
                              const GameProjectMassCommandWriter& commands,
                              const FrameContext& frame) const noexcept override;
    const GameProjectRenderObserverDesc& GetRenderObserverDesc() const noexcept override;
    bool ObserveRenderFrame(const GameProjectRenderFrameView& frame) const noexcept override;
    const GameProjectRenderPassDesc& GetRenderPassDesc() const noexcept override;
    bool ExecuteRenderPass(const GameProjectRenderPassContext& context) const noexcept override;

private:
    std::string systemId_{};
    IGameSystem* system_ = nullptr;
    DestroyGameSystemFn destroy_ = nullptr;
    GameProjectSystemExtensions extensions_{};
};

} // namespace HIKARI
