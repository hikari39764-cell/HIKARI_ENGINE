#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace HIKARI::AUDIO {
    // PCM / IEEE float の単一 data chunk。常駐と stream が同じ検証結果を使う。
    struct AudioWaveInfo {
        std::vector<uint8_t> format;
        uint64_t dataOffset = 0u;
        uint32_t dataBytes = 0u;
    };
    bool ReadAudioWaveInfo(const std::filesystem::path& path, AudioWaveInfo& out);
}
