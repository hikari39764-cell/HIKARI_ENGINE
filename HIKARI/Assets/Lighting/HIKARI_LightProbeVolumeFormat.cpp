#include "HIKARI_LightProbeVolumeFormat.h"

#include <array>
#include <algorithm>
#include <cmath>
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
            uint32_t visibilityResolution = kLightProbeVisibilityResolution;
            float visibilityDistanceScale = 0.0f;
        };
        static_assert(sizeof(HlpvHeader) == 76u);

        // GPU Texture3D の寸法上限。積は必ず 64 bit で検証してから確保する。
        constexpr uint32_t kMaximumGridDimension = 2048u;
        constexpr uint64_t kMaximumProbeCount = 4096u;
        constexpr uint64_t kProbePayloadBytes =
            (kLightProbeShCoeffCount * 3u + kLightProbeVisibilityTexelCount * 2u) * sizeof(float);

        bool IsGridCountValid(uint32_t x, uint32_t y, uint32_t z, uint64_t count) {
            return x >= 2u && y >= 1u && z >= 2u &&
                x <= kMaximumGridDimension && y <= kMaximumGridDimension &&
                z <= kMaximumGridDimension && count > 0u && count <= kMaximumProbeCount &&
                static_cast<uint64_t>(x) * y * z == count &&
                static_cast<uint64_t>(x) * y * (kLightProbeVisibilityResolution + 2u) <= 16384u &&
                static_cast<uint64_t>(z) * (kLightProbeVisibilityResolution + 2u) <= 16384u;
        }

        bool IsAxisValid(float origin, float size, float spacing, uint32_t count) {
            if (!std::isfinite(origin) || !std::isfinite(size) ||
                !std::isfinite(origin + size) || !std::isfinite(spacing) ||
                size <= 0.0f || spacing <= 0.0f) return false;
            if (count == 1u) return true;
            const double extent = static_cast<double>(spacing) * (count - 1u);
            return std::abs(extent - size) <= (std::max)(1.0e-5, static_cast<double>(size) * 1.0e-5);
        }

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

    bool ValidateLightProbeVolumeData(
        const LightProbeVolumeFileData& data,
        std::string* outMessage) {
        if (!IsGridCountValid(data.countX, data.countY, data.countZ, data.probes.size()) ||
            data.shOrder != kLightProbeShOrder || data.coeffCount != kLightProbeShCoeffCount) {
            SetMessage(outMessage, "Light probe grid count or SH layout is invalid.");
            return false;
        }
        if (!IsAxisValid(data.origin.x, data.size.x, data.spacing.x, data.countX) ||
            !IsAxisValid(data.origin.y, data.size.y, data.spacing.y, data.countY) ||
            !IsAxisValid(data.origin.z, data.size.z, data.spacing.z, data.countZ)) {
            SetMessage(outMessage, "Light probe grid bounds/spacing are non-finite or inconsistent.");
            return false;
        }
        if (!std::isfinite(data.visibilityDistanceScale) || data.visibilityDistanceScale <= 0.0f ||
            data.visibilityMoments.size() != data.probes.size() * kLightProbeVisibilityTexelCount) {
            SetMessage(outMessage, "Light probe visibility data is missing or invalid; rebake the volume.");
            return false;
        }
        for (const auto& moments : data.visibilityMoments) {
            if (!std::isfinite(moments.x) || !std::isfinite(moments.y) ||
                moments.x < 0.0f || moments.x > 1.0f || moments.y < 0.0f || moments.y > 1.0f ||
                moments.y + 1.0e-5f < moments.x * moments.x || moments.y > moments.x + 1.0e-5f) {
                SetMessage(outMessage, "Light probe visibility moments are non-finite or inconsistent.");
                return false;
            }
        }
        for (const LightProbeSh9& probe : data.probes) {
            for (const MATH::Vec3& coeff : probe.coeffs) {
                // SH は符号付き。負の係数は正常だが、半精度に入らない値は GPU を汚染する。
                if (!std::isfinite(coeff.x) || !std::isfinite(coeff.y) || !std::isfinite(coeff.z) ||
                    std::abs(coeff.x) > 65504.0f || std::abs(coeff.y) > 65504.0f ||
                    std::abs(coeff.z) > 65504.0f) {
                    SetMessage(outMessage, "Light probe SH coefficient cannot be represented by the runtime.");
                    return false;
                }
            }
        }
        SetMessage(outMessage, "Light probe volume data validated.");
        return true;
    }

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
            header.coeffCount != kLightProbeShCoeffCount ||
            header.visibilityResolution != kLightProbeVisibilityResolution) {
            SetMessage(outMessage, "HLPV version/layout is unsupported; rebake for visibility-aware irradiance: " + path.generic_string());
            return false;
        }

        if (!IsGridCountValid(header.countX, header.countY, header.countZ, header.probeCount)) {
            SetMessage(outMessage, "HLPV grid count is invalid: " + path.generic_string());
            return false;
        }

        // 欠損ファイルや偽の個数で大きな確保を行わない。末尾データも別版として拒否する。
        ifs.seekg(0, std::ios::end);
        const auto fileBytes = ifs.tellg();
        const uint64_t expectedBytes = sizeof(HlpvHeader) +
            static_cast<uint64_t>(header.probeCount) * kProbePayloadBytes;
        if (fileBytes < 0 || static_cast<uint64_t>(fileBytes) != expectedBytes) {
            SetMessage(outMessage, "HLPV payload size is invalid: " + path.generic_string());
            return false;
        }
        ifs.seekg(sizeof(HlpvHeader), std::ios::beg);

        LightProbeVolumeFileData data{};
        data.countX = header.countX;
        data.countY = header.countY;
        data.countZ = header.countZ;
        data.shOrder = header.shOrder;
        data.coeffCount = header.coeffCount;
        data.origin = ReadVec3(header.origin);
        data.size = ReadVec3(header.size);
        data.spacing = ReadVec3(header.spacing);
        data.visibilityDistanceScale = header.visibilityDistanceScale;
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
        data.visibilityMoments.resize(data.probes.size() * kLightProbeVisibilityTexelCount);
        for (auto& moments : data.visibilityMoments) {
            float raw[2]{};
            if (!ReadTrivialArray(ifs, raw, std::size(raw))) {
                SetMessage(outMessage, "HLPV visibility payload is truncated: " + path.generic_string());
                return false;
            }
            moments = {raw[0], raw[1]};
        }

        if (!ValidateLightProbeVolumeData(data, outMessage)) return false;
        outData = std::move(data);
        SetMessage(outMessage, "HLPV loaded: " + path.generic_string());
        return true;
    }

    bool SaveLightProbeVolumeFile(
        const std::filesystem::path& path,
        const LightProbeVolumeFileData& data,
        std::string* outMessage) {

        // 出力を開く前に検証し、無効な再ベイクで既存の有効ファイルを壊さない。
        if (!ValidateLightProbeVolumeData(data, outMessage)) return false;
        const uint32_t expectedProbeCount = static_cast<uint32_t>(data.probes.size());

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
        header.visibilityDistanceScale = data.visibilityDistanceScale;

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
        for (const auto& moments : data.visibilityMoments) {
            const float raw[2] = {moments.x, moments.y};
            writeSucceeded = writeSucceeded && WriteTrivialArray(ofs, raw, std::size(raw));
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
