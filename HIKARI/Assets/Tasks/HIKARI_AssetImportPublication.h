#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include "Core/IO/HIKARI_FileReplacementTransaction.h"

namespace HIKARI {
    // worker は専用領域へ書き、owner thread だけが artifact と manifest を公開する。
    class AssetImportPublication final {
    public:
        explicit AssetImportPublication(const std::filesystem::path& projectRoot,
            const std::filesystem::path& libraryRoot = {},
            const std::filesystem::path& sourceMetaRoot = {});
        ~AssetImportPublication();
        AssetImportPublication(const AssetImportPublication&) = delete;
        AssetImportPublication& operator=(const AssetImportPublication&) = delete;
        std::filesystem::path StageFile(const std::filesystem::path& finalPath);
        std::filesystem::path StageDirectory(const std::filesystem::path& finalPath);
        void DiscardFile(const std::filesystem::path& finalPath);
        void DeleteOnPublish(const std::filesystem::path& finalPath);
        void AddValidation(std::function<bool(std::string&)> validation);
        std::filesystem::path ReadPreparedFile(const std::filesystem::path& finalPath) const;
        bool Publish(std::string& error);
    private:
        std::filesystem::path NormalizeDestination(const std::filesystem::path& path) const;
        struct Root {
            std::filesystem::path destination, stagingParent, staging;
        };
        Root& StagingOwner(const std::filesystem::path& destination);
        std::filesystem::path projectRoot_;
        std::vector<Root> roots_;
        std::vector<IO::FileReplacementOperation> files_;
        std::vector<std::function<bool(std::string&)>> validations_;
        bool published_ = false;
    };
}
