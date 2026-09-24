#pragma once

#include <string_view>

namespace HIKARI {

class RuntimeFeatureCatalog;
class WorldServiceRegistry;

class IRuntimeExtension {
public:
    virtual ~IRuntimeExtension() = default;

    virtual std::string_view GetExtensionId() const noexcept = 0;
    virtual bool RegisterWorldServices(WorldServiceRegistry&) { return true; }
    virtual bool RegisterRuntimeFeatures(RuntimeFeatureCatalog&) = 0;
};

} // namespace HIKARI
