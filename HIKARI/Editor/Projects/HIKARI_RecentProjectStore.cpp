#include "Editor/Projects/HIKARI_RecentProjectStore.h"

#include <ShlObj.h>

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <utility>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/HIKARI_ProjectDescriptor.h"
#include "Project/Paths/HIKARI_ProjectPath.h"

namespace HIKARI::EDITOR {
namespace {

    constexpr size_t kMaximumRecentProjects = 64u;

    void SetMessage(
        std::string* outMessage,
        std::string message) {

        if (outMessage != nullptr) {
            *outMessage = std::move(message);
        }
    }

    std::filesystem::path ResolveStoragePath() {
        PWSTR localAppData = nullptr;
        const HRESULT result = SHGetKnownFolderPath(
            FOLDERID_LocalAppData,
            KF_FLAG_DEFAULT,
            nullptr,
            &localAppData);
        if (FAILED(result) || localAppData == nullptr) {
            return {};
        }
        const std::filesystem::path path =
            std::filesystem::path(localAppData) /
            "HIKARI" /
            "Editor" /
            "recent_projects.json";
        CoTaskMemFree(localAppData);
        return path;
    }

    bool SameRoot(
        const std::filesystem::path& lhs,
        const std::filesystem::path& rhs) {

        return _wcsicmp(
            lhs.lexically_normal().c_str(),
            rhs.lexically_normal().c_str()) == 0;
    }

    int64_t CurrentUnixSeconds() {
        return std::chrono::duration_cast<
            std::chrono::seconds>(
                std::chrono::system_clock::now()
                    .time_since_epoch())
            .count();
    }

} // namespace

    bool RecentProjectStore::Load(
        std::string* outMessage) {

        entries_.clear();
        storagePath_ = ResolveStoragePath();
        if (storagePath_.empty()) {
            SetMessage(
                outMessage,
                "Could not resolve the local editor data directory.");
            return false;
        }

        std::error_code ec{};
        if (!std::filesystem::is_regular_file(
                storagePath_,
                ec)) {
            SetMessage(outMessage, {});
            return true;
        }

        nlohmann::json document{};
        std::string readMessage{};
        if (!SERIALIZATION::JSON::ReadJsonFile(
                storagePath_,
                document,
                &readMessage) ||
            !document.is_object() ||
            !document.contains("projects") ||
            !document["projects"].is_array()) {
            SetMessage(
                outMessage,
                readMessage.empty()
                    ? "Recent project data is invalid."
                    : std::move(readMessage));
            return false;
        }

        for (const nlohmann::json& item :
            document["projects"]) {
            if (!item.is_object() ||
                !item.contains("root") ||
                !item["root"].is_string()) {
                continue;
            }
            const std::filesystem::path root =
                PROJECT_PATHS::NormalizeProjectRoot(
                    TEXT::PathFromUtf8(
                        item["root"].get<std::string>()));
            ProjectDescriptor descriptor{};
            if (root.empty()) continue;
            const bool available = LoadProjectDescriptor(root / kProjectDescriptorFilename, descriptor);
            if (std::any_of(entries_.begin(), entries_.end(), [&](const auto& entry) { return SameRoot(entry.root, root); })) continue;
            RecentProjectEntry entry{};
            entry.name = available ? descriptor.name :
                (item.contains("name") && item["name"].is_string() ? item["name"].get<std::string>() : TEXT::PathToUtf8(root.filename()));
            entry.root = root;
            entry.available = available;
            entry.pinned = item.contains("pinned") && item["pinned"].is_boolean() && item["pinned"].get<bool>();
            entry.thumbnail = item.contains("thumbnail") && item["thumbnail"].is_string() ? item["thumbnail"].get<std::string>() : std::string{};
            entry.lastOpenedUnixSeconds = item.contains("lastOpened") && item["lastOpened"].is_number_integer()
                ? item["lastOpened"].get<int64_t>() : 0;
            entries_.push_back(std::move(entry));
        }

        SortAndTrim();
        SetMessage(outMessage, {});
        return true;
    }

