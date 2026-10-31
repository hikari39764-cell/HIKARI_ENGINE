#include "Platform/HIKARI_LoadingScreen.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "Core/HIKARI_Logger.h"
#include "Platform/HIKARI_ApplicationIcon.h"

namespace HIKARI::PLATFORM {
namespace {
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

constexpr wchar_t kWindowClass[] = L"HIKARI.LoadingScreen";
constexpr int kStageCount = 4;
// 前回の記録が無い時の各段階の目安（ミリ秒）。初回はシェーダーの準備が長い。
constexpr std::array<double, kStageCount> kDefaultStageMs = {300.0, 4500.0, 2500.0, 9000.0};
constexpr char kProfileHeader[] = "HIKARI-STARTUP";
constexpr int kProfileVersion = 1;

// 起動中 / グラフィックスを準備中 / データを読み込み中 / シェーダーを準備中
constexpr std::array<const wchar_t*, kStageCount> kStageText = {
    L"起動中",
    L"グラフィックスを準備中",
    L"データを読み込み中",
    L"シェーダーを準備中",
};
// まもなく はじまります
constexpr wchar_t kFinishText[] = L"まもなく はじまります";
// 初回のみ、少し時間がかかります
constexpr wchar_t kFirstLaunchNote[] =
    L"初回のみ、少し時間がかかります";

// 色は標志から取る（星の黄、A の桃色、純黒の縁）。
struct Rgb {
    int r = 0, g = 0, b = 0;
};
constexpr Rgb kBackground{0x1E, 0x18, 0x14};
constexpr Rgb kGlow{0x30, 0x26, 0x1C};
constexpr Rgb kDot{0x3B, 0x2F, 0x22};
constexpr Rgb kInk{0x00, 0x00, 0x00};
constexpr Rgb kCream{0xFF, 0xF1, 0xD2};
constexpr Rgb kYellow{0xF2, 0xF7, 0x72};
constexpr Rgb kYellowHigh{0xFC, 0xFF, 0xC6};
constexpr Rgb kYellowDim{0xD2, 0xD8, 0x56};
constexpr Rgb kPeach{0xFF, 0xBC, 0x7B};
constexpr Rgb kPeachDark{0xD9, 0x92, 0x55};
constexpr Rgb kTrack{0x33, 0x29, 0x21};
constexpr Rgb kTrackShade{0x24, 0x1D, 0x18};
constexpr Rgb kNote{0x93, 0x7D, 0x64};
constexpr Rgb kWhite{0xFF, 0xFF, 0xFF};

// 進捗の先端に乗る 15×15 の星（# 黒、Y 黄、P 桃色の影、H 白い光点）。
constexpr std::array<const char*, 15> kStarSprite = {
    ".......#.......", "......#H#......", "......#Y#......", ".....#YYY#.....", ".....#YYY#.....",
    "######YYY######", "#YYHYYYYYYYYYY#", ".#YYYYYYYYYYY#.", "..##YYYYYYY##..", "...#YYYYYYY#...",
    "..#YYYYPYYYY#..", "..#YYYP#PYYY#..", ".#YYP##.##PYY#.", ".#YP#.....#PY#.", ".###.......###.",
};

uint32_t Pack(const Rgb& c) {
    return (static_cast<uint32_t>(c.r) << 16) | (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.b);
}
uint32_t BlendPixel(uint32_t dst, const Rgb& c, float a) {
    if (a >= 0.999f)
        return Pack(c);
    const int k = static_cast<int>(std::clamp(a, 0.0f, 1.0f) * 256.0f);
    const int dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    return Pack({dr + (((c.r - dr) * k) >> 8), dg + (((c.g - dg) * k) >> 8), db + (((c.b - db) * k) >> 8)});
}
float Smooth01(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

// 文字の形（1 画素が 1 マス）。
struct Glyphs {
    int width = 0, height = 0;
    std::vector<uint8_t> on;
};

// 本体のスレッドと窓のスレッドが共有する値。
struct Shared {
    std::mutex mutex;
    bool planReady = false;
    bool coverMonitor = true;
    int clientWidth = 0, clientHeight = 0;
    std::wstring title;
    std::filesystem::path userRoot;
    std::array<Clock::time_point, kStageCount + 1> stageStart{};
    std::atomic<int> stage{0};
    std::atomic<bool> userRootReady{false};
    std::atomic<bool> revealRequested{false};
};
} // namespace

struct LoadingScreen::State {
    HANDLE stopEvent = nullptr;
    HANDLE planEvent = nullptr;
    HANDLE revealEvent = nullptr;
    std::thread worker;
    Shared shared;
};

namespace {
std::atomic<LoadingScreen::State*> gActive{nullptr};

// 窓のスレッドで動く描画と演出。
class Painter {
public:
    explicit Painter(LoadingScreen::State& state) : state_(state), shared_(state.shared) {}
    void Run();

private:
    static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    bool LoadResources();
    void WaitForPlan();
    bool CreateSurface();
    void BuildLayout();
    void BuildBase();
    void ReadProfile();
    void WriteProfile();
    double TargetProgress(Clock::time_point now) const;
    void Frame(Clock::time_point now);
    void RestoreRect(const RECT& rect, float darkness);
    void Fill(int x, int y, int w, int h, const Rgb& c, float a);
    void Cell(int x, int y, const Rgb& c, float a) { Fill(x, y, cell_, cell_, c, a); }
    void DrawGlyphs(const Glyphs& glyphs, int left, int top, const Rgb& fill, bool outlined, float alpha);
    void DrawBar(double progress, double seconds, float alpha);
    void DrawSparkles(double seconds, float alpha);
    const Glyphs& Text(HFONT font, const std::wstring& text);
    int TextWidth(HFONT font, const std::wstring& text) { return Text(font, text).width; }

