#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Core/HIKARI_StartupTimer.h"

#include <Windows.h>

#include <cstring>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <d3dcompiler.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/Internal/HIKARI_DxcRuntime.h"

#pragma comment(lib, "d3dcompiler.lib")

namespace HIKARI::GFX {

using Microsoft::WRL::ComPtr;

namespace {
bool IsEngineShaderLogicalPath(const std::filesystem::path& path) {
    const std::filesystem::path normalized = path.lexically_normal();
    auto part = normalized.begin();
    if (part == normalized.end() || *part != L"HIKARI") {
        return false;
    }
    ++part;
    return part != normalized.end() && *part == L"Shaders";
}

std::filesystem::path MakeEngineContentRelativePath(const std::filesystem::path& logicalPath) {
    std::filesystem::path relative{};
    const std::filesystem::path normalized = logicalPath.lexically_normal();
    auto part = normalized.begin();
    const auto end = normalized.end();
    if (part != end && *part == L"HIKARI") {
        ++part;
    }
    for (; part != end; ++part) {
        relative /= *part;
    }
    return relative;
}

std::wstring WidenAscii(const char* text) {
    std::wstring out;
    if (text == nullptr) {
        return out;
    }
    while (*text != '\0') {
        out.push_back(static_cast<wchar_t>(*text));
        ++text;
    }
    return out;
}

std::string NarrowUtf8(const wchar_t* text) {
    if (text == nullptr || *text == L'\0') {
        return {};
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) {
        return {};
    }
    std::string out(static_cast<size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), bytes, nullptr, nullptr);
    return out;
}

std::string BlobToString(IDxcBlobUtf8* blob) {
    if (blob == nullptr || blob->GetStringLength() == 0) {
        return {};
    }
    return std::string(blob->GetStringPointer(), blob->GetStringPointer() + blob->GetStringLength());
}

void ReportCompileError(const std::string& message) {
    if (!message.empty()) {
        DEBUGLOG::PushRenderError(message);
    }
}

void AppendCommonArgs(std::vector<LPCWSTR>& args, std::vector<std::wstring>& owned, const wchar_t* sourceName,
                      const char* entry, const wchar_t* profile, const wchar_t* filePath) {
    const std::wstring entryW = WidenAscii(entry);
    owned.push_back(sourceName != nullptr ? std::wstring(sourceName) : L"HIKARI_RuntimeShader");
    owned.push_back(entryW);
    owned.push_back(profile != nullptr ? std::wstring(profile) : L"ps_6_0");
    owned.push_back(ResolveShaderSourcePath(L"HIKARI/Shaders").wstring());
    if (filePath != nullptr) {
        const std::filesystem::path sourcePath(filePath);
        if (sourcePath.has_parent_path()) {
            owned.push_back(sourcePath.parent_path().wstring());
        }
    }

    args.push_back(owned[0].c_str());
    args.push_back(L"-E");
    args.push_back(owned[1].c_str());
    args.push_back(L"-T");
    args.push_back(owned[2].c_str());
    args.push_back(L"-HV");
    args.push_back(L"2021");
    args.push_back(L"-I");
    args.push_back(owned[3].c_str());
    if (owned.size() > 4) {
        args.push_back(L"-I");
        args.push_back(owned[4].c_str());
    }
#if defined(_DEBUG)
    args.push_back(DXC_ARG_DEBUG);
    args.push_back(L"-Qembed_debug");
#endif
    // Runtime PSOs must use production-grade DXIL in every C++ build.
    // Unoptimized mesh/amplification shaders can exceed driver-side
    // compilation budgets even though their source is valid. Debug
    // builds retain embedded symbols for PIX and diagnostics.
    args.push_back(DXC_ARG_OPTIMIZATION_LEVEL3);
}

bool CopyDxcBlobToD3DBlob(IDxcBlob* shaderBlob, ID3DBlob** outBlob) {
    if (shaderBlob == nullptr || outBlob == nullptr) {
        return false;
    }
    *outBlob = nullptr;
    ComPtr<ID3DBlob> compatBlob;
    HRESULT hr = D3DCreateBlob(shaderBlob->GetBufferSize(), compatBlob.GetAddressOf());
    if (!HIKARI_DX_CHECK(hr, "ShaderCompiler::Create D3DBlob")) {
        return false;
    }
    std::memcpy(compatBlob->GetBufferPointer(), shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());
    *outBlob = compatBlob.Detach();
    return true;
}

bool CompileDxcBuffer(const DxcBuffer& source, const wchar_t* sourceName, const char* entry, const wchar_t* profile,
                      const wchar_t* filePath, ID3DBlob** outBlob) {
    if (outBlob == nullptr) {
        return false;
    }
    *outBlob = nullptr;
    INTERNAL::DxcRuntimeSnapshot runtime{};
    std::string runtimeError{};
    if (!INTERNAL::AcquireDxcRuntime(INTERNAL::DxcRuntimePurpose::RuntimeSourceFallback, runtime, runtimeError)) {
        ReportCompileError("[ShaderCompiler][ERROR] " + runtimeError);
        return false;
    }

    std::vector<LPCWSTR> args;
    std::vector<std::wstring> owned;
    owned.reserve(5);
    args.reserve(16);
    AppendCommonArgs(args, owned, sourceName, entry, profile, filePath);

    std::scoped_lock invocationLock(INTERNAL::GetDxcInvocationMutex());
    ComPtr<IDxcResult> result;
    HRESULT hr = runtime.compiler->Compile(&source, args.data(), static_cast<UINT32>(args.size()),
                                           runtime.defaultIncludeHandler.Get(), IID_PPV_ARGS(result.GetAddressOf()));
    if (!HIKARI_DX_CHECK(hr, "ShaderCompiler::Compile")) {
        return false;
    }

    ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(errors.GetAddressOf()), nullptr);
    std::string errorText = BlobToString(errors.Get());

