#include "Scene/HIKARI_SystemTypeRegistry.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

namespace {
    constexpr const char* kSettingsVersionKey = "_schemaVersion";

    void MergeMissingSettings(
        const nlohmann::json& defaults,
        nlohmann::json& destination) {

        if (!defaults.is_object()) {
            return;
        }
        if (!destination.is_object()) {
            destination = nlohmann::json::object();
        }
        for (const auto& [key, value] : defaults.items()) {
            if (!destination.contains(key)) {
                destination[key] = value;
                continue;
            }
            if (value.is_object() && destination[key].is_object()) {
                MergeMissingSettings(value, destination[key]);
            }
        }
    }
}

bool SystemSettingsContract::IsConfigured() const noexcept {
    return version != 0 ||
        (defaultSettings.is_object() && !defaultSettings.empty()) ||
        static_cast<bool>(migrate) ||
        static_cast<bool>(normalize) ||
        static_cast<bool>(validate);
}

bool SystemTypeRegistry::Register(SystemTypeInfo info) {
    if (info.systemId.empty() || !info.factory) {
        return false;
    }
    if (info.displayName.empty()) {
        info.displayName = info.systemId;
    }
    if (!info.settings.defaultSettings.is_object()) {
        info.settings.defaultSettings = nlohmann::json::object();
    }
    const std::string systemId = info.systemId;
    return byName_.emplace(systemId, std::move(info)).second;
}

void SystemTypeRegistry::Clear() noexcept {
    byName_.clear();
}

const SystemTypeInfo* SystemTypeRegistry::Find(std::string_view systemId) const {
    const auto it = byName_.find(std::string(systemId));
    if (it == byName_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::unique_ptr<ISystem> SystemTypeRegistry::Create(
    std::string_view systemId,
    const nlohmann::json& settings) const {

    const SystemTypeInfo* info = Find(systemId);
    if (!info) {
        return nullptr;
    }
    if (!info->settings.IsConfigured()) {
        return info->factory(settings);
    }

    nlohmann::json preparedSettings{};
    if (!PrepareSettings(
            systemId,
            settings,
            preparedSettings,
            nullptr)) {
        return nullptr;
    }
    return info->factory(preparedSettings);
}

nlohmann::json SystemTypeRegistry::CreateDefaultSettings(
    std::string_view systemId) const {

    const SystemTypeInfo* info = Find(systemId);
    if (!info) {
        return nlohmann::json::object();
    }
    nlohmann::json defaults = info->settings.defaultSettings.is_object()
        ? info->settings.defaultSettings
        : nlohmann::json::object();
    if (info->settings.version != 0) {
        defaults[kSettingsVersionKey] = info->settings.version;
    }
    if (info->settings.normalize) {
        info->settings.normalize(defaults);
    }
    return defaults;
}

bool SystemTypeRegistry::PrepareSettings(
    std::string_view systemId,
    const nlohmann::json& source,
    nlohmann::json& outSettings,
    std::vector<std::string>* outIssues) const {

    const SystemTypeInfo* info = Find(systemId);
    if (!info) {
        if (outIssues) {
            outIssues->push_back("System type is not registered.");
        }
        return false;
    }

    std::vector<std::string> issues;
    outSettings = source.is_object()
        ? source
        : nlohmann::json::object();
    const SystemSettingsContract& contract = info->settings;
    if (!contract.IsConfigured()) {
        if (outIssues) {
            *outIssues = std::move(issues);
        }
        return true;
    }

    uint32_t sourceVersion = 0;
    const auto versionIt = outSettings.find(kSettingsVersionKey);
    if (versionIt != outSettings.end() &&
        versionIt->is_number_unsigned()) {
        sourceVersion = versionIt->get<uint32_t>();
    } else if (versionIt != outSettings.end() &&
        versionIt->is_number_integer()) {
        const int64_t signedVersion = versionIt->get<int64_t>();
        sourceVersion = signedVersion > 0
            ? static_cast<uint32_t>(signedVersion)
            : 0;
    }

    if (contract.version != 0 && sourceVersion > contract.version) {
        issues.push_back(
            "Settings were authored by a newer system schema.");
    } else if (contract.version != 0 &&
        sourceVersion < contract.version && contract.migrate) {
        if (!contract.migrate(
                sourceVersion,
                contract.version,
                outSettings,
                issues)) {
            issues.push_back("System settings migration failed.");
        }
    }

    MergeMissingSettings(contract.defaultSettings, outSettings);
    if (contract.version != 0) {
        outSettings[kSettingsVersionKey] = contract.version;
    }
    if (contract.normalize) {
        contract.normalize(outSettings);
    }
    if (contract.validate) {
        std::vector<std::string> validationIssues =
            contract.validate(outSettings);
        issues.insert(
            issues.end(),
            std::make_move_iterator(validationIssues.begin()),
            std::make_move_iterator(validationIssues.end()));
    }

    if (outIssues) {
        *outIssues = issues;
    }
    return issues.empty();
}

std::vector<std::string> SystemTypeRegistry::GetTypeNames() const {
    std::vector<std::string> names;
    names.reserve(byName_.size());
    for (const auto& [name, _] : byName_) {
        names.push_back(name);
    }
    return names;
}

} // namespace HIKARI