    LoadingScreen::State& state_;
    Shared& shared_;
    HWND hwnd_ = nullptr;
    HDC memoryDc_ = nullptr;
    HBITMAP surface_ = nullptr;
    HGDIOBJ previousSurface_ = nullptr;
    uint32_t* pixels_ = nullptr;
    std::vector<uint32_t> base_;
    int width_ = 0, height_ = 0, cell_ = 1;

    // 標志（切り抜いた乗算済み BGRA）。
    std::vector<uint8_t> logo_;
    int logoSourceWidth_ = 0, logoSourceHeight_ = 0;
    HANDLE statusFontResource_ = nullptr, noteFontResource_ = nullptr;
    HFONT statusFont_ = nullptr, noteFont_ = nullptr;
    std::map<std::pair<uintptr_t, std::wstring>, Glyphs> glyphCache_;

    // 配置（画素）。
    RECT logoRect_{};
    int barLeft_ = 0, barTop_ = 0, barCells_ = 0, statusTop_ = 0, noteTop_ = 0;
    int glowX_ = 0, glowY_ = 0, glowRadius_ = 1;
    RECT uiRect_{};

    // 演出。
    Clock::time_point begin_{};
    Clock::time_point lastFrame_{};
    double shown_ = 0.0;
    int observedStage_ = 0;
    Clock::time_point observedStageStart_{};
    std::array<double, kStageCount> expectedMs_ = kDefaultStageMs;
    bool firstLaunch_ = false;
    bool profileRead_ = false;
    bool revealStarted_ = false;
    Clock::time_point revealStart_{};
    bool revealSignaled_ = false;
    bool blackDrawn_ = false;
    struct Sparkle {
        int x = 0, y = 0;
        double born = -10.0;
    };
    std::array<Sparkle, 2> sparkles_{};
    std::vector<RECT> previousSparkleRects_;
    double nextSparkle_ = 0.8;
    uint32_t random_ = 0x9E3779B9u;
    double Random01() {
        random_ = random_ * 1664525u + 1013904223u;
        return static_cast<double>(random_ >> 8) / static_cast<double>(1u << 24);
    }
};

LRESULT CALLBACK Painter::Proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<Painter*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        if (self && self->memoryDc_) {
            const RECT& r = paint.rcPaint;
            BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, self->memoryDc_, r.left, r.top, SRCCOPY);
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_PRINTCLIENT:
        if (self && self->memoryDc_)
            BitBlt(reinterpret_cast<HDC>(wparam), 0, 0, self->width_, self->height_, self->memoryDc_, 0, 0, SRCCOPY);
        return 0;
    case WM_ERASEBKGND: return 1;
    // 焦点を取らない（壊す時に本体の窓への活性化の受け渡しが起きないように）。閉じるのは持ち主だけ。
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_CLOSE: return 0;
    default: return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

bool Painter::LoadResources() {
    const HMODULE module = GetModuleHandleW(nullptr);
    auto resourceBytes = [module](const wchar_t* name, DWORD& size) -> const BYTE* {
        size = 0;
        const HRSRC resource = FindResourceW(module, name, RT_RCDATA);
        if (!resource)
            return nullptr;
        size = SizeofResource(module, resource);
        const HGLOBAL loaded = LoadResource(module, resource);
        return loaded ? static_cast<const BYTE*>(LockResource(loaded)) : nullptr;
    };

    // 字体は実行ファイルに埋め込んだものを、この処理だけで使う。
    DWORD fontSize = 0;
    DWORD installed = 0;
    if (const BYTE* bytes = resourceBytes(L"HIKARI_LOADING_FONT_STATUS", fontSize))
        statusFontResource_ = AddFontMemResourceEx(const_cast<BYTE*>(bytes), fontSize, nullptr, &installed);
    if (const BYTE* bytes = resourceBytes(L"HIKARI_LOADING_FONT_NOTE", fontSize))
        noteFontResource_ = AddFontMemResourceEx(const_cast<BYTE*>(bytes), fontSize, nullptr, &installed);
    // 点の字体は設計の大きさで描く時だけ線が 1 画素に揃う（M+ は 11、k12x10 は 10）。
    auto makeFont = [](int pixels, const wchar_t* face) {
        return CreateFontW(-pixels, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                           CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, face);
    };
    statusFont_ = makeFont(11, L"JF Dot M+ 12");
    noteFont_ = makeFont(10, L"JF Dot k12x10");

    DWORD logoSize = 0;
    const BYTE* logoBytes = resourceBytes(L"HIKARI_STARTUP_LOGO", logoSize);
    if (!logoBytes || !logoSize)
        return false;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(logoBytes), logoSize)) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeCustom)))
        return false;
    UINT w = 0, h = 0;
    if (FAILED(converter->GetSize(&w, &h)) || !w || !h || w > 4096 || h > 4096)
        return false;
    std::vector<uint8_t> pixels(size_t(w) * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data())))
        return false;
    // 透明な余白を切り抜く。
    UINT left = w, top = h, right = 0, bottom = 0;
    for (UINT y = 0; y < h; ++y)
        for (UINT x = 0; x < w; ++x)
            if (pixels[(size_t(y) * w + x) * 4 + 3]) {
                left = (std::min)(left, x), top = (std::min)(top, y);
                right = (std::max)(right, x + 1), bottom = (std::max)(bottom, y + 1);
            }
    if (right <= left || bottom <= top)
        return false;
    logoSourceWidth_ = static_cast<int>(right - left);
    logoSourceHeight_ = static_cast<int>(bottom - top);
    logo_.resize(size_t(logoSourceWidth_) * logoSourceHeight_ * 4);
    for (int y = 0; y < logoSourceHeight_; ++y)
        std::copy_n(&pixels[(size_t(top + y) * w + left) * 4], size_t(logoSourceWidth_) * 4,
                    &logo_[size_t(y) * logoSourceWidth_ * 4]);
    return true;
}

