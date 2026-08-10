#pragma once

#include <string>
#include <string_view>

#include <json.hpp>

#include "Runtime/Binding/HIKARI_RuntimeBindingCatalog.h"

namespace HIKARI::EDITOR {

    bool TryParseRuntimeBindingEditorValueType(
        std::string_view text,
        BINDING::ValueType& outType) noexcept;

    bool TryReadRuntimeBindingEditorValue(
        const nlohmann::json& node,
        BINDING::ValueType expectedType,
        BINDING::Value& outValue,
        std::string& outMessage);

    nlohmann::json WriteRuntimeBindingEditorValue(
        const BINDING::Value& value);

#if defined(HIKARI_WITH_EDITOR)
    struct RuntimeBindingValueEditResult {
        bool changed = false;
        bool committed = false;
    };

    RuntimeBindingValueEditResult DrawRuntimeBindingValueEditor(
        const char* id,
        BINDING::Value& value,
        const BINDING::VariableDefinition* range = nullptr,
        float width = -1.0f);
#endif

} // namespace HIKARI::EDITOR
