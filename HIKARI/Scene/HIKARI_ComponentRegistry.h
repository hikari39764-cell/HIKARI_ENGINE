#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <json.hpp>

#include "Authoring/HIKARI_ComponentAuthoringTypes.h"
#include "HIKARI/Runtime/ComponentTypeId.h"

namespace HIKARI {

class GameObject;
class IComponent;
struct InspectorContext;
struct SceneObjectData;

struct ComponentTypePresentation {
    std::string displayName{};
    std::string category{"Other"};
    std::string description{};
    std::string featureId{};
};

enum class ComponentRuntimeApplyPolicy : uint8_t {
    Immediate,
    OnEditCommit,
};

enum class ComponentRegistrationSource : uint8_t {
    Engine,
    GameProject,
};

struct ComponentTypeInfo {
    using FactoryFn = std::function<std::unique_ptr<IComponent>()>;
    using InitializeDefaultsFn = std::function<void(const SceneObjectData& object, nlohmann::json& properties)>;
    using ConfigureDependenciesFn =
        std::function<void(SceneObjectData& object, const std::vector<std::string>& newlyAddedDependencies)>;
    using ValidateAuthoringFn =
        std::function<AuthoringCommandResult(const IComponent& component, const InspectorContext& context)>;

    std::string typeName{};
    ComponentTypeId typeId{};
    FactoryFn factory{};
    std::vector<std::string> requiredComponents{};
    std::vector<std::string> optionalComponents{};
    std::vector<std::string> incompatibleComponents{};
    bool allowMultiple = false;
    bool affectsRenderState = false;
    ComponentRegistrationSource registrationSource = ComponentRegistrationSource::Engine;
    // GameProject 由来の型だけが持つ、モジュール内で一意な型の印（ForEachComponent の型確認用）。
    const void* gameProjectTypeToken = nullptr;
    ComponentRuntimeApplyPolicy runtimeApplyPolicy = ComponentRuntimeApplyPolicy::Immediate;
    InitializeDefaultsFn initializeDefaults{};
    ConfigureDependenciesFn configureDependencies{};
    ComponentTypePresentation presentation{};
    std::vector<ComponentAuthoringCommandDefinition> authoringCommands{};
    ValidateAuthoringFn validateAuthoring{};
};

class ComponentRegistry {
public:
    bool Register(ComponentTypeInfo info);
    void Clear() noexcept;
    const ComponentTypeInfo* Find(std::string_view typeName) const;
    const ComponentTypeInfo* Find(ComponentTypeId typeId) const noexcept;
    std::vector<std::string> GetTypeNames() const;
    std::vector<const ComponentTypeInfo*> GetTypeInfos() const;

private:
    std::unordered_map<std::string, ComponentTypeInfo> byName_{};
    std::unordered_map<uint64_t, std::string> byId_{};
};

} // namespace HIKARI
