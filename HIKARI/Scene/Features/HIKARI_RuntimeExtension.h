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
};

class RuntimeExtensionHost {
public:
    bool Add(std::unique_ptr<IRuntimeExtension> extension);
    bool AddBorrowed(IRuntimeExtension& extension);
    void Clear() noexcept;

    bool RegisterWorldServices(WorldServiceRegistry& services);
    bool RegisterRuntimeFeatures(RuntimeFeatureCatalog& catalog);

    size_t Size() const noexcept;

private:
    struct Entry {
        std::unique_ptr<IRuntimeExtension> owned{};
        IRuntimeExtension* extension = nullptr;
    };

    bool Contains(std::string_view extensionId) const noexcept;

    std::vector<Entry> extensions_{};
};

} // namespace HIKARI
