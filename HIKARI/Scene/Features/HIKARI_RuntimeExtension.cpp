#include "Scene/Features/HIKARI_RuntimeExtension.h"

#include <algorithm>
#include <utility>

#include "Scene/Features/HIKARI_RuntimeFeatureCatalog.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"

namespace HIKARI {

bool RuntimeExtensionHost::Add(std::unique_ptr<IRuntimeExtension> extension) {
    if (!extension || extension->GetExtensionId().empty()) {
        return false;
    }
    if (Contains(extension->GetExtensionId())) {
        return false;
    }
    Entry entry{};
    entry.extension = extension.get();
    entry.owned = std::move(extension);
    extensions_.push_back(std::move(entry));
    return true;
}

bool RuntimeExtensionHost::AddBorrowed(IRuntimeExtension& extension) {
    if (extension.GetExtensionId().empty() || Contains(extension.GetExtensionId())) {
        return false;
    }
    Entry entry{};
    entry.extension = &extension;
    extensions_.push_back(std::move(entry));
    return true;
}

void RuntimeExtensionHost::Clear() noexcept { extensions_.clear(); }

bool RuntimeExtensionHost::RegisterWorldServices(WorldServiceRegistry& services) {
    bool success = true;
    for (const Entry& entry : extensions_) {
        success = entry.extension->RegisterWorldServices(services) && success;
    }
    return success;
}

bool RuntimeExtensionHost::RegisterRuntimeFeatures(RuntimeFeatureCatalog& catalog) {
    bool success = true;
    for (const Entry& entry : extensions_) {
        success = entry.extension->RegisterRuntimeFeatures(catalog) && success;
    }
    return success;
}

size_t RuntimeExtensionHost::Size() const noexcept { return extensions_.size(); }

bool RuntimeExtensionHost::Contains(std::string_view extensionId) const noexcept {
    return std::any_of(extensions_.begin(), extensions_.end(), [extensionId](const Entry& entry) {
        return entry.extension != nullptr && entry.extension->GetExtensionId() == extensionId;
    });
}

} // namespace HIKARI