void Painter::WaitForPlan() {
    // 窓の出し方は本体が起動設定を読んだ直後に届く。届かなければ画面全体で出す。
    const HANDLE handles[] = {state_.planEvent, state_.stopEvent};
    (void)WaitForMultipleObjects(2, handles, FALSE, 2000);
}

bool Painter::CreateSurface() {
    bool cover = true;
    int clientWidth = 0, clientHeight = 0;
    std::wstring title;
    {
        std::scoped_lock lock(shared_.mutex);
        cover = shared_.coverMonitor || !shared_.planReady;
        clientWidth = shared_.clientWidth;
        clientHeight = shared_.clientHeight;
        title = shared_.title;
    }
    // ゲームの窓は主画面に出るので、同じ場所を覆う。
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor))
        return false;
    RECT rect = monitor.rcMonitor;
    if (!cover && clientWidth > 0 && clientHeight > 0) {
        const RECT& work = monitor.rcWork;
        const int w = (std::min)(clientWidth, static_cast<int>(work.right - work.left));
        const int h = (std::min)(clientHeight, static_cast<int>(work.bottom - work.top));
        rect.left = work.left + (work.right - work.left - w) / 2;
        rect.top = work.top + (work.bottom - work.top - h) / 2;
        rect.right = rect.left + w;
        rect.bottom = rect.top + h;
    }
    width_ = rect.right - rect.left;
    height_ = rect.bottom - rect.top;
    if (width_ < 64 || height_ < 64)
        return false;

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
    windowClass.lpfnWndProc = &Painter::Proc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_APPSTARTING);
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(kApplicationIconResourceId));
    windowClass.hbrBackground = CreateSolidBrush(RGB(kBackground.r, kBackground.g, kBackground.b));
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;
    // 起動中もタスクバーに出す。焦点は取らない。
    hwnd_ = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_NOACTIVATE, kWindowClass, title.empty() ? L"HIKARI" : title.c_str(),
                            WS_POPUP, rect.left, rect.top, width_, height_, nullptr, nullptr, instance, nullptr);
    if (!hwnd_)
        return false;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width_;
    info.bmiHeader.biHeight = -height_;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    memoryDc_ = CreateCompatibleDC(nullptr);
    surface_ = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!memoryDc_ || !surface_ || !bits)
        return false;
    previousSurface_ = SelectObject(memoryDc_, surface_);
    pixels_ = static_cast<uint32_t*>(bits);
    return true;
}

void Painter::BuildLayout() {
    // 1 マスの大きさ。1080 行で 3 画素。標志もマス単位で決め、どの解像度でも同じ釣り合いにする。
    cell_ = (std::max)(1, (std::min)(height_ / 360, width_ / 240));
    const int s = cell_;
    const int logoHeight = 145 * s;
    const int logoWidth =
        static_cast<int>(std::lround(double(logoHeight) * logoSourceWidth_ / (std::max)(1, logoSourceHeight_)));
    const int cx = width_ / 2;
    const int cy = static_cast<int>(std::lround(height_ * 0.41));
    logoRect_ = {cx - logoWidth / 2, cy - logoHeight / 2, cx - logoWidth / 2 + logoWidth, cy - logoHeight / 2 + logoHeight};
    // 棒は標志の幅に揃え、標志の下から 39 マス空けて文、その下に棒、さらに注記。
    barCells_ = (std::max)(120, static_cast<int>(std::lround(double(logoWidth) / s)));
    barLeft_ = cx - barCells_ * s / 2;
    statusTop_ = logoRect_.bottom + 39 * s;
    barTop_ = statusTop_ + 17 * s;
    noteTop_ = barTop_ + 24 * s;
    // 光は星の中心から。
    glowX_ = logoRect_.left + static_cast<int>(logoWidth * 0.387);
    glowY_ = logoRect_.top + static_cast<int>(logoHeight * 0.366);
    glowRadius_ = (std::max)(1, 153 * s);
    // 毎回描き直す範囲：文から注記まで、先端のきらめきを含めて左右に余白。
    uiRect_ = {(std::max)(0, barLeft_ - 16 * s), (std::max)(0, statusTop_ - 4 * s),
               (std::min)(width_, barLeft_ + (barCells_ + 16) * s), (std::min)(height_, noteTop_ + 14 * s)};
}

