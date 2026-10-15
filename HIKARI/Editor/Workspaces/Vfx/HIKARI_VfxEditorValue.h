#pragma once

#include <string>

#include <json.hpp>

#include "Editor/Widgets/HIKARI_TypedValueEditor.h"

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::EDITOR {

bool TryReadVfxEditorValue(const nlohmann::json& node, VFX::VfxValueType expectedType, VFX::VfxValue& outValue,
                           std::string& outMessage);
nlohmann::json WriteVfxEditorValue(const VFX::VfxValue& value);

using VfxValueEditResult = TypedValueEditResult;

VfxValueEditResult DrawVfxValueEditor(const char* id, VFX::VfxValue& value, float width = -1.0f);

} // namespace HIKARI::EDITOR
