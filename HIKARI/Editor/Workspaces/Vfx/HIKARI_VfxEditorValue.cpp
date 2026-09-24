#include "Editor/Workspaces/Vfx/HIKARI_VfxEditorValue.h"

#include "Editor/Widgets/HIKARI_TypedValueEditor.h"

namespace HIKARI::EDITOR {
namespace {
constexpr TypedValueMessages kMessages{" VFX", "VFX", "VFX"};
} // namespace

bool TryReadVfxEditorValue(const nlohmann::json& node, VFX::VfxValueType expectedType, VFX::VfxValue& outValue,
                           std::string& outMessage) {
    return TryReadTypedEditorValue(node, expectedType, VFX::MakeDefaultVfxValue(expectedType), outValue, outMessage,
                                   kMessages);
}

nlohmann::json WriteVfxEditorValue(const VFX::VfxValue& value) { return WriteTypedEditorValue(value); }

#if defined(HIKARI_WITH_EDITOR)
VfxValueEditResult DrawVfxValueEditor(const char* id, VFX::VfxValue& value, float width) {
    return DrawTypedEditorValue(id, value, {}, width);
}
#endif

} // namespace HIKARI::EDITOR
