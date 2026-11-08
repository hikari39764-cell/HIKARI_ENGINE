#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace HIKARI {

class RuntimeFeatureCatalog;
class WorldServiceRegistry;

// 引擎内部の拡張点（World サービスと Feature の登録）。プロジェクトは GameProjectModule から Feature だけを登録する。
class IRuntimeExtension {
public:
    virtual ~IRuntimeExtension() = default;

    virtual std::string_view GetExtensionId() const noexcept = 0;
    virtual bool RegisterWorldServices(WorldServiceRegistry&) { return true; }
    virtual bool RegisterRuntimeFeatures(RuntimeFeatureCatalog&) = 0;
    // 項目に入れたモジュールは、入れたこと自体が有効化。項目設定の一覧に書かなくても登録した Feature を有効にする。
    virtual bool EnablesRegisteredFeatures() const noexcept { return false; }
};

class RuntimeExtensionHost {
public:
    bool Add(std::unique_ptr<IRuntimeExtension> extension);
    bool AddBorrowed(IRuntimeExtension& extension);
    void Clear() noexcept;

    bool RegisterWorldServices(WorldServiceRegistry& services);
    bool RegisterRuntimeFeatures(RuntimeFeatureCatalog& catalog);

    size_t Size() const noexcept;
    // 直近の登録で、拡張が有効化する Feature の ID。
    const std::vector<std::string>& GetEnabledByExtensions() const noexcept { return enabledByExtensions_; }
    // 直近の登録で、その拡張が登録した Feature の ID。
    std::vector<std::string> GetRegisteredFeatures(std::string_view extensionId) const;

private:
    struct Entry {
        std::unique_ptr<IRuntimeExtension> owned{};
        IRuntimeExtension* extension = nullptr;
        std::vector<std::string> registeredFeatures{};
    };

    bool Contains(std::string_view extensionId) const noexcept;

    std::vector<Entry> extensions_{};
    std::vector<std::string> enabledByExtensions_{};
};

} // namespace HIKARI
