#include "Editor/Project/HIKARI_GameProjectEditorDocumentService.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#include <Windows.h>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/IO/HIKARI_FileReplacementTransaction.h"
#include "Core/IO/HIKARI_NativeFilePath.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Documents/HIKARI_EditorSourceVersion.h"
#include "Editor/History/HIKARI_DocumentSnapshotHistory.h"
#include "Project/Paths/HIKARI_ProjectDataFiles.h"

namespace HIKARI::EDITOR {
namespace {

using namespace EDITOR_PROJECT;
using DocumentText = std::shared_ptr<const std::string>;
using DocumentHistory = DocumentSnapshotHistory<DocumentText>;

std::atomic<uint64_t> gNextProjectEditorDocumentHandle{1u};

EditorDocumentHandle AllocateDocumentHandle() noexcept {
    uint64_t current = gNextProjectEditorDocumentHandle.load(std::memory_order_relaxed);
    while (current != 0u) {
        const uint64_t next = current == (std::numeric_limits<uint64_t>::max)() ? 0u : current + 1u;
        if (gNextProjectEditorDocumentHandle.compare_exchange_weak(current, next, std::memory_order_relaxed,
                                                                   std::memory_order_relaxed)) {
            return current;
        }
    }
    return kInvalidEditorDocumentHandle;
}

EditorDocumentCode MapDataFileCode(PROJECT_DATA::ProjectDataFileCode code) noexcept {
    switch (code) {
    case PROJECT_DATA::ProjectDataFileCode::InvalidRelativePath:
    case PROJECT_DATA::ProjectDataFileCode::NotDirectory: return EditorDocumentCode::InvalidPath;
    case PROJECT_DATA::ProjectDataFileCode::ReparsePoint: return EditorDocumentCode::ReparsePoint;
    case PROJECT_DATA::ProjectDataFileCode::Resolved: return EditorDocumentCode::Ok;
    case PROJECT_DATA::ProjectDataFileCode::NotFound: return EditorDocumentCode::NotFound;
    case PROJECT_DATA::ProjectDataFileCode::TooLarge: return EditorDocumentCode::TooLarge;
    case PROJECT_DATA::ProjectDataFileCode::InvalidUtf8: return EditorDocumentCode::InvalidUtf8;
    case PROJECT_DATA::ProjectDataFileCode::IoFailure: return EditorDocumentCode::IoFailure;
    }
    return EditorDocumentCode::InvalidPath;
}

std::filesystem::path MakeStagedPath(const std::filesystem::path& finalPath) {
    static std::atomic<uint64_t> serial{1u};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path staged = finalPath;
    staged += L".hikari-project-document-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(ticks) +
              L"-" + std::to_wstring(serial.fetch_add(1u, std::memory_order_relaxed));
    return staged;
}

void RemoveStagedFile(const std::filesystem::path& path) noexcept {
    std::error_code ignored{};
    std::filesystem::remove(IO::NativeFilePath(path), ignored);
}

bool WriteStagedText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream output(IO::NativeFilePath(path), std::ios::binary | std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }
    if (!text.empty()) {
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
    output.flush();
    output.close();
    return !output.fail();
}

bool SamePath(const std::filesystem::path& lhs, const std::filesystem::path& rhs) noexcept {
    const std::wstring& left = lhs.native();
    const std::wstring& right = rhs.native();
    return ::CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()), right.c_str(),
                                  static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

EditorDocumentResult EmptyResult(EditorDocumentCode code) noexcept {
    EditorDocumentResult result{};
    result.code = code;
    return result;
}

} // namespace

struct GameProjectEditorDocumentService::Document {
    explicit Document(EditorDocumentHandle documentHandle, std::filesystem::path resolvedPath,
                      std::string normalizedPath, DocumentText initialText, bool initiallySaved,
                      EditorSourceVersion baseline)
        : handle(documentHandle), path(std::move(resolvedPath)), relativePath(std::move(normalizedPath)),
          current(std::move(initialText)), saved(initiallySaved ? current : DocumentText{}), history(1u),
          sourceVersion(std::move(baseline)), maximumAcceptedBytes(current ? current->size() : 0u) {
        history.Reset(initiallySaved);
    }

