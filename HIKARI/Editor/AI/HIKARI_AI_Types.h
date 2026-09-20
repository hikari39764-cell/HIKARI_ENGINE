#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <json.hpp>
namespace HIKARI::EDITOR {
    namespace AI_ASSISTANT {

        struct AiCapabilityParameter {
            std::string name{};
            std::string type{};
            bool required = false;
            std::string description{};
        };

        struct AiCapability {
            std::string id;
            std::string summary;
            std::string status; // stable | planned | unsupported
            std::vector<AiCapabilityParameter> parameters{};
            std::string workspace = "general";
        };

        struct AiRequestResult {
            bool ok = false;
            // Stable machine-readable error code for failures (for example
            // NODE_NOT_FOUND / PIN_TYPE_MISMATCH / COMPILE_FAILED).
            std::string code{};
            std::string message;
            nlohmann::json data = nlohmann::json::object();
            std::vector<std::string> details;
            // Actionable suggestions for the model to self-correct.
            std::vector<std::string> hints;
        };

        // Assistant-local discovery/help and optional vision; authoring uses
        // the workspace operation registry.
        const std::vector<AiCapability>& BuiltInCapabilities();
        AiRequestResult SubmitAiRequest(
            std::string_view requestJson,
            const std::vector<AiCapability>& capabilities,
            const std::function<bool()>& shouldCancel = {});

    } // namespace AI_ASSISTANT

}
