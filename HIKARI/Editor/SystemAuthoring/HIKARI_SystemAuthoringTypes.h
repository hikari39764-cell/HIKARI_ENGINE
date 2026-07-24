#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

namespace HIKARI {
    class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

    enum class SystemAuthoringScope {
        RuntimeOnly,
        ComponentOwned,
        SceneSettings,
        ProjectSettings,
        DedicatedTool,
    };

    enum class SystemSettingFieldType {
        Boolean,
        Integer,
        Float,
        String,
        Enum,
        Vec3,
    };

    struct SystemSettingOption {
        std::string value{};
        std::string displayName{};
    };

    struct SystemSettingField {
        std::string jsonPointer{};
        std::string displayName{};
        std::string description{};
        SystemSettingFieldType type = SystemSettingFieldType::String;
        bool advanced = false;
        bool hasRange = false;
        double minimum = 0.0;
        double maximum = 0.0;
        double step = 0.1;
        std::vector<SystemSettingOption> options{};
    };

    struct SystemSettingsEditorContext {
        DocumentSceneBase& scene;
        std::string_view systemId{};
    };

    using DrawCustomSystemSettingsFn = std::function<bool(
        SystemSettingsEditorContext& context,
        nlohmann::json& settings)>;

    struct SystemAuthoringDescriptor {
        std::string systemId{};
        std::string description{};
        SystemAuthoringScope scope = SystemAuthoringScope::RuntimeOnly;
        std::string configurationHint{};
        std::vector<SystemSettingField> fields{};
        DrawCustomSystemSettingsFn drawCustomSettings{};
        std::string dedicatedToolId{};
    };

    const char* ToDisplayName(SystemAuthoringScope scope) noexcept;

} // namespace HIKARI::EDITOR
