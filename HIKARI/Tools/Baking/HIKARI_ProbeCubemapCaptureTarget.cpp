#include "HIKARI_ProbeCubemapCaptureTarget.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <utility>

#include <DirectXTex.h>
#include <DirectXPackedVector.h>
#include <d3dx12.h>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"

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

    bool ProbeCubemapCaptureTarget::Initialize(uint32_t resolution, DXGI_FORMAT format,
        float nearPlane, float farPlane) {
        if (!std::isfinite(nearPlane) || !std::isfinite(farPlane * 1.7320508075688772f) ||
            !((nearPlane == 0.0f && farPlane == 0.0f) ||
                (nearPlane > 0.0f && farPlane > nearPlane && format == DXGI_FORMAT_R16G16B16A16_FLOAT))) return false;
        if (initialized_ && resolution_ == resolution && format_ == format &&
            nearPlane_ == nearPlane && farPlane_ == farPlane) {
            return true;
        }

        Release();

        resolution_ = std::max(16u, resolution);
        format_ = format;
        nearPlane_ = nearPlane;
        farPlane_ = farPlane;

        for (uint32_t face = 0; face < 6u; ++face) {
            faces_[face].UpdateContext(GFX::GetCurrentContext());
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
            GFX::RetireD3D12ObjectForCurrentFrame(readback.buffer, "Probe capture color readback");
            readback = {};
        }
        for (FaceReadback& readback : depthReadbacks_) {
            GFX::RetireD3D12ObjectForCurrentFrame(readback.buffer, "Probe capture depth readback");
            readback = {};
        }

        resolution_ = 0;
        format_ = DXGI_FORMAT_UNKNOWN;
        nearPlane_ = farPlane_ = 0.0f;
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

        faces_[faceIndex].UpdateContext(GFX::GetCurrentContext());
        faces_[faceIndex].BeginCapture(r, g, b, a, depth);
        return true;
    }

    void ProbeCubemapCaptureTarget::EndFace(uint32_t faceIndex) {
        if (!initialized_ || !IsValidFace(faceIndex)) {
            return;
        }

        faces_[faceIndex].EndCapture();
    }

    bool ProbeCubemapCaptureTarget::CreateReadback(
        ID3D12Resource* resource,
        FaceReadback& readback,
        std::string* outMessage) {
        ID3D12Device* device = GFX::GetCurrentContext().device;
        if (device == nullptr || resource == nullptr) {
            SetMessage(outMessage, "Capture face resource is missing.");
            return false;
        }

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

        // 前の capture は workflow の fence 待機後に読み終わる。同寸法の buffer は再利用する。
        if (readback.buffer && readback.buffer->GetDesc().Width >= readback.totalBytes) return true;
        GFX::RetireD3D12ObjectForCurrentFrame(readback.buffer, "Probe capture resized readback");

        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_READBACK);
        const CD3DX12_RESOURCE_DESC bufferDesc =
            CD3DX12_RESOURCE_DESC::Buffer(readback.totalBytes);

        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(readback.buffer.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ProbeCubemapCaptureTarget::CreateReadback")) {
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

        ID3D12GraphicsCommandList* cmd = GFX::GetCurrentContext().cmdList;
        if (cmd == nullptr) {
            SetMessage(outMessage, "Command list is missing.");
            return false;
        }

        if (!CreateReadback(faces_[faceIndex].GetResource(), readbacks_[faceIndex], outMessage) ||
            (farPlane_ > 0.0f && !CreateReadback(faces_[faceIndex].GetDepthResource(),
                depthReadbacks_[faceIndex], outMessage))) {
            return false;
        }

        faces_[faceIndex].UpdateContext(GFX::GetCurrentContext());
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
        if (farPlane_ > 0.0f && !faces_[faceIndex].CopyDepthToReadback(
            depthReadbacks_[faceIndex].buffer.Get(), depthReadbacks_[faceIndex].footprint)) {
            SetMessage(outMessage, "Failed to copy probe visibility depth.");
            return false;
        }

        SetMessage(outMessage, "Reflection probe capture face readback queued: " +
            std::to_string(faceIndex));
        return true;
    }

    float ProbeCubemapCaptureTarget::GetVisibilityDistanceScale() const {
        return farPlane_ * 1.7320508075688772f;
    }

    bool ProbeCubemapCaptureTarget::StoreDepthInCaptureAlpha(uint32_t faceIndex,
        uint8_t* pixels, size_t rowPitch, std::string* outMessage) const {
        if (farPlane_ <= 0.0f) return true;
        const auto& readback = depthReadbacks_[faceIndex];
        void* mapped = nullptr;
        if (!readback.buffer || FAILED(readback.buffer->Map(0, nullptr, &mapped)) || !mapped) {
            SetMessage(outMessage, "Probe visibility readback is missing.");
            return false;
        }
        bool valid = true;
        for (uint32_t y = 0; y < resolution_ && valid; ++y) {
            const auto* depth = reinterpret_cast<const float*>(static_cast<const uint8_t*>(mapped) +
                readback.footprint.Offset + static_cast<size_t>(y) * readback.footprint.Footprint.RowPitch);
            auto* color = reinterpret_cast<uint16_t*>(pixels + static_cast<size_t>(y) * rowPitch);
            for (uint32_t x = 0; x < resolution_; ++x) {
                if (!std::isfinite(depth[x]) || depth[x] < 0.0f || depth[x] > 1.0f) { valid = false; break; }
                float distance = 1.0f;
                if (depth[x] < 1.0f) {
                    const double z = static_cast<double>(nearPlane_) * farPlane_ /
                        (farPlane_ - static_cast<double>(depth[x]) * (farPlane_ - nearPlane_));
                    const double u = 2.0 * (x + 0.5) / resolution_ - 1.0;
                    const double v = 2.0 * (y + 0.5) / resolution_ - 1.0;
                    distance = static_cast<float>(z * std::sqrt(1.0 + u*u + v*v) / GetVisibilityDistanceScale());
                }
                color[x * 4u + 3u] = DirectX::PackedVector::XMConvertFloatToHalf(std::clamp(distance, 0.0f, 1.0f));
            }
        }
        readback.buffer->Unmap(0, nullptr);
        if (!valid) SetMessage(outMessage, "Probe visibility capture contains invalid depth.");
        return valid;
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
            if (!StoreDepthInCaptureAlpha(face, image->pixels, image->rowPitch, outMessage)) return false;
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
