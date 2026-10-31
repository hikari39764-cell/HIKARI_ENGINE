#include "HIKARI_AudioWaveFile.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <windows.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

namespace HIKARI::AUDIO {
bool ReadAudioWaveInfo(const std::filesystem::path& path, AudioWaveInfo& out) {
    out = {};
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    file.seekg(0, std::ios::end);
    const auto length = file.tellg();
    if (length < 12)
        return false;
    const auto bytes = static_cast<uint64_t>(length);
    file.seekg(0);
    uint32_t header[3]{};
    if (!file.read(reinterpret_cast<char*>(header), sizeof(header)) || header[0] != 0x46464952u ||
        header[2] != 0x45564157u || header[1] < 4u || uint64_t(header[1]) + 8u > bytes)
        return false;
    const uint64_t end = uint64_t(header[1]) + 8u;
    AudioWaveInfo result;
    uint64_t cursor = 12u;
    while (cursor + 8u <= end) {
        file.seekg(static_cast<std::streamoff>(cursor));
        uint32_t chunk[2]{};
        if (!file.read(reinterpret_cast<char*>(chunk), sizeof(chunk)))
            return false;
        cursor += 8u;
        const uint64_t padded = uint64_t(chunk[1]) + (chunk[1] & 1u);
        if (padded > end - cursor)
            return false;
        if (chunk[0] == 0x20746d66u) {
            if (!result.format.empty() || chunk[1] < 16u || chunk[1] > 65536u)
                return false;
            result.format.resize((std::max)(size_t(chunk[1]), sizeof(WAVEFORMATEX)));
            if (!file.read(reinterpret_cast<char*>(result.format.data()), chunk[1]))
                return false;
        } else if (chunk[0] == 0x61746164u) {
            if (result.dataOffset != 0u || chunk[1] == 0u)
                return false;
            result.dataOffset = cursor;
            result.dataBytes = chunk[1];
        }
        cursor += padded;
    }
    if (result.format.empty() || result.dataBytes == 0u)
        return false;
    const auto& format = *reinterpret_cast<const WAVEFORMATEX*>(result.format.data());
    uint16_t tag = format.wFormatTag;
    if (format.cbSize > result.format.size() - sizeof(WAVEFORMATEX))
        return false;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        if (result.format.size() < sizeof(WAVEFORMATEXTENSIBLE) || format.cbSize < 22u)
            return false;
        const auto& ext = *reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(result.format.data());
        if (ext.SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
            tag = WAVE_FORMAT_PCM;
        else if (ext.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
            tag = WAVE_FORMAT_IEEE_FLOAT;
        else
            return false;
        if (ext.Samples.wValidBitsPerSample > format.wBitsPerSample)
            return false;
    }
    if ((tag != WAVE_FORMAT_PCM && tag != WAVE_FORMAT_IEEE_FLOAT) || format.nChannels == 0u || format.nChannels > 8u ||
        format.nSamplesPerSec < 8000u || format.nSamplesPerSec > 192000u ||
        (format.wBitsPerSample != 8u && format.wBitsPerSample != 16u && format.wBitsPerSample != 24u &&
         format.wBitsPerSample != 32u) ||
        (tag == WAVE_FORMAT_IEEE_FLOAT && format.wBitsPerSample != 32u) ||
        format.nBlockAlign != format.nChannels * (format.wBitsPerSample / 8u) ||
        format.nAvgBytesPerSec != format.nSamplesPerSec * format.nBlockAlign ||
        result.dataBytes % format.nBlockAlign != 0u)
        return false;
    out = std::move(result);
    return true;
}
} // namespace HIKARI::AUDIO
