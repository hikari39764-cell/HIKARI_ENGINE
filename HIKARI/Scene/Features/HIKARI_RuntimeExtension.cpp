#include "Scene/Features/HIKARI_RuntimeExtension.h"

#include <algorithm>
#include <utility>

#include "Scene/Features/HIKARI_RuntimeFeatureCatalog.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"

namespace HIKARI {

    bool RuntimeExtensionHost::Add(
        std::unique_ptr<IRuntimeExtension> extension) {

        if (!extension || extension->GetExtensionId().empty()) {
            return false;
        }
        const std::string id(extension->GetExtensionId());
        const bool duplicate = std::any_of(
            extensions_.begin(),
            extensions_.end(),
            [&id](const std::unique_ptr<IRuntimeExtension>& existing) {
                return existing && existing->GetExtensionId() == id;
            });
        if (duplicate) {
            return false;
        }
        extensions_.push_back(std::move(extension));
        return true;
    }

    void RuntimeExtensionHost::Clear() noexcept {
        extensions_.clear();
    }

    bool RuntimeExtensionHost::RegisterWorldServices(
        WorldServiceRegistry& services) {

        bool success = true;
        for (const std::unique_ptr<IRuntimeExtension>& extension :
            extensions_) {
            success = extension->RegisterWorldServices(services) && success;
        }
        return success;
    }

    bool RuntimeExtensionHost::RegisterRuntimeFeatures(
        RuntimeFeatureCatalog& catalog) {

        bool success = true;
        for (const std::unique_ptr<IRuntimeExtension>& extension :
            extensions_) {
            success = extension->RegisterRuntimeFeatures(catalog) && success;
        }
        return success;
    }

    std::vector<std::string>
        RuntimeExtensionHost::GetExtensionIds() const {

        std::vector<std::string> ids;
        ids.reserve(extensions_.size());
        for (const std::unique_ptr<IRuntimeExtension>& extension :
            extensions_) {
            ids.emplace_back(extension->GetExtensionId());
        }
        return ids;
    }

    size_t RuntimeExtensionHost::Size() const noexcept {
        return extensions_.size();
    }

} // namespace HIKARI
