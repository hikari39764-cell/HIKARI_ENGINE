#pragma once

#include <span>
#include <string>
#include <string_view>

#include "Authoring/HIKARI_ComponentAuthoringTypes.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingCatalog.h"

namespace HIKARI::EDITOR {

    bool DrawRuntimeVariableField(
        const BINDING::Catalog& catalog,
        std::string_view widgetId,
        BINDING::ValueType expectedType,
        std::string& value);

    bool DrawRuntimeVariableField(
        const BINDING::Catalog& catalog,
        std::string_view widgetId,
        std::span<const BINDING::ValueType> allowedTypes,
        std::string& value);

    bool DrawRuntimeEventField(
        const BINDING::Catalog& catalog,
        std::string_view widgetId,
        bool hasPayload,
        BINDING::ValueType payloadType,
        std::string& value);

    bool DrawRuntimeEventField(
        const BINDING::Catalog& catalog,
        std::string_view widgetId,
        AuthoringEventPayloadPolicy policy,
        BINDING::ValueType payloadType,
        std::string& value);

} // namespace HIKARI::EDITOR
