#include "Gfx/HIKARI_ShaderCompiler.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include <d3dcompiler.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"

#pragma comment(lib, "d3dcompiler.lib")

namespace HIKARI::GFX {

    using Microsoft::WRL::ComPtr;

    namespace {
        struct DxcRuntime {
            bool attempted = false;
            bool ready = false;
            HMODULE dxilModule = nullptr;
            HMODULE compilerModule = nullptr;
            DxcCreateInstanceProc createInstance = nullptr;
            ComPtr<IDxcUtils> utils;
            ComPtr<IDxcCompiler3> compiler;
            ComPtr<IDxcIncludeHandler> includeHandler;
            std::wstring loadedPath{};
            std::string lastError{};
        };

        DxcRuntime gDxc{};

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
                OutputDebugStringA(message.c_str());
                if (message.back() != '\n') {
                    OutputDebugStringA("\n");
                }
                DEBUGLOG::PushRenderError(message);
                HIKARI_LOG_ERROR(message);
            }
        }

        bool TryLoadDxcFromDirectory(const std::filesystem::path& directory) {
            const std::filesystem::path dxilPath = directory / L"dxil.dll";
            const std::filesystem::path compilerPath = directory / L"dxcompiler.dll";
            if (!std::filesystem::exists(compilerPath)) {
                return false;
            }

            if (std::filesystem::exists(dxilPath)) {
                gDxc.dxilModule = LoadLibraryW(dxilPath.c_str());
            }
            gDxc.compilerModule = LoadLibraryW(compilerPath.c_str());
            if (gDxc.compilerModule == nullptr) {
                if (gDxc.dxilModule != nullptr) {
                    FreeLibrary(gDxc.dxilModule);
                    gDxc.dxilModule = nullptr;
                }
                return false;
            }
            gDxc.loadedPath = compilerPath.wstring();
            return true;
        }

        bool TryLoadDxcDefault() {
            gDxc.dxilModule = LoadLibraryW(L"dxil.dll");
            gDxc.compilerModule = LoadLibraryW(L"dxcompiler.dll");
            if (gDxc.compilerModule == nullptr) {
                if (gDxc.dxilModule != nullptr) {
                    FreeLibrary(gDxc.dxilModule);
                    gDxc.dxilModule = nullptr;
                }
                return false;
            }
            gDxc.loadedPath = L"dxcompiler.dll";
            return true;
        }

        bool TryLoadDxcFromWindowsKits() {
            const std::filesystem::path kitsBin = L"C:\\Program Files (x86)\\Windows Kits\\10\\bin";
            if (!std::filesystem::exists(kitsBin)) {
                return false;
            }

            std::vector<std::filesystem::path> candidateDirs;
            for (const auto& entry : std::filesystem::directory_iterator(kitsBin)) {
                if (!entry.is_directory()) {
                    continue;
                }
                const std::filesystem::path x64Dir = entry.path() / L"x64";
                if (std::filesystem::exists(x64Dir / L"dxcompiler.dll")) {
                    candidateDirs.push_back(x64Dir);
                }
            }
            std::sort(candidateDirs.begin(), candidateDirs.end(), std::greater<std::filesystem::path>{});
            for (const std::filesystem::path& dir : candidateDirs) {
                if (TryLoadDxcFromDirectory(dir)) {
                    return true;
                }
            }
            return false;
        }

        bool EnsureDxcRuntime() {
            if (gDxc.ready) {
                return true;
            }
            if (gDxc.attempted) {
                return false;
            }
            gDxc.attempted = true;

            if (!TryLoadDxcDefault() && !TryLoadDxcFromWindowsKits()) {
                gDxc.lastError = "[ShaderCompiler][ERROR] dxcompiler.dll was not found. Install/copy DXC runtime before using Shader Model 6.";
                ReportCompileError(gDxc.lastError);
                return false;
            }

            gDxc.createInstance = reinterpret_cast<DxcCreateInstanceProc>(
                GetProcAddress(gDxc.compilerModule, "DxcCreateInstance"));
            if (gDxc.createInstance == nullptr) {
                gDxc.lastError = "[ShaderCompiler][ERROR] DxcCreateInstance was not found in dxcompiler.dll.";
                ReportCompileError(gDxc.lastError);
                return false;
            }

            HRESULT hr = gDxc.createInstance(CLSID_DxcUtils, IID_PPV_ARGS(gDxc.utils.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "ShaderCompiler::Create DxcUtils")) {
                return false;
            }
            hr = gDxc.createInstance(CLSID_DxcCompiler, IID_PPV_ARGS(gDxc.compiler.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "ShaderCompiler::Create DxcCompiler")) {
                return false;
            }
            hr = gDxc.utils->CreateDefaultIncludeHandler(gDxc.includeHandler.GetAddressOf());
            if (!HIKARI_DX_CHECK(hr, "ShaderCompiler::Create IncludeHandler")) {
                return false;
            }

            gDxc.ready = true;
            HIKARI_LOG_INFO("[ShaderCompiler] DXC runtime loaded: " + NarrowUtf8(gDxc.loadedPath.c_str()));
            return true;
        }

        void AppendCommonArgs(
            std::vector<LPCWSTR>& args,
            std::vector<std::wstring>& owned,
            const wchar_t* sourceName,
            const char* entry,
            const wchar_t* profile,
            const wchar_t* filePath) {

            const std::wstring entryW = WidenAscii(entry);
            owned.push_back(sourceName != nullptr ? std::wstring(sourceName) : L"HIKARI_RuntimeShader");
            owned.push_back(entryW);
            owned.push_back(profile != nullptr ? std::wstring(profile) : L"ps_6_0");
            owned.push_back(L"HIKARI/Shaders");
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

        bool CompileDxcBuffer(
            const DxcBuffer& source,
            const wchar_t* sourceName,
            const char* entry,
            const wchar_t* profile,
            const wchar_t* filePath,
            ID3DBlob** outBlob) {

            if (outBlob == nullptr) {
                return false;
            }
            *outBlob = nullptr;
            if (!EnsureDxcRuntime()) {
                return false;
            }

            std::vector<LPCWSTR> args;
            std::vector<std::wstring> owned;
            owned.reserve(5);
            args.reserve(16);
            AppendCommonArgs(args, owned, sourceName, entry, profile, filePath);

            ComPtr<IDxcResult> result;
            HRESULT hr = gDxc.compiler->Compile(
                &source,
                args.data(),
                static_cast<UINT32>(args.size()),
                gDxc.includeHandler.Get(),
                IID_PPV_ARGS(result.GetAddressOf()));
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
                oss << "[ShaderCompiler][ERROR] SM6 compile failed. source="
                    << NarrowUtf8(sourceName)
                    << " entry=" << (entry ? entry : "")
                    << " profile=" << NarrowUtf8(profile)
                    << "\n" << errorText;
                ReportCompileError(oss.str());
                return false;
            }

            if (!errorText.empty()) {
                OutputDebugStringA(errorText.c_str());
            }

            ComPtr<IDxcBlob> shaderBlob;
            hr = result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(shaderBlob.GetAddressOf()), nullptr);
            if (!HIKARI_DX_CHECK(hr, "ShaderCompiler::GetObject")) {
                return false;
            }
            return CopyDxcBlobToD3DBlob(shaderBlob.Get(), outBlob);
        }
    }

    const wchar_t* ShaderModel6Profile(ShaderStage stage) {
        switch (stage) {
        case ShaderStage::Vertex: return L"vs_6_0";
        case ShaderStage::Compute: return L"cs_6_0";
        case ShaderStage::Amplification: return L"as_6_5";
        case ShaderStage::Mesh: return L"ms_6_5";
        case ShaderStage::Pixel:
        default: return L"ps_6_0";
        }
    }

    const wchar_t* UpgradeToShaderModel6Profile(const char* legacyProfile) {
        if (legacyProfile == nullptr) {
            return L"ps_6_0";
        }
        if (legacyProfile[0] == 'v' && legacyProfile[1] == 's') {
            return L"vs_6_0";
        }
        if (legacyProfile[0] == 'c' && legacyProfile[1] == 's') {
            return L"cs_6_0";
        }
        return L"ps_6_0";
    }

    bool SupportsShaderModel6(ID3D12Device* device) {
        return SupportsShaderModel(device, D3D_SHADER_MODEL_6_0);
    }

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

    bool CompileShaderFileSm6(
        const wchar_t* path,
        const char* entry,
        const wchar_t* profile,
        ID3DBlob** outBlob) {

        if (path == nullptr) {
            return false;
        }
        if (!EnsureDxcRuntime()) {
            return false;
        }

        ComPtr<IDxcBlobEncoding> sourceBlob;
        HRESULT hr = gDxc.utils->LoadFile(path, nullptr, sourceBlob.GetAddressOf());
        if (!HIKARI_DX_CHECK(hr, "ShaderCompiler::LoadFile")) {
            std::ostringstream oss;
            oss << "[ShaderCompiler][ERROR] Failed to load shader file: " << NarrowUtf8(path);
            ReportCompileError(oss.str());
            return false;
        }

        DxcBuffer source{};
        source.Ptr = sourceBlob->GetBufferPointer();
        source.Size = sourceBlob->GetBufferSize();
        source.Encoding = DXC_CP_UTF8;
        return CompileDxcBuffer(source, path, entry, profile, path, outBlob);
    }

    bool CompileShaderFileSm6(
        const wchar_t* path,
        const char* entry,
        ShaderStage stage,
        ID3DBlob** outBlob) {

        return CompileShaderFileSm6(path, entry, ShaderModel6Profile(stage), outBlob);
    }

    bool CompileShaderSourceSm6(
        const char* source,
        size_t sourceBytes,
        const wchar_t* virtualName,
        const char* entry,
        const wchar_t* profile,
        ID3DBlob** outBlob) {

        if (source == nullptr || sourceBytes == 0) {
            return false;
        }

        DxcBuffer buffer{};
        buffer.Ptr = source;
        buffer.Size = sourceBytes;
        buffer.Encoding = DXC_CP_UTF8;
        return CompileDxcBuffer(buffer, virtualName, entry, profile, nullptr, outBlob);
    }

    bool CompileShaderSourceSm6(
        const char* source,
        size_t sourceBytes,
        const wchar_t* virtualName,
        const char* entry,
        ShaderStage stage,
        ID3DBlob** outBlob) {

        return CompileShaderSourceSm6(source, sourceBytes, virtualName, entry, ShaderModel6Profile(stage), outBlob);
    }

} // namespace HIKARI::GFX
