#pragma once
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include "RenderCore/Targets/HIKARI_RenderTarget2D.h"
#include "RenderCore/Resources/HIKARI_TextureResourceSystem.h"
namespace HIKARI {
// 画面へ貼る描画先の束。Count 枚を writeIndex で順に使う（Count = 1 は毎回同じ一枚へ書く公開用）。
template <size_t Count>
struct SurfaceTextureTargets {
    std::array<std::unique_ptr<RenderTarget2D>, Count> targets{};
    std::array<RENDER3D::TextureResourceHandle, Count> textures{};
    uint64_t deviceGeneration = 0u;
    uint64_t worldIdentity = 0u;
    uint64_t outputRevision = 0u;
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint32_t writeIndex = 0u;

    void Reset() noexcept {
        for (RENDER3D::TextureResourceHandle& texture : textures) {
            if (texture) {
                (void)RENDER3D::ReleaseTextureResource(texture);
                texture = {};
            }
        }
        targets = {};
        deviceGeneration = 0u;
        worldIdentity = 0u;
        outputRevision = 0u;
        width = 0u;
        height = 0u;
        writeIndex = 0u;
    }

    bool depthSrvPublished = false;
    // 記帳の所有者。既定は実行時の面（シーン描画先）。使う側が別なら Ensure の前に差し替える。
    GFX::VIDEO_MEMORY::Owner memoryOwner = GFX::VIDEO_MEMORY::Owner::SceneTargets;

    bool Ensure(const GFX::Context& graphics, uint64_t requestedWorldIdentity, uint32_t captureSlot,
                uint32_t requestedWidth, uint32_t requestedHeight, DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT,
                bool withDepth = true, std::array<float, 4> clearColor = {0.015f, 0.02f, 0.035f, 1.0f},
                bool publishDepthSrv = false) {
        if (graphics.device == nullptr || graphics.cmdList == nullptr || requestedWorldIdentity == 0u) {
            return false;
        }
        const bool recreate =
            depthSrvPublished != publishDepthSrv || deviceGeneration != graphics.deviceGeneration ||
            worldIdentity != requestedWorldIdentity || width != requestedWidth || height != requestedHeight ||
            std::any_of(targets.begin(), targets.end(), [](const auto& target) { return target == nullptr; });
        if (recreate) {
            Reset();
            deviceGeneration = graphics.deviceGeneration;
            worldIdentity = requestedWorldIdentity;
            depthSrvPublished = publishDepthSrv;
            width = requestedWidth;
            height = requestedHeight;
            for (size_t index = 0u; index < targets.size(); ++index) {
                targets[index] = std::make_unique<RenderTarget2D>();
                targets[index]->UpdateContext(graphics);
                targets[index]->SetDebugName("Runtime.Surface." + std::to_string(captureSlot) + "." +
                                             std::to_string(index));
                if (!targets[index]->Init(memoryOwner, static_cast<int>(width), static_cast<int>(height), format,
                                          withDepth, clearColor, publishDepthSrv)) {
                    Reset();
                    return false;
                }
                RENDER3D::RenderResourceDesc resource{};
                resource.kind = RENDER3D::RenderResourceKind::Texture;
                resource.usage = RENDER3D::RenderResourceUsageFlags::ShaderResource |
                                 RENDER3D::RenderResourceUsageFlags::RenderTarget;
                resource.lifetime = RENDER3D::RenderResourceLifetime::External;
                resource.debugName =
                    "Runtime.Surface.Texture." + std::to_string(captureSlot) + "." + std::to_string(index);
                resource.sourceKey = "runtime-surface:" + std::to_string(requestedWorldIdentity) + ":" +
                                     std::to_string(captureSlot) + ":" + std::to_string(index);
                resource.width = width;
                resource.height = height;
                resource.depthOrArraySize = 1u;
                resource.mipLevels = 1u;
                resource.format = format;
                textures[index] = RENDER3D::RegisterTextureResourceFromNative(targets[index]->GetResource(), format,
                                                                              std::move(resource));
                if (!textures[index]) {
                    Reset();
                    return false;
                }
            }
        }
        for (const auto& target : targets) {
            target->UpdateContext(graphics);
        }
        return true;
    }
};
using SurfaceTextureTargetRing = SurfaceTextureTargets<GFX::kFrameResourceCount>;
} // namespace HIKARI
