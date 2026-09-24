#include "Platform/HIKARI_StartupSplash.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <future>
#include <thread>
#include <vector>

namespace HIKARI::PLATFORM {
namespace {
using Microsoft::WRL::ComPtr;
constexpr wchar_t kSplashClass[] = L"HIKARI.StartupSplash";

struct SplashImage {
    HBITMAP bitmap = nullptr;
    int width = 0, height = 0;
    ~SplashImage() {
        if (bitmap)
            DeleteObject(bitmap);
    }

    bool Load(UINT dpi, const RECT& work) {
        const HMODULE module = GetModuleHandleW(nullptr);
        const HRSRC resource = FindResourceW(module, L"HIKARI_STARTUP_LOGO", RT_RCDATA);
        if (!resource)
            return false;
        const DWORD size = SizeofResource(module, resource);
        const HGLOBAL loaded = LoadResource(module, resource);
        auto* bytes = static_cast<BYTE*>(LockResource(loaded));
        if (!bytes || !size)
            return false;
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
            FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromMemory(bytes, size)) ||
            FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) ||
            FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                         0, WICBitmapPaletteTypeCustom)))
            return false;
        UINT sourceWidth = 0, sourceHeight = 0;
        if (FAILED(converter->GetSize(&sourceWidth, &sourceHeight)) || !sourceWidth || !sourceHeight ||
            sourceWidth > 4096 || sourceHeight > 4096)
            return false;
        const UINT stride = sourceWidth * 4;
        std::vector<BYTE> pixels(size_t(stride) * sourceHeight);
        if (FAILED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data())))
            return false;

        // 透明な余白を表示サイズへ含めず、半透明の縁はそのまま保持する。
        UINT left = sourceWidth, top = sourceHeight, right = 0, bottom = 0;
        for (UINT y = 0; y < sourceHeight; ++y) {
            for (UINT x = 0; x < sourceWidth; ++x) {
                if (pixels[size_t(y) * stride + x * 4 + 3]) {
                    left = (std::min)(left, x);
                    top = (std::min)(top, y);
                    right = (std::max)(right, x + 1);
                    bottom = (std::max)(bottom, y + 1);
                }
            }
        }
        if (right <= left || bottom <= top)
            return false;
        WICRect crop{static_cast<INT>(left), static_cast<INT>(top), static_cast<INT>(right - left),
                     static_cast<INT>(bottom - top)};
        const double scale =
            (std::min)({double(MulDiv(400, dpi, 96)) / crop.Width, double(work.right - work.left) * 0.8 / crop.Width,
                        double(work.bottom - work.top) * 0.8 / crop.Height});
        width = (std::max)(1, static_cast<int>(crop.Width * scale));
        height = (std::max)(1, static_cast<int>(crop.Height * scale));
        ComPtr<IWICBitmapClipper> clipper;
        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(factory->CreateBitmapClipper(&clipper)) || FAILED(clipper->Initialize(converter.Get(), &crop)) ||
            FAILED(factory->CreateBitmapScaler(&scaler)) ||
            FAILED(scaler->Initialize(clipper.Get(), width, height, WICBitmapInterpolationModeFant)))
            return false;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* destination = nullptr;
        bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &destination, nullptr, 0);
        return bitmap && destination &&
               SUCCEEDED(scaler->CopyPixels(nullptr, width * 4, width * height * 4, static_cast<BYTE*>(destination)));
    }
};

LRESULT CALLBACK SplashWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    // 起動スレッドへ WM_QUIT を送らず、所有側の停止イベントだけで閉じる。
    if (message == WM_CLOSE)
        return 0;
    if (message == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    return DefWindowProcW(window, message, wParam, lParam);
}

void RunSplash(HANDLE stopEvent, std::promise<bool> ready) {
    bool reported = false;
    HWND window = nullptr;
    HDC memory = nullptr;
    HGDIOBJ previous = nullptr;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SplashImage image;
    try {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
        windowClass.hInstance = instance;
        windowClass.lpfnWndProc = SplashWndProc;
        windowClass.lpszClassName = kSplashClass;
        if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw 0;
        POINT cursor{};
        GetCursorPos(&cursor);
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (!GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor))
            throw 0;
        window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, kSplashClass,
                                 L"HIKARI", WS_POPUP, monitor.rcWork.left, monitor.rcWork.top, 1, 1, nullptr, nullptr,
                                 instance, nullptr);
        if (FAILED(com) || !window || !image.Load(GetDpiForWindow(window), monitor.rcWork))
            throw 0;
        memory = CreateCompatibleDC(nullptr);
        if (!memory)
            throw 0;
        previous = SelectObject(memory, image.bitmap);
        POINT position{monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - image.width) / 2,
                       monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - image.height) / 2};
        POINT source{};
        SIZE dimensions{image.width, image.height};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        if (!UpdateLayeredWindow(window, nullptr, &position, &dimensions, memory, &source, 0, &blend, ULW_ALPHA))
            throw 0;
        ShowWindow(window, SW_SHOWNOACTIVATE);
        ready.set_value(true);
        reported = true;
        // 描画は Windows に委ねる。vendor 初期化中も native UI を処理する。
        for (;;) {
            const DWORD result = MsgWaitForMultipleObjectsEx(1, &stopEvent, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (result != WAIT_OBJECT_0 + 1)
                break;
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
    } catch (...) {
        // 装飾の失敗で本体を起動不能にしない。
    }
    if (!reported)
        ready.set_value(false);
    if (previous)
        SelectObject(memory, previous);
    if (memory)
        DeleteDC(memory);
    if (window)
        DestroyWindow(window);
    UnregisterClassW(kSplashClass, GetModuleHandleW(nullptr));
    if (SUCCEEDED(com))
        CoUninitialize();
}
} // namespace

struct StartupSplash::State {
    HANDLE stopEvent = nullptr;
    std::thread worker;
};

StartupSplash::StartupSplash() : state_(std::make_unique<State>()) {}
StartupSplash::~StartupSplash() { Stop(); }

bool StartupSplash::Start() {
    Stop();
    state_->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!state_->stopEvent)
        return false;
    try {
        std::promise<bool> ready;
        auto result = ready.get_future();
        state_->worker = std::thread(RunSplash, state_->stopEvent, std::move(ready));
        if (result.get())
            return true;
    } catch (...) {
    }
    Stop();
    return false;
}

void StartupSplash::Stop() {
    if (state_->stopEvent)
        SetEvent(state_->stopEvent);
    if (state_->worker.joinable())
        state_->worker.join();
    if (state_->stopEvent)
        CloseHandle(state_->stopEvent);
    state_->stopEvent = nullptr;
}
} // namespace HIKARI::PLATFORM