void Painter::BuildBase() {
    base_.assign(size_t(width_) * height_, Pack(kBackground));
    // 光：星の後ろから暖かく広がる。4×4 の順序ディザで帯を出さない。
    static constexpr int kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    const int y0 = (std::max)(0, glowY_ - glowRadius_), y1 = (std::min)(height_, glowY_ + glowRadius_);
    const int x0 = (std::max)(0, glowX_ - glowRadius_), x1 = (std::min)(width_, glowX_ + glowRadius_);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const float dx = float(x - glowX_), dy = float(y - glowY_);
            const float t = 1.0f - std::sqrt(dx * dx + dy * dy) / float(glowRadius_);
            if (t <= 0.0f)
                continue;
            const float k = Smooth01(t);
            const float dither = (kBayer[y & 3][x & 3] + 0.5f) / 16.0f;
            auto channel = [&](int a, int b) { return int(a + (b - a) * k + dither); };
            base_[size_t(y) * width_ + x] = Pack({channel(kBackground.r, kGlow.r), channel(kBackground.g, kGlow.g),
                                                  channel(kBackground.b, kGlow.b)});
        }
    }
    // 標志の周りの、ほとんど見えない小さな点。
    const int s = cell_;
    const int cx = (logoRect_.left + logoRect_.right) / 2, cy = (logoRect_.top + logoRect_.bottom) / 2;
    for (int i = 0; i < 14; ++i) {
        const double angle = (i / 14.0 + Random01() * 0.05) * 6.283185;
        const double radius = (100.0 + Random01() * 73.0) * s;
        const int x = (int(cx + std::cos(angle) * radius) / s) * s, y = (int(cy + std::sin(angle) * radius) / s) * s;
        if (y > logoRect_.bottom - 4 * s || x < 0 || y < 0 || x + s > width_ || y + s > height_)
            continue;
        for (int yy = y; yy < y + s; ++yy)
            std::fill_n(&base_[size_t(yy) * width_ + x], s, Pack(kDot));
    }
    // 標志は WIC の高品質縮小で合わせ、乗算済みのまま重ねる。
    const int lw = logoRect_.right - logoRect_.left, lh = logoRect_.bottom - logoRect_.top;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmap> source;
    ComPtr<IWICBitmapScaler> scaler;
    std::vector<uint8_t> scaled(size_t(lw) * lh * 4);
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateBitmapFromMemory(logoSourceWidth_, logoSourceHeight_, GUID_WICPixelFormat32bppPBGRA,
                                                  logoSourceWidth_ * 4, static_cast<UINT>(logo_.size()), logo_.data(),
                                                  &source)) &&
        SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
        (SUCCEEDED(scaler->Initialize(source.Get(), lw, lh, WICBitmapInterpolationModeHighQualityCubic)) ||
         SUCCEEDED(scaler->Initialize(source.Get(), lw, lh, WICBitmapInterpolationModeFant))) &&
        SUCCEEDED(scaler->CopyPixels(nullptr, lw * 4, static_cast<UINT>(scaled.size()), scaled.data()))) {
        for (int y = 0; y < lh; ++y) {
            const int dy = logoRect_.top + y;
            if (dy < 0 || dy >= height_)
                continue;
            for (int x = 0; x < lw; ++x) {
                const int dx = logoRect_.left + x;
                if (dx < 0 || dx >= width_)
                    continue;
                const uint8_t* p = &scaled[(size_t(y) * lw + x) * 4];
                const int a = p[3];
                if (!a)
                    continue;
                uint32_t& d = base_[size_t(dy) * width_ + dx];
                const int inv = 255 - a;
                const int r = (std::min)(255, p[2] + ((int((d >> 16) & 0xFF) * inv) / 255));
                const int g = (std::min)(255, p[1] + ((int((d >> 8) & 0xFF) * inv) / 255));
                const int b = (std::min)(255, p[0] + ((int(d & 0xFF) * inv) / 255));
                d = Pack({r, g, b});
            }
        }
    }
}

void Painter::ReadProfile() {
    std::filesystem::path root;
    {
        std::scoped_lock lock(shared_.mutex);
        root = shared_.userRoot;
    }
    profileRead_ = true;
    std::ifstream in(root / "Cache" / "StartupProfile.txt");
    std::string header;
    int version = 0;
    std::array<double, kStageCount> measured{};
    if (in >> header >> version && header == kProfileHeader && version == kProfileVersion) {
        bool ok = true;
        for (double& ms : measured)
            ok = ok && static_cast<bool>(in >> ms);
        if (ok) {
            for (int i = 0; i < kStageCount; ++i)
                expectedMs_[i] = std::clamp(measured[i], 50.0, 120000.0);
            return;
        }
    }
    // 記録が無い＝この PC で初めての起動。
    firstLaunch_ = true;
}

