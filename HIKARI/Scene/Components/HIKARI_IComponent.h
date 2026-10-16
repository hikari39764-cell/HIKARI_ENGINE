#pragma once

#include <cstdint>
#include <exception>
#include <json.hpp>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <string>
#include <string_view>

namespace HIKARI {

class IInspectorBuilder;

class GameObject;
class World;

class IComponent {
public:
    virtual ~IComponent() = default;

    // 具象型の override から実行能力を導出する。独自 factory は従来通り全 callback を許可。
    template <class T, class... Args>
    static std::unique_ptr<T> Create(Args&&... args) {
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        auto& base = static_cast<IComponent&>(*component);
        if constexpr (requires { &T::Update; })
            base.needsUpdate_ = !std::is_same_v<decltype(&T::Update), decltype(&IComponent::Update)>;
        if constexpr (requires { &T::Render; })
            base.needsRender_ = !std::is_same_v<decltype(&T::Render), decltype(&IComponent::Render)>;
        return component;
    }
    bool NeedsUpdate() const noexcept { return needsUpdate_; }
    bool NeedsRender() const noexcept { return needsRender_; }

    void SetOwner(GameObject* owner) { owner_ = owner; }
    GameObject* GetOwner() { return owner_; }
    const GameObject* GetOwner() const { return owner_; }

    virtual std::string_view GetTypeName() const = 0;
    virtual void OnAttach() {}
    virtual void OnDetach() {}
    virtual void Update(float dt) { (void)dt; }
    virtual void Render() {}

    virtual void Serialize(nlohmann::json& out) const { out = nlohmann::json::object(); }
    virtual void Deserialize(const nlohmann::json& in) { (void)in; }
    virtual bool TryDeserialize(const nlohmann::json& in, std::string& outDiagnostic) {
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

protected:
    // 描画に効く値が変わった時、所有オブジェクトの描画状態を再投影させる。
    void NotifyRenderStateDirty();

private:
    friend class World;

    GameObject* owner_ = nullptr;
    bool needsUpdate_ = true;
    bool needsRender_ = true;
    uint64_t worldComponentTypeId_ = 0u;
    uint32_t worldComponentIndex_ = (std::numeric_limits<uint32_t>::max)();
};

} // namespace HIKARI
