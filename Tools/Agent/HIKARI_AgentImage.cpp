#include "HIKARI_AgentImage.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace HIKARI::AGENT_CLI {
namespace {

using Microsoft::WRL::ComPtr;

// 縮めた PNG。失敗したら空。
std::string ScalePng(const std::filesystem::path& path, uint32_t longSide) {
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return {};
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    UINT width = 0u;
    UINT height = 0u;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                  &decoder)) ||
        FAILED(decoder->GetFrame(0u, &frame)) || FAILED(frame->GetSize(&width, &height)) ||
        (std::max)(width, height) <= longSide)
        return {};
    const double scale = static_cast<double>(longSide) / static_cast<double>((std::max)(width, height));
    const UINT scaledWidth = (std::max)(1u, static_cast<UINT>(std::lround(width * scale)));
    const UINT scaledHeight = (std::max)(1u, static_cast<UINT>(std::lround(height * scale)));
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(frame.Get(), scaledWidth, scaledHeight, WICBitmapInterpolationModeFant)) ||
        FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom)))
        return {};
    ComPtr<IStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> output;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&output, nullptr)) || FAILED(output->Initialize(nullptr)) ||
        FAILED(output->SetSize(scaledWidth, scaledHeight)) || FAILED(output->SetPixelFormat(&format)) ||
        FAILED(output->WriteSource(converter.Get(), nullptr)) || FAILED(output->Commit()) || FAILED(encoder->Commit()))
        return {};
    STATSTG stat{};
    HGLOBAL memory = nullptr;
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || FAILED(GetHGlobalFromStream(stream.Get(), &memory)))
        return {};
    const void* bytes = GlobalLock(memory);
    if (bytes == nullptr)
        return {};
    std::string png(static_cast<size_t>(stat.cbSize.QuadPart), '\0');
    std::memcpy(png.data(), bytes, png.size());
    GlobalUnlock(memory);
    return png;
}

} // namespace

std::string ReadImageForModel(const std::filesystem::path& path, uint32_t longSide) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::string scaled = ScalePng(path, longSide);
    if (SUCCEEDED(com))
        CoUninitialize();
    if (!scaled.empty())
        return scaled;
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

} // namespace HIKARI::AGENT_CLI
