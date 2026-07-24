#pragma once

#include <filesystem>
#include <string>

#include <json.hpp>

namespace HIKARI::SERIALIZATION::JSON {

    // Reads one JSON document without applying format-specific schema rules.
    bool ReadJsonFile(
        const std::filesystem::path& path,
        nlohmann::json& outDocument,
        std::string* outMessage = nullptr);

    // Writes the canonical indented engine JSON representation and commits it
    // through the shared staged-file transaction.
    bool WriteJsonFile(
        const std::filesystem::path& path,
        const nlohmann::json& document,
        std::string* outMessage = nullptr);

} // namespace HIKARI::SERIALIZATION::JSON
