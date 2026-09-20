#include "HIKARI_EditorAuthoringSessionHost.h"
#include <algorithm>
#include <fstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace HIKARI::EDITOR {
namespace {
bool ValidSessionId(const std::string& id) {
    return id.starts_with("authoring-") && id.size() == 42 &&
        std::all_of(id.begin() + 10, id.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        });
}
bool WriteCheckpoint(const std::filesystem::path& path, const std::string& bytes) {
    auto temporary = path; temporary.replace_extension(".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) return false;
    }
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    return !error;
#endif
}
}

void EditorAuthoringSessionHost::SetRecoveryRoot(const std::filesystem::path& libraryRoot) {
    const auto folder = libraryRoot / "EditorAI" / "Drafts";
    if (folder == recoveryRoot_) return;
    Reset();
    recoveryRoot_ = folder;
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error) { recoveryErrors_.push_back({{"message", error.message()}}); return; }
    std::filesystem::directory_iterator iterator(folder, error), end;
    for (; !error && iterator != end; iterator.increment(error)) {
        const auto path = iterator->path();
        if (path.extension() != ".json") continue;
        const auto id = path.stem().string();
        if (!ValidSessionId(id)) continue;
        try {
            if (iterator->file_size() > 32u * 1024u * 1024u) throw std::runtime_error("Draft exceeds recovery file limit.");
            std::ifstream input(path, std::ios::binary);
            const auto data = nlohmann::json::parse(input);
            if (data.at("version") != 1 || data.at("sessionId") != id)
                throw std::runtime_error("Invalid draft version or identity.");
            const auto workspace = data.at("workspace").get<std::string>();
            const auto provider = workspaces_.find(workspace);
            if (provider == workspaces_.end()) throw std::runtime_error("Draft workspace is unavailable.");
            if (sessions_.size() >= 8) throw std::runtime_error("Close a session before restoring additional drafts.");
            auto backend = provider->second.create();
            if (!backend) throw std::runtime_error("Draft provider is unavailable.");
            const auto result = backend->Restore(data.at("document"));
            if (!result.ok) throw std::runtime_error(result.message);
            sessions_.emplace(id, Session{workspace, std::move(backend), true, true, data.dump()});
        } catch (const std::exception& exception) {
            // 壊れた原本を保持し、ほかの文書の復旧は続ける。
            recoveryErrors_.push_back({{"sessionId", id}, {"message", exception.what()}});
        }
    }
    if (error) recoveryErrors_.push_back({{"message", error.message()}});
}

void EditorAuthoringSessionHost::FlushRecovery() {
    if (recoveryRoot_.empty()) return;
    for (auto& [id, session] : sessions_) {
        try {
            const auto state = session.backend->Describe().dump();
            if (state == session.checkpointState) continue;
            auto document = session.backend->Checkpoint();
            if (document.is_null()) { session.checkpointState = state; continue; }
            const auto bytes = nlohmann::json{{"version", 1}, {"sessionId", id},
                {"workspace", session.workspace}, {"document", std::move(document)}}.dump();
            if (bytes.size() > 32u * 1024u * 1024u)
                throw std::runtime_error("Draft exceeds recovery file limit; save the document as an asset.");
            if (bytes == session.checkpoint) { session.checkpointState = state; continue; }
            if (!WriteCheckpoint(recoveryRoot_ / (id + ".json"), bytes))
                throw std::runtime_error("Could not save background draft recovery data.");
            session.checkpoint = bytes;
            session.checkpointState = state;
            session.checkpointed = true;
            // 一時的な書込失敗が回復したら、この文書の古い警告を残さない。
            auto& issues = recoveryErrors_.get_ref<nlohmann::json::array_t&>();
            std::erase_if(issues, [&](const auto& issue) {
                return issue.value("sessionId", std::string{}) == id;
            });
        } catch (const std::exception& exception) {
            session.checkpointed = false;
            const nlohmann::json issue{{"sessionId", id}, {"message", exception.what()}};
            if (std::find(recoveryErrors_.begin(), recoveryErrors_.end(), issue) == recoveryErrors_.end())
                recoveryErrors_.push_back(issue);
        }
    }
}

bool EditorAuthoringSessionHost::RemoveRecovery(const std::string& id) {
    if (recoveryRoot_.empty()) return true;
    if (!ValidSessionId(id)) return false;
    std::error_code error;
    std::filesystem::remove(recoveryRoot_ / (id + ".json"), error);
    if (error) recoveryErrors_.push_back({{"sessionId", id}, {"message", error.message()}});
    return !error;
}
}