void Painter::WriteProfile() {
    std::filesystem::path root;
    std::array<Clock::time_point, kStageCount + 1> starts{};
    {
        std::scoped_lock lock(shared_.mutex);
        root = shared_.userRoot;
        starts = shared_.stageStart;
    }
    if (root.empty())
        return;
    std::array<double, kStageCount> measured{};
    for (int i = 0; i < kStageCount; ++i) {
        // 飛ばした段階は 0 として、次の段階の始まりまでを前の段階に含める。
        int next = i + 1;
        while (next < kStageCount && starts[next] == Clock::time_point{})
            ++next;
        if (starts[i] == Clock::time_point{} || starts[next] == Clock::time_point{})
            continue;
        measured[i] = std::chrono::duration<double, std::milli>(starts[next] - starts[i]).count();
    }
    std::error_code error{};
    const std::filesystem::path directory = root / "Cache";
    std::filesystem::create_directories(directory, error);
    const std::filesystem::path path = directory / "StartupProfile.txt";
    const std::filesystem::path temporary = directory / "StartupProfile.txt.tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        out << kProfileHeader << ' ' << kProfileVersion;
        for (double ms : measured)
            out << ' ' << static_cast<long long>(ms);
        out << '\n';
        if (!out)
            return;
    }
    std::filesystem::rename(temporary, path, error);
}

double Painter::TargetProgress(Clock::time_point now) const {
    double total = 0.0;
    for (double ms : expectedMs_)
        total += ms;
    double start = 0.0;
    for (int i = 0; i < observedStage_; ++i)
        start += expectedMs_[i] / total;
    const double width = expectedMs_[observedStage_] / total;
    // 予想の 85% までは時間どおりに進め、それ以降は段階の終わりへ近づくだけにして、そこで止まって見せない。
    const double x = std::chrono::duration<double, std::milli>(now - observedStageStart_).count() /
                     expectedMs_[observedStage_];
    const double f = x <= 0.85 ? x : 0.85 + 0.13 * (1.0 - std::exp(-(x - 0.85) * 2.5));
    return start + width * f;
}

void Painter::Fill(int x, int y, int w, int h, const Rgb& c, float a) {
    if (a <= 0.003f)
        return;
    const int x0 = (std::max)(0, x), y0 = (std::max)(0, y);
    const int x1 = (std::min)(width_, x + w), y1 = (std::min)(height_, y + h);
    for (int yy = y0; yy < y1; ++yy) {
        uint32_t* row = pixels_ + size_t(yy) * width_;
        for (int xx = x0; xx < x1; ++xx)
            row[xx] = BlendPixel(row[xx], c, a);
    }
}

void Painter::RestoreRect(const RECT& rect, float darkness) {
    const int x0 = (std::max)(0, int(rect.left)), x1 = (std::min)(width_, int(rect.right));
    const int y0 = (std::max)(0, int(rect.top)), y1 = (std::min)(height_, int(rect.bottom));
    if (x1 <= x0 || y1 <= y0)
        return;
    const int keep = static_cast<int>((1.0f - std::clamp(darkness, 0.0f, 1.0f)) * 256.0f);
    for (int y = y0; y < y1; ++y) {
        const uint32_t* src = &base_[size_t(y) * width_];
        uint32_t* dst = pixels_ + size_t(y) * width_;
        if (keep >= 256) {
            std::copy(src + x0, src + x1, dst + x0);
            continue;
        }
        for (int x = x0; x < x1; ++x) {
            const uint32_t p = src[x];
            dst[x] = ((((p >> 16) & 0xFF) * keep >> 8) << 16) | ((((p >> 8) & 0xFF) * keep >> 8) << 8) |
                     ((p & 0xFF) * keep >> 8);
        }
    }
}

const Glyphs& Painter::Text(HFONT font, const std::wstring& text) {
    const auto key = std::make_pair(reinterpret_cast<uintptr_t>(font), text);
    if (const auto found = glyphCache_.find(key); found != glyphCache_.end())
        return found->second;
    Glyphs& glyphs = glyphCache_[key];
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc || !font)
        return glyphs;
    const HGDIOBJ previousFont = SelectObject(dc, font);
    SIZE extent{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &extent);
    glyphs.width = extent.cx + 2;
    glyphs.height = extent.cy + 2;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = glyphs.width;
    info.bmiHeader.biHeight = -glyphs.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap && bits) {
        const HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
        std::fill_n(static_cast<uint32_t*>(bits), size_t(glyphs.width) * glyphs.height, 0u);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutW(dc, 1, 1, text.c_str(), static_cast<int>(text.size()));
        GdiFlush();
        glyphs.on.resize(size_t(glyphs.width) * glyphs.height);
        const auto* p = static_cast<const uint32_t*>(bits);
        for (size_t i = 0; i < glyphs.on.size(); ++i)
            glyphs.on[i] = (p[i] & 0xFF) > 127 ? 1 : 0;
        SelectObject(dc, previousBitmap);
    }
    if (bitmap)
        DeleteObject(bitmap);
    SelectObject(dc, previousFont);
    DeleteDC(dc);
    return glyphs;
}

