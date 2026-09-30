#pragma once

#include <cstdint>
#include <string_view>

#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

class GameObject;

enum class UiBindingSourceState : uint8_t {
    None,
    Resolved,
    Invalid,
};

class UiBindingSourceComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "UiBindingSourceComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void BuildInspector(IInspectorBuilder& builder) override;

    SceneObjectId GetTargetObjectId() const noexcept { return targetObjectId_; }
    void SetTargetObjectId(SceneObjectId target) noexcept { targetObjectId_ = target; }
    bool TryResolveTarget(RuntimeObjectHandle& outTarget) const noexcept;

private:
    SceneObjectId targetObjectId_{};
};

UiBindingSourceState ResolveInheritedUiBindingSource(const GameObject& object, RuntimeObjectHandle& outTarget) noexcept;

} // namespace HIKARI
