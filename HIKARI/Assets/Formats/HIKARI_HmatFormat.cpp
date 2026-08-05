#include "HIKARI_HmatFormat.h"

#include <algorithm>
#include <fstream>
#include <utility>

#include "Assets/Material/Compiler/HIKARI_MaterialProgramSerialization.h"
#include "Assets/Material/Serialization/HIKARI_MaterialSourceJson.h"
#include "Core/Serialization/Binary/HIKARI_BinaryStream.h"

namespace HIKARI {

    namespace {

        constexpr uint32_t kHmatMagic = 0x54414D48u; // 'HMAT'
        constexpr uint32_t kHmatVersion = 5u;
        constexpr uint32_t kFirstGraphHmatVersion = 4u;
        constexpr uint32_t kMaxStringBytes = 1024u * 1024u;
        constexpr uint32_t kMaxMaterialPayloadBytes = 16u * 1024u * 1024u;

        struct HmatFileHeader {
            uint32_t magic = kHmatMagic;
            uint32_t version = kHmatVersion;
            uint32_t headerSize = sizeof(HmatFileHeader);
            uint32_t reserved = 0u;
        };

        using SERIALIZATION::BINARY::STREAM::ReadBoolean8;
        using SERIALIZATION::BINARY::STREAM::ReadLengthPrefixedString32;
        using SERIALIZATION::BINARY::STREAM::ReadTrivial;
        using SERIALIZATION::BINARY::STREAM::WriteLengthPrefixedString32;
        using SERIALIZATION::BINARY::STREAM::WriteTrivial;

        bool ReadSlot(
            std::ifstream& stream,
            MaterialTextureSlotData& slot,
            uint32_t version) {

            if (!ReadBoolean8(stream, slot.useTexture) ||
                !ReadLengthPrefixedString32<kMaxStringBytes>(
                    stream, slot.textureAssetGuid.value)) {
                return false;
            }
            if (version >= 3u &&
                (!ReadTrivial(stream, slot.texCoord) ||
                    !ReadTrivial(stream, slot.uvScale) ||
                    !ReadTrivial(stream, slot.uvOffset) ||
                    !ReadTrivial(stream, slot.uvRotation))) {
                return false;
            }
            slot.texCoord = std::clamp(slot.texCoord, 0, 1);
            return true;
        }

        bool ReadLegacyPbrPayload(
            std::ifstream& stream,
            uint32_t hmatVersion,
            PbrMaterialAssetData& outMaterial,
            std::string& outMessage) {

            PbrMaterialAssetData material{};
            const bool baseOk =
                ReadTrivial(stream, material.version) &&
                ReadLengthPrefixedString32<kMaxStringBytes>(
                    stream, material.materialName) &&
                ReadSlot(stream, material.baseColorTexture, hmatVersion) &&
                ReadSlot(stream, material.normalTexture, hmatVersion) &&
                ReadSlot(stream, material.metallicRoughnessTexture, hmatVersion) &&
                ReadSlot(stream, material.occlusionTexture, hmatVersion) &&
                ReadSlot(stream, material.emissiveTexture, hmatVersion) &&
                ReadTrivial(stream, material.baseColorFactor) &&
                ReadTrivial(stream, material.metallicFactor) &&
                ReadTrivial(stream, material.roughnessFactor) &&
                ReadTrivial(stream, material.normalScale) &&
                ReadTrivial(stream, material.occlusionStrength) &&
                ReadTrivial(stream, material.emissiveFactor) &&
                ReadTrivial(stream, material.emissiveStrength) &&
                ReadBoolean8(stream, material.doubleSided) &&
                ReadBoolean8(stream, material.unlit);
            if (!baseOk) {
                outMessage = "[HMAT] failed while reading legacy PBR payload";
                return false;
            }
            if (hmatVersion >= 2u &&
                (!ReadSlot(stream, material.specularTexture, hmatVersion) ||
                    !ReadSlot(stream, material.specularColorTexture, hmatVersion) ||
                    !ReadTrivial(stream, material.specularFactor) ||
                    !ReadTrivial(stream, material.specularColorFactor))) {
                outMessage = "[HMAT] failed while reading legacy PBR specular fields";
                return false;
            }
            material.version = hmatVersion;
            outMaterial = std::move(material);
            return true;
        }

        bool OpenHmatForWrite(
            const std::filesystem::path& path,
            std::ofstream& outStream,
            std::string& outMessage) {

            std::error_code ec{};
            if (!path.parent_path().empty()) {
                std::filesystem::create_directories(path.parent_path(), ec);
            }
            if (ec) {
                outMessage = "[HMAT] failed to create output directory: " + ec.message();
                return false;
            }
            outStream.open(path, std::ios::binary | std::ios::trunc);
            if (!outStream.is_open()) {
                outMessage = "[HMAT] failed to open for write: " + path.generic_string();
                return false;
            }
            return true;
        }

    } // namespace

