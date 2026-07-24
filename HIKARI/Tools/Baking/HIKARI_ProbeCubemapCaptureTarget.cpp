#include "HIKARI_ProbeCubemapCaptureTarget.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <utility>

#include <DirectXTex.h>
#include <d3dx12.h>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "HIKARI_Services.h"

namespace HIKARI::TOOLS::BAKING {

    namespace {

        void SetMessage(std::string* outMessage, std::string message) {
            if (outMessage != nullptr) {
                *outMessage = std::move(message);
            }
        }

        bool IsValidFace(uint32_t faceIndex) {
            return faceIndex < 6u;
        }

    } // namespace

    bool ProbeCubemapCaptureTarget::Initialize(uint32_t resolution, DXGI_FORMAT format) {
        if (initialized_ && resolution_ == resolution && format_ == format) {
            return true;
        }

        Release();

        resolution_ = std::max(16u, resolution);
        format_ = format;

        for (uint32_t face = 0; face < 6u; ++face) {
            faces_[face].UpdateContext(SERVICES::gCtx);
            if (!faces_[face].Init(
                    static_cast<int>(resolution_),
                    static_cast<int>(resolution_),
                    format_,
                    true,
                    { 0.0f, 0.0f, 0.0f, 1.0f },
                    false)) {
                Release();
                return false;
            }
            faces_[face].SetDebugName("ReflectionProbeCaptureFace" + std::to_string(face));
        }

        initialized_ = true;
        return true;
    }

    void ProbeCubemapCaptureTarget::Release() {
        for (RenderTarget2D& face : faces_) {
            face.Finalize();
        }
        for (FaceReadback& readback : readbacks_) {
            readback = {};
        }

        resolution_ = 0;
        format_ = DXGI_FORMAT_UNKNOWN;
        initialized_ = false;
    }

    bool ProbeCubemapCaptureTarget::BeginFace(
        uint32_t faceIndex,
        float r,
        float g,
        float b,
        float a,
        float depth) {

        if (!initialized_ || !IsValidFace(faceIndex)) {
            return false;
        }

        faces_[faceIndex].UpdateContext(SERVICES::gCtx);
        faces_[faceIndex].BeginCapture(r, g, b, a, depth);
        return true;
    }

    void ProbeCubemapCaptureTarget::EndFace(uint32_t faceIndex) {
        if (!initialized_ || !IsValidFace(faceIndex)) {
            return;
        }

        faces_[faceIndex].EndCapture();
    }

    bool ProbeCubemapCaptureTarget::CreateReadbackForFace(
        uint32_t faceIndex,
        std::string* outMessage) {

        if (!initialized_ || !IsValidFace(faceIndex)) {
            SetMessage(outMessage, "Invalid capture face index.");
            return false;
        }

        ID3D12Device* device = SERVICES::gCtx.device;
        ID3D12Resource* resource = faces_[faceIndex].GetResource();
        if (device == nullptr || resource == nullptr) {
            SetMessage(outMessage, "Capture face resource is missing.");
            return false;
        }

        FaceReadback& readback = readbacks_[faceIndex];
        const D3D12_RESOURCE_DESC desc = resource->GetDesc();
        device->GetCopyableFootprints(
            &desc,
            0,
            1,
            0,
            &readback.footprint,
            &readback.numRows,
            &readback.rowSizeInBytes,
            &readback.totalBytes);

        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_READBACK);
        const CD3DX12_RESOURCE_DESC bufferDesc =
            CD3DX12_RESOURCE_DESC::Buffer(readback.totalBytes);

        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(readback.buffer.ReleaseAndGetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ProbeCubemapCaptureTarget::CreateReadbackForFace")) {
            SetMessage(outMessage, "Failed to create reflection probe readback buffer.");
            return false;
        }

