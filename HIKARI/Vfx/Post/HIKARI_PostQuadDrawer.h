#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_GfxContext.h"

namespace HIKARI {
    namespace POST {

        enum class BlendOption {
            Alpha,
            PremultipliedAlpha,
            Additive,
            Multiply
        };

        class QuadDrawer
        {
        public:
            QuadDrawer() = default;
            ~QuadDrawer() { Finalize(); }

            bool Init(const GFX::Context& ctx);
            void UpdateContext(const GFX::Context& ctx);
            void Finalize();
            std::string DumpState() const;
            const char* GetOutputFormatName() const;
            bool SetOutputFormat(DXGI_FORMAT format);

            void DrawFullscreen(ID3D12DescriptorHeap* srvHeap, D3D12_GPU_DESCRIPTOR_HANDLE srvGpu);
            void SetInputTexture(ID3D12DescriptorHeap* srvHeap, D3D12_GPU_DESCRIPTOR_HANDLE srvGpu);
            bool SetPixelShader(ID3DBlob* psBlob);
            void SetConstantBuffer(D3D12_GPU_VIRTUAL_ADDRESS cbv0);
            void DrawFullscreen();
            void DrawBlended(ID3D12DescriptorHeap* srvHeap, D3D12_GPU_DESCRIPTOR_HANDLE srvGpu, BlendOption mode = BlendOption::Alpha);

        private:
            struct PipelineSet
            {
                DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
                Microsoft::WRL::ComPtr<ID3D12PipelineState> copy;
                Microsoft::WRL::ComPtr<ID3D12PipelineState> blendAlpha;
                Microsoft::WRL::ComPtr<ID3D12PipelineState> blendPremultipliedAlpha;
                Microsoft::WRL::ComPtr<ID3D12PipelineState> blendAdd;
                Microsoft::WRL::ComPtr<ID3D12PipelineState> blendMultiply;
                std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ID3D12PipelineState>> postByShader;
            };

            bool CreateRootSignature();
            bool CreatePipeline(
                ID3DBlob* psBlob,
                DXGI_FORMAT format,
                Microsoft::WRL::ComPtr<ID3D12PipelineState>& outPso,
                const char* debugName);
            bool CreateBlendPipelines(DXGI_FORMAT format, PipelineSet& outSet);
            bool EnsurePipelineSet(DXGI_FORMAT format);

        private:
            bool initialized_ = false;
            Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig_;
            Microsoft::WRL::ComPtr<ID3DBlob> vsBlob_;
            Microsoft::WRL::ComPtr<ID3DBlob> psCopyBlob_;
            std::unordered_map<int, PipelineSet> pipelineCache_;
            PipelineSet* currentPipelineSet_ = nullptr;
            ID3D12PipelineState* currentPostPso_ = nullptr;

            ID3DBlob* currentPostPS_ = nullptr;
            DXGI_FORMAT outputFormat_ = DXGI_FORMAT_R8G8B8A8_UNORM;
            ID3D12DescriptorHeap* currentSrvHeap_ = nullptr;
            D3D12_GPU_DESCRIPTOR_HANDLE currentSrvGpu_{};
            D3D12_GPU_VIRTUAL_ADDRESS currentCBV0_ = 0;
            GFX::Context context_{};
        };

    } // namespace POST
} // namespace HIKARI
