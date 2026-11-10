#include "Platform/HIKARI_Win32Window.h"
#include "Platform/HIKARI_ApplicationIcon.h"

#include <algorithm>
#include <commctrl.h>
#include <filesystem>
#include <imm.h>
#include <limits>
#include <shellapi.h>
#include <string>
#include <string_view>
#include <vector>

#include "Core/HIKARI_Logger.h"

namespace HIKARI::PLATFORM {

namespace {
struct ExternalFileDrop {
    HWND window = nullptr;
    POINT screenPosition{};
    ULONGLONG queuedAtMilliseconds = 0u;
    std::vector<std::filesystem::path> files{};
};

std::vector<ExternalFileDrop> gExternalFileDrops{};
constexpr size_t kMaximumTextInputEventsPerFrame = 1024u;
constexpr size_t kMaximumImeCompositionLength = 256u * 1024u;

bool TryEncodeUtf16(std::wstring_view text, std::string& outUtf8) {
    outUtf8.clear();
    if (text.empty()) {
        return true;
    }
    if (text.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    const int sourceLength = static_cast<int>(text.size());
    const int byteCount =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), sourceLength, nullptr, 0, nullptr, nullptr);
    if (byteCount <= 0) {
        return false;
    }
    outUtf8.resize(static_cast<size_t>(byteCount));
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), sourceLength, outUtf8.data(), byteCount,
                               nullptr, nullptr) == byteCount;
}

void QueueExternalFileDrop(HWND hwnd, HDROP drop) {
    if (drop == nullptr) {
        return;
    }

    ExternalFileDrop queued{};
    queued.window = hwnd;
    POINT clientPosition{};
    if (DragQueryPoint(drop, &clientPosition) != FALSE) {
        queued.screenPosition = clientPosition;
        ClientToScreen(hwnd, &queued.screenPosition);
    } else {
        GetCursorPos(&queued.screenPosition);
    }
    queued.queuedAtMilliseconds = GetTickCount64();

    const UINT fileCount = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    queued.files.reserve(fileCount);
    for (UINT i = 0; i < fileCount; ++i) {
        const UINT length = DragQueryFileW(drop, i, nullptr, 0);
        if (length == 0) {
            continue;
        }
        std::wstring path(static_cast<size_t>(length) + 1u, L'\0');
        DragQueryFileW(drop, i, path.data(), length + 1u);
        path.resize(static_cast<size_t>(length));
        queued.files.emplace_back(std::move(path));
    }
    DragFinish(drop);

    if (!queued.files.empty()) {
        gExternalFileDrops.push_back(std::move(queued));
    }
}

#if defined(HIKARI_WITH_EDITOR)
constexpr UINT_PTR kExternalFileDropSubclassId = 0x48494B41u;

LRESULT CALLBACK ExternalFileDropSubclassProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                              UINT_PTR subclassId, DWORD_PTR) {
    if (message == WM_DROPFILES) {
        QueueExternalFileDrop(hwnd, reinterpret_cast<HDROP>(wparam));
        return 0;
    }
    if (message == WM_NCDESTROY) {
        DragAcceptFiles(hwnd, FALSE);
        RemoveWindowSubclass(hwnd, ExternalFileDropSubclassProc, subclassId);
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}
#endif
} // namespace

void RegisterExternalFileDropWindow(HWND hwnd) {
#if defined(HIKARI_WITH_EDITOR)
    if (hwnd == nullptr) {
        return;
    }
    if (SetWindowSubclass(hwnd, ExternalFileDropSubclassProc, kExternalFileDropSubclassId, 0u) != FALSE) {
        DragAcceptFiles(hwnd, TRUE);
    }
#else
    (void)hwnd;
#endif
}

void UnregisterExternalFileDropWindow(HWND hwnd) {
#if defined(HIKARI_WITH_EDITOR)
    if (hwnd == nullptr) {
        return;
    }
    DragAcceptFiles(hwnd, FALSE);
    RemoveWindowSubclass(hwnd, ExternalFileDropSubclassProc, kExternalFileDropSubclassId);
#else
    (void)hwnd;
#endif
}

