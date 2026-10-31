#include "Editor/Export/HIKARI_GameExportJob.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Editor/Export/HIKARI_GameShippingArtifactValidator.h"
#include "Editor/Projects/HIKARI_EditorChildProcess.h"
#include "Platform/Paths/HIKARI_ExecutablePath.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/HIKARI_ProjectContext.h"
#include <Windows.h>
#include <objbase.h>
#include <chrono>
#include <fstream>
#include <unordered_set>

namespace HIKARI::EDITOR {
namespace {
nlohmann::json Read(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input)
        return nlohmann::json::object();
    auto value = nlohmann::json::parse(input, nullptr, false);
    return value.is_object() ? value : nlohmann::json::object();
}
void Write(const std::filesystem::path& path, const nlohmann::json& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << value.dump(2);
    if (!output)
        throw std::runtime_error("Could not write export job state.");
}
// 書き出しは保存された取り込み結果を焼く。中身が変わった・取り込みが古い・前回失敗した資産を先に取り込み直し、
// 他の人の提交や取り込み器の版上げの後でも、エディターで開き直さずに書き出せるようにする。取り込み直しても失敗した
// 資産は、焼く時の検査が書き出しに使う物だけを改めて止める（使わない資産の失敗では止めない）。
// 依存する資産（模型が使う画像など）を取り込み直すと、使う側が古くなるので、古い物が無くなるまで数回繰り返す。
// 一度試して失敗した資産は同じ回の中では試し直さない。回の合間で取り消しを見る。
bool ReimportChangedAssets(const std::filesystem::path& project, const std::filesystem::path& jobRoot,
                           int& outReimported, std::string& message) {
    outReimported = 0;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED); // 画像の取り込みが COM を使う
    AssetDatabase database{};
    bool ok = database.Initialize(project) && database.ScanAssets(false);
    if (!ok)
        message = "Could not scan project assets before export.";
    std::unordered_set<std::string> attempted;
    for (int pass = 0; ok && pass < 4; ++pass) {
        if (std::filesystem::exists(jobRoot / "cancel")) {
            message = "Export cancelled while re-importing assets. The previous package was preserved.";
            ok = false;
            break;
        }
        std::vector<AssetGuid> guids;
        for (const AssetRecord* record : database.CollectAll())
            if (record && record->CanImport() &&
                (record->importOutdated || record->artifactMissing || !record->lastImportSucceeded) &&
                attempted.insert(record->guid.value).second)
                guids.push_back(record->guid);
        if (guids.empty())
            break;
        const AssetImportBatchResult imported = database.ImportAssets(guids);
        outReimported += imported.succeeded;
        HIKARI_LOG_INFO("[GameExport] Re-imported changed assets before export (pass " + std::to_string(pass + 1) +
                        "): attempted=" + std::to_string(imported.attempted) + ", succeeded=" +
                        std::to_string(imported.succeeded) + ", failed=" + std::to_string(imported.failed));
        ok = database.ScanAssets(false); // 取り込み直した結果で、使う側の古さを見直す
        if (!ok)
            message = "Could not rescan project assets after re-importing.";
    }
    if (SUCCEEDED(com))
        CoUninitialize();
    return ok;
}
// 源から作った単体版の運行時を、エディターの横の Runtime/Shipping へ写す。エディターの bin をそのまま配れば、
// エンジンの源の無い人も同じ単体版で書き出せる（Build-HikariProject.ps1 -PrepareGameExport がここを使う）。
// 写すのはエンジンの install が置いたファイルだけ（install_manifest.txt。項目のモジュールや項目が足した DLL は入れない）。
// 変わったファイルだけ写し、一覧に無くなったファイルは消す。途中で失敗したら何も消さず、写しかけの印を残す。
constexpr const char* kShippingKitUpdatingMarker = ".updating";
void RefreshEditorShippingKit(const std::filesystem::path& engineSource, const std::filesystem::path& installed) {
    const auto target = PLATFORM::GetExecutableDirectory() / "Shipping";
    const auto fail = [&](const std::string& why) {
        HIKARI_LOG_WARN("[GameExport] Editor Shipping kit was not refreshed (" + TEXT::PathToUtf8(target) + "): " + why);
    };
    const auto key = [](const std::filesystem::path& path) {
        return TEXT::ToLowerAsciiCopy(path.lexically_normal().generic_string());
    };
    // エンジンの install が置いたファイルの一覧（直前の install の接頭辞はこの項目の out/install/game-shipping）。
    std::ifstream list(engineSource / "out/build/game-shipping/install_manifest.txt");
    if (!list)
        return fail("install_manifest.txt is missing");
    const std::string prefix = key(installed) + "/";
    std::vector<std::filesystem::path> files;
    for (std::string line; std::getline(list, line);) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        std::filesystem::path installedFile;
        try {
            installedFile = TEXT::PathFromUtf8(line).lexically_normal();
        } catch (const std::exception&) {
            return fail("unreadable install_manifest.txt entry");
        }
        const std::string generic = key(installedFile);
        if (!generic.starts_with(prefix))
            return fail("install_manifest.txt belongs to another install prefix");
        files.push_back(TEXT::PathFromUtf8(installedFile.generic_string().substr(prefix.size())));
    }
    // 運行時として揃っているか（-PrepareGameExport と同じ要件）を確かめてから写す。
    std::error_code ec;
    for (const char* required : {"HIKARI_Game.exe", "EngineContent/Shaders/Cooked", "Sdk/lib/HIKARI_Engine.lib"})
        if (!std::filesystem::exists(installed / required, ec))
            return fail(std::string("installed Shipping runtime is incomplete: ") + required);

    std::filesystem::create_directories(target, ec);
    const auto marker = target / kShippingKitUpdatingMarker;
    if (ec || !std::ofstream(marker))
        return fail("target is not writable");
    std::unordered_set<std::string> kept{key(kShippingKitUpdatingMarker)};
    int copied = 0;
    for (const auto& relative : files) {
        const auto from = installed / relative, to = target / relative;
        kept.insert(key(relative));
        std::error_code sizeError, timeError;
        const auto size = std::filesystem::file_size(from, sizeError);
        const auto time = std::filesystem::last_write_time(from, timeError);
        if (sizeError || timeError)
            return fail("cannot read " + TEXT::PathToUtf8(from));
        std::error_code targetSizeError, targetTimeError;
        const bool unchanged = std::filesystem::file_size(to, targetSizeError) == size &&
                               std::filesystem::last_write_time(to, targetTimeError) == time;
        if (unchanged && !targetSizeError && !targetTimeError)
            continue;
        std::filesystem::create_directories(to.parent_path(), ec);
        if (!ec)
            std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec)
            std::filesystem::last_write_time(to, time, ec);
        if (ec)
            return fail("cannot copy " + TEXT::PathToUtf8(relative) + ": " + ec.message());
        ++copied;
    }
    std::vector<std::filesystem::path> stale;
    for (std::filesystem::recursive_directory_iterator it(target, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entry;
        const bool file = it->is_regular_file(entry);
        const auto relative = it->path().lexically_relative(target);
        if (entry || relative.empty())
            return fail("cannot inspect " + TEXT::PathToUtf8(it->path()));
        if (file && !kept.contains(key(relative)))
            stale.push_back(it->path());
    }
    if (ec)
        return fail("cannot list the existing kit: " + ec.message());
    for (const auto& file : stale)
        std::filesystem::remove(file, ec);
    std::filesystem::remove(marker, ec);
    HIKARI_LOG_INFO("[GameExport] Editor Shipping kit refreshed: " + std::to_string(copied) + " file(s) updated, " +
                    std::to_string(stale.size()) + " removed.");
}
bool PrepareShipping(const std::filesystem::path& project, const std::filesystem::path& jobRoot, std::string& message) {
    const auto sourceRoot = ResolveExportEngineSourceRoot();
    std::filesystem::path script;
    std::vector<std::wstring> arguments{L"-NoProfile", L"-ExecutionPolicy", L"Bypass", L"-File"};
    if (!sourceRoot.empty()) {
        script = sourceRoot / "Tools/Build/Build-Hikari.ps1";
        arguments.insert(arguments.end(),
                         {script.wstring(), L"-Preset", L"game-shipping", L"-Project", project.wstring(), L"-Install"});
    } else {
        const auto runtime = PLATFORM::GetExecutableDirectory() / "Shipping";
        const auto sdk = runtime / "Sdk";
        script = sdk / "Tools/Build-HikariProject.ps1";
        arguments.insert(arguments.end(), {script.wstring(), L"-Project", project.wstring(), L"-Preset",
                                           L"game-shipping", L"-SdkRoot", sdk.wstring(), L"-PrepareGameExport"});
    }
    if (!std::filesystem::is_regular_file(script)) {
        message =
            "Paired Shipping runtime kit is missing. Reinstall the complete Editor distribution (Runtime/Shipping).";
        return false;
    }
    if (sourceRoot.empty() && std::filesystem::exists(PLATFORM::GetExecutableDirectory() / "Shipping" /
                                                      kShippingKitUpdatingMarker)) {
        message = "The Editor's Shipping kit (Runtime/Shipping) was left half-updated. Export once on the machine "
                  "with the engine source, then redistribute the Editor.";
        return false;
    }
    EditorChildProcess build;
    if (!build.Start(EditorChildProcess::PowerShell(), arguments, project, jobRoot / "shipping-build.log", message))
        return false;
    while (build.Running()) {
        if (std::filesystem::exists(jobRoot / "cancel")) {
            message = "Export cancelled during Shipping build. The previous package was preserved.";
            return false;
        }
        build.Wait(100);
    }
    if (build.ExitCode() != 0) {
        message = "Shipping build failed. See " + TEXT::PathToUtf8(jobRoot / "shipping-build.log");
        return false;
    }
    if (!sourceRoot.empty())
        RefreshEditorShippingKit(sourceRoot, project / "out/install/game-shipping");
    return true;
}
} // namespace
struct GameExportJob::State {
    std::unique_ptr<EditorChildProcess> process;
    std::filesystem::path root;
    nlohmann::json status = {{"running", false}, {"message", ""}, {"progress", 0.0f}};
};
GameExportJob::GameExportJob() : state_(std::make_unique<State>()) {}
GameExportJob::~GameExportJob() { Cancel(); }
bool GameExportJob::IsRunning() const { return state_->process && state_->process->Running(); }
const nlohmann::json& GameExportJob::Status() const { return state_->status; }
std::filesystem::path GameExportJob::WriteRequest(GameExportOptions& options, std::string& message) {
    if (!GetProjectContext().IsOpen()) {
        message = "Open a project before exporting.";
        return {};
    }
    try {
        const auto project = GetProjectContext().GetRoot();
        const auto root = project / "Library/Build/Exports" /
                          std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root);
        if (options.outputDirectory.empty())
            options.outputDirectory = GameExporter::GetDefaultOutputDirectory();
        GameExportIdentity identity;
        if (options.identity)
            identity = *options.identity;
        else if (!LoadGameExportIdentity(project, identity, message))
            return {};
        if (!ValidateGameExportIdentity(project, identity, message))
            return {};
        Write(root / "request.json", {{"outputDirectory", TEXT::PathToUtf8(options.outputDirectory)},
                                      {"sceneGuids", options.sceneGuids},
                                      {"startupSceneGuid", options.startupSceneGuid},
                                      {"windowMode", static_cast<int>(options.windowMode)},
                                      {"prepareShipping", true},
                                      {"identity", GameExportIdentityJson(identity)}});
        return root / "request.json";
    } catch (const std::exception& e) {
        message = e.what();
        return {};
    }
}
bool GameExportJob::Start(const GameExportOptions& options, std::string& message) {
    Poll();
    if (IsRunning()) {
        message = "An export is already running.";
        return false;
    }
    GameExportOptions requested = options;
    const auto request = WriteRequest(requested, message);
    if (request.empty())
        return false;
    try {
        const auto project = GetProjectContext().GetRoot();
        state_->root = request.parent_path();
        auto child = std::make_unique<EditorChildProcess>();
        if (!child->Start(PLATFORM::GetExecutablePath(),
                          {L"--project", project.wstring(), L"--export-request", request.wstring()}, project,
                          state_->root / "export.log", message, {.killOnClose = false}))
            return false;
        state_->process = std::move(child);
        state_->status = {{"running", true},
                          {"message", "Preparing Shipping export"},
                          {"progress", 0.0f},
                          {"outputDirectory", TEXT::PathToUtf8(requested.outputDirectory)},
                          {"logDirectory", TEXT::PathToUtf8(state_->root)}};
        return true;
    } catch (const std::exception& e) {
        message = e.what();
        return false;
    }
}
void GameExportJob::Poll() {
    if (!state_->process)
        return;
    const auto progress = Read(state_->root / "status.json");
    if (!progress.empty())
        state_->status.update(progress);
    state_->status["running"] = IsRunning();
    if (!IsRunning()) {
        const auto result = Read(state_->root / "result.json");
        if (result.empty())
            state_->status.update(
                {{"success", false}, {"message", "Export process stopped. Inspect export.log in the log directory."}});
        else
            state_->status.update(result);
        state_->process.reset();
    }
}
void GameExportJob::Cancel() {
    if (!IsRunning())
        return;
    std::ofstream marker(state_->root / "cancel");
    marker << "cancel";
    state_->status["message"] = "Cancelling safely; the previous package is preserved.";
}
int GameExportJob::RunRequest(const std::filesystem::path& requestPath) {
    const auto root = requestPath.parent_path();
    GameExportResult result;
    try {
        const auto request = Read(requestPath);
        if (!request.contains("outputDirectory"))
            throw std::runtime_error("Invalid export request.");
        GameExportOptions options;
        if (request.contains("identity")) {
            GameExportIdentity identity;
            if (!ParseGameExportIdentity(request["identity"], identity, result.message) ||
                !ValidateGameExportIdentity(GetProjectContext().GetRoot(), identity, result.message))
                throw std::runtime_error(result.message);
            options.identity = std::move(identity);
        }
        options.outputDirectory = TEXT::PathFromUtf8(request.at("outputDirectory").get<std::string>());
        options.sceneGuids = request.value("sceneGuids", std::vector<std::string>{});
        options.startupSceneGuid = request.value("startupSceneGuid", std::string{});
        const int mode = request.value("windowMode", 1);
        if (mode < 0 || mode > 2)
            throw std::runtime_error("Invalid export window mode.");
        options.windowMode = static_cast<GameExportWindowMode>(mode);
        auto lastReport = std::chrono::steady_clock::time_point{};
        options.progress = [&](const std::string& stage, float progress) {
            if (std::filesystem::exists(root / "cancel"))
                return false;
            const auto now = std::chrono::steady_clock::now();
            if (now - lastReport > std::chrono::milliseconds(100) || progress >= 0.98f) {
                Write(root / "status.json", {{"message", stage}, {"progress", progress}});
                lastReport = now;
            }
            return true;
        };
        options.progress("Re-importing changed assets", 0.0f);
        int reimported = 0;
        if (!ReimportChangedAssets(GetProjectContext().GetRoot(), root, reimported, result.message))
            throw std::runtime_error(result.message);
        options.progress("Building paired Shipping runtime and game module", 0.0f);
        if (!request.value("prepareShipping", true) ||
            PrepareShipping(GetProjectContext().GetRoot(), root, result.message))
            result = GameExporter::Export(options);
        if (result.success && reimported > 0)
            result.message += " Re-imported " + std::to_string(reimported) + " changed asset(s) first.";
    } catch (const std::exception& e) {
        result.message = e.what();
    }
    Write(root / "result.json", {{"success", result.success},
                                 {"message", result.message},
                                 {"outputDirectory", TEXT::PathToUtf8(result.outputDirectory)}});
    return result.success ? 0 : 4;
}
} // namespace HIKARI::EDITOR
