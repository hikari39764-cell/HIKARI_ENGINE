#pragma once

#include <cstdint>

struct ID3D12Device;

namespace HIKARI::GFX::RAYTRACING {

enum class DxrSupportTier : uint8_t {
    Unsupported,
    Tier10,
    Tier11,
};

struct DxrCapabilities {
    DxrSupportTier tier = DxrSupportTier::Unsupported;
    uint32_t shaderModel = 0u;
    bool deviceInterfaceAvailable = false;
    bool inlineRayQueryAvailable = false;

    bool SupportsInlineRayQuery() const noexcept {
        return deviceInterfaceAvailable && inlineRayQueryAvailable && shaderModel >= 0x65u;
    }
};

DxrCapabilities QueryDxrCapabilities(ID3D12Device* device) noexcept;
const char* ToString(DxrSupportTier tier) noexcept;

} // namespace HIKARI::GFX::RAYTRACING
