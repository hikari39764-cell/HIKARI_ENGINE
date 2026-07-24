#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace HIKARI {

    class DocumentSceneBase;
    struct EditorContext;

    namespace EDITOR {

        enum class EditorToolTargetKind : uint8_t {
            None,
            Scene,
            SceneObject,
            Component,
            Asset,
            System,
        };

        struct EditorToolTarget {
            EditorToolTargetKind kind = EditorToolTargetKind::None;
            uint64_t sceneObjectId = 0;
            std::string typeId{};
            std::string assetGuid{};
        };

        struct EditorToolOpenRequest {
            std::string toolId{};
            EditorToolTarget target{};
        };

        struct EditorToolContext {
            DocumentSceneBase& scene;
            EditorContext& editorContext;
            // The request is valid only for the current Draw call. Tools that
            // keep a target must copy the fields they need.
            const EditorToolOpenRequest* openRequest = nullptr;
        };

        class IEditorTool {
        public:
            virtual ~IEditorTool() = default;
            virtual void Draw(EditorToolContext& context, bool& open) = 0;
        };

        using EditorToolFactoryFn = std::unique_ptr<IEditorTool>(*)();

        struct EditorToolDescriptor {
            std::string_view toolId{};
            std::string_view displayName{};
            std::string_view menuGroup{};
            bool defaultOpen = false;
            EditorToolFactoryFn factory = nullptr;
        };

    } // namespace EDITOR

} // namespace HIKARI
