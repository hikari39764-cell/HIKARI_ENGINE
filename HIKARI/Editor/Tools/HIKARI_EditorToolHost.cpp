#include "HIKARI_EditorToolHost.h"

#include "Core/HIKARI_Logger.h"

#include <algorithm>
#include <utility>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    bool EditorToolHost::Register(EditorToolDescriptor descriptor) {
        if (descriptor.toolId.empty() ||
            descriptor.displayName.empty() ||
            descriptor.factory == nullptr ||
            Find(descriptor.toolId) != nullptr) {
            return false;
        }

        ToolEntry entry{};
        entry.toolId = descriptor.toolId;
        entry.displayName = descriptor.displayName;
        entry.menuGroup = descriptor.menuGroup;
        entry.open = descriptor.defaultOpen;
        entry.factory = descriptor.factory;
        tools_.push_back(std::move(entry));
        return true;
    }

    bool EditorToolHost::OpenTool(std::string_view toolId) {
        ToolEntry* entry = Find(toolId);
        if (!entry) {
            return false;
        }
        entry->open = true;
        return true;
    }

    bool EditorToolHost::RequestOpen(EditorToolOpenRequest request) {
        ToolEntry* entry = Find(request.toolId);
        if (!entry) {
            return false;
        }
        entry->open = true;
        entry->pendingOpenRequest = std::move(request);
        return true;
    }

    bool EditorToolHost::CloseTool(std::string_view toolId) {
        ToolEntry* entry = Find(toolId);
        if (!entry) {
            return false;
        }
        entry->open = false;
        entry->pendingOpenRequest.reset();
        return true;
    }

    bool EditorToolHost::IsToolOpen(std::string_view toolId) const {
        const ToolEntry* entry = Find(toolId);
        return entry && entry->open;
    }

    void EditorToolHost::DrawMenuItem(std::string_view toolId) {
#if defined(HIKARI_WITH_EDITOR)
        ToolEntry* entry = Find(toolId);
        if (!entry) {
            return;
        }
        ImGui::MenuItem(entry->displayName.c_str(), nullptr, &entry->open);
#else
        (void)toolId;
#endif
    }

    void EditorToolHost::DrawMenuContents() {
#if defined(HIKARI_WITH_EDITOR)
        for (size_t i = 0; i < tools_.size(); ++i) {
            ToolEntry& entry = tools_[i];
            if (entry.menuGroup.empty()) {
                ImGui::MenuItem(entry.displayName.c_str(), nullptr, &entry.open);
                continue;
            }

            const bool groupAlreadyDrawn = std::any_of(
                tools_.begin(),
                tools_.begin() + static_cast<std::ptrdiff_t>(i),
                [&entry](const ToolEntry& candidate) {
                    return candidate.menuGroup == entry.menuGroup;
                });
            if (groupAlreadyDrawn || !ImGui::BeginMenu(entry.menuGroup.c_str())) {
                continue;
            }

            for (ToolEntry& groupedEntry : tools_) {
                if (groupedEntry.menuGroup == entry.menuGroup) {
                    ImGui::MenuItem(
                        groupedEntry.displayName.c_str(),
                        nullptr,
                        &groupedEntry.open);
                }
            }
            ImGui::EndMenu();
        }
#endif
    }

    void EditorToolHost::Draw(EditorToolContext& context) {
        for (ToolEntry& entry : tools_) {
            if (!entry.open) {
                continue;
            }

            if (!entry.instance) {
                entry.instance = entry.factory();
                if (!entry.instance) {
                    entry.open = false;
                    entry.pendingOpenRequest.reset();
                    HIKARI_LOG_ERROR(
                        "[EditorTool] factory failed: " + entry.toolId);
                    continue;
                }
            }

            EditorToolContext toolContext{
                context.scene,
                context.editorContext,
                entry.pendingOpenRequest
                    ? &*entry.pendingOpenRequest
                    : nullptr
            };
            entry.instance->Draw(toolContext, entry.open);
            entry.pendingOpenRequest.reset();
        }
    }

    size_t EditorToolHost::GetRegisteredToolCount() const noexcept {
        return tools_.size();
    }

    EditorToolHost::ToolEntry* EditorToolHost::Find(std::string_view toolId) {
        const auto it = std::find_if(
            tools_.begin(),
            tools_.end(),
            [toolId](const ToolEntry& entry) {
                return entry.toolId == toolId;
            });
        return it == tools_.end() ? nullptr : &*it;
    }

    const EditorToolHost::ToolEntry* EditorToolHost::Find(std::string_view toolId) const {
        const auto it = std::find_if(
            tools_.begin(),
            tools_.end(),
            [toolId](const ToolEntry& entry) {
                return entry.toolId == toolId;
            });
        return it == tools_.end() ? nullptr : &*it;
    }

} // namespace HIKARI::EDITOR
