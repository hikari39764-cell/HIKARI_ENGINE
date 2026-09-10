#include "Editor/Export/HIKARI_GameExportPublication.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include <chrono>

namespace HIKARI::EDITOR {
    namespace {
        bool Contains(const std::filesystem::path& root, const std::filesystem::path& child) {
            auto r = root.begin(), c = child.begin();
            for (; r != root.end(); ++r, ++c) {
                if (c == child.end()) return false;
                if (_wcsicmp(r->c_str(), c->c_str()) != 0) return false;
            }
            return true;
        }
        bool IsExport(const std::filesystem::path& directory) {
            return std::filesystem::is_regular_file(directory / "HIKARI/runtime_config.json");
        }
    }
    GameExportPublication::~GameExportPublication() {
        std::error_code ec;
        if (!staging_.empty()) std::filesystem::remove_all(staging_, ec);
        if (ownsLock_) std::filesystem::remove(lock_, ec);
    }
    bool GameExportPublication::Begin(const std::filesystem::path& destination,
        const std::filesystem::path& projectRoot, std::string& message) {
        if (ownsLock_) { message = "Export transaction is already active."; return false; }
        try {
            destination_ = std::filesystem::weakly_canonical(destination);
            const auto project = std::filesystem::weakly_canonical(projectRoot);
            if (destination_.empty() || destination_ == destination_.root_path() ||
                Contains(destination_, project)) {
                message = "Export output must not replace a project or its parent directory.";
                return false;
            }
            for (const auto* folder : {"Assets", "Source", "ProjectSettings", "Library", "out"}) {
                if (Contains(project / folder, destination_)) {
                    message = "Export output must be outside project source, settings and build caches.";
                    return false;
                }
            }
            if (std::filesystem::exists(destination_) &&
                (!std::filesystem::is_directory(destination_) ||
                 (!std::filesystem::is_empty(destination_) && !IsExport(destination_)))) {
                message = "Output is not empty and is not a HIKARI game package.";
                return false;
            }
            std::filesystem::create_directories(destination_.parent_path());
            lock_ = destination_; lock_ += L".export-lock";
            ownsLock_ = std::filesystem::create_directory(lock_);
            if (!ownsLock_) {
                message = "Another export owns this output, or an interrupted export needs inspection: " + TEXT::PathToUtf8(lock_);
                return false;
            }
            auto candidate = destination_;
            candidate += L".staging-" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count());
            if (!std::filesystem::create_directory(candidate)) {
                message = "Could not reserve a fresh export staging directory.";
                return false;
            }
            staging_ = candidate;
            previous_ = candidate; previous_ += L".previous";
            return true;
        } catch (const std::exception& e) { message = e.what(); return false; }
    }
    bool GameExportPublication::Publish(std::string& message) {
        try {
            if (staging_.empty() || !std::filesystem::is_regular_file(staging_ / "product_manifest.json")) {
                message = "Only a validated and hashed package can be published.";
                return false;
            }
            const bool hadOutput = std::filesystem::exists(destination_);
            if (hadOutput) std::filesystem::rename(destination_, previous_);
            std::error_code publishError;
            std::filesystem::rename(staging_, destination_, publishError);
            if (publishError) {
                std::error_code restoreError;
                if (hadOutput) std::filesystem::rename(previous_, destination_, restoreError);
                message = "Could not publish package: " + publishError.message();
                if (restoreError) message += ". Previous package retained at " + TEXT::PathToUtf8(previous_);
                return false;
            }
            staging_.clear();
            std::error_code cleanupError;
            if (hadOutput) std::filesystem::remove_all(previous_, cleanupError);
            if (cleanupError) message = "Previous package retained at " + TEXT::PathToUtf8(previous_);
            return true;
        } catch (const std::exception& e) { message = e.what(); return false; }
    }
}