    bool RecentProjectStore::AddOrTouch(
        const std::filesystem::path& projectRoot,
        std::string* outMessage) {

        const std::filesystem::path normalized =
            PROJECT_PATHS::NormalizeProjectRoot(projectRoot);
        ProjectDescriptor descriptor{};
        std::string descriptorMessage{};
        if (normalized.empty() ||
            !LoadProjectDescriptor(
                normalized /
                    kProjectDescriptorFilename,
                descriptor,
                &descriptorMessage)) {
            SetMessage(
                outMessage,
                descriptorMessage.empty()
                    ? "Project descriptor is unavailable."
                    : std::move(descriptorMessage));
            return false;
        }
        if (storagePath_.empty()) {
            storagePath_ = ResolveStoragePath();
        }
        if (storagePath_.empty()) {
            SetMessage(
                outMessage,
                "Could not resolve the local editor data directory.");
            return false;
        }

        const auto previous = entries_;
        bool pinned = false;
        std::string thumbnail;
        for (const auto& entry : entries_) if (SameRoot(entry.root, normalized)) {pinned = entry.pinned; thumbnail = entry.thumbnail;}
        entries_.erase(
            std::remove_if(
                entries_.begin(),
                entries_.end(),
                [&normalized](
                    const RecentProjectEntry& entry) {
                    return SameRoot(
                        entry.root,
                        normalized);
                }),
            entries_.end());
        entries_.insert(
            entries_.begin(),
            RecentProjectEntry{
                .name = std::move(descriptor.name),
                .root = normalized,
                .lastOpenedUnixSeconds =
                    CurrentUnixSeconds(),
                .pinned = pinned,
                .thumbnail = std::move(thumbnail),
            });
        SortAndTrim();
        if (Save(outMessage)) return true;
        entries_ = previous; return false;
    }

    bool RecentProjectStore::Remove(
        const std::filesystem::path& projectRoot,
        std::string* outMessage) {

        const auto previous = entries_;
        const size_t previousSize = entries_.size();
        entries_.erase(
            std::remove_if(
                entries_.begin(),
                entries_.end(),
                [&projectRoot](
                    const RecentProjectEntry& entry) {
                    return SameRoot(
                        entry.root,
                        projectRoot);
                }),
            entries_.end());
        if (entries_.size() == previousSize) {
            SetMessage(outMessage, {});
            return true;
        }
        if (Save(outMessage)) return true;
        entries_ = previous; return false;
    }

    const std::vector<RecentProjectEntry>&
        RecentProjectStore::GetEntries() const noexcept {
        return entries_;
    }

    const std::filesystem::path&
        RecentProjectStore::GetStoragePath() const noexcept {
        return storagePath_;
    }

    bool RecentProjectStore::Save(
        std::string* outMessage) {

        nlohmann::json projects =
            nlohmann::json::array();
        for (const RecentProjectEntry& entry : entries_) {
            projects.push_back({
                { "lastOpened", entry.lastOpenedUnixSeconds },
                { "name", entry.name },
                { "pinned", entry.pinned },
                { "thumbnail", entry.thumbnail },
                {
                    "root",
                    TEXT::PathToUtf8(entry.root)
                },
            });
        }
        return SERIALIZATION::JSON::WriteJsonFile(
            storagePath_,
            nlohmann::json{
                { "projects", std::move(projects) },
                { "version", 2 },
            },
            outMessage);
    }

    void RecentProjectStore::SortAndTrim() {
        std::stable_sort(entries_.begin(), entries_.end(), [](const auto& a, const auto& b) {
            if (a.pinned != b.pinned) return a.pinned;
            return a.lastOpenedUnixSeconds > b.lastOpenedUnixSeconds;
        });
        const auto pinned = std::count_if(entries_.begin(), entries_.end(), [](const auto& entry) { return entry.pinned; });
        if (entries_.size() > kMaximumRecentProjects + pinned) entries_.resize(kMaximumRecentProjects + pinned);
    }

    bool RecentProjectStore::SetThumbnail(const std::filesystem::path& root, std::string path, std::string* message) {
        const auto previous = entries_;
        for (auto& entry : entries_) if (SameRoot(entry.root, root)) entry.thumbnail = path;
        if (Save(message)) return true;
        entries_ = previous; return false;
    }

    bool RecentProjectStore::SetPinned(const std::filesystem::path& root, bool pinned, std::string* message) {
        const auto previous = entries_;
        for (auto& entry : entries_) if (SameRoot(entry.root, root)) entry.pinned = pinned;
        SortAndTrim();
        if (Save(message)) return true;
        entries_ = previous; return false;
    }

    bool RecentProjectStore::Relocate(const std::filesystem::path& root, const std::filesystem::path& replacement, std::string* message) {
        const auto normalized = PROJECT_PATHS::NormalizeProjectRoot(replacement);
        ProjectDescriptor descriptor;
        if (!LoadProjectDescriptor(normalized / kProjectDescriptorFilename, descriptor, message)) return false;
        const auto previous = entries_;
        auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) { return SameRoot(entry.root, root); });
        if (found == entries_.end()) return false;
        const bool pinned = found->pinned;
        const auto lastOpened = found->lastOpenedUnixSeconds;
        const auto thumbnail = found->thumbnail;
        std::erase_if(entries_, [&](const auto& entry) { return SameRoot(entry.root, root) || SameRoot(entry.root, normalized); });
        entries_.push_back({descriptor.name, normalized, lastOpened, pinned, true, thumbnail});
        SortAndTrim();
        if (Save(message)) return true;
        entries_ = previous; return false;
    }

} // namespace HIKARI::EDITOR