    size_t CalculateHistoryCapacity(size_t proposedCurrentBytes, size_t proposedMaximumAcceptedBytes) const noexcept {
        const size_t savedBytes = saved ? saved->size() : 0u;
        const size_t baseBytes = proposedCurrentBytes + savedBytes;
        if (baseBytes > kProjectDocumentHistoryBytes) {
            return 0u;
        }
        const size_t largestSnapshot =
            std::max<size_t>({proposedMaximumAcceptedBytes, proposedCurrentBytes, savedBytes, size_t{1u}});
        const size_t remainingBytes = kProjectDocumentHistoryBytes - baseBytes;
        const size_t maximumSnapshotCount = remainingBytes / largestSnapshot;
        if (maximumSnapshotCount < 2u) {
            return 0u;
        }
        return std::min(kProjectDocumentHistorySteps, maximumSnapshotCount - 1u);
    }

    void UpdateHistoryCapacity() {
        const size_t capacity = CalculateHistoryCapacity(current ? current->size() : 0u, maximumAcceptedBytes);
        history.SetCapacity(capacity);
    }

    EditorDocumentHandle handle = kInvalidEditorDocumentHandle;
    std::filesystem::path path{};
    std::string relativePath{};
    DocumentText current{};
    DocumentText saved{};
    DocumentHistory history{};
    EditorSourceVersion sourceVersion{};
    uint64_t revision = 1u;
    size_t maximumAcceptedBytes = 0u;
    // 資産の文書（Assets の下）。GUID は資産庫が知っている資産だけが持つ（新しく作ったものは最初の保存の後）。
    bool asset = false;
    std::string assetGuid{};
};

GameProjectEditorDocumentService::GameProjectEditorDocumentService(std::filesystem::path dataRoot) {
    std::error_code error{};
    dataRoot_ = std::filesystem::absolute(std::move(dataRoot), error).lexically_normal();
    if (error) {
        dataRoot_.clear();
    }
}

GameProjectEditorDocumentService::~GameProjectEditorDocumentService() = default;

EditorDocumentResult GameProjectEditorDocumentService::Open(std::string_view relativePath) {
    return OpenAt(relativePath, false);
}

EditorDocumentResult GameProjectEditorDocumentService::Create(std::string_view relativePath,
                                                              std::string_view initialUtf8Text) {
    return CreateAt(relativePath, initialUtf8Text, false);
}

void GameProjectEditorDocumentService::SetAssetDatabase(AssetDatabase* database) {
    if (assetDatabase_ == database) {
        return;
    }
    assetDatabase_ = database;
    assetsRoot_.clear();
    if (database != nullptr) {
        std::error_code error{};
        assetsRoot_ = std::filesystem::absolute(database->GetAssetsRoot(), error).lexically_normal();
        if (error) {
            assetsRoot_.clear();
        }
    }
}

EditorDocumentResult GameProjectEditorDocumentService::OpenAsset(std::string_view assetGuid) {
    if (assetDatabase_ == nullptr || assetsRoot_.empty()) {
        return EmptyResult(EditorDocumentCode::IoFailure);
    }
    const AssetRecord* record = assetDatabase_->FindByGuid(AssetGuid{std::string(assetGuid)});
    if (record == nullptr || record->type != AssetType::ModuleAsset) {
        return EmptyResult(EditorDocumentCode::NotFound);
    }
    const std::filesystem::path relative = assetDatabase_->ResolveSourcePath(*record).lexically_relative(assetsRoot_);
    if (relative.empty() || *relative.begin() == "..") {
        return EmptyResult(EditorDocumentCode::InvalidPath);
    }
    const std::string guid = record->guid.value;
    const EditorDocumentResult result = OpenAt(TEXT::PathToUtf8(relative), true);
    if (Document* document = Find(result.document)) {
        document->assetGuid = guid;
    }
    return result;
}

EditorDocumentResult GameProjectEditorDocumentService::CreateAsset(std::string_view assetsRelativePath,
                                                                   std::string_view initialUtf8Text) {
    if (assetDatabase_ == nullptr || assetsRoot_.empty()) {
        return EmptyResult(EditorDocumentCode::IoFailure);
    }
    // モジュールが登記した種類の拡張子だけ。引擎の資産はそれぞれの編集器が書く。
    if (ASSETS::SEMANTICS::FindModuleAssetTypeForPath(TEXT::PathFromUtf8(assetsRelativePath)) == nullptr) {
        return EmptyResult(EditorDocumentCode::InvalidPath);
    }
    return CreateAt(assetsRelativePath, initialUtf8Text, true);
}

size_t GameProjectEditorDocumentService::GetAssetGuid(EditorDocumentHandle handle, char* utf8Buffer,
                                                      size_t bufferCapacity) const {
    const Document* document = Find(handle);
    if (document == nullptr || document->assetGuid.empty() || utf8Buffer == nullptr ||
        document->assetGuid.size() >= bufferCapacity) {
        return 0u;
    }
    std::memcpy(utf8Buffer, document->assetGuid.c_str(), document->assetGuid.size() + 1u);
    return document->assetGuid.size();
}

void GameProjectEditorDocumentService::PublishSavedAsset(Document& document) {
    if (assetDatabase_ == nullptr) {
        return;
    }
    if (document.assetGuid.empty()) {
        if (!assetDatabase_->ScanAssets(true)) {
            return;
        }
        const AssetRecord* record =
            assetDatabase_->FindByPath(document.path.lexically_relative(assetDatabase_->GetProjectRoot()));
        if (record == nullptr) {
            return;
        }
        document.assetGuid = record->guid.value;
    }
    // 導入し直すと資産庫の内容の版が進み、実行中のゲームも新しい中身を読む。
    (void)assetDatabase_->ImportAsset(AssetGuid{document.assetGuid});
}

EditorDocumentResult GameProjectEditorDocumentService::OpenAt(std::string_view relativePath, bool asset) {
    const std::filesystem::path& root = RootOf(asset);
    std::filesystem::path path{};
    std::string pathMessage{};
    const auto pathCode = PROJECT_DATA::ResolveProjectDataFile(root, relativePath, path, pathMessage);
    if (pathCode != PROJECT_DATA::ProjectDataFileCode::Resolved) {
        return EmptyResult(MapDataFileCode(pathCode));
    }
    if (Document* existing = FindPath(path)) {
        return MakeResult(EditorDocumentCode::Ok, existing);
    }
    if (documents_.size() >= kMaximumProjectDocumentCount) {
        return EmptyResult(EditorDocumentCode::TooManyDocuments);
    }

    uint64_t fileBytes = 0u;
    std::string fileMessage{};
    const auto inspectCode = PROJECT_DATA::InspectProjectDataTextFile(path, fileBytes, fileMessage);
    if (inspectCode != PROJECT_DATA::ProjectDataFileCode::Resolved) {
        return EmptyResult(MapDataFileCode(inspectCode));
    }

    EditorSourceVersion sourceVersion{};
    sourceVersion.Accept(path);
    if (!sourceVersion.IsReadable()) {
        return EmptyResult(EditorDocumentCode::IoFailure);
    }

    std::string text{};
    const auto readCode = PROJECT_DATA::ReadProjectDataTextFile(path, fileBytes, text, fileMessage);
    if (readCode != PROJECT_DATA::ProjectDataFileCode::Resolved) {
        return EmptyResult(MapDataFileCode(readCode));
    }
    std::string conflictMessage{};
    if (!sourceVersion.CanSave(conflictMessage)) {
        return EmptyResult(EditorDocumentCode::ExternalConflict);
    }

    const auto textSnapshot = std::make_shared<const std::string>(std::move(text));
    const EditorDocumentHandle handle = AllocateDocumentHandle();
    if (handle == kInvalidEditorDocumentHandle) {
        return EmptyResult(EditorDocumentCode::IoFailure);
    }
    const std::string normalizedPath = TEXT::PathToUtf8(path.lexically_relative(root));
    auto document = std::make_unique<Document>(handle, std::move(path), normalizedPath, textSnapshot, true,
                                               std::move(sourceVersion));
    document->asset = asset;
    document->UpdateHistoryCapacity();
    Document* resultDocument = document.get();
    documents_.push_back(std::move(document));
    return MakeResult(EditorDocumentCode::Ok, resultDocument);
}

EditorDocumentResult GameProjectEditorDocumentService::CreateAt(std::string_view relativePath,
                                                                std::string_view initialUtf8Text, bool asset) {
    const std::filesystem::path& root = RootOf(asset);
    if (!PROJECT_DATA::IsValidProjectDataText(initialUtf8Text)) {
        return EmptyResult(initialUtf8Text.size() > kMaximumProjectDataTextBytes ? EditorDocumentCode::TooLarge
                                                                                 : EditorDocumentCode::InvalidUtf8);
    }

    std::filesystem::path path{};
    std::string pathMessage{};
    const auto pathCode = PROJECT_DATA::ResolveProjectDataFile(root, relativePath, path, pathMessage);
    if (pathCode != PROJECT_DATA::ProjectDataFileCode::Resolved) {
        return EmptyResult(MapDataFileCode(pathCode));
    }
    if (Document* existing = FindPath(path)) {
        return MakeResult(EditorDocumentCode::AlreadyExists, existing);
    }
    if (documents_.size() >= kMaximumProjectDocumentCount) {
        return EmptyResult(EditorDocumentCode::TooManyDocuments);
    }

    uint64_t existingBytes = 0u;
    std::string fileMessage{};
    const auto existingCode = PROJECT_DATA::InspectProjectDataTextFile(path, existingBytes, fileMessage);
    if (existingCode != PROJECT_DATA::ProjectDataFileCode::NotFound) {
        return EmptyResult(existingCode == PROJECT_DATA::ProjectDataFileCode::IoFailure ? EditorDocumentCode::IoFailure
                           : existingCode == PROJECT_DATA::ProjectDataFileCode::ReparsePoint
                               ? EditorDocumentCode::ReparsePoint
                               : EditorDocumentCode::AlreadyExists);
    }

    EditorSourceVersion sourceVersion{};
    sourceVersion.Accept(path);
    if (!sourceVersion.IsReadable()) {
        return EmptyResult(EditorDocumentCode::IoFailure);
    }
    const auto racedCode = PROJECT_DATA::InspectProjectDataTextFile(path, existingBytes, fileMessage);
    if (racedCode != PROJECT_DATA::ProjectDataFileCode::NotFound) {
        return EmptyResult(racedCode == PROJECT_DATA::ProjectDataFileCode::IoFailure ? EditorDocumentCode::IoFailure
                           : racedCode == PROJECT_DATA::ProjectDataFileCode::ReparsePoint
                               ? EditorDocumentCode::ReparsePoint
                               : EditorDocumentCode::AlreadyExists);
    }
    std::string conflictMessage{};
    if (!sourceVersion.CanSave(conflictMessage)) {
        return EmptyResult(EditorDocumentCode::ExternalConflict);
    }

    const EditorDocumentHandle handle = AllocateDocumentHandle();
    if (handle == kInvalidEditorDocumentHandle) {
        return EmptyResult(EditorDocumentCode::IoFailure);
    }
    const std::string normalizedPath = TEXT::PathToUtf8(path.lexically_relative(root));
    const auto textSnapshot = std::make_shared<const std::string>(initialUtf8Text);
    auto document = std::make_unique<Document>(handle, std::move(path), normalizedPath, textSnapshot, false,
                                               std::move(sourceVersion));
    document->asset = asset;
    document->UpdateHistoryCapacity();
    Document* resultDocument = document.get();
    documents_.push_back(std::move(document));
    return MakeResult(EditorDocumentCode::Ok, resultDocument);
}

EditorDocumentResult GameProjectEditorDocumentService::Read(EditorDocumentHandle handle, uint64_t expectedRevision,
                                                            IEditorDocumentReadSink& sink) const {
    const Document* document = Find(handle);
    if (document == nullptr) {
        return EmptyResult(EditorDocumentCode::InvalidHandle);
    }
    const EditorDocumentCode code =
        document->revision == expectedRevision ? EditorDocumentCode::Ok : EditorDocumentCode::RevisionConflict;
    const EditorDocumentResult result = MakeResult(code, document);
    if (code == EditorDocumentCode::Ok && document->current) {
        sink.Receive(result, *document->current);
    }
    return result;
}

EditorDocumentResult GameProjectEditorDocumentService::Apply(EditorDocumentHandle handle, uint64_t expectedRevision,
                                                             std::string_view validatedUtf8Text) {
    Document* document = Find(handle);
    if (document == nullptr) {
        return EmptyResult(EditorDocumentCode::InvalidHandle);
    }
    if (document->revision != expectedRevision) {
        return MakeResult(EditorDocumentCode::RevisionConflict, document);
    }
    if (!PROJECT_DATA::IsValidProjectDataText(validatedUtf8Text)) {
        return MakeResult(validatedUtf8Text.size() > kMaximumProjectDataTextBytes ? EditorDocumentCode::TooLarge
                                                                                  : EditorDocumentCode::InvalidUtf8,
                          document);
    }
    if (document->current && *document->current == validatedUtf8Text) {
        return MakeResult(EditorDocumentCode::NoChange, document);
    }
    if (document->revision == (std::numeric_limits<uint64_t>::max)()) {
        return MakeResult(EditorDocumentCode::HistoryBudgetExceeded, document);
    }

    const size_t proposedMaximum = std::max(document->maximumAcceptedBytes, validatedUtf8Text.size());
    const size_t proposedCapacity = document->CalculateHistoryCapacity(validatedUtf8Text.size(), proposedMaximum);
    if (proposedCapacity == 0u) {
        return MakeResult(EditorDocumentCode::HistoryBudgetExceeded, document);
    }

    const DocumentText next = std::make_shared<const std::string>(validatedUtf8Text);
    document->history.Record(document->current, next);
    document->current = next;
    document->maximumAcceptedBytes = proposedMaximum;
    ++document->revision;
    document->history.SetCapacity(proposedCapacity);
    return MakeResult(EditorDocumentCode::Ok, document);
}

EditorDocumentResult GameProjectEditorDocumentService::Undo(EditorDocumentHandle handle, uint64_t expectedRevision) {
    Document* document = Find(handle);
    if (document == nullptr) {
        return EmptyResult(EditorDocumentCode::InvalidHandle);
    }
    if (document->revision != expectedRevision) {
        return MakeResult(EditorDocumentCode::RevisionConflict, document);
    }
    if (!document->history.CanUndo()) {
        return MakeResult(EditorDocumentCode::NoChange, document);
    }
    if (document->revision == (std::numeric_limits<uint64_t>::max)()) {
        return MakeResult(EditorDocumentCode::HistoryBudgetExceeded, document);
    }
    DocumentText current = document->current;
    if (!document->history.Undo(current)) {
        return MakeResult(EditorDocumentCode::NoChange, document);
    }
    document->current = std::move(current);
    ++document->revision;
    document->UpdateHistoryCapacity();
    return MakeResult(EditorDocumentCode::Ok, document);
}

EditorDocumentResult GameProjectEditorDocumentService::Redo(EditorDocumentHandle handle, uint64_t expectedRevision) {
    Document* document = Find(handle);
    if (document == nullptr) {
        return EmptyResult(EditorDocumentCode::InvalidHandle);
    }
    if (document->revision != expectedRevision) {
        return MakeResult(EditorDocumentCode::RevisionConflict, document);
    }
    if (!document->history.CanRedo()) {
        return MakeResult(EditorDocumentCode::NoChange, document);
    }
    if (document->revision == (std::numeric_limits<uint64_t>::max)()) {
        return MakeResult(EditorDocumentCode::HistoryBudgetExceeded, document);
    }
    DocumentText current = document->current;
    if (!document->history.Redo(current)) {
        return MakeResult(EditorDocumentCode::NoChange, document);
    }
    document->current = std::move(current);
    ++document->revision;
    document->UpdateHistoryCapacity();
    return MakeResult(EditorDocumentCode::Ok, document);
}

EditorDocumentResult GameProjectEditorDocumentService::Save(EditorDocumentHandle handle, uint64_t expectedRevision) {
    Document* document = Find(handle);
    if (document == nullptr) {
        return EmptyResult(EditorDocumentCode::InvalidHandle);
    }
    if (document->revision != expectedRevision) {
        return MakeResult(EditorDocumentCode::RevisionConflict, document);
    }
    if (!document->history.IsDirty()) {
        return MakeResult(EditorDocumentCode::NoChange, document);
    }
    std::string conflictMessage{};
    if (!document->sourceVersion.CanSave(conflictMessage)) {
        return MakeResult(EditorDocumentCode::ExternalConflict, document);
    }

    const std::filesystem::path& root = RootOf(document->asset);
    std::filesystem::path finalPath{};
    std::string pathMessage{};
    const auto pathCode = PROJECT_DATA::ResolveProjectDataFile(root, document->relativePath, finalPath, pathMessage);
    if (pathCode != PROJECT_DATA::ProjectDataFileCode::Resolved) {
        return MakeResult(MapDataFileCode(pathCode), document);
    }
    if (!SamePath(finalPath, document->path)) {
        return MakeResult(EditorDocumentCode::InvalidPath, document);
    }

    std::error_code error{};
    std::filesystem::create_directories(IO::NativeFilePath(finalPath.parent_path()), error);
    if (error) {
        return MakeResult(EditorDocumentCode::IoFailure, document);
    }
    const auto revalidated = PROJECT_DATA::ResolveProjectDataFile(root, document->relativePath, finalPath, pathMessage);
    if (revalidated != PROJECT_DATA::ProjectDataFileCode::Resolved || !SamePath(finalPath, document->path)) {
        return MakeResult(revalidated == PROJECT_DATA::ProjectDataFileCode::Resolved ? EditorDocumentCode::InvalidPath
                                                                                     : MapDataFileCode(revalidated),
                          document);
    }

    const std::filesystem::path stagedPath = MakeStagedPath(finalPath);
    if (!WriteStagedText(stagedPath, *document->current)) {
        RemoveStagedFile(stagedPath);
        return MakeResult(EditorDocumentCode::IoFailure, document);
    }
    if (!document->sourceVersion.CanSave(conflictMessage)) {
        RemoveStagedFile(stagedPath);
        return MakeResult(EditorDocumentCode::ExternalConflict, document);
    }
    const auto finalRevalidation =
        PROJECT_DATA::ResolveProjectDataFile(root, document->relativePath, finalPath, pathMessage);
    if (finalRevalidation != PROJECT_DATA::ProjectDataFileCode::Resolved || !SamePath(finalPath, document->path)) {
        RemoveStagedFile(stagedPath);
        return MakeResult(finalRevalidation == PROJECT_DATA::ProjectDataFileCode::Resolved
                              ? EditorDocumentCode::InvalidPath
                              : MapDataFileCode(finalRevalidation),
                          document);
    }

    std::string commitMessage{};
    if (!IO::CommitStagedFile(stagedPath, finalPath, commitMessage)) {
        RemoveStagedFile(stagedPath);
        return MakeResult(EditorDocumentCode::IoFailure, document);
    }
    document->sourceVersion.Accept(finalPath);
    if (!document->sourceVersion.IsReadable()) {
        return MakeResult(EditorDocumentCode::IoFailure, document);
    }
    document->saved = document->current;
    document->history.MarkSaved();
    document->UpdateHistoryCapacity();
    if (document->asset) {
        PublishSavedAsset(*document);
    }
    return MakeResult(EditorDocumentCode::Ok, document);
}

EditorDocumentResult GameProjectEditorDocumentService::Close(EditorDocumentHandle handle, bool discardChanges) {
    const auto documentIt = std::find_if(documents_.begin(), documents_.end(), [handle](const auto& document) {
        return document && document->handle == handle;
    });
    if (documentIt == documents_.end()) {
        return EmptyResult(EditorDocumentCode::InvalidHandle);
    }
    Document* document = documentIt->get();
    if (document->history.IsDirty() && !discardChanges) {
        return MakeResult(EditorDocumentCode::Dirty, document);
    }
    const EditorDocumentResult result = MakeResult(EditorDocumentCode::Ok, document);
    documents_.erase(documentIt);
    return result;
}

bool GameProjectEditorDocumentService::HasUnsavedDocuments() const noexcept {
    return std::any_of(documents_.begin(), documents_.end(),
                       [](const auto& document) { return document != nullptr && document->history.IsDirty(); });
}

void GameProjectEditorDocumentService::CloseAll() noexcept { documents_.clear(); }

GameProjectEditorDocumentService::Document* GameProjectEditorDocumentService::Find(
    EditorDocumentHandle handle) noexcept {
    const auto found = std::find_if(documents_.begin(), documents_.end(),
                                    [handle](const auto& document) { return document && document->handle == handle; });
    return found == documents_.end() ? nullptr : found->get();
}

const GameProjectEditorDocumentService::Document* GameProjectEditorDocumentService::Find(
    EditorDocumentHandle handle) const noexcept {
    const auto found = std::find_if(documents_.begin(), documents_.end(),
                                    [handle](const auto& document) { return document && document->handle == handle; });
    return found == documents_.end() ? nullptr : found->get();
}

GameProjectEditorDocumentService::Document* GameProjectEditorDocumentService::FindPath(
    const std::filesystem::path& path) noexcept {
    const auto found = std::find_if(documents_.begin(), documents_.end(), [&path](const auto& document) {
        return document && SamePath(document->path, path);
    });
    return found == documents_.end() ? nullptr : found->get();
}

EditorDocumentResult GameProjectEditorDocumentService::MakeResult(EditorDocumentCode code,
                                                                  const Document* document) const noexcept {
    EditorDocumentResult result = EmptyResult(code);
    if (document == nullptr) {
        return result;
    }
    result.document = document->handle;
    result.revision = document->revision;
    result.byteCount = document->current ? document->current->size() : 0u;
    if (document->history.IsDirty()) {
        result.stateFlags |= EditorDocumentStateDirty;
    }
    if (document->history.CanUndo()) {
        result.stateFlags |= EditorDocumentStateCanUndo;
    }
    if (document->history.CanRedo()) {
        result.stateFlags |= EditorDocumentStateCanRedo;
    }
    return result;
}

} // namespace HIKARI::EDITOR