std::vector<std::filesystem::path> ConsumeDroppedFilesInScreenRect(const RECT& screenRect) {
    std::vector<std::filesystem::path> files{};
    const ULONGLONG now = GetTickCount64();
    for (auto it = gExternalFileDrops.begin(); it != gExternalFileDrops.end();) {
        const bool expired = now - it->queuedAtMilliseconds > 10000u;
        const bool inside = it->screenPosition.x >= screenRect.left && it->screenPosition.x < screenRect.right &&
                            it->screenPosition.y >= screenRect.top && it->screenPosition.y < screenRect.bottom;
        if (inside) {
            files.insert(files.end(), std::make_move_iterator(it->files.begin()),
                         std::make_move_iterator(it->files.end()));
        }
        if (inside || expired) {
            it = gExternalFileDrops.erase(it);
        } else {
            ++it;
        }
    }
    return files;
}

bool Win32Window::Initialize(const wchar_t* title, int width, int height, bool resizable,
                             WindowCloseBehavior closeBehavior, bool dispatchHostMessages, bool startHidden) {
    HIKARI_LOG_INFO("Win32Window initialization started.");

    hInstance_ = GetModuleHandleW(nullptr);
    width_ = width;
    height_ = height;
    resizable_ = resizable;
    closeBehavior_ = closeBehavior;
    dispatchHostMessages_ = dispatchHostMessages;
    startupHidden_ = startHidden;
    closeRequested_ = false;

    WNDCLASSW wc{};
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = hInstance_;
    wc.lpszClassName = L"HIKARI_WindowClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance_, MAKEINTRESOURCEW(PLATFORM::kApplicationIconResourceId));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    const ATOM cls = RegisterClassW(&wc);
    if (cls == 0) {
        const DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            HIKARI_LOG_ERROR("[Win32Window] RegisterClassW failed. err=" + std::to_string(err));
            return false;
        }
    }
    HIKARI_LOG_INFO("Window class registered.");

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!resizable) {
        style &= ~WS_THICKFRAME;
        style &= ~WS_MAXIMIZEBOX;
    }
    windowedStyle_ = style;

    // クライアントサイズは DIP 指定。Per-Monitor DPI 環境では実ピクセルへ
    // 換算してからウィンドウ矩形を求める（150% スケーリング時の縮小表示対策）。
    const UINT windowDpi = GetDpiForSystem();
    const float dpiScale = windowDpi > 0u ? static_cast<float>(windowDpi) / 96.0f : 1.0f;
    width_ = static_cast<int>(static_cast<float>(width) * dpiScale + 0.5f);
    height_ = static_cast<int>(static_cast<float>(height) * dpiScale + 0.5f);
    RECT rect{0, 0, width_, height_};
    if (AdjustWindowRectExForDpi(&rect, style, FALSE, 0, windowDpi) == FALSE) {
        AdjustWindowRect(&rect, style, FALSE);
    }

    hwnd_ = CreateWindowExW(0, wc.lpszClassName, title, style, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                            rect.bottom - rect.top, nullptr, nullptr, hInstance_, this);

    if (!hwnd_) {
        const DWORD err = GetLastError();
        HIKARI_LOG_ERROR("[Win32Window] CreateWindowExW failed. err=" + std::to_string(err));
        return false;
    }
    HIKARI_LOG_INFO("Window created.");

    for (const bool useSmallIcon : {false, true}) {
        const HICON icon =
            static_cast<HICON>(LoadImageW(hInstance_, MAKEINTRESOURCEW(PLATFORM::kApplicationIconResourceId),
                                          IMAGE_ICON, GetSystemMetrics(useSmallIcon ? SM_CXSMICON : SM_CXICON),
                                          GetSystemMetrics(useSmallIcon ? SM_CYSMICON : SM_CYICON), LR_SHARED));
        if (icon)
            SendMessageW(hwnd_, WM_SETICON, useSmallIcon ? ICON_SMALL : ICON_BIG, reinterpret_cast<LPARAM>(icon));
    }

    if (!startupHidden_)
        Show();
    DragAcceptFiles(hwnd_, TRUE);
    // 创建后以实际客户端区为准，消除 AdjustWindowRect 边框估算与真实值的偏差。
    RefreshClientSize();
    running_ = true;
    HIKARI_LOG_INFO("Win32Window initialization completed.");
    return true;
}

bool Win32Window::SetTitle(const wchar_t* title) {
    return hwnd_ != nullptr && title != nullptr && SetWindowTextW(hwnd_, title) != FALSE;
}

