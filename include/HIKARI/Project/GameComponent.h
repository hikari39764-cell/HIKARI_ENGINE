#pragma once

#include "HIKARI/Project/Inspector.h"
#include "HIKARI/Project/PropertyArchive.h"
#include "HIKARI/Project/WorldView.h"

namespace HIKARI {

struct GameProjectSdkAccess;

// Callback-scoped access to the owning object, World, runtime variables,
// and events. Variable ownership is assigned by the engine per component
// instance; callers never invent writer strings and every owned variable
// is released automatically when the component detaches.
class GameComponentContext final {
public:
    ObjectView GetObject() const noexcept { return object_; }
    WorldView GetWorld() const noexcept { return world_; }

    bool ReadVariable(std::string_view variableId, GameProjectBindingValue& outValue) const noexcept {
        return world_.ReadVariable(variableId, outValue);
    }
    bool ReadObjectVariable(std::string_view variableId, GameProjectBindingValue& outValue) const noexcept {
        return world_.ReadObjectVariable(object_.GetHandle(), variableId, outValue);
    }
    HIKARI_GAME_PROJECT_SDK_API bool WriteVariable(std::string_view variableId,
                                                   const GameProjectBindingValue& value) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool ReleaseVariable(std::string_view variableId) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool WriteObjectVariable(std::string_view variableId,
                                                         const GameProjectBindingValue& value) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool ReleaseObjectVariable(std::string_view variableId) const noexcept;
    bool EmitEvent(std::string_view eventId) const noexcept { return world_.EmitEvent(eventId, object_.GetHandle()); }
    bool EmitEvent(std::string_view eventId, const GameProjectBindingValue& payload) const noexcept {
        return world_.EmitEvent(eventId, object_.GetHandle(), payload);
    }

private:
    friend struct GameProjectSdkAccess;

    GameComponentContext(ObjectView object, WorldView world, void* bindingOwner) noexcept
        : object_(object), world_(world), bindingOwner_(bindingOwner) {}

    ObjectView object_{};
    WorldView world_{};
    void* bindingOwner_ = nullptr;
};

class IGameComponent {
public:
    virtual ~IGameComponent() = default;

    virtual void OnAttach(const ObjectView&) {}
    virtual void OnAttach(GameComponentContext& context) { OnAttach(context.GetObject()); }
    virtual void OnDetach(GameComponentContext&) {}
    virtual void Update(float deltaTime) { (void)deltaTime; }
    virtual void Update(GameComponentContext&, float deltaTime) { Update(deltaTime); }
    virtual void LoadProperties(const PropertyReader&) {}
    virtual void SaveProperties(PropertyWriter&) const {}
    virtual void BuildInspector(GameProjectInspector&) {}
};

} // namespace HIKARI
