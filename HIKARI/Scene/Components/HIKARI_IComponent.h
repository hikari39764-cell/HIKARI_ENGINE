#pragma once

#include <json.hpp>
#include <string_view>

namespace HIKARI {

    class IInspectorBuilder;

    class GameObject;

    class IComponent {
    public:
        virtual ~IComponent() = default;

        void SetOwner(GameObject* owner) { owner_ = owner; }
        GameObject* GetOwner() { return owner_; }
        const GameObject* GetOwner() const { return owner_; }

        virtual std::string_view GetTypeName() const = 0;
        virtual void OnAttach() {}
        virtual void Update(float dt) { (void)dt; }
        virtual void Render() {}
        virtual void RenderImGui() {}

        virtual void Serialize(nlohmann::json& out) const { out = nlohmann::json::object(); }
        virtual void Deserialize(const nlohmann::json& in) { (void)in; }
        virtual void BuildInspector(IInspectorBuilder& builder) { (void)builder; }

    private:
        GameObject* owner_ = nullptr;
    };

} // namespace HIKARI
