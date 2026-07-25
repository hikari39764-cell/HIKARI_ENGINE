#include "Runtime/Project/HIKARI_IGameProjectModule.h"

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "Scene/Features/HIKARI_RuntimeFeatureCatalog.h"

namespace HIKARI::GAME_PROJECT {
namespace {

    class DevelopmentRuntimeExtension final : public IRuntimeExtension {
    public:
        std::string_view GetExtensionId() const noexcept override {
            return "HIKARI.DevelopmentProject";
        }

        bool RegisterRuntimeFeatures(
            RuntimeFeatureCatalog&) override {
            return true;
        }
    };

    class DevelopmentGameProjectModule final
        : public RUNTIME::IGameProjectModule {
    public:
        std::string_view GetModuleId() const noexcept override {
            return "HIKARI.DevelopmentProject";
        }

        std::vector<std::unique_ptr<IRuntimeExtension>>
            CreateRuntimeExtensions() const override {

            std::vector<std::unique_ptr<IRuntimeExtension>> extensions;
            extensions.push_back(
                std::make_unique<DevelopmentRuntimeExtension>());
            return extensions;
        }
    };

} // namespace
} // namespace HIKARI::GAME_PROJECT

namespace HIKARI::RUNTIME {

    std::unique_ptr<IGameProjectModule> CreateGameProjectModule() {
        return std::make_unique<
            GAME_PROJECT::DevelopmentGameProjectModule>();
    }

} // namespace HIKARI::RUNTIME