        return true;
    }

    bool ProbeCubemapCaptureTarget::QueueReadbackFace(uint32_t faceIndex, std::string* outMessage) {
        if (!initialized_ || !IsValidFace(faceIndex)) {
            SetMessage(outMessage, "Invalid reflection probe capture face.");
            return false;
        }

        ID3D12GraphicsCommandList* cmd = SERVICES::gCtx.cmdList;
        if (cmd == nullptr) {
            SetMessage(outMessage, "Command list is missing.");
            return false;
        }

        if (!CreateReadbackForFace(faceIndex, outMessage)) {
            return false;
        }

        faces_[faceIndex].UpdateContext(SERVICES::gCtx);
        faces_[faceIndex].TransitionColor(D3D12_RESOURCE_STATE_COPY_SOURCE);

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = faces_[faceIndex].GetResource();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = readbacks_[faceIndex].buffer.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = readbacks_[faceIndex].footprint;

        // 現在の face だけを readback buffer にコピーする。
        cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        SetMessage(outMessage, "Reflection probe capture face readback queued: " +
            std::to_string(faceIndex));
        return true;
    }

    bool ProbeCubemapCaptureTarget::QueueReadback(std::string* outMessage) {
        if (!initialized_) {
            SetMessage(outMessage, "Probe capture target is not initialized.");
            return false;
        }

        for (uint32_t face = 0; face < 6u; ++face) {
            if (!QueueReadbackFace(face, outMessage)) {
                return false;
            }
        }

        SetMessage(outMessage, "Reflection probe capture readback queued.");
        return true;
    }

    bool ProbeCubemapCaptureTarget::SaveReadbackToCubemapDds(
        const std::filesystem::path& outputPath,
        std::string* outMessage) const {

        if (!initialized_) {
            SetMessage(outMessage, "Probe capture target is not initialized.");
            return false;
        }

        DirectX::ScratchImage cubemap{};
        HRESULT hr = cubemap.InitializeCube(format_, resolution_, resolution_, 1, 1);
        if (FAILED(hr)) {
            SetMessage(outMessage, "Failed to initialize cubemap image.");
            return false;
        }

        for (uint32_t face = 0; face < 6u; ++face) {
            const FaceReadback& readback = readbacks_[face];
            if (!readback.buffer) {
                SetMessage(outMessage, "Readback buffer is missing for face " + std::to_string(face) + ".");
                return false;
            }

            const DirectX::Image* image = cubemap.GetImage(0, face, 0);
            if (image == nullptr || image->pixels == nullptr) {
                SetMessage(outMessage, "Cubemap image face is missing.");
                return false;
            }

            void* mapped = nullptr;
            hr = readback.buffer->Map(0, nullptr, &mapped);
            if (FAILED(hr) || mapped == nullptr) {
                SetMessage(outMessage, "Failed to map readback buffer.");
                return false;
            }

            const uint8_t* srcBase =
                static_cast<const uint8_t*>(mapped) + readback.footprint.Offset;
            uint8_t* dstBase = image->pixels;
            const size_t copyBytes =
                std::min<size_t>(static_cast<size_t>(readback.rowSizeInBytes), image->rowPitch);

            for (UINT row = 0; row < readback.numRows; ++row) {
                const uint8_t* src = srcBase + static_cast<size_t>(readback.footprint.Footprint.RowPitch) * row;
                uint8_t* dst = dstBase + image->rowPitch * row;
                std::memcpy(dst, src, copyBytes);
            }

            readback.buffer->Unmap(0, nullptr);
        }

        std::error_code ec{};
        std::filesystem::create_directories(outputPath.parent_path(), ec);
        if (ec) {
            SetMessage(outMessage, "Failed to create capture folder: " + ec.message());
            return false;
        }

        hr = DirectX::SaveToDDSFile(
            cubemap.GetImages(),
            cubemap.GetImageCount(),
            cubemap.GetMetadata(),
            DirectX::DDS_FLAGS_NONE,
            outputPath.wstring().c_str());
        if (FAILED(hr)) {
            SetMessage(outMessage, "Failed to save reflection probe capture DDS.");
            return false;
        }

        SetMessage(outMessage, "Reflection probe capture DDS saved: " + outputPath.generic_string());
        HIKARI_LOG_INFO("[ReflectionProbeCapture] saved capture: " + outputPath.generic_string());
        return true;
    }

} // namespace HIKARI::TOOLS::BAKING
