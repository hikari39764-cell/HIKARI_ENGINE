#include "HIKARI_LightProbeVolumeFormat.h"

#include <array>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"
#include "Core/Serialization/Binary/HIKARI_BinaryStream.h"

namespace HIKARI::ASSETS::LIGHTING {

    namespace {

        using SERIALIZATION::BINARY::STREAM::ReadTrivial;
        using SERIALIZATION::BINARY::STREAM::ReadTrivialArray;
        using SERIALIZATION::BINARY::STREAM::WriteTrivial;
        using SERIALIZATION::BINARY::STREAM::WriteTrivialArray;

        struct HlpvHeader {
            char magic[4] = { 'H', 'L', 'P', 'V' };
            uint32_t version = kLightProbeVolumeVersion;
            uint32_t countX = 0;
            uint32_t countY = 0;
            uint32_t countZ = 0;
            uint32_t probeCount = 0;
            float origin[3]{};
            float size[3]{};
            float spacing[3]{};
            uint32_t shOrder = kLightProbeShOrder;
            uint32_t coeffCount = kLightProbeShCoeffCount;
        };

        void SetMessage(std::string* outMessage, std::string message) {
            if (outMessage != nullptr) {
                *outMessage = std::move(message);
            }
        }

        bool IsMagicValid(const HlpvHeader& header) {
            return header.magic[0] == 'H' &&
                header.magic[1] == 'L' &&
                header.magic[2] == 'P' &&
                header.magic[3] == 'V';
        }

        MATH::Vec3 ReadVec3(const float values[3]) {
            return { values[0], values[1], values[2] };
        }

        void WriteVec3(const MATH::Vec3& value, float out[3]) {
            out[0] = value.x;
            out[1] = value.y;
            out[2] = value.z;
        }

    } // namespace

    bool LoadLightProbeVolumeFile(
        const std::filesystem::path& path,
        LightProbeVolumeFileData& outData,
        std::string* outMessage) {

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            SetMessage(outMessage, "HLPV not found: " + path.generic_string());
            return false;
        }

        HlpvHeader header{};
        if (!ReadTrivial(ifs, header) || !IsMagicValid(header)) {
            SetMessage(outMessage, "HLPV header is invalid: " + path.generic_string());
            return false;
        }

        if (header.version != kLightProbeVolumeVersion ||
            header.shOrder != kLightProbeShOrder ||
            header.coeffCount != kLightProbeShCoeffCount) {
            SetMessage(outMessage, "HLPV version or SH layout is unsupported: " + path.generic_string());
            return false;
        }

        const uint32_t expectedProbeCount = header.countX * header.countY * header.countZ;
        if (header.countX < 2u ||
            header.countY < 1u ||
            header.countZ < 2u ||
            header.probeCount == 0u ||
            header.probeCount != expectedProbeCount) {
            SetMessage(outMessage, "HLPV grid count is invalid: " + path.generic_string());
            return false;
        }

        LightProbeVolumeFileData data{};
        data.countX = header.countX;
        data.countY = header.countY;
        data.countZ = header.countZ;
        data.shOrder = header.shOrder;
        data.coeffCount = header.coeffCount;
        data.origin = ReadVec3(header.origin);
        data.size = ReadVec3(header.size);
        data.spacing = ReadVec3(header.spacing);
        data.probes.resize(header.probeCount);

        for (LightProbeSh9& probe : data.probes) {
            for (MATH::Vec3& coeff : probe.coeffs) {
                float raw[3]{};
                if (!ReadTrivialArray(ifs, raw, std::size(raw))) {
                    SetMessage(outMessage, "HLPV SH payload is truncated: " + path.generic_string());
                    return false;
                }
                coeff = ReadVec3(raw);
            }
        }

        outData = std::move(data);
        SetMessage(outMessage, "HLPV loaded: " + path.generic_string());
        return true;
    }

    bool SaveLightProbeVolumeFile(
        const std::filesystem::path& path,
        const LightProbeVolumeFileData& data,
        std::string* outMessage) {

        const uint32_t expectedProbeCount = data.countX * data.countY * data.countZ;
        if (data.countX < 2u ||
            data.countY < 1u ||
            data.countZ < 2u ||
            expectedProbeCount == 0u ||
            data.probes.size() != expectedProbeCount) {
            SetMessage(outMessage, "Light probe volume data is invalid.");
            return false;
        }

        std::error_code ec{};
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            SetMessage(outMessage, "failed to create light probe folder: " + ec.message());
            return false;
        }

        HlpvHeader header{};
        header.countX = data.countX;
        header.countY = data.countY;
        header.countZ = data.countZ;
        header.probeCount = expectedProbeCount;
        WriteVec3(data.origin, header.origin);
        WriteVec3(data.size, header.size);
        WriteVec3(data.spacing, header.spacing);
        header.shOrder = kLightProbeShOrder;
        header.coeffCount = kLightProbeShCoeffCount;

        std::ofstream ofs(path, std::ios::binary);
        if (!ofs.is_open()) {
            SetMessage(outMessage, "failed to write HLPV: " + path.generic_string());
            return false;
        }

        bool writeSucceeded = WriteTrivial(ofs, header);
        for (const LightProbeSh9& probe : data.probes) {
            for (const MATH::Vec3& coeff : probe.coeffs) {
                const float raw[3] = { coeff.x, coeff.y, coeff.z };
                writeSucceeded = writeSucceeded &&
                    WriteTrivialArray(ofs, raw, std::size(raw));
            }
        }

        if (!writeSucceeded || !ofs) {
            SetMessage(outMessage, "failed to flush HLPV: " + path.generic_string());
            return false;
        }

        SetMessage(outMessage, "HLPV saved: " + path.generic_string());
        return true;
    }

    std::filesystem::path BuildLightProbeVolumeOutputPath(
        const std::filesystem::path& projectRoot,
        const std::string& sceneGuid) {

        return (BuildLightingBakeRoot(projectRoot, sceneGuid) /
            "light_probes" /
            "light_probe_volume.hlpv").lexically_normal();
    }

} // namespace HIKARI::ASSETS::LIGHTING