void Painter::DrawGlyphs(const Glyphs& glyphs, int left, int top, const Rgb& fill, bool outlined, float alpha) {
    const int s = cell_;
    auto on = [&glyphs](int x, int y) {
        return x >= 0 && y >= 0 && x < glyphs.width && y < glyphs.height && glyphs.on[size_t(y) * glyphs.width + x];
    };
    // 縁と下への影を先に、本体を最後に置く（標志の黒縁に合わせる）。
    if (outlined) {
        for (int y = -1; y <= glyphs.height + 2; ++y)
            for (int x = -1; x <= glyphs.width; ++x) {
                if (on(x, y - 2) && !on(x, y))
                    Cell(left + x * s, top + y * s, kInk, 0.55f * alpha);
                bool touching = false;
                for (int oy = -1; oy <= 1 && !touching; ++oy)
                    for (int ox = -1; ox <= 1 && !touching; ++ox)
                        touching = on(x + ox, y + oy);
                if (touching && !on(x, y))
                    Cell(left + x * s, top + y * s, kInk, alpha);
            }
    }
    for (int y = 0; y < glyphs.height; ++y)
        for (int x = 0; x < glyphs.width; ++x)
            if (on(x, y))
                Cell(left + x * s, top + y * s, fill, alpha);
}

void Painter::DrawBar(double progress, double seconds, float alpha) {
    const int s = cell_;
    const int bw = barCells_, bh = 16;
    auto R = [&](int ux, int uy, int uw, int uh, const Rgb& c, float a = 1.0f) {
        Fill(barLeft_ + ux * s, barTop_ + uy * s, uw * s, uh * s, c, a * alpha);
    };
    // 枠は標志の星と同じ「黒 2・桃色 1・黒 1」。角は 1 マス欠いて丸める。
    R(1, 0, bw - 2, bh, kInk);
    R(0, 1, bw, bh - 2, kInk);
    R(3, 2, bw - 6, bh - 4, kPeach);
    R(2, 3, bw - 4, bh - 6, kPeach);
    R(3, 3, bw - 6, bh - 6, kInk);
    const int ix = 4, iy = 4, iw = bw - 8, ih = bh - 8;
    R(ix, iy, iw, ih, kTrack);
    R(ix, iy, iw, 1, kTrackShade);
    const int fw = static_cast<int>(std::lround(iw * std::clamp(progress, 0.0, 1.0)));
    if (fw > 0) {
        // 中は星の黄、下の 2 段だけ A の桃色。8 マスごとに区切り、光が流れる。
        R(ix, iy, fw, ih, kYellow);
        R(ix, iy, fw, 1, kYellowHigh);
        R(ix, iy + ih - 2, fw, 1, kPeach);
        R(ix, iy + ih - 1, fw, 1, kPeachDark);
        for (int sx = 8; sx < fw; sx += 8)
            R(ix + sx, iy + 1, 1, ih - 3, kYellowDim);
        const int sweep = static_cast<int>(std::fmod(seconds * 70.0, double(fw + 20))) - 10;
        for (int k = 0; k < 3; ++k)
            if (sweep + k >= 0 && sweep + k < fw)
                R(ix + sweep + k, iy + 1, 1, ih - 3, kWhite, 0.55f - 0.15f * k);
    }
    // 先端の星は枠の内側に収める。
    const int tip = std::clamp(ix + fw, ix + 7, ix + iw - 7);
    const int cy = bh / 2 - 1;
    for (int y = 0; y < 15; ++y)
        for (int x = 0; x < 15; ++x) {
            const char ch = kStarSprite[y][x];
            if (ch == '.')
                continue;
            const Rgb& c = ch == '#' ? kInk : ch == 'H' ? kWhite : ch == 'P' ? kPeach : kYellow;
            R(tip - 7 + x, cy - 7 + y, 1, 1, c);
        }
    // 星の周りのきらめき。
    const double phase = std::fmod(seconds * 1.4, 1.0);
    auto sparkle = [&](int ux, int uy, int size, const Rgb& c, float a) {
        R(ux, uy, 1, 1, kYellowHigh, a);
        for (int i = 1; i <= size; ++i) {
            const float fade = a * (1.0f - (i - 1) * 0.35f);
            R(ux + i, uy, 1, 1, c, fade);
            R(ux - i, uy, 1, 1, c, fade);
            R(ux, uy + i, 1, 1, c, fade);
            R(ux, uy - i, 1, 1, c, fade);
        }
    };
    sparkle(tip + 11, cy - 10, phase < 0.5 ? 2 : 1, kYellowHigh, float(0.9 * (1.0 - phase)));
    sparkle(tip - 12, cy + 9, phase > 0.5 ? 1 : 0, kPeach, float(0.8 * phase));
}

void Painter::DrawSparkles(double seconds, float alpha) {
    const int s = cell_;
    for (Sparkle& sparkle : sparkles_) {
        const double age = seconds - sparkle.born;
        if (age < 0.0 || age > 0.6)
            continue;
        const double phase = age / 0.6;
        const float a = float(0.5 * std::sin(phase * 3.14159265)) * alpha;
        const int size = phase > 0.3 && phase < 0.7 ? 2 : 1;
        Cell(sparkle.x, sparkle.y, kYellowHigh, a);
        for (int i = 1; i <= size; ++i) {
            const float fade = a * (1.0f - (i - 1) * 0.4f);
            Cell(sparkle.x + i * s, sparkle.y, kYellowHigh, fade);
            Cell(sparkle.x - i * s, sparkle.y, kYellowHigh, fade);
            Cell(sparkle.x, sparkle.y + i * s, kYellowHigh, fade);
            Cell(sparkle.x, sparkle.y - i * s, kYellowHigh, fade);
        }
    }
}