    bool WriteHmatFile(
        const std::filesystem::path& path,
        const MaterialAssetData& material,
        const MaterialCompileResult& compileResult,
        std::string& outMessage) {

        std::string sourceJson{};
        std::string programJson{};
        std::string serializationError{};
        if (!MATERIAL_SERIALIZATION::SerializeMaterialSourceJson(
                material, sourceJson, serializationError) ||
            !MATERIAL_SERIALIZATION::SerializeMaterialCompileResult(
                compileResult, programJson, serializationError)) {
            outMessage = "[HMAT] failed to serialize v5 payload: " + serializationError;
            return false;
        }

        std::ofstream stream{};
        if (!OpenHmatForWrite(path, stream, outMessage)) {
            return false;
        }
        const HmatFileHeader header{};
        const bool ok =
            WriteTrivial(stream, header) &&
            WriteLengthPrefixedString32<kMaxMaterialPayloadBytes>(stream, sourceJson) &&
            WriteLengthPrefixedString32<kMaxMaterialPayloadBytes>(stream, programJson);
        if (!ok || !stream.good()) {
            outMessage = "[HMAT] failed while writing v5 payload: " + path.generic_string();
            return false;
        }
        outMessage = "[HMAT] wrote v5 " + path.generic_string();
        return true;
    }

    bool ReadHmatFile(
        const std::filesystem::path& path,
        MaterialAssetData& outMaterial,
        MaterialCompileResult& outCompileResult,
        const MaterialCompileOptions& migrationCompileOptions,
        std::string& outMessage) {

        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open()) {
            outMessage = "[HMAT] failed to open: " + path.generic_string();
            return false;
        }

        HmatFileHeader header{};
        if (!ReadTrivial(stream, header) ||
            header.magic != kHmatMagic ||
            header.headerSize != sizeof(HmatFileHeader)) {
            outMessage = "[HMAT] invalid header: " +
                path.generic_string();
            return false;
        }
        if (header.version < 1u || header.version > kHmatVersion) {
            outMessage = "[HMAT] unsupported version " +
                std::to_string(header.version) +
                "; supported versions are 1 through " +
                std::to_string(kHmatVersion) + ": " +
                path.generic_string();
            return false;
        }

        if (header.version < kFirstGraphHmatVersion) {
            PbrMaterialAssetData legacy{};
            if (!ReadLegacyPbrPayload(
                    stream, header.version, legacy, outMessage)) {
                outMessage += ": " + path.generic_string();
                return false;
            }
            outMaterial = MakeStandardPbrMaterialAssetData(legacy);
            outCompileResult = CompileMaterialAsset(
                outMaterial, migrationCompileOptions);
            if (!outCompileResult.success) {
                outMessage = "[HMAT] legacy PBR migration did not compile: " +
                    path.generic_string();
                return false;
            }
            outMessage = "[HMAT] read and migrated v" +
                std::to_string(header.version) + " to v5 " +
                path.generic_string();
            return true;
        }

        std::string sourceJson{};
        std::string programJson{};
        if (!ReadLengthPrefixedString32<kMaxMaterialPayloadBytes>(stream, sourceJson) ||
            !ReadLengthPrefixedString32<kMaxMaterialPayloadBytes>(stream, programJson)) {
            outMessage = "[HMAT] failed while reading v" +
                std::to_string(header.version) + " payload: " +
                path.generic_string();
            return false;
        }
        std::string parseError{};
        if (!MATERIAL_SERIALIZATION::ParseMaterialSourceJson(
                sourceJson, outMaterial, parseError)) {
            outMessage = "[HMAT] invalid v" +
                std::to_string(header.version) +
                " source payload: " + parseError;
            return false;
        }
        if (header.version == 4u) {
            // v4 cooked programs predate the current Eye socket and runtime
            // expression contract. Recompile the migrated source instead of
            // silently publishing stale IR from the old cache.
            outCompileResult = CompileMaterialAsset(
                outMaterial, migrationCompileOptions);
            if (!outCompileResult.success) {
                outMessage =
                    "[HMAT] migrated v4 source did not compile as Material v5: " +
                    path.generic_string();
                return false;
            }
            outMessage = "[HMAT] read and migrated v4 to v5 " +
                path.generic_string();
            return true;
        }
        if (!MATERIAL_SERIALIZATION::ParseMaterialCompileResult(
                programJson, outCompileResult, parseError)) {
            outMessage = "[HMAT] invalid v5 program payload: " + parseError;
            return false;
        }
        outMessage = "[HMAT] read v5 " + path.generic_string();
        return true;
    }

    bool ReadHmatFile(
        const std::filesystem::path& path,
        MaterialAssetData& outMaterial,
        MaterialCompileResult& outCompileResult,
        std::string& outMessage) {

        return ReadHmatFile(
            path,
            outMaterial,
            outCompileResult,
            MaterialCompileOptions{},
            outMessage);
    }

    bool WriteHmatFile(
        const std::filesystem::path& path,
        const PbrMaterialAssetData& material,
        std::string& outMessage) {

        const MaterialAssetData source = MakeStandardPbrMaterialAssetData(material);
        return WriteHmatFile(path, source, CompileMaterialAsset(source), outMessage);
    }

    bool ReadHmatFile(
        const std::filesystem::path& path,
        PbrMaterialAssetData& outMaterial,
        std::string& outMessage) {

        MaterialAssetData source{};
        MaterialCompileResult compileResult{};
        if (!ReadHmatFile(path, source, compileResult, outMessage)) {
            return false;
        }
        outMaterial = ProjectPbrMaterialAssetData(source);
        return true;
    }

} // namespace HIKARI
