#pragma once
#include "Scene/Components/HIKARI_IComponent.h"
#include <string>

namespace HIKARI {
namespace BINDING {
class RuntimeBindingStore;
}
// 表示への単方向射影。変数の書き込みやゲームルールは所有しない。
class UiDataBindingComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "UiDataBindingComponent"; }
    void Serialize(nlohmann::json&) const override;
    void Deserialize(const nlohmann::json&) override;
    void BuildInspector(IInspectorBuilder&) override;
    std::string ResolveText(const BINDING::RuntimeBindingStore*, std::string_view fallback) const;
    bool IsVisible(const BINDING::RuntimeBindingStore*) const;
    bool IsInteractable(const BINDING::RuntimeBindingStore*) const;

private:
    bool enabled_ = true, integer_ = true;
    std::string textVariable_, visibleVariable_, interactableVariable_, prefix_, suffix_;
    int decimals_ = 0;
};
} // namespace HIKARI
