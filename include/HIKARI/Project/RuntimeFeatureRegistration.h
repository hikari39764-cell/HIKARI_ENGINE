#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/RuntimeFeature.h"

namespace HIKARI {

    class RuntimeFeatureCatalog;

    using RuntimeFeatureDestroyFn =
        void (*)(IRuntimeFeature*) noexcept;

    HIKARI_GAME_PROJECT_SDK_API bool
        RegisterModuleOwnedRuntimeFeature(
            RuntimeFeatureCatalog& catalog,
            IRuntimeFeature* feature,
            RuntimeFeatureDestroyFn destroy);

    template<class Feature, class... Arguments>
    bool RegisterRuntimeFeature(
        RuntimeFeatureCatalog& catalog,
        Arguments&&... arguments) {

        static_assert(
            std::is_base_of_v<IRuntimeFeature, Feature>);
        auto feature = std::make_unique<Feature>(
            std::forward<Arguments>(arguments)...);
        IRuntimeFeature* value = feature.get();
        if (!RegisterModuleOwnedRuntimeFeature(
                catalog,
                value,
                [](IRuntimeFeature* owned) noexcept {
                    delete static_cast<Feature*>(owned);
                })) {
            return false;
        }
        feature.release();
        return true;
    }

} // namespace HIKARI