bool Win32Window::RefreshClientSize() {
    if (hwnd_ == nullptr) {
        return false;
    }
    RECT clientRect{};
    if (GetClientRect(hwnd_, &clientRect) == FALSE) {
        return false;
    }
    const int clientWidth = clientRect.right - clientRect.left;
    const int clientHeight = clientRect.bottom - clientRect.top;
    if (clientWidth <= 0 || clientHeight <= 0) {
        return false;
    }
    width_ = clientWidth;
    height_ = clientHeight;
    isMinimized_ = IsIconic(hwnd_) != FALSE;
    return !isMinimized_;
}

bool Win32Window::ApplyWindowMode(WindowMode mode, int clientWidth, int clientHeight) {
    if (!hwnd_) {
        return false;
    }

    clientWidth = (std::max)(clientWidth, 16);
    clientHeight = (std::max)(clientHeight, 16);

    auto getMonitorInfo = [&]() -> MONITORINFO {
        HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(MONITORINFO);
        if (!GetMonitorInfoW(monitor, &monitorInfo)) {
            monitorInfo.rcMonitor = {0, 0, clientWidth, clientHeight};
            monitorInfo.rcWork = monitorInfo.rcMonitor;
        }
        return monitorInfo;
    };

    auto clampWindowRectToWorkArea = [](const RECT& work, int windowWidth, int windowHeight, POINT desiredCenter) {
        const int workWidth = work.right - work.left;
        const int workHeight = work.bottom - work.top;
        int x = desiredCenter.x - windowWidth / 2;
        int y = desiredCenter.y - windowHeight / 2;

        if (windowWidth <= workWidth) {
            const int minX = static_cast<int>(work.left);
            const int maxX = static_cast<int>(work.right) - windowWidth;
            x = (std::max)(minX, (std::min)(x, maxX));
        } else {
            x = static_cast<int>(work.left);
        }

        if (windowHeight <= workHeight) {
            const int minY = static_cast<int>(work.top);
            const int maxY = static_cast<int>(work.bottom) - windowHeight;
            y = (std::max)(minY, (std::min)(y, maxY));
        } else {
            y = static_cast<int>(work.top);
        }

        return POINT{x, y};
    };

    if (mode == WindowMode::Windowed) {
        DWORD style = WS_OVERLAPPEDWINDOW;
        if (!resizable_) {
            style &= ~WS_THICKFRAME;
            style &= ~WS_MAXIMIZEBOX;
        }
        windowedStyle_ = style;

        RECT rect{0, 0, clientWidth, clientHeight};
        // 切回窗口模式同样按当前显示器 DPI 换算边框，避免 150% 缩放下窗口偏小。
        const UINT currentDpi = hwnd_ ? GetDpiForWindow(hwnd_) : GetDpiForSystem();
        if (AdjustWindowRectExForDpi(&rect, style, FALSE, 0, currentDpi) == FALSE) {
            AdjustWindowRect(&rect, style, FALSE);
        }
        const int windowWidth = rect.right - rect.left;
        const int windowHeight = rect.bottom - rect.top;

        const MONITORINFO monitorInfo = getMonitorInfo();
        POINT desiredCenter{
            (monitorInfo.rcWork.left + monitorInfo.rcWork.right) / 2,
            (monitorInfo.rcWork.top + monitorInfo.rcWork.bottom) / 2,
        };

        if (windowMode_ == WindowMode::Windowed) {
            RECT currentRect{};
            if (GetWindowRect(hwnd_, &currentRect)) {
                desiredCenter.x = (currentRect.left + currentRect.right) / 2;
                desiredCenter.y = (currentRect.top + currentRect.bottom) / 2;
            }
        } else if (windowedPlacement_.rcNormalPosition.right > windowedPlacement_.rcNormalPosition.left &&
                   windowedPlacement_.rcNormalPosition.bottom > windowedPlacement_.rcNormalPosition.top) {
            desiredCenter.x =
                (windowedPlacement_.rcNormalPosition.left + windowedPlacement_.rcNormalPosition.right) / 2;
            desiredCenter.y =
                (windowedPlacement_.rcNormalPosition.top + windowedPlacement_.rcNormalPosition.bottom) / 2;
        }

        const POINT target = clampWindowRectToWorkArea(monitorInfo.rcWork, windowWidth, windowHeight, desiredCenter);

        SetWindowLongPtrW(hwnd_, GWL_STYLE, static_cast<LONG_PTR>(style));
        if (!startupHidden_)
            ShowWindow(hwnd_, SW_RESTORE);
        const BOOL moved =
            SetWindowPos(hwnd_, HWND_NOTOPMOST, target.x, target.y, windowWidth, windowHeight,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED | (startupHidden_ ? SWP_NOACTIVATE : SWP_SHOWWINDOW));
        if (!moved) {
            return false;
        }
        windowMode_ = mode;
        return true;
    }

    if (windowMode_ == WindowMode::Windowed) {
        windowedPlacement_.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hwnd_, &windowedPlacement_);
    }

    const MONITORINFO monitorInfo = getMonitorInfo();

    SetWindowLongPtrW(hwnd_, GWL_STYLE, static_cast<LONG_PTR>(WS_POPUP | (startupHidden_ ? 0 : WS_VISIBLE)));
    const RECT& target = monitorInfo.rcMonitor;
    const BOOL moved =
        SetWindowPos(hwnd_, mode == WindowMode::Fullscreen ? HWND_TOPMOST : HWND_TOP, target.left, target.top,
                     target.right - target.left, target.bottom - target.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED | (startupHidden_ ? SWP_NOACTIVATE : SWP_SHOWWINDOW));
    if (!moved) {
        return false;
    }
    windowMode_ = mode;
    return true;
}

