#include "HIKARI_DXCheck.h"

#include <Windows.h>
#include <iomanip>
#include <sstream>
#include "Diagnostics/HIKARI_DebugLogBuffer.h"

namespace HIKARI::GFX {
	// HRESULT を 16 進数の文字列にフォーマットする
    std::string FormatHRESULT(HRESULT hr) {
        std::ostringstream oss;
        oss << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
            << static_cast<unsigned long>(hr);
        return oss.str();
    }
	// HRESULT をシステムから取得したエラーメッセージに変換する
    std::string HResultToString(HRESULT hr) {
        LPSTR messageBuffer = nullptr;
        const DWORD size = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            static_cast<DWORD>(hr),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPSTR>(&messageBuffer),
            0,
            nullptr);

        if (size == 0 || messageBuffer == nullptr) {
            return "Unknown HRESULT";
        }

        std::string message(messageBuffer, size);
        LocalFree(messageBuffer);
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
            message.pop_back();
        }
        return message;
    }
	// HRESULT をチェックし、失敗していたらログに出力する。expr にはチェックした式の文字列を、message には任意の追加メッセージを指定できる。
    bool CheckHRESULT(HRESULT hr, const char* expr, const char* message, const char* file, int line) {
        if (SUCCEEDED(hr)) {
            return true;
        }

        std::ostringstream oss;
        oss << "[GFX][ERROR] HRESULT failed. file=" << (file ? file : "")
            << " line=" << line
            << " expr=" << (expr ? expr : "")
            << " message=" << (message ? message : "")
            << " hr=" << FormatHRESULT(hr)
            << " text=\"" << HResultToString(hr) << "\"";
        DEBUGLOG::PushRenderError(oss.str());
        return false;
    }
	// DXGI_FORMAT を文字列に変換する
    const char* FormatToString(DXGI_FORMAT format) {
        switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
        case DXGI_FORMAT_R32_TYPELESS: return "R32_TYPELESS";
        case DXGI_FORMAT_R32_FLOAT: return "R32_FLOAT";
        case DXGI_FORMAT_D32_FLOAT: return "D32_FLOAT";
        case DXGI_FORMAT_UNKNOWN: return "UNKNOWN";
        default: return "DXGI_FORMAT_OTHER";
        }
    }
	// D3D12_RESOURCE_STATES を文字列に変換する
    const char* ResourceStateToString(D3D12_RESOURCE_STATES state) {
        if (state == D3D12_RESOURCE_STATE_COMMON) { return "COMMON"; }
        if (state == D3D12_RESOURCE_STATE_RENDER_TARGET) { return "RENDER_TARGET"; }
        if (state == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) { return "PIXEL_SHADER_RESOURCE"; }
        if (state == D3D12_RESOURCE_STATE_DEPTH_WRITE) { return "DEPTH_WRITE"; }
        if (state == D3D12_RESOURCE_STATE_PRESENT) { return "PRESENT"; }
        if (state == D3D12_RESOURCE_STATE_GENERIC_READ) { return "GENERIC_READ"; }
        return "D3D12_RESOURCE_STATE_OTHER";
    }
	// D3D12 オブジェクトに名前を設定する。デバッグビルドでのみ有効で、リリースビルドでは引数を無視する。
    void SetD3D12Name(ID3D12Object* object, const wchar_t* name) {
#if defined(_DEBUG)
        if (object && name) {
            object->SetName(name);
        }
#else
        (void)object;
        (void)name;
#endif
    }

    std::wstring Widen(const std::string& text) {
        if (text.empty()) {
            return {};
        }
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        if (length <= 0) {
            return {};
        }
        std::wstring result(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), length);
        if (!result.empty() && result.back() == L'\0') {
            result.pop_back();
        }
        return result;
    }
}
