#include <Windows.h>
#include <filesystem>
#include <string>
#include <vector>

// 配布ルート唯一の入口。描画・SDK・外部 DLL は Runtime 内の Hub が所有する。
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int show) {
    std::wstring self(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
    if (!length || length >= self.size())
        return 1;
    self.resize(length);
    const auto runtime = std::filesystem::path(self).parent_path() / L"Runtime";
    const auto hub = runtime / L"HIKARI_ProjectHub.exe";
    std::wstring command = L"\"" + hub.wstring() + L"\"";
    if (arguments && *arguments) {
        command += L" ";
        command += arguments;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = static_cast<WORD>(show);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(hub.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr,
                        runtime.c_str(), &startup, &process)) {
        const auto code = GetLastError();
        const auto language = PRIMARYLANGID(GetUserDefaultUILanguage());
        std::wstring message =
            language == LANG_CHINESE ? L"无法启动 HIKARI。请保留 Launcher 旁完整的 Runtime 文件夹。\n"
            : language == LANG_JAPANESE
                ? L"HIKARI を起動できません。Launcher と同じ場所に Runtime フォルダー一式が必要です。\n"
                : L"HIKARI could not start. Keep the complete Runtime folder beside the Launcher.\n";
        message += hub.wstring() + L"\n(" + std::to_wstring(code) + L")";
        MessageBoxW(nullptr, message.c_str(), L"HIKARI", MB_OK | MB_ICONERROR);
        return static_cast<int>(code);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
