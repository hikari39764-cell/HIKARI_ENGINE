#include "Editor/Platform/HIKARI_EditorFileDialog.h"

#include <thread>

#include <Windows.h>
#include <shobjidl.h>

namespace HIKARI::EDITOR::FILE_DIALOG {
namespace {

std::optional<std::filesystem::path> PickWithInitializedCom(const std::filesystem::path& initialPath, bool folder,
                                                            const std::wstring& filterLabel = {},
                                                            const std::wstring& filterPattern = {}) {
    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || dialog == nullptr) {
        return std::nullopt;
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST) | FOS_FORCEFILESYSTEM);
    }
    if (!folder) {
        const COMDLG_FILTERSPEC filter{filterLabel.c_str(), filterPattern.c_str()};
        dialog->SetFileTypes(1, &filter);
    }

    std::filesystem::path initialFolder = initialPath;
    std::error_code ec{};
    if (!initialFolder.empty() && !std::filesystem::is_directory(initialFolder, ec)) {
        initialFolder = initialFolder.parent_path();
    }
    if (!initialFolder.empty()) {
        IShellItem* folderItem = nullptr;
        hr = SHCreateItemFromParsingName(initialFolder.wstring().c_str(), nullptr, IID_PPV_ARGS(&folderItem));
        if (SUCCEEDED(hr) && folderItem != nullptr) {
            dialog->SetFolder(folderItem);
            folderItem->Release();
        }
    }

    std::optional<std::filesystem::path> pickedPath{};
    hr = dialog->Show(nullptr);
    if (SUCCEEDED(hr)) {
        IShellItem* resultItem = nullptr;
        hr = dialog->GetResult(&resultItem);
        if (SUCCEEDED(hr) && resultItem != nullptr) {
            PWSTR rawPath = nullptr;
            hr = resultItem->GetDisplayName(SIGDN_FILESYSPATH, &rawPath);
            if (SUCCEEDED(hr) && rawPath != nullptr) {
                pickedPath = std::filesystem::path(rawPath);
                CoTaskMemFree(rawPath);
            }
            resultItem->Release();
        }
    }

    dialog->Release();
    return pickedPath;
}

} // namespace

std::optional<std::filesystem::path> PickFolder(const std::filesystem::path& initialPath) {
    std::optional<std::filesystem::path> pickedPath{};
    std::thread pickerThread([initialPath, &pickedPath]() {
        const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(initResult)) {
            return;
        }

        pickedPath = PickWithInitializedCom(initialPath, true);
        CoUninitialize();
    });
    pickerThread.join();
    return pickedPath;
}

std::optional<std::filesystem::path> PickFile(const std::filesystem::path& initialPath, const std::wstring& filterLabel,
                                              const std::wstring& filterPattern) {
    std::optional<std::filesystem::path> pickedPath;
    std::thread picker([&]() {
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
            return;
        pickedPath = PickWithInitializedCom(initialPath, false, filterLabel, filterPattern);
        CoUninitialize();
    });
    picker.join();
    return pickedPath;
}

} // namespace HIKARI::EDITOR::FILE_DIALOG