void Win32Window::Shutdown() {
    messageHandler_ = {};
    if (hwnd_) {
        ResetTextInput(hwnd_, true);
        DragAcceptFiles(hwnd_, FALSE);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    running_ = false;
    closeRequested_ = false;
}
// メッセージループを処理する。WM_QUIT メッセージが来たら false を返す。それ以外は true を返す。
bool Win32Window::PumpMessages() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            running_ = false;
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return running_;
}
// マウスホイールのデルタを消費する。呼び出すと現在のデルタが返され、内部のデルタはリセットされる。
float Win32Window::ConsumeMouseWheelDelta() {
    const float delta = mouseWheelDelta_;
    mouseWheelDelta_ = 0.0f;
    return delta;
}

std::vector<INPUT::TextInputEvent> Win32Window::ConsumeTextInputEvents() {
    std::vector<INPUT::TextInputEvent> events = std::move(textInputEvents_);
    textInputEvents_.clear();
    return events;
}

void Win32Window::DiscardTextInputEvents() noexcept { textInputEvents_.clear(); }

void Win32Window::ResetTextInputForWindow(HWND hwnd) noexcept {
    if (hwnd == nullptr || IsWindow(hwnd) == FALSE) {
        return;
    }
    auto* window = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (window != nullptr && window->hwnd_ == hwnd) {
        window->ResetTextInput(hwnd, true);
    }
}

void Win32Window::ApplyImeCaretAnchorForWindow(HWND hwnd, INPUT::ImeCaretAnchor anchor) noexcept {
    if (hwnd == nullptr || IsWindow(hwnd) == FALSE) {
        return;
    }
    HIMC context = ImmGetContext(hwnd);
    if (context == nullptr) {
        return;
    }
    if (!anchor.valid) {
        (void)ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0u);
        (void)ImmReleaseContext(hwnd, context);
        return;
    }

    RECT clientRect{};
    if (GetClientRect(hwnd, &clientRect) != FALSE) {
        anchor.clientX = (std::clamp)(anchor.clientX, 0, static_cast<int32_t>((std::max)(clientRect.right - 1, 0L)));
        anchor.clientY = (std::clamp)(anchor.clientY, 0, static_cast<int32_t>((std::max)(clientRect.bottom - 1, 0L)));
    }
    const POINT position{anchor.clientX, anchor.clientY};
    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos = position;
    (void)ImmSetCompositionWindow(context, &composition);

    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0u;
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos = position;
    (void)ImmSetCandidateWindow(context, &candidate);
    (void)ImmReleaseContext(hwnd, context);
}

void Win32Window::AppendTextInput(INPUT::TextInputEventKind kind, std::string utf8) {
    try {
        if (kind == INPUT::TextInputEventKind::CompositionUpdated && !textInputEvents_.empty() &&
            textInputEvents_.back().kind == kind) {
            textInputEvents_.back().utf8 = std::move(utf8);
            return;
        }
        if (textInputEvents_.size() >= kMaximumTextInputEventsPerFrame) {
            return;
        }
        textInputEvents_.push_back(INPUT::TextInputEvent{kind, std::move(utf8)});
    } catch (...) {
    }
}

