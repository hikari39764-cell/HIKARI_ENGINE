#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "Core/HIKARI_AssetGuid.h"
#include "Editor/Views/Director/HIKARI_EditorDirectorCameraController.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspace.h"

namespace HIKARI::EDITOR {

// Tool Preview のカメラ状態をアセットごとに保持する。
// この状態は Editor session 専用であり、Scene や Asset には保存しない。
class ToolPreviewCameraStateStore {
public:
    bool Save(EditorWorkspaceId workspaceId, const AssetGuid& assetGuid,
              const EditorDirectorCameraController& controller) {
        const std::string key = BuildKey(workspaceId, assetGuid);
        if (key.empty()) {
            return false;
        }
        states_[key] = controller.CaptureState();
        return true;
    }

    bool Restore(EditorWorkspaceId workspaceId, const AssetGuid& assetGuid,
                 EditorDirectorCameraController& controller) const {
        const std::string key = BuildKey(workspaceId, assetGuid);
        const auto found = states_.find(key);
        if (found == states_.end()) {
            return false;
        }
        return controller.RestoreState(found->second);
    }

    void Erase(EditorWorkspaceId workspaceId, const AssetGuid& assetGuid) {
        const std::string key = BuildKey(workspaceId, assetGuid);
        if (!key.empty()) {
            states_.erase(key);
        }
    }

    void Clear() noexcept { states_.clear(); }
    size_t Size() const noexcept { return states_.size(); }

private:
    static std::string BuildKey(EditorWorkspaceId workspaceId, const AssetGuid& assetGuid) {
        if (workspaceId == EditorWorkspaceId::Count || !assetGuid.IsValid()) {
            return {};
        }
        return std::to_string(static_cast<uint32_t>(workspaceId)) + ":" + assetGuid.value;
    }

    std::unordered_map<std::string, EditorDirectorCameraState> states_{};
};

} // namespace HIKARI::EDITOR
