#include "HIKARI_BuiltInEditorTools.h"

#include "Editor/Panels/HIKARI_LightingBakePanel.h"
#include "Editor/Tools/HIKARI_EditorToolHost.h"

#include <array>
#include <memory>

namespace HIKARI::EDITOR {

    namespace {

        class LightingBakeEditorTool final : public IEditorTool {
        public:
            void Draw(EditorToolContext& context, bool& open) override {
                panel_.Draw(context.scene, open);
            }

        private:
            LightingBakePanel panel_{};
        };

        std::unique_ptr<IEditorTool> CreateLightingBakeTool() {
            return std::make_unique<LightingBakeEditorTool>();
        }

        constexpr std::array<EditorToolDescriptor, 1> kBuiltInEditorTools{
            EditorToolDescriptor{
                kLightingBakeToolId,
                "Lighting Bake",
                "Lighting",
                false,
                &CreateLightingBakeTool
            }
        };

    } // namespace

    std::span<const EditorToolDescriptor> GetBuiltInEditorTools() {
        return kBuiltInEditorTools;
    }

    void RegisterBuiltInEditorTools(EditorToolHost& host) {
        for (const EditorToolDescriptor& descriptor : GetBuiltInEditorTools()) {
            host.Register(descriptor);
        }
    }

} // namespace HIKARI::EDITOR
