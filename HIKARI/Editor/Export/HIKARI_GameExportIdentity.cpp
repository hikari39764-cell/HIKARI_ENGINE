#include "Editor/Export/HIKARI_GameExportIdentity.h"
#include "Editor/Export/HIKARI_GameExecutableIcon.h"
#include "Editor/Export/HIKARI_GamePackageHash.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include <algorithm>

namespace HIKARI::EDITOR {
namespace {
bool LoadDocument(const std::filesystem::path& root, nlohmann::json& document, std::string& message) {
    const auto path = root / "ProjectSettings/game_export.json";
    document = {{"version", 1}};
    if (std::filesystem::exists(path) && !SERIALIZATION::JSON::ReadJsonFile(path, document, &message))
        return false;
    if (!document.is_object() || document.value("version", 1) != 1) {
        message = "game_export.json の形式またはバージョンが無効です。";
        return false;
    }
    return true;
}
} // namespace
bool ResolveExportExecutableFileName(std::string_view requested, std::filesystem::path& output, std::string& message) {
    const std::string name(requested.empty() ? "HIKARI_Game.exe" : requested);
    const auto path = TEXT::PathFromUtf8(name);
    auto deviceName = TEXT::ToLowerAsciiCopy(name.substr(0, name.find('.')));
    while (!deviceName.empty() && deviceName.back() == ' ')
        deviceName.pop_back();
    const bool reserved = deviceName == "con" || deviceName == "prn" || deviceName == "aux" || deviceName == "nul" ||
                          (deviceName.size() == 4 && (deviceName.starts_with("com") || deviceName.starts_with("lpt")) &&
                           deviceName.back() >= '1' && deviceName.back() <= '9');
    if (name.find_first_of("<>:\"/\\|?*") != std::string::npos || name.back() == '.' || name.back() == ' ' ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; }) || path.is_absolute() ||
        path.has_parent_path() || path.stem().empty() || path.stem() == ".exe" || path.native().size() > 240 ||
        TEXT::ToLowerAsciiCopy(TEXT::PathToUtf8(path.extension())) != ".exe" || reserved) {
        message = "EXE 名は予約名や禁止文字を使わない、単一の .exe ファイル名にしてください。";
        return false;
    }
    output = path;
    return true;
}
bool ParseGameExportIdentity(const nlohmann::json& document, GameExportIdentity& identity, std::string& message) {
    if (!document.is_object()) {
        message = "ゲーム情報がオブジェクトではありません。";
        return false;
    }
    for (const auto* key : {"executableName", "windowTitle", "iconPath"}) {
        if (document.contains(key) && !document[key].is_string()) {
            message = std::string(key) + " は文字列で指定してください。";
            return false;
        }
    }
    identity = {document.value("executableName", std::string("HIKARI_Game.exe")),
                document.value("windowTitle", std::string{}), document.value("iconPath", std::string{})};
    if (identity.executableName.empty())
        identity.executableName = "HIKARI_Game.exe";
    return true;
}
nlohmann::json GameExportIdentityJson(const GameExportIdentity& identity) {
    return {{"executableName", identity.executableName},
            {"windowTitle", identity.windowTitle},
            {"iconPath", identity.iconPath}};
}
bool ValidateGameExportIdentity(const std::filesystem::path& projectRoot, const GameExportIdentity& identity,
                                std::string& message) {
    try {
        std::filesystem::path executable;
        if (!ResolveExportExecutableFileName(identity.executableName, executable, message))
            return false;
        if (identity.windowTitle.size() > 1023 || std::any_of(identity.windowTitle.begin(), identity.windowTitle.end(),
                                                              [](unsigned char c) { return c < 32; })) {
            message = "ウィンドウタイトルは改行なし、1023 バイト以内で指定してください。";
            return false;
        }
        if (identity.iconPath.empty())
            return true;
        const auto relative = TEXT::PathFromUtf8(identity.iconPath);
        if (relative.is_absolute() || relative.has_root_name() ||
            TEXT::ToLowerAsciiCopy(TEXT::PathToUtf8(relative.extension())) != ".ico") {
            message = "アイコンはプロジェクト内の .ico ファイルを指定してください。";
            return false;
        }
        const auto root = std::filesystem::weakly_canonical(projectRoot);
        const auto file = std::filesystem::weakly_canonical(root / relative);
        auto r = root.begin(), f = file.begin();
        for (; r != root.end(); ++r, ++f) {
            if (f == file.end() || _wcsicmp(r->c_str(), f->c_str()) != 0) {
                message = "プロジェクト外のアイコンは使用できません。先にプロジェクト内へコピーしてください。";
                return false;
            }
        }
        return ValidateGameExecutableIcon(file, message);
    } catch (const std::exception& error) {
        message = error.what();
        return false;
    }
}
bool LoadGameExportIdentity(const std::filesystem::path& projectRoot, GameExportIdentity& identity,
                            std::string& message) {
    try {
        nlohmann::json document;
        return LoadDocument(projectRoot, document, message) && ParseGameExportIdentity(document, identity, message);
    } catch (const std::exception& error) {
        message = error.what();
        return false;
    }
}
bool SaveGameExportIdentity(const std::filesystem::path& projectRoot, const GameExportIdentity& identity,
                            std::string& message) {
    auto saved = identity;
    std::filesystem::path imported;
    try {
        const auto source = TEXT::PathFromUtf8(saved.iconPath);
        if (source.is_absolute()) {
            auto withoutIcon = saved;
            withoutIcon.iconPath.clear();
            if (!ValidateGameExportIdentity(projectRoot, withoutIcon, message))
                return false;
            if (TEXT::ToLowerAsciiCopy(TEXT::PathToUtf8(source.extension())) != ".ico") {
                message = "Windows ICO ファイルを選んでください。";
                return false;
            }
            if (!ValidateGameExecutableIcon(source, message))
                return false;
            std::string hash;
            if (!ComputeGamePackageFileSha256(source, hash, &message))
                return false;
            const auto relative = std::filesystem::path("ProjectSettings/Branding") / ("icon-" + hash + ".ico");
            const auto destination = projectRoot / relative;
            const auto canonicalRoot = std::filesystem::weakly_canonical(projectRoot);
            const auto canonicalDestination = std::filesystem::weakly_canonical(destination);
            auto r = canonicalRoot.begin(), d = canonicalDestination.begin();
            for (; r != canonicalRoot.end(); ++r, ++d) {
                if (d == canonicalDestination.end() || _wcsicmp(r->c_str(), d->c_str()) != 0)
                    throw std::runtime_error("アイコンの保存先がプロジェクト外を参照しています。");
            }
            std::filesystem::create_directories(destination.parent_path());
            if (!std::filesystem::exists(destination)) {
                std::filesystem::copy_file(source, destination);
                imported = destination;
            }
            std::string copiedHash;
            if (!ComputeGamePackageFileSha256(destination, copiedHash, &message) || copiedHash != hash)
                throw std::runtime_error("コピーしたアイコンの検証に失敗しました。");
            saved.iconPath = TEXT::PathToUtf8(relative);
        }
        if (!ValidateGameExportIdentity(projectRoot, saved, message))
            throw std::runtime_error(message);
        nlohmann::json document;
        if (!LoadDocument(projectRoot, document, message))
            throw std::runtime_error(message);
        auto updated = document;
        updated.update(GameExportIdentityJson(saved));
        if (updated == document)
            return true;
        if (!SERIALIZATION::JSON::WriteJsonFile(projectRoot / "ProjectSettings/game_export.json", updated, &message))
            throw std::runtime_error(message);
        return true;
    } catch (const std::exception& error) {
        if (!imported.empty()) {
            std::error_code ignored;
            std::filesystem::remove(imported, ignored);
        }
        message = error.what();
        return false;
    }
}
} // namespace HIKARI::EDITOR
