#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Input/Runtime/HIKARI_InputTypes.h"

namespace HIKARI::INPUT {
	// 入力アクションの定義を表す構造体
struct InputActionDefinition {
    std::string actionId{};
    std::string displayName{};
    InputActionValueType valueType = InputActionValueType::Button;
    bool clampValue = true;
};
// 入力バインディングの定義を表す構造体
struct InputBinding {
    std::string actionId{};
    InputBindingSource source = InputBindingSource::Keyboard;
    std::string control{};
    float scale = 1.0f;
    uint8_t component = 0;
    std::string modifierControl{};
    float deadZone = 0.0f;
};
// 入力コンテキストの定義を表す構造体
struct InputContextDefinition {
    std::string contextId{};
    std::string displayName{};
    int priority = 0;
    bool consumeInput = false;
    bool enabledByDefault = true;
    std::vector<InputBinding> bindings{};
};
// 入力バインディングの競合を表す構造体
struct InputBindingConflict {
    std::string contextId{};
    std::string control{};
    std::string firstActionId{};
    std::string secondActionId{};
};

class InputActionMap {
public:
    uint32_t version = 1;
	// 入力アクションの定義のリスト
    std::vector<InputActionDefinition> actions{};
    std::vector<InputContextDefinition> contexts{};
	// 入力アクションの定義を検索する
    const InputActionDefinition* FindAction(std::string_view actionId) const;
    InputActionDefinition* FindAction(std::string_view actionId);
    const InputContextDefinition* FindContext(std::string_view contextId) const;
    InputContextDefinition* FindContext(std::string_view contextId);
    bool RemoveAction(std::string_view actionId);
    bool RemoveContext(std::string_view contextId);
    std::vector<std::string> Validate() const;
    std::vector<InputBindingConflict> FindConflicts() const;
};
// 入力アクションの値の型を文字列に変換する
const char* ToString(InputActionValueType type) noexcept;
bool TryParseInputActionValueType(
    std::string_view value,
    InputActionValueType& out) noexcept;
const char* ToString(InputBindingSource source) noexcept;
bool TryParseInputBindingSource(
    std::string_view value,
    InputBindingSource& out) noexcept;

} // namespace HIKARI::INPUT