void Win32Window::AppendUnicodeCharacter(uint32_t value) {
    if (value == 0u || value > 0x10FFFFu || (value >= 0xD800u && value <= 0xDFFFu)) {
        return;
    }
    std::string utf8{};
    if (value <= 0x7Fu) {
        utf8.push_back(static_cast<char>(value));
    } else if (value <= 0x7FFu) {
        utf8.push_back(static_cast<char>(0xC0u | (value >> 6u)));
        utf8.push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
    } else if (value <= 0xFFFFu) {
        utf8.push_back(static_cast<char>(0xE0u | (value >> 12u)));
        utf8.push_back(static_cast<char>(0x80u | ((value >> 6u) & 0x3Fu)));
        utf8.push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
    } else {
        utf8.push_back(static_cast<char>(0xF0u | (value >> 18u)));
        utf8.push_back(static_cast<char>(0x80u | ((value >> 12u) & 0x3Fu)));
        utf8.push_back(static_cast<char>(0x80u | ((value >> 6u) & 0x3Fu)));
        utf8.push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
    }
    AppendTextInput(INPUT::TextInputEventKind::Committed, std::move(utf8));
}

void Win32Window::AppendUtf16Character(uint16_t value) {
    if (value >= 0xD800u && value <= 0xDBFFu) {
        pendingHighSurrogate_ = static_cast<wchar_t>(value);
        return;
    }
    if (value >= 0xDC00u && value <= 0xDFFFu) {
        if (pendingHighSurrogate_ != L'\0') {
            const uint32_t high = static_cast<uint16_t>(pendingHighSurrogate_);
            pendingHighSurrogate_ = L'\0';
            AppendUnicodeCharacter(0x10000u + ((high - 0xD800u) << 10u) + (value - 0xDC00u));
        }
        return;
    }
    pendingHighSurrogate_ = L'\0';
    AppendUnicodeCharacter(value);
}

void Win32Window::ResetTextInput(HWND hwnd, bool cancelComposition, bool publishCompositionEnded) noexcept {
    const bool wasComposing = imeCompositionActive_;
    const bool hadCompositionEnd = std::any_of(textInputEvents_.begin(), textInputEvents_.end(), [](const auto& event) {
        return event.kind == INPUT::TextInputEventKind::CompositionEnded;
    });
    textInputEvents_.clear();
    pendingHighSurrogate_ = L'\0';
    imeCompositionActive_ = false;
    if (publishCompositionEnded && (wasComposing || hadCompositionEnd)) {
        AppendTextInput(INPUT::TextInputEventKind::CompositionEnded);
    }
    if (cancelComposition && wasComposing && hwnd != nullptr) {
        HIMC context = ImmGetContext(hwnd);
        if (context != nullptr) {
            (void)ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0u);
            (void)ImmReleaseContext(hwnd, context);
        }
    }
}

