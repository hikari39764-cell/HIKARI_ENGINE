#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "HIKARI/Project/RuntimeExtension.h"

namespace HIKARI {

    class RuntimeExtensionHost {
    public:
        bool Add(std::unique_ptr<IRuntimeExtension> extension);
        bool AddBorrowed(IRuntimeExtension& extension);
        void Clear() noexcept;

        bool RegisterWorldServices(
            WorldServiceRegistry& services);
        bool RegisterRuntimeFeatures(
            RuntimeFeatureCatalog& catalog);

        std::vector<std::string> GetExtensionIds() const;
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
