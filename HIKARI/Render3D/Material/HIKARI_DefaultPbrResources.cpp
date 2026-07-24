#include "HIKARI_DefaultPbrResources.h"

#include <utility>

#include "Core/HIKARI_Logger.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI {

    namespace {
        bool gInitialized = false;
        RuntimeTextureSlot gWhiteSlot{};
        RuntimeTextureSlot gBlackSlot{};
        RuntimeTextureSlot gFlatNormalSlot{};
        RuntimeTextureSlot gMetallicRoughnessSlot{};
        RuntimeTextureSlot gMissingSlot{};

        RuntimeTextureSlot MakeDefaultSlot(
            std::string resolvedPath,
            RENDER3D::TextureResourceHandle resource)
        {
            RuntimeTextureSlot slot{};
            slot.sourcePath = resolvedPath;
            slot.resolvedPath = std::move(resolvedPath);
            slot.resource = resource;
            slot.handle = RENDER3D::GetTextureResourceBackendHandle(resource);
            // デフォルト貼り付けは SRV を埋めるだけで、ユーザー texture としては扱わない。
            slot.enabled = false;
            return slot;
        }

        void ReleaseSlot(RuntimeTextureSlot& slot)
        {
            if (slot.resource) {
                RENDER3D::ReleaseTextureResource(slot.resource);
            } else if (slot.handle >= 0) {
                RENDER3D::ReleaseTextureResource(
                    RENDER3D::RegisterTextureResourceFromBackendHandle(slot.handle));
            }
            slot = {};
        }
    }

    void DefaultPbrResources::Initialize()
    {
        if (gInitialized) {
            return;
        }

        gWhiteSlot = MakeDefaultSlot(
            "generated://pbr/default_white",
            RENDER3D::CreateSolidColorTextureResource(
                "default_pbr/white",
                0xffffffffu,
                RENDER3D::TextureResourceColorSpace::Linear));
        gBlackSlot = MakeDefaultSlot(
            "generated://pbr/default_black",
            RENDER3D::CreateSolidColorTextureResource(
                "default_pbr/black",
                0x000000ffu,
                RENDER3D::TextureResourceColorSpace::Linear));
        gFlatNormalSlot = MakeDefaultSlot(
            "generated://pbr/default_flat_normal",
            RENDER3D::CreateSolidColorTextureResource(
                "default_pbr/flat_normal",
                0x8080ffffu,
                RENDER3D::TextureResourceColorSpace::Linear));
        gMetallicRoughnessSlot = MakeDefaultSlot(
            "generated://pbr/default_metallic_roughness",
            RENDER3D::CreateSolidColorTextureResource(
                "default_pbr/metallic_roughness",
                0xffff00ffu,
                RENDER3D::TextureResourceColorSpace::Linear));
        gMissingSlot = MakeDefaultSlot(
            "generated://pbr/missing_checker",
            RENDER3D::CreateCheckerTextureResource(
                "default_pbr/missing",
                0xff00ffffu,
                0x000000ffu,
                RENDER3D::TextureResourceColorSpace::Linear));

        gInitialized = true;
        HIKARI_LOG_INFO("[DefaultPbrResources] initialized.");
    }

    void DefaultPbrResources::Shutdown()
    {
        if (!gInitialized) {
            return;
        }

        ReleaseSlot(gWhiteSlot);
        ReleaseSlot(gBlackSlot);
        ReleaseSlot(gFlatNormalSlot);
        ReleaseSlot(gMetallicRoughnessSlot);
        ReleaseSlot(gMissingSlot);
        gInitialized = false;
        HIKARI_LOG_INFO("[DefaultPbrResources] shutdown.");
    }

    RuntimeTextureSlot DefaultPbrResources::WhiteSlot()
    {
        Initialize();
        return gWhiteSlot;
    }

    RuntimeTextureSlot DefaultPbrResources::BlackSlot()
    {
        Initialize();
        return gBlackSlot;
    }

    RuntimeTextureSlot DefaultPbrResources::FlatNormalSlot()
    {
        Initialize();
        return gFlatNormalSlot;
    }

    RuntimeTextureSlot DefaultPbrResources::MetallicRoughnessSlot()
    {
        Initialize();
        return gMetallicRoughnessSlot;
    }

    RuntimeTextureSlot DefaultPbrResources::MissingSlot()
    {
        Initialize();
        return gMissingSlot;
    }

} // namespace HIKARI
