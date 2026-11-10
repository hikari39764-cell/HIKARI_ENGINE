#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Core/HIKARI_StartupTimer.h"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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
#include "Core/Paths/HIKARI_UserDataPath.h"

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

// 単体ゲームでは、実行時に原文から作ったシェーダーを利用者フォルダーに残し、次の起動では読み直すだけにする
// （原文の翻訳と dxcompiler の読み込みを省く）。引擎に同梱の原文と翻訳器は実行ファイルと一緒にしか変わらないので、
// 実行ファイルの大きさと更新時刻を鍵に含める。モジュールの着色器は実行ファイルと別に入れ替わるので、
// 同じフォルダの原文（取り込まれる .hlsli を含む）の大きさと更新時刻も鍵に含める。
// 原文を直す開発中のビルドでは使わない（出荷ビルドだけ）。
uint64_t Fnv1a(const void* data, size_t size, uint64_t hash = 1469598103934665603ull) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
}

const std::string& ExecutableStamp() {
    static const std::string stamp = [] {
        wchar_t buffer[MAX_PATH * 2]{};
        const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
        std::error_code error{};
        const std::filesystem::path exe(std::wstring(buffer, length));
        const auto size = std::filesystem::file_size(exe, error);
        const auto time = std::filesystem::last_write_time(exe, error).time_since_epoch().count();
        return std::to_string(error ? 0 : size) + ":" + std::to_string(time);
    }();
    return stamp;
}

// 引擎の着色器フォルダの外にある原文（モジュールの着色器）：そのフォルダの .hlsl / .hlsli 全部の印。
std::wstring SiblingSourceStamp(const std::filesystem::path& source) {
    std::vector<std::wstring> entries{};
    std::error_code error{};
    for (std::filesystem::recursive_directory_iterator it(source.parent_path(), error), end; !error && it != end;
         it.increment(error)) {
        const std::filesystem::path extension = it->path().extension();
        std::error_code entryError{};
        if ((extension == L".hlsl" || extension == L".hlsli") && it->is_regular_file(entryError)) {
            entries.push_back(it->path().generic_wstring() + L":" + std::to_wstring(it->file_size(entryError)) + L":" +
                              std::to_wstring(it->last_write_time(entryError).time_since_epoch().count()));
        }
    }
    std::sort(entries.begin(), entries.end());
    std::wstring stamp{};
    for (const std::wstring& entry : entries) {
        stamp += entry + L"\n";
    }
    return stamp;
}

std::filesystem::path RuntimeShaderCacheDirectory() {
#if defined(HIKARI_SHIPPING)
    const std::filesystem::path root = PLATFORM::GetHikariRuntimeUserRoot();
    return root.empty() ? std::filesystem::path{} : root / "Cache" / "Shaders";
#else
    return {};
#endif
}

uint64_t RuntimeShaderKey(const void* source, size_t sourceBytes, const wchar_t* name, const char* entry,
                          const wchar_t* profile) {
    uint64_t key = Fnv1a(source, sourceBytes);
    const std::wstring nameText = name != nullptr ? name : L"";
    const std::string entryText = entry != nullptr ? entry : "";
    const std::wstring profileText = profile != nullptr ? profile : L"";
    key = Fnv1a(nameText.data(), nameText.size() * sizeof(wchar_t), key);
    key = Fnv1a(entryText.data(), entryText.size(), key);
    key = Fnv1a(profileText.data(), profileText.size() * sizeof(wchar_t), key);
    key = Fnv1a(ExecutableStamp().data(), ExecutableStamp().size(), key);
#if defined(_DEBUG)
    key = Fnv1a("debug", 5, key);
#endif
    return key;
}

struct RuntimeShaderCacheHeader {
    uint32_t magic = 0x31435348u; // "HSC1"
    uint32_t size = 0;
    uint64_t key = 0;
    uint64_t checksum = 0;
};

std::filesystem::path RuntimeShaderCachePath(const std::filesystem::path& directory, uint64_t key) {
    char name[32]{};
    std::snprintf(name, sizeof(name), "%016llx.dxil", static_cast<unsigned long long>(key));
    return directory / name;
}

