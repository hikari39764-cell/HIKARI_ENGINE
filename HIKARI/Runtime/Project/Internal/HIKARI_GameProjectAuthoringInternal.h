#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

#include <json.hpp>

#include "HIKARI/Project/GameComponent.h"
#include "HIKARI/Project/GameSystem.h"
#include "HIKARI/Project/RuntimeRegistration.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

    class ComponentRegistry;
    class ComponentSystemPolicy;
    class GameProjectComponentAdapter;
    class GameObject;
    class IInspectorBuilder;
    class SystemTypeRegistry;
    class World;

    struct RuntimeFeatureContextAccess {
        static RuntimeFeatureContext Create(
            ComponentRegistry& componentRegistry,
            SystemTypeRegistry& systemTypeRegistry,
            ComponentSystemPolicy& componentSystemPolicy) noexcept;

        static ComponentRegistry& Components(
            RuntimeFeatureContext& context) noexcept;
        static SystemTypeRegistry& Systems(
            RuntimeFeatureContext& context) noexcept;
        static ComponentSystemPolicy& Policy(
            RuntimeFeatureContext& context) noexcept;
    };

    struct GameProjectSdkAccess {
        static PropertyReader MakePropertyReader(
            const void* properties) noexcept;
        static PropertyWriter MakePropertyWriter(
            void* properties) noexcept;
        static GameProjectInspector MakeInspector(
            IInspectorBuilder& builder) noexcept;
        static ObjectView MakeObjectView(
            GameObject& object) noexcept;
        static WorldView MakeWorldView(
            World& world) noexcept;
        static GameComponentContext MakeGameComponentContext(
            GameProjectComponentAdapter& adapter,
            GameObject& object,
            World& world) noexcept;
        static GameObject* GetObject(
            const ObjectView& view) noexcept;
        static World* GetWorld(
            const WorldView& view) noexcept;
    };

    class GameProjectComponentAdapter final
        : public IComponent {
    public:
        GameProjectComponentAdapter(
            std::string typeName,
            IGameComponent* component,
            DestroyGameComponentFn destroy);
        ~GameProjectComponentAdapter() override;

        std::string_view GetTypeName() const override;
        void OnAttach() override;
        void OnDetach() override;
        void Update(float deltaTime) override;
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        IGameComponent* GetGameComponent() noexcept;
        bool WriteVariable(
            const WorldView& world,
            std::string_view variableId,
            const GameProjectBindingValue& value) noexcept;
        bool ReleaseVariable(
            const WorldView& world,
            std::string_view variableId) noexcept;

    private:
        void ReleaseAllVariables(const WorldView& world) noexcept;

        std::string typeName_{};
        std::string bindingWriterId_{};
        std::unordered_set<std::string> writtenVariableIds_{};
        IGameComponent* component_ = nullptr;
        DestroyGameComponentFn destroy_ = nullptr;
    };

    class GameProjectSystemAdapter final
        : public ISystem {
    public:
        GameProjectSystemAdapter(
            std::string systemId,
            IGameSystem* system,
            DestroyGameSystemFn destroy) noexcept;
        ~GameProjectSystemAdapter() override;

        std::string_view GetName() const override;
        void OnWorldAttached(World& world) override;
        void OnWorldDetached(World& world) override;
        void PreUpdate(
            World& world,
            const FrameContext& frame) override;
        void PreFixedUpdate(
            World& world,
            const FrameContext& frame) override;
        void FixedUpdate(
            World& world,
            const FrameContext& frame) override;
        void PostFixedUpdate(
            World& world,
            const FrameContext& frame) override;
        void Update(
            World& world,
            const FrameContext& frame) override;
        void LateUpdate(
            World& world,
            const FrameContext& frame) override;
        void PreRender(
            World& world,
            const FrameContext& frame) override;
        void Render(
            World& world,
            const FrameContext& frame) override;
        void PostRender(
            World& world,
            const FrameContext& frame) override;

    private:
        std::string systemId_{};
        IGameSystem* system_ = nullptr;
        DestroyGameSystemFn destroy_ = nullptr;
    };

} // namespace HIKARI
