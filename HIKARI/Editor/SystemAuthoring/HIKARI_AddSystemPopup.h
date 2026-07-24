#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace HIKARI::EDITOR {

    class SystemAuthoringRegistry;
    struct SceneSystemAuthoringRow;

    class AddSystemPopup {
    public:
        void Open();

        std::optional<std::string> Draw(
            const std::vector<SceneSystemAuthoringRow>& rows,
            const SystemAuthoringRegistry& authoringRegistry);

    private:
        bool openRequested_ = false;
        std::array<char, 128> filter_{};
        std::string selectedSystemId_{};
    };

} // namespace HIKARI::EDITOR