bool LoadRuntimeShaderCache(uint64_t key, ID3DBlob** outBlob) {
    const std::filesystem::path directory = RuntimeShaderCacheDirectory();
    if (directory.empty())
        return false;
    std::ifstream in(RuntimeShaderCachePath(directory, key), std::ios::binary);
    RuntimeShaderCacheHeader header{};
    if (!in.read(reinterpret_cast<char*>(&header), sizeof(header)) || header.magic != RuntimeShaderCacheHeader{}.magic ||
        header.key != key || header.size == 0 || header.size > (64u << 20))
        return false;
    ComPtr<ID3DBlob> blob;
    if (FAILED(D3DCreateBlob(header.size, blob.GetAddressOf())) ||
        !in.read(static_cast<char*>(blob->GetBufferPointer()), header.size) ||
        Fnv1a(blob->GetBufferPointer(), header.size) != header.checksum)
        return false;
    *outBlob = blob.Detach();
    return true;
}

void StoreRuntimeShaderCache(uint64_t key, ID3DBlob* blob) {
    const std::filesystem::path directory = RuntimeShaderCacheDirectory();
    if (directory.empty() || blob == nullptr || blob->GetBufferSize() == 0)
        return;
    std::error_code error{};
    std::filesystem::create_directories(directory, error);
    RuntimeShaderCacheHeader header{};
    header.size = static_cast<uint32_t>(blob->GetBufferSize());
    header.key = key;
    header.checksum = Fnv1a(blob->GetBufferPointer(), blob->GetBufferSize());
    // 別のスレッドが同じ鍵を書いても壊れないよう、一時名で書いてから置き換える。
    const std::filesystem::path path = RuntimeShaderCachePath(directory, key);
    std::filesystem::path temporary = path;
    temporary += L"." + std::to_wstring(GetCurrentThreadId()) + L".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        out.write(static_cast<const char*>(blob->GetBufferPointer()), header.size);
        if (!out)
            return;
    }
    std::filesystem::rename(temporary, path, error);
    if (error)
        std::filesystem::remove(temporary, error);
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
    if (outBlob == nullptr)
        return false;
    *outBlob = nullptr;
    // 原文が前回と同じなら、残しておいた結果を使う。
    uint64_t cacheKey = 0;
    const bool cacheable = !RuntimeShaderCacheDirectory().empty();
    if (cacheable) {
        const std::filesystem::path source = ResolveShaderSourcePath(path);
        std::ifstream file(source, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (!text.empty()) {
            if (!IsEngineShaderLogicalPath(std::filesystem::path(path).lexically_normal())) {
                const std::wstring siblings = SiblingSourceStamp(source);
                text.append(reinterpret_cast<const char*>(siblings.data()), siblings.size() * sizeof(wchar_t));
            }
            cacheKey = RuntimeShaderKey(text.data(), text.size(), path, entry, profile);
            if (LoadRuntimeShaderCache(cacheKey, outBlob))
                return true;
        }
    }
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
    if (!CompileDxcBuffer(source, resolvedPath.c_str(), entry, profile, resolvedPath.c_str(), outBlob))
        return false;
    if (cacheKey != 0)
        StoreRuntimeShaderCache(cacheKey, *outBlob);
    return true;
}

bool CompileShaderFileSm6(const wchar_t* path, const char* entry, ShaderStage stage, ID3DBlob** outBlob) {
    return CompileShaderFileSm6(path, entry, ShaderModel6Profile(stage), outBlob);
}

bool CompileShaderSourceSm6(const char* source, size_t sourceBytes, const wchar_t* virtualName, const char* entry,
                            const wchar_t* profile, ID3DBlob** outBlob) {
    if (source == nullptr || sourceBytes == 0) {
        return false;
    }

    const bool cacheable = outBlob != nullptr && !RuntimeShaderCacheDirectory().empty();
    const uint64_t cacheKey = cacheable ? RuntimeShaderKey(source, sourceBytes, virtualName, entry, profile) : 0;
    if (cacheable && LoadRuntimeShaderCache(cacheKey, outBlob))
        return true;
    DxcBuffer buffer{};
    buffer.Ptr = source;
    buffer.Size = sourceBytes;
    buffer.Encoding = DXC_CP_UTF8;
    if (!CompileDxcBuffer(buffer, virtualName, entry, profile, nullptr, outBlob))
        return false;
    if (cacheable)
        StoreRuntimeShaderCache(cacheKey, *outBlob);
    return true;
}

bool CompileShaderSourceSm6(const char* source, size_t sourceBytes, const wchar_t* virtualName, const char* entry,
                            ShaderStage stage, ID3DBlob** outBlob) {
    return CompileShaderSourceSm6(source, sourceBytes, virtualName, entry, ShaderModel6Profile(stage), outBlob);
}

} // namespace HIKARI::GFX
