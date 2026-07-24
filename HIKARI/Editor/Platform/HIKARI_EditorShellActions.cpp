#include "Editor/Platform/HIKARI_EditorShellActions.h"

#include <cstdint>

#if defined(_WIN32)
#include <Windows.h>
#include <shellapi.h>
#endif

namespace HIKARI::EDITOR::SHELL {

namespace {

#if defined(_WIN32)
bool DidShellActionSucceed(HINSTANCE result) {
  return reinterpret_cast<intptr_t>(result) > 32;
}
#endif

} // namespace

bool ShowFileInExplorer(const std::filesystem::path &path) {
  if (path.empty()) {
    return false;
  }
#if defined(_WIN32)
  const std::wstring parameter = L"/select,\"" + path.wstring() + L"\"";
  return DidShellActionSucceed(ShellExecuteW(nullptr, L"open", L"explorer.exe",
                                             parameter.c_str(), nullptr,
                                             SW_SHOWNORMAL));
#else
  return false;
#endif
}

bool OpenFolderInExplorer(const std::filesystem::path &path) {
  if (path.empty()) {
    return false;
  }
#if defined(_WIN32)
  return DidShellActionSucceed(ShellExecuteW(nullptr, L"open",
                                             path.wstring().c_str(), nullptr,
                                             nullptr, SW_SHOWNORMAL));
#else
  return false;
#endif
}

} // namespace HIKARI::EDITOR::SHELL
