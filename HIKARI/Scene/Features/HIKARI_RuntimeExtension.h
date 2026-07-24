#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace HIKARI {

    class RuntimeFeatureCatalog;
    class WorldServiceRegistry;

    class IRuntimeExtension {
    public:
        virtual ~IRuntimeExtension() = default;

        virtual std::string_view GetExtensionId() const noexcept = 0;
        virtual bool RegisterWorldServices(
            WorldServiceRegistry&) { return true; }
        virtual bool RegisterRuntimeFeatures(
            RuntimeFeatureCatalog&) = 0;
    };

    class RuntimeExtensionHost {
    public:
        bool Add(std::unique_ptr<IRuntimeExtension> extension);
        void Clear() noexcept;

        bool RegisterWorldServices(
            WorldServiceRegistry& services);
        bool RegisterRuntimeFeatures(
            RuntimeFeatureCatalog& catalog);

        std::vector<std::string> GetExtensionIds() const;
        size_t Size() const noexcept;

    private:
        std::vector<std::unique_ptr<IRuntimeExtension>> extensions_{};
    };

} // namespace HIKARI
