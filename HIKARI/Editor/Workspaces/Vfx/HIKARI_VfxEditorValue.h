#pragma once

#include <string>

#include <json.hpp>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::EDITOR {

    bool TryReadVfxEditorValue(
        const nlohmann::json& node,
        VFX::VfxValueType expectedType,
        VFX::VfxValue& outValue,
        std::string& outMessage);
    nlohmann::json WriteVfxEditorValue(
        const VFX::VfxValue& value);

#if defined(HIKARI_WITH_EDITOR)
    struct VfxValueEditResult {
        bool changed = false;
        bool committed = false;
    };

    VfxValueEditResult DrawVfxValueEditor(
        const char* id,
        VFX::VfxValue& value,
        float width = -1.0f);
#endif

} // namespace HIKARI::EDITOR
