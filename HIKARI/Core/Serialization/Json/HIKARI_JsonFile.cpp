#include "Core/Serialization/Json/HIKARI_JsonFile.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <utility>

#include "Core/IO/HIKARI_FileReplacementTransaction.h"

namespace HIKARI::SERIALIZATION::JSON {
    namespace {

        void SetMessage(
            std::string* outMessage,
            std::string message) {

            if (outMessage != nullptr) {
                *outMessage = std::move(message);
            }
        }

        std::filesystem::path MakeStagedPath(
            const std::filesystem::path& finalPath) {

            static std::atomic_uint64_t sequence{ 0u };
            const auto timestamp = std::chrono::steady_clock::now()
                .time_since_epoch()
                .count();
            std::filesystem::path stagedPath = finalPath;
            stagedPath += std::filesystem::path(
                ".hikari-json-" +
                std::to_string(timestamp) + "-" +
                std::to_string(sequence.fetch_add(
                    1u,
                    std::memory_order_relaxed))).native();
            return stagedPath;
        }

        void RemoveStagedFile(
            const std::filesystem::path& path) noexcept {

            std::error_code ignored{};
            std::filesystem::remove(path, ignored);
        }

    } // namespace

    bool ReadJsonFile(
        const std::filesystem::path& path,
        nlohmann::json& outDocument,
        std::string* outMessage) {

        std::ifstream input(path);
        if (!input.is_open()) {
            SetMessage(
                outMessage,
                "failed to open JSON file: " + path.generic_string());
            return false;
        }

        nlohmann::json document =
            nlohmann::json::parse(input, nullptr, false);
        if (document.is_discarded()) {
            SetMessage(
                outMessage,
                "failed to parse JSON file: " + path.generic_string());
            return false;
        }

        outDocument = std::move(document);
        SetMessage(outMessage, {});
        return true;
    }

    bool WriteJsonFile(
        const std::filesystem::path& path,
        const nlohmann::json& document,
        std::string* outMessage) {

        if (path.empty()) {
            SetMessage(outMessage, "JSON output path is empty");
            return false;
        }

        std::string serializedDocument{};
        try {
            serializedDocument = document.dump(2);
        } catch (const std::exception& exception) {
            SetMessage(
                outMessage,
                "failed to serialize JSON document: " +
                    std::string(exception.what()));
            return false;
        }

        std::error_code ec{};
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path(), ec);
        }
        if (ec) {
            SetMessage(
                outMessage,
                "failed to create JSON directory: " + ec.message());
            return false;
        }

        const std::filesystem::path stagedPath =
            MakeStagedPath(path);
        std::ofstream output(
            stagedPath,
            std::ios::out | std::ios::trunc);
        if (!output.is_open()) {
            SetMessage(
                outMessage,
                "failed to open staged JSON file: " +
                    stagedPath.generic_string());
            return false;
        }

        output << serializedDocument << '\n';
        output.close();
        if (output.fail()) {
            RemoveStagedFile(stagedPath);
            SetMessage(
                outMessage,
                "failed while writing JSON file: " +
                    path.generic_string());
            return false;
        }

        std::string commitMessage{};
        if (!IO::CommitStagedFile(
            stagedPath,
            path,
            commitMessage)) {
            RemoveStagedFile(stagedPath);
            SetMessage(
                outMessage,
                "failed to commit JSON file: " + commitMessage);
            return false;
        }

        SetMessage(outMessage, {});
        return true;
    }

} // namespace HIKARI::SERIALIZATION::JSON
