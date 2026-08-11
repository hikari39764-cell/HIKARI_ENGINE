#pragma once

#include <exception>
#include <json.hpp>
#include <string>
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
        virtual void OnDetach() {}
        virtual void Update(float dt) { (void)dt; }
        virtual void Render() {}
        virtual void RenderImGui() {}

        virtual void Serialize(nlohmann::json& out) const { out = nlohmann::json::object(); }
        virtual void Deserialize(const nlohmann::json& in) { (void)in; }
        virtual bool TryDeserialize(
            const nlohmann::json& in,
            std::string& outDiagnostic) {

            try {
                Deserialize(in);
                outDiagnostic.clear();
                return true;
            } catch (const std::exception& exception) {
                outDiagnostic = exception.what();
                return false;
            } catch (...) {
                outDiagnostic = "component deserializer threw an unknown exception";
                return false;
            }
        }
        virtual void BuildInspector(IInspectorBuilder& builder) { (void)builder; }

    private:
        GameObject* owner_ = nullptr;
    };

} // namespace HIKARI
