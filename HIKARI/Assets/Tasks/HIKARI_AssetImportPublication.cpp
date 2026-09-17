#include "Assets/Tasks/HIKARI_AssetImportPublication.h"
#include "Core/IO/HIKARI_NativeFilePath.h"
#include <atomic>
#include <algorithm>
#include <stdexcept>
#include <Windows.h>

namespace HIKARI {
    namespace fs = std::filesystem;
    namespace {
        bool IsChild(const fs::path& root, const fs::path& path) {
            const auto relative = path.lexically_relative(root);
            if (relative.empty() || relative.is_absolute() || relative == ".") return false;
            for (const auto& part : relative) if (part == "..") return false;
            return true;
        }
    }

    AssetImportPublication::AssetImportPublication(const fs::path& projectRoot,
        const fs::path& libraryRoot, const fs::path& sourceMetaRoot)
        : projectRoot_(fs::weakly_canonical(fs::absolute(projectRoot))) {
        const auto normalize = [&](const fs::path& value, const fs::path& fallback) {
            const auto path = value.empty() ? projectRoot_ / fallback : value;
            return fs::weakly_canonical(path.is_absolute() ? path : projectRoot_ / path);
        };
        roots_.push_back({normalize(libraryRoot, "Library"), {}, {}});
        roots_.push_back({normalize(sourceMetaRoot, "ProjectSettings/AssetMeta"), {}, {}});
        roots_.push_back({projectRoot_, {}, {}});
    }

    AssetImportPublication::Root& AssetImportPublication::StagingOwner(const fs::path& destination) {
        Root* selected = nullptr;
        for (auto& root : roots_) {
            if ((destination == root.destination || IsChild(root.destination, destination)) &&
                    (!selected || root.destination.native().size() > selected->destination.native().size()))
                selected = &root;
        }
        if (!selected) throw std::runtime_error("Import destination has no declared owner");
        if (!selected->staging.empty()) return *selected;
        selected->stagingParent = fs::weakly_canonical(selected->destination / ".ImportStaging");
        if (!IsChild(selected->destination, selected->stagingParent))
            throw std::runtime_error("Import staging directory is outside its declared owner");
        fs::create_directories(selected->stagingParent);
        static std::atomic_uint64_t serial{0};
        const auto staging = selected->stagingParent / ("job-" + std::to_string(GetCurrentProcessId()) +
            "-" + std::to_string(GetTickCount64()) + "-" + std::to_string(++serial));
        if (!fs::create_directory(staging))
            throw std::runtime_error("Could not create a private import staging directory");
        selected->staging = staging;
        return *selected;
    }

    AssetImportPublication::~AssetImportPublication() {
        // 所有する job directory だけを破棄する。既存 artifact には触れない。
        for (const auto& root : roots_) {
            if (root.staging.empty()) continue;
            std::error_code error{};
            const auto actual = fs::weakly_canonical(root.staging, error);
            if (!error && actual.parent_path() == root.stagingParent &&
                    actual.filename().native().starts_with(L"job-")) {
                fs::remove_all(IO::NativeFilePath(actual), error);
            }
        }
    }

    fs::path AssetImportPublication::NormalizeDestination(const fs::path& path) const {
        const auto absolute = fs::weakly_canonical(path.is_absolute() ? path : projectRoot_ / path);
        bool owned = false, privatePath = false;
        for (const auto& root : roots_) {
            owned |= IsChild(root.destination, absolute);
            const auto reserved = root.destination / ".ImportStaging";
            privatePath |= absolute == reserved || IsChild(reserved, absolute);
        }
        if (published_ || !owned || privatePath) {
            throw std::runtime_error("Invalid import publication destination");
        }
        return absolute;
    }

    fs::path AssetImportPublication::StageDirectory(const fs::path& finalPath) {
        const auto destination = NormalizeDestination(finalPath);
        auto& root = StagingOwner(destination);
        const auto staged = root.staging / destination.lexically_relative(root.destination);
        fs::create_directories(IO::NativeFilePath(staged));
        return staged;
    }

    fs::path AssetImportPublication::StageFile(const fs::path& finalPath) {
        const auto destination = NormalizeDestination(finalPath);
        for (const auto& file : files_) if (file.finalPath == destination) return file.stagedPath;
        auto& root = StagingOwner(destination);
        const auto staged = root.staging / destination.lexically_relative(root.destination);
        fs::create_directories(IO::NativeFilePath(staged.parent_path()));
        files_.push_back({ destination, staged });
        return staged;
    }

    fs::path AssetImportPublication::ReadPreparedFile(const fs::path& finalPath) const {
        const auto destination = NormalizeDestination(finalPath);
        for (const auto& file : files_) if (file.finalPath == destination) return file.stagedPath;
        return destination;
    }

    void AssetImportPublication::DiscardFile(const fs::path& finalPath) {
        const auto destination = NormalizeDestination(finalPath);
        std::erase_if(files_, [&](const auto& file) { return file.finalPath == destination; });
    }

    void AssetImportPublication::DeleteOnPublish(const fs::path& finalPath) {
        const auto destination = NormalizeDestination(finalPath);
        DiscardFile(destination);
        files_.push_back({ destination, {}, true });
    }

    void AssetImportPublication::AddValidation(std::function<bool(std::string&)> validation) {
        if (published_ || !validation || validations_.size() >= 8)
            throw std::runtime_error("Invalid import publication validation");
        validations_.push_back(std::move(validation));
    }

    bool AssetImportPublication::Publish(std::string& error) {
        if (published_ || files_.empty()) {
            error = "Import publication is empty or already committed";
            return false;
        }
        for (const auto& validate : validations_) if (!validate(error)) return false;
        for (const auto& file : files_) {
            bool ownedStage = file.deleteFinal;
            if (!ownedStage) {
                const auto staged = fs::weakly_canonical(file.stagedPath);
                for (const auto& root : roots_)
                    ownedStage |= !root.staging.empty() && IsChild(root.staging, staged);
            }
            if (fs::weakly_canonical(file.finalPath) != file.finalPath ||
                    !ownedStage) {
                error = "Import publication path changed during preparation";
                return false;
            }
        }
        for (const auto& file : files_) {
            if (file.deleteFinal) continue;
            std::error_code directoryError;
            fs::create_directories(file.finalPath.parent_path(), directoryError);
            if (directoryError) {
                error = "Could not prepare import destination: " + directoryError.message();
                return false;
            }
        }
        if (!IO::CommitFileReplacementTransaction(files_, error)) return false;
        published_ = true;
        return true;
    }
}
