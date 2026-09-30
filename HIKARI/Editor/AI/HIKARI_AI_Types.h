#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <json.hpp>
#include "Core/Text/HIKARI_EnumNames.h"
namespace HIKARI::EDITOR {
enum class AiProjectAccessMode : uint8_t { Content, ReadProjectCode, DevelopProject };
inline constexpr TEXT::EnumName<AiProjectAccessMode> kAiProjectAccessModeNames[] = {
    {AiProjectAccessMode::Content, "content"},
    {AiProjectAccessMode::ReadProjectCode, "readProjectCode"},
    {AiProjectAccessMode::DevelopProject, "developProject"},
};

enum class AiRequestSource : uint8_t { BuiltInAi, LocalBridge };

namespace AI_ASSISTANT {

std::optional<AiProjectAccessMode> RequiredProjectAccess(std::string_view operationId) noexcept;
bool AllowsProjectAccess(AiProjectAccessMode mode, std::string_view operationId) noexcept;

struct AiCapabilityParameter {
    std::string name{};
    std::string type{};
    bool required = false;
    std::string description{};
    nlohmann::json schema = nlohmann::json::object();
};

struct AiCapability {
    std::string id;
    std::string summary;
    std::string status; // stable | planned | unsupported
    std::vector<AiCapabilityParameter> parameters{};
    std::string workspace = "general";
    nlohmann::json semantics = nlohmann::json::object();
    std::vector<std::string> searchTerms{};
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
nlohmann::json DescribeAiCapability(const AiCapability& capability, bool parameters = true, bool forModel = false);
const std::vector<AiCapability>& BuiltInCapabilities();
AiRequestResult SubmitAiRequest(std::string_view requestJson, const std::vector<AiCapability>& capabilities);

} // namespace AI_ASSISTANT

} // namespace HIKARI::EDITOR
