#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace HIKARI::ASSETS::GEOMETRY::COMPATIBILITY {

    constexpr uint32_t kStaticContainerVersion = 15u;

    bool UpgradeStaticPackedGpuChunks(
        std::vector<uint8_t>& geometryBytes,
        std::vector<uint8_t>& metadataBytes,
        uint32_t& outHeaderGrowth,
        std::string& outMessage);

} // namespace HIKARI::ASSETS::GEOMETRY::COMPATIBILITY
