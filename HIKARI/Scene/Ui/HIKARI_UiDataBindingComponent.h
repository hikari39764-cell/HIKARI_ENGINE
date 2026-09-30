#pragma once
#include <cstdint>
#include <string_view>

#include "Scene/Components/HIKARI_IComponent.h"
#include <string>

namespace HIKARI {
namespace BINDING {
class RuntimeBindingStore;
struct Value;
} // namespace BINDING

enum class UiBoundValueStatus : uint8_t {
    Unbound,
    Value,
    InvalidObjectSource,
};

enum class UiBindingReadMode : uint8_t {
    Runtime,
    AuthoringPreview,
};

UiBoundValueStatus ReadUiBoundValue(const IComponent& component, const BINDING::RuntimeBindingStore* store,
                                    std::string_view variableId, BINDING::Value& outValue,
                                    UiBindingReadMode mode = UiBindingReadMode::Runtime) noexcept;

// 表示への単方向射影。変数の書き込みやゲームルールは所有しない。
class UiDataBindingComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "UiDataBindingComponent"; }
    void Serialize(nlohmann::json&) const override;
    void Deserialize(const nlohmann::json&) override;
    void BuildInspector(IInspectorBuilder&) override;
    bool HasTextBinding() const noexcept { return enabled_ && !textVariable_.empty(); }
    std::string ResolveText(const BINDING::RuntimeBindingStore*, std::string_view fallback) const;
    bool IsVisible(const BINDING::RuntimeBindingStore*) const;
    bool IsInteractable(const BINDING::RuntimeBindingStore*) const;
    std::string ResolveTextForPreview(const BINDING::RuntimeBindingStore*, std::string_view fallback) const;
    bool IsVisibleForPreview(const BINDING::RuntimeBindingStore*) const;
    bool IsInteractableForPreview(const BINDING::RuntimeBindingStore*) const;

private:
    std::string ResolveTextWithMode(const BINDING::RuntimeBindingStore*, std::string_view fallback,
                                    UiBindingReadMode mode) const;
    bool IsVisibleWithMode(const BINDING::RuntimeBindingStore*, UiBindingReadMode mode) const;
    bool IsInteractableWithMode(const BINDING::RuntimeBindingStore*, UiBindingReadMode mode) const;

    bool enabled_ = true, integer_ = true;
    std::string textVariable_, visibleVariable_, interactableVariable_, prefix_, suffix_;
    int decimals_ = 0;
};
} // namespace HIKARI
