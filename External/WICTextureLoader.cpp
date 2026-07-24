#include "WICTextureLoader.h"

#include <wrl.h>
#include <wincodec.h>
#include <d3dx12.h>
#include <vector>
#include <cassert>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace {

    void LogHR(const char* where, HRESULT hr)
    {
        char buf[256];
        sprintf_s(buf, "[WIC] %s failed. hr=0x%08X\n", where, (unsigned)hr);
        OutputDebugStringA(buf);
    }

    ComPtr<IWICImagingFactory> g_wicFactory;
    static std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> g_uploadKeepAlive;
    IWICImagingFactory* GetWIC()
    {
        if (g_wicFactory) {
            return g_wicFactory.Get();
        }



        HRESULT hr = CoCreateInstance(
            CLSID_WICImagingFactory2, nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&g_wicFactory));

        if (FAILED(hr)) {

            LogHR("CoCreateInstance(CLSID_WICImagingFactory2)", hr);
            hr = CoCreateInstance(
                CLSID_WICImagingFactory, nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&g_wicFactory));
            if (FAILED(hr)) {
                LogHR("CoCreateInstance(CLSID_WICImagingFactory)", hr);
                g_wicFactory.Reset();
                return nullptr;
            }
        }

        OutputDebugStringA("[WIC] ImagingFactory created.\n");
        return g_wicFactory.Get();
    }

} // anonymous

namespace HIKARI {
    namespace DXTEX {

        /// ***簡易版***
        /// UPLOAD ヒープ上に RowMajor のテクスチャを作り、CPU から memcpy するだけ。
        HRESULT CreateWICTextureFromFile(
            ID3D12Device* device,
            ID3D12GraphicsCommandList* cmdList,
            const wchar_t* fileName,
            ID3D12Resource** texture)
        {
            if (!device || !cmdList || !fileName || !texture) {
                return E_INVALIDARG;
            }
            *texture = nullptr;

            // ===== WIC 工厂 =====
            auto* factory = GetWIC();
            if (!factory) return E_FAIL;

            // ===== 解码 =====
            ComPtr<IWICBitmapDecoder> decoder;
            HRESULT hr = factory->CreateDecoderFromFilename(
                fileName, nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnLoad,
                &decoder);
            if (FAILED(hr)) {
                LogHR("CreateDecoderFromFilename", hr);
                return hr;
            }

            ComPtr<IWICBitmapFrameDecode> frame;
            hr = decoder->GetFrame(0, &frame);
            if (FAILED(hr)) {
                LogHR("GetFrame", hr);
                return hr;
            }

            UINT w = 0, h = 0;
            frame->GetSize(&w, &h);

            // 转为 RGBA
            ComPtr<IWICFormatConverter> converter;
            hr = factory->CreateFormatConverter(&converter);
            if (FAILED(hr)) {
                LogHR("CreateFormatConverter", hr);
                return hr;
            }

            hr = converter->Initialize(
                frame.Get(),
                GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone,
                nullptr, 0.0f,
                WICBitmapPaletteTypeCustom);
            if (FAILED(hr)) {
                LogHR("FormatConverter::Initialize", hr);
                return hr;
            }

            const UINT stride = w * 4;
            const UINT imageSize = stride * h;
            std::vector<BYTE> pixels(imageSize);
            hr = converter->CopyPixels(nullptr, stride, imageSize, pixels.data());
            if (FAILED(hr)) {
                LogHR("CopyPixels", hr);
                return hr;
            }

            // ===== GPU 纹理=====
         
   
            CD3DX12_RESOURCE_DESC texDesc =
                CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, w, h);

            CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT); // 先放到局部变量

            ComPtr<ID3D12Resource> tex;
            hr = device->CreateCommittedResource(
                &defaultHeapProps,                //  对 lvalue 取地址
                D3D12_HEAP_FLAG_NONE,
                &texDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&tex));
            if (FAILED(hr)) {
                LogHR("CreateCommittedResource(DEFAULT Texture)", hr);
                return hr;
            }

            // ===== Upload Buffer =====
            UINT64 uploadSize = GetRequiredIntermediateSize(tex.Get(), 0, 1);

            CD3DX12_RESOURCE_DESC uploadDesc =
                CD3DX12_RESOURCE_DESC::Buffer(uploadSize);           // desc 局部变量
            CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD); // heap 局部变量

            ComPtr<ID3D12Resource> upload;
            hr = device->CreateCommittedResource(
                &uploadHeapProps,                 // 对 lvalue 取地址
                D3D12_HEAP_FLAG_NONE,
                &uploadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&upload));
            if (FAILED(hr)) {
                LogHR("CreateCommittedResource(UPLOAD Texture)", hr);
                return hr;
            }

            // =====  把 CPU 像素写进 Upload Buffer，再拷贝到 GPU 纹理 =====
            D3D12_SUBRESOURCE_DATA sub{};
            sub.pData = pixels.data();
            sub.RowPitch = stride;
            sub.SlicePitch = imageSize;

            UpdateSubresources(cmdList, tex.Get(), upload.Get(), 0, 0, 1, &sub);
            g_uploadKeepAlive.push_back(upload);
            CD3DX12_RESOURCE_BARRIER barrier =
                CD3DX12_RESOURCE_BARRIER::Transition(
                    tex.Get(),
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

            cmdList->ResourceBarrier(1, &barrier);


            *texture = tex.Detach();
            OutputDebugStringA("[WIC] Texture created OK.\n");
            return S_OK;
        }

        void CleanupWICResources()
        {
            g_uploadKeepAlive.clear(); 
            g_wicFactory.Reset(); 
            OutputDebugStringA("[WIC] CleanupWICResources.\n");
        }


    } // namespace DXTEX
} // namespace HIKARI