void Painter::Frame(Clock::time_point now) {
    // 黒になった後は描き直さない。
    if (blackDrawn_)
        return;
    const double seconds = std::chrono::duration<double>(now - begin_).count();
    const double dt = std::clamp(std::chrono::duration<double>(now - lastFrame_).count(), 0.0, 0.1);
    lastFrame_ = now;
    const int s = cell_;

    // 本体が知らせた段階を取り込む（戻ることはない）。
    const int stage = std::clamp(shared_.stage.load(), 0, kStageCount - 1);
    if (stage > observedStage_) {
        observedStage_ = stage;
        observedStageStart_ = now;
    }
    if (!profileRead_ && shared_.userRootReady.load())
        ReadProfile();
    if (!revealStarted_ && shared_.revealRequested.load()) {
        revealStarted_ = true;
        revealStart_ = now;
        WriteProfile();
    }

    // 表示する進捗は、目標へなめらかに追いつき、戻らない。
    const double target = revealStarted_ ? 1.0 : TargetProgress(now);
    const double tau = revealStarted_ ? 0.05 : 0.18;
    shown_ = (std::max)(shown_, shown_ + (target - shown_) * (1.0 - std::exp(-dt / tau)));

    // 終わり：100% で一瞬止め、文と棒を先に消し、標志と光を黒へ沈め、黒のまま少し置いてから切り替える。
    float uiAlpha = 1.0f, darkness = 0.0f;
    if (revealStarted_) {
        const double t = std::chrono::duration<double>(now - revealStart_).count();
        uiAlpha = 1.0f - Smooth01(float((t - 0.12) / 0.18));
        const float k = std::clamp(float((t - 0.12) / 0.36), 0.0f, 1.0f);
        darkness = k * k;
        if (t >= 0.54 && !revealSignaled_) {
            revealSignaled_ = true;
            SetEvent(state_.revealEvent);
        }
        blackDrawn_ = revealSignaled_ && darkness >= 1.0f;
    }

    // 標志の周りのきらめき：同時に 2 つまで、0.6 秒で消える。
    std::vector<RECT> sparkleRects;
    if (!revealStarted_ && seconds >= nextSparkle_) {
        for (Sparkle& sparkle : sparkles_) {
            if (seconds - sparkle.born <= 0.6)
                continue;
            const int cx = (logoRect_.left + logoRect_.right) / 2, cy = (logoRect_.top + logoRect_.bottom) / 2;
            const double angle = Random01() * 6.283185;
            const double radius = (100.0 + Random01() * 73.0) * s;
            const int x = (int(cx + std::cos(angle) * radius) / s) * s, y = (int(cy + std::sin(angle) * radius) / s) * s;
            if (y < logoRect_.bottom - 6 * s && x > 3 * s && y > 3 * s && x < width_ - 3 * s) {
                sparkle = {x, y, seconds};
                nextSparkle_ = seconds + 0.4 + Random01() * 0.8;
            }
            break;
        }
    }
    for (const Sparkle& sparkle : sparkles_)
        if (seconds - sparkle.born <= 0.65)
            sparkleRects.push_back({sparkle.x - 3 * s, sparkle.y - 3 * s, sparkle.x + 4 * s, sparkle.y + 4 * s});

    // 変わる所だけ下地から戻して描き直す。黒へ沈める間だけは全体。
    std::vector<RECT> dirty;
    if (revealStarted_ && darkness > 0.0f) {
        dirty.push_back({0, 0, width_, height_});
    } else {
        dirty.push_back(uiRect_);
        dirty.insert(dirty.end(), previousSparkleRects_.begin(), previousSparkleRects_.end());
        dirty.insert(dirty.end(), sparkleRects.begin(), sparkleRects.end());
    }
    for (const RECT& rect : dirty)
        RestoreRect(rect, darkness);
    previousSparkleRects_ = sparkleRects;

    const float ui = uiAlpha * (1.0f - darkness);
    DrawSparkles(seconds, 1.0f - darkness);
    if (ui > 0.003f) {
        const std::wstring status =
            (revealStarted_ ? std::wstring(kFinishText) : std::wstring(kStageText[observedStage_])) +
            std::wstring(static_cast<size_t>(int(seconds / 0.4) % 4), L'.');
        DrawGlyphs(Text(statusFont_, status), barLeft_ + 2 * s, statusTop_, kCream, true, ui);
        const int percent = revealStarted_ ? 100 : (std::min)(99, static_cast<int>(shown_ * 100.0));
        const std::wstring percentText = std::to_wstring(percent) + L"%";
        const Glyphs& percentGlyphs = Text(statusFont_, percentText);
        DrawGlyphs(percentGlyphs, barLeft_ + (barCells_ - 2 - percentGlyphs.width) * s, statusTop_ + s, kYellow, true,
                   ui);
        DrawBar(shown_, seconds, ui);
        if (firstLaunch_ && observedStage_ > 0) {
            const Glyphs& note = Text(noteFont_, kFirstLaunchNote);
            DrawGlyphs(note, (width_ - note.width * s) / 2 / s * s, noteTop_, kNote, false, ui);
        }
    }
    for (const RECT& rect : dirty)
        InvalidateRect(hwnd_, &rect, FALSE);
}

