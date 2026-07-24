#pragma once

#include <d3d12.h>
#include <string>

namespace HIKARI::GFX {

    bool CheckHRESULT(HRESULT hr, const char* expr, const char* message, const char* file, int line);
    std::string HResultToString(HRESULT hr);
    std::string FormatHRESULT(HRESULT hr);
    const char* FormatToString(DXGI_FORMAT format);
    const char* ResourceStateToString(D3D12_RESOURCE_STATES state);
    void SetD3D12Name(ID3D12Object* object, const wchar_t* name);
    std::wstring Widen(const std::string& text);

}

#define HIKARI_DX_CHECK(hr, message) \
    HIKARI::GFX::CheckHRESULT((hr), #hr, (message), __FILE__, __LINE__)
