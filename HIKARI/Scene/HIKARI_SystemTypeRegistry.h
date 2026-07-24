#pragma once

#include <functional>
#include <memory>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <json.hpp>

namespace HIKARI {

class ISystem;

struct SystemSettingsContract {
    using MigrateFn = std::function<bool(
        uint32_t fromVersion,
        uint32_t toVersion,
        nlohmann::json& settings,
        std::vector<std::string>& issues)>;
    using NormalizeFn = std::function<void(nlohmann::json& settings)>;
    using ValidateFn = std::function<std::vector<std::string>(
        const nlohmann::json& settings)>;

    uint32_t version = 0;
    nlohmann::json defaultSettings = nlohmann::json::object();
    MigrateFn migrate{};
    NormalizeFn normalize{};
    ValidateFn validate{};

    bool IsConfigured() const noexcept;
};

struct SystemTypeInfo {
    using FactoryFn = std::function<std::unique_ptr<ISystem>(const nlohmann::json& settings)>;

    std::string systemId{};
    FactoryFn factory{};
    std::string displayName{};
    std::string featureId{};
    SystemSettingsContract settings{};
};

class SystemTypeRegistry {
public:
    bool Register(SystemTypeInfo info);
    void Clear() noexcept;
    const SystemTypeInfo* Find(std::string_view systemId) const;
    std::unique_ptr<ISystem> Create(std::string_view systemId, const nlohmann::json& settings) const;
    nlohmann::json CreateDefaultSettings(
        std::string_view systemId) const;
    bool PrepareSettings(
        std::string_view systemId,
        const nlohmann::json& source,
        nlohmann::json& outSettings,
        std::vector<std::string>* outIssues = nullptr) const;
    std::vector<std::string> GetTypeNames() const;

private:
    std::unordered_map<std::string, SystemTypeInfo> byName_{};
};

} // namespace HIKARI