    HRESULT status = S_OK;
    result->GetStatus(&status);
    if (FAILED(status)) {
        std::ostringstream oss;
        oss << "[ShaderCompiler][ERROR] SM6 compile failed. source=" << NarrowUtf8(sourceName)
            << " entry=" << (entry ? entry : "") << " profile=" << NarrowUtf8(profile) << "\n"
            << errorText;
        ReportCompileError(oss.str());
        return false;
    }

    if (!errorText.empty()) {
        HIKARI_LOG_WARN("[ShaderCompiler] warnings. source=" + NarrowUtf8(sourceName) +
                        " entry=" + (entry ? entry : "") + "\n" + errorText);
    }

    ComPtr<IDxcBlob> shaderBlob;
    hr = result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(shaderBlob.GetAddressOf()), nullptr);
    if (!HIKARI_DX_CHECK(hr, "ShaderCompiler::GetObject")) {
        return false;
    }
    return CopyDxcBlobToD3DBlob(shaderBlob.Get(), outBlob);
}
} // namespace

const wchar_t* ShaderModel6Profile(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex: return L"vs_6_0";
    case ShaderStage::Compute: return L"cs_6_0";
    case ShaderStage::Amplification: return L"as_6_5";
    case ShaderStage::Mesh: return L"ms_6_5";
    case ShaderStage::Geometry: return L"gs_6_0";
    case ShaderStage::Pixel:
    default: return L"ps_6_0";
    }
}

bool SupportsShaderModel6(ID3D12Device* device) { return SupportsShaderModel(device, D3D_SHADER_MODEL_6_0); }

bool SupportsShaderModel(ID3D12Device* device, D3D_SHADER_MODEL minimumModel) {
    if (device == nullptr) {
        return false;
    }

    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{};
    shaderModel.HighestShaderModel = minimumModel;
    HRESULT hr = device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel));
    if (FAILED(hr)) {
        return false;
    }
    return shaderModel.HighestShaderModel >= minimumModel;
}

std::filesystem::path ResolveShaderSourcePath(const std::filesystem::path& path) {
    const std::filesystem::path normalized = path.lexically_normal();
    if (normalized.is_absolute() || !IsEngineShaderLogicalPath(normalized)) {
        return normalized;
    }

    const CONTENT::EngineContentContext& content = CONTENT::GetEngineContentContext();
    if (!content.IsOpen()) {
        return normalized;
    }
    return content.Resolve(MakeEngineContentRelativePath(normalized));
}

bool CompileShaderFileSm6(const wchar_t* path, const char* entry, const wchar_t* profile, ID3DBlob** outBlob) {
    if (path == nullptr) {
        return false;
    }
    const std::string compileStage = "Shader.compile." + std::filesystem::path(path).filename().string() + "/" +
                                     (entry != nullptr ? entry : "<null>");
    CORE::StartupTimer compileTimer(compileStage);
    INTERNAL::DxcRuntimeSnapshot runtime{};
    std::string runtimeError{};
    if (!INTERNAL::AcquireDxcRuntime(INTERNAL::DxcRuntimePurpose::RuntimeSourceFallback, runtime, runtimeError)) {
        ReportCompileError("[ShaderCompiler][ERROR] " + runtimeError);
        return false;
    }

    const std::filesystem::path resolvedPath = ResolveShaderSourcePath(path);
    if (resolvedPath.empty()) {
        return false;
    }

    ComPtr<IDxcBlobEncoding> sourceBlob;
    HRESULT hr = runtime.utils->LoadFile(resolvedPath.c_str(), nullptr, sourceBlob.GetAddressOf());
    if (!HIKARI_DX_CHECK(hr, "ShaderCompiler::LoadFile")) {
        std::ostringstream oss;
        oss << "[ShaderCompiler][ERROR] Failed to load shader file: " << resolvedPath.generic_string();
        ReportCompileError(oss.str());
        return false;
    }

    DxcBuffer source{};
    source.Ptr = sourceBlob->GetBufferPointer();
    source.Size = sourceBlob->GetBufferSize();
    source.Encoding = DXC_CP_UTF8;
    return CompileDxcBuffer(source, resolvedPath.c_str(), entry, profile, resolvedPath.c_str(), outBlob);
}

bool CompileShaderFileSm6(const wchar_t* path, const char* entry, ShaderStage stage, ID3DBlob** outBlob) {
    return CompileShaderFileSm6(path, entry, ShaderModel6Profile(stage), outBlob);
}

bool CompileShaderSourceSm6(const char* source, size_t sourceBytes, const wchar_t* virtualName, const char* entry,
                            const wchar_t* profile, ID3DBlob** outBlob) {
    if (source == nullptr || sourceBytes == 0) {
        return false;
    }

    DxcBuffer buffer{};
    buffer.Ptr = source;
    buffer.Size = sourceBytes;
    buffer.Encoding = DXC_CP_UTF8;
    return CompileDxcBuffer(buffer, virtualName, entry, profile, nullptr, outBlob);
}

bool CompileShaderSourceSm6(const char* source, size_t sourceBytes, const wchar_t* virtualName, const char* entry,
                            ShaderStage stage, ID3DBlob** outBlob) {
    return CompileShaderSourceSm6(source, sourceBytes, virtualName, entry, ShaderModel6Profile(stage), outBlob);
}

} // namespace HIKARI::GFX