void Win32Window::CaptureTextInput(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CHAR: AppendUtf16Character(static_cast<uint16_t>(wparam)); break;
    case WM_UNICHAR:
        if (wparam != UNICODE_NOCHAR) {
            pendingHighSurrogate_ = L'\0';
            AppendUnicodeCharacter(static_cast<uint32_t>(wparam));
        }
        break;
    case WM_IME_STARTCOMPOSITION:
        pendingHighSurrogate_ = L'\0';
        if (!imeCompositionActive_) {
            imeCompositionActive_ = true;
            AppendTextInput(INPUT::TextInputEventKind::CompositionStarted);
        }
        break;
    case WM_IME_COMPOSITION:
        if ((static_cast<uint32_t>(lparam) & GCS_COMPSTR) != 0u) {
            if (!imeCompositionActive_) {
                imeCompositionActive_ = true;
                AppendTextInput(INPUT::TextInputEventKind::CompositionStarted);
            }
            HIMC context = ImmGetContext(hwnd);
            if (context != nullptr) {
                const LONG byteCount = ImmGetCompositionStringW(context, GCS_COMPSTR, nullptr, 0u);
                if (byteCount >= 0 && static_cast<size_t>(byteCount) <= kMaximumImeCompositionLength &&
                    (static_cast<size_t>(byteCount) % sizeof(wchar_t)) == 0u) {
                    std::wstring composition(static_cast<size_t>(byteCount) / sizeof(wchar_t), L'\0');
                    const LONG readBytes = byteCount == 0
                                               ? 0
                                               : ImmGetCompositionStringW(context, GCS_COMPSTR, composition.data(),
                                                                          static_cast<DWORD>(byteCount));
                    if (readBytes == byteCount) {
                        std::string utf8{};
                        if (TryEncodeUtf16(composition, utf8)) {
                            AppendTextInput(INPUT::TextInputEventKind::CompositionUpdated, std::move(utf8));
                        }
                    }
                }
                (void)ImmReleaseContext(hwnd, context);
            }
        }
        break;
    case WM_IME_ENDCOMPOSITION:
        if (imeCompositionActive_) {
            imeCompositionActive_ = false;
            AppendTextInput(INPUT::TextInputEventKind::CompositionEnded);
        }
        pendingHighSurrogate_ = L'\0';
        break;
    case WM_KILLFOCUS: ResetTextInput(hwnd, true, true); break;
    case WM_ACTIVATEAPP:
        if (wparam == FALSE) {
            ResetTextInput(hwnd, true, true);
        }
        break;
    default: break;
    }
}
//
LRESULT CALLBACK Win32Window::StaticWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Win32Window* window = nullptr;
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        window = reinterpret_cast<Win32Window*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    } else {
        window = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (window) {
        return window->WndProc(hwnd, msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void Win32Window::Show() {
    if (hwnd_) {
        startupHidden_ = false;
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        HIKARI_LOG_INFO("Window shown.");
    }
}

namespace {
float gPenPressure = 1.0f;

// ペンの筆圧だけを覚える。メッセージは消費しないので、マウスのメッセージへの変換はそのまま。
void CapturePenPressure(UINT msg, WPARAM wparam) {
    if (msg == WM_POINTERUP || msg == WM_POINTERLEAVE || msg == WM_POINTERCAPTURECHANGED) {
        gPenPressure = 1.0f;
        return;
    }
    if (msg != WM_POINTERDOWN && msg != WM_POINTERUPDATE) {
        return;
    }
    const UINT32 pointer = GET_POINTERID_WPARAM(wparam);
    POINTER_INPUT_TYPE type = PT_POINTER;
    POINTER_PEN_INFO pen{};
    if (GetPointerType(pointer, &type) && type == PT_PEN && GetPointerPenInfo(pointer, &pen) &&
        (pen.penMask & PEN_MASK_PRESSURE) != 0u) {
        gPenPressure = static_cast<float>(pen.pressure) / 1024.0f;
    }
}
} // namespace

float CurrentPenPressure() noexcept { return gPenPressure; }

LRESULT Win32Window::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    CapturePenPressure(msg, wparam);
    try {
        CaptureTextInput(hwnd, msg, wparam, lparam);
    } catch (...) {
        pendingHighSurrogate_ = L'\0';
        imeCompositionActive_ = false;
    }
    if (msg == WM_UNICHAR && wparam == UNICODE_NOCHAR) {
        return TRUE;
    }
    if (dispatchHostMessages_ && messageHandler_ && messageHandler_(hwnd, msg, wparam, lparam)) {
        return TRUE;
    }

    switch (msg) {
    case WM_CLOSE:
        if (closeBehavior_ == WindowCloseBehavior::SignalOnly) {
            closeRequested_ = true;
            return 0;
        }
        break;
    case WM_DPICHANGED: {
        // モニター間移動などで DPI が変わったら、OS 推奨矩形へ追従する。
        // SetWindowPos により WM_SIZE が送られ、クライアントサイズと
        // リサイズコールバックはそちらで更新される。
        const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
        if (suggested != nullptr) {
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    case WM_SIZE:
        width_ = LOWORD(lparam);
        height_ = HIWORD(lparam);
        isMinimized_ = (wparam == SIZE_MINIMIZED);
        if (!isMinimized_ && onResize_) {
            onResize_(width_, height_);
        }
        break;
    case WM_DESTROY:
        running_ = false;
        if (closeBehavior_ == WindowCloseBehavior::QuitApplication) {
            PostQuitMessage(0);
        }
        return 0;
    case WM_MOUSEWHEEL:
        mouseWheelDelta_ += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / static_cast<float>(WHEEL_DELTA);
        return 0;
    case WM_DROPFILES: {
        QueueExternalFileDrop(hwnd, reinterpret_cast<HDROP>(wparam));
        return 0;
    }
    default: break;
    }
    // NOTE: 必ず「今処理中の hwnd」を渡す。CreateWindow 直後などで hwnd_ が未確定でも安全。
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace HIKARI::PLATFORM
