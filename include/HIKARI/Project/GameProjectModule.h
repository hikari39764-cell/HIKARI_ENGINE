#pragma once

#include <memory>
#include <string_view>

#include "HIKARI/Project/GameProjectSession.h"
#include "HIKARI/Project/RuntimeFeatureRegistration.h"

namespace HIKARI::RUNTIME {

class IGameProjectModule {
public:
    virtual ~IGameProjectModule() = default;

    virtual std::string_view GetModuleId() const noexcept = 0;
    // RegisterRuntimeFeature<Feature>(catalog) でプロジェクトの Feature を追加する。
    virtual bool RegisterRuntimeFeatures(RuntimeFeatureCatalog& catalog) = 0;

    // Return an empty pointer when this project does not need session state.
    virtual std::unique_ptr<IGameProjectSession> CreateGameSession() { return {}; }
};

std::unique_ptr<IGameProjectModule> CreateGameProjectModule();

} // namespace HIKARI::RUNTIME
