#include "Editor/Workspaces/Vfx/HIKARI_VfxEditorValue.h"

#include "Editor/Widgets/HIKARI_TypedValueEditor.h"
#include "Core/Serialization/Json/HIKARI_TypedValueJson.h"

namespace HIKARI::EDITOR {
namespace {
constexpr SERIALIZATION::JSON::TypedValueMessages kMessages{" VFX", "VFX", "VFX"};
} // namespace

bool TryReadVfxEditorValue(const nlohmann::json& node, VFX::VfxValueType expectedType, VFX::VfxValue& outValue,
                           std::string& outMessage) {
    return SERIALIZATION::JSON::TryReadTypedValue(node, expectedType, VFX::MakeDefaultVfxValue(expectedType), outValue,
                                                  outMessage, kMessages);
}

nlohmann::json WriteVfxEditorValue(const VFX::VfxValue& value) { return SERIALIZATION::JSON::WriteTypedValue(value); }

VfxValueEditResult DrawVfxValueEditor(const char* id, VFX::VfxValue& value, float width) {
    return DrawTypedEditorValue(id, value, {}, width);
}

} // namespace HIKARI::EDITOR