void Painter::Run() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool resources = SUCCEEDED(com) && LoadResources();
    WaitForPlan();
    const bool stopped = WaitForSingleObject(state_.stopEvent, 0) == WAIT_OBJECT_0;
    if (resources && !stopped && CreateSurface()) {
        BuildLayout();
        BuildBase();
        begin_ = lastFrame_ = observedStageStart_ = Clock::now();
        RestoreRect({0, 0, width_, height_}, 0.0f);
        Frame(Clock::now());
        // 最初の一枚を描いてから出す（白や灰色が一瞬見えないように）。
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        UpdateWindow(hwnd_);
        for (;;) {
            // 通常は 30 回／秒、黒へ溶かす間は 60 回／秒で描く。
            const DWORD interval = revealStarted_ ? 16u : 33u;
            const DWORD result = MsgWaitForMultipleObjectsEx(1, &state_.stopEvent, interval, QS_ALLINPUT,
                                                             MWMO_INPUTAVAILABLE);
            if (result == WAIT_OBJECT_0)
                break;
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            const auto now = Clock::now();
            if (std::chrono::duration<double, std::milli>(now - lastFrame_).count() >= interval - 1)
                Frame(now);
            // 持ち主が閉じ忘れても、黒になってから 5 秒で自分で閉じる。
            if (revealSignaled_ && std::chrono::duration<double>(now - revealStart_).count() > 5.0)
                break;
        }
    } else if (!stopped) {
        HIKARI_LOG_WARN("[LoadingScreen] could not create the loading window; starting without it.");
    }
    // 待っている本体を必ず起こす。
    SetEvent(state_.revealEvent);
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
    if (previousSurface_)
        SelectObject(memoryDc_, previousSurface_);
    if (surface_)
        DeleteObject(surface_);
    if (memoryDc_)
        DeleteDC(memoryDc_);
    if (statusFont_)
        DeleteObject(statusFont_);
    if (noteFont_)
        DeleteObject(noteFont_);
    if (statusFontResource_)
        RemoveFontMemResourceEx(statusFontResource_);
    if (noteFontResource_)
        RemoveFontMemResourceEx(noteFontResource_);
    UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
    if (SUCCEEDED(com))
        CoUninitialize();
}
} // namespace

LoadingScreen::LoadingScreen() : state_(std::make_unique<State>()) {}
LoadingScreen::~LoadingScreen() { Stop(); }

bool LoadingScreen::Start() {
    Stop();
    if (!state_)
        state_ = std::make_unique<State>();
    state_->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    state_->planEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    state_->revealEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!state_->stopEvent || !state_->planEvent || !state_->revealEvent) {
        Stop();
        return false;
    }
    state_->shared.stageStart[0] = Clock::now();
    try {
        State* state = state_.get();
        state_->worker = std::thread([state] {
            Painter painter(*state);
            painter.Run();
        });
    } catch (...) {
        Stop();
        return false;
    }
    gActive = state_.get();
    return true;
}

void LoadingScreen::Stop() {
    if (!state_)
        return;
    if (gActive.load() == state_.get())
        gActive = nullptr;
    if (state_->stopEvent)
        SetEvent(state_->stopEvent);
    if (state_->worker.joinable()) {
        // 窓のスレッドは止まる理由が無いが、万一戻らなくても本体を止めない。
        if (WaitForSingleObject(state_->worker.native_handle(), 3000) == WAIT_OBJECT_0) {
            state_->worker.join();
        } else {
            HIKARI_LOG_WARN("[LoadingScreen] the loading thread did not finish; leaving it behind.");
            state_->worker.detach();
            (void)state_.release();
            return;
        }
    }
    for (HANDLE* handle : {&state_->stopEvent, &state_->planEvent, &state_->revealEvent}) {
        if (*handle)
            CloseHandle(*handle);
        *handle = nullptr;
    }
}

void ReportStartupWindowPlan(bool coverMonitor, int clientWidth, int clientHeight, const std::wstring& title) {
    LoadingScreen::State* state = gActive.load();
    if (!state)
        return;
    {
        std::scoped_lock lock(state->shared.mutex);
        state->shared.planReady = true;
        state->shared.coverMonitor = coverMonitor;
        state->shared.clientWidth = clientWidth;
        state->shared.clientHeight = clientHeight;
        state->shared.title = title;
    }
    SetEvent(state->planEvent);
}

void ReportStartupUserRoot(const std::filesystem::path& root) {
    LoadingScreen::State* state = gActive.load();
    if (!state)
        return;
    {
        std::scoped_lock lock(state->shared.mutex);
        state->shared.userRoot = root;
    }
    state->shared.userRootReady = true;
}

void ReportStartupStage(StartupStage stage) {
    LoadingScreen::State* state = gActive.load();
    if (!state)
        return;
    const int index = static_cast<int>(stage);
    if (index <= state->shared.stage.load() || index >= kStageCount)
        return;
    {
        std::scoped_lock lock(state->shared.mutex);
        state->shared.stageStart[index] = Clock::now();
    }
    state->shared.stage = index;
}

void WaitForStartupReveal(unsigned timeoutMs) {
    LoadingScreen::State* state = gActive.load();
    if (!state)
        return;
    if (!state->shared.revealRequested.load()) {
        {
            std::scoped_lock lock(state->shared.mutex);
            state->shared.stageStart[kStageCount] = Clock::now();
        }
        state->shared.revealRequested = true;
    }
    (void)WaitForSingleObject(state->revealEvent, timeoutMs);
}

} // namespace HIKARI::PLATFORM
