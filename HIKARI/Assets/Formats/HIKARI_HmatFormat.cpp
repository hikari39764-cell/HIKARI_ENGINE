#include "HIKARI_HmatFormat.h"

#include <algorithm>
#include <fstream>
#include <utility>

#include "Core/Serialization/Binary/HIKARI_BinaryStream.h"

namespace HIKARI {

    namespace {
        constexpr uint32_t kHmatMagic = 0x54414D48u; // 'HMAT'
        constexpr uint32_t kHmatVersion = 3;
        constexpr uint32_t kMaxStringBytes = 1024u * 1024u;

        struct HmatFileHeader {
            uint32_t magic = kHmatMagic;
            uint32_t version = kHmatVersion;
            uint32_t headerSize = sizeof(HmatFileHeader);
            uint32_t reserved = 0;
        };

        using SERIALIZATION::BINARY::STREAM::ReadBoolean8;
        using SERIALIZATION::BINARY::STREAM::ReadLengthPrefixedString32;
        using SERIALIZATION::BINARY::STREAM::ReadTrivial;
        using SERIALIZATION::BINARY::STREAM::WriteBoolean8;
        using SERIALIZATION::BINARY::STREAM::WriteLengthPrefixedString32;
        using SERIALIZATION::BINARY::STREAM::WriteTrivial;

        bool WriteSlot(std::ofstream& ofs, const MaterialTextureSlotData& slot) {
            return WriteBoolean8(ofs, slot.useTexture) &&
                WriteLengthPrefixedString32<kMaxStringBytes>(ofs, slot.textureAssetGuid.value) &&
                WriteTrivial(ofs, slot.texCoord) &&
                WriteTrivial(ofs, slot.uvScale) &&
                WriteTrivial(ofs, slot.uvOffset) &&
                WriteTrivial(ofs, slot.uvRotation);
        }

        bool ReadSlot(std::ifstream& ifs, MaterialTextureSlotData& slot, uint32_t version) {
            if (!ReadBoolean8(ifs, slot.useTexture) ||
                !ReadLengthPrefixedString32<kMaxStringBytes>(ifs, slot.textureAssetGuid.value)) {
                return false;
            }
            if (version >= 3u &&
                (!ReadTrivial(ifs, slot.texCoord) ||
                 !ReadTrivial(ifs, slot.uvScale) ||
                 !ReadTrivial(ifs, slot.uvOffset) ||
                 !ReadTrivial(ifs, slot.uvRotation))) {
                return false;
            }
            slot.texCoord = std::clamp(slot.texCoord, 0, 1);
            return true;
        }
    }

    bool WriteHmatFile(
        const std::filesystem::path& path,
        const PbrMaterialAssetData& material,
        std::string& outMessage) {

        std::error_code ec{};
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            outMessage = "[HMAT] failed to create output directory: " + ec.message();
            return false;
        }

        std::ofstream ofs(path, std::ios::binary);
        if (!ofs.is_open()) {
            outMessage = "[HMAT] failed to open for write: " + path.generic_string();
            return false;
        }

        const HmatFileHeader header{};
        const bool ok =
            WriteTrivial(ofs, header) &&
            WriteTrivial(ofs, material.version) &&
            WriteLengthPrefixedString32<kMaxStringBytes>(ofs, material.materialName) &&
            WriteSlot(ofs, material.baseColorTexture) &&
            WriteSlot(ofs, material.normalTexture) &&
            WriteSlot(ofs, material.metallicRoughnessTexture) &&
            WriteSlot(ofs, material.occlusionTexture) &&
            WriteSlot(ofs, material.emissiveTexture) &&
            WriteTrivial(ofs, material.baseColorFactor) &&
            WriteTrivial(ofs, material.metallicFactor) &&
            WriteTrivial(ofs, material.roughnessFactor) &&
            WriteTrivial(ofs, material.normalScale) &&
            WriteTrivial(ofs, material.occlusionStrength) &&
            WriteTrivial(ofs, material.emissiveFactor) &&
            WriteTrivial(ofs, material.emissiveStrength) &&
            WriteBoolean8(ofs, material.doubleSided) &&
            WriteBoolean8(ofs, material.unlit) &&
            WriteSlot(ofs, material.specularTexture) &&
            WriteSlot(ofs, material.specularColorTexture) &&
            WriteTrivial(ofs, material.specularFactor) &&
            WriteTrivial(ofs, material.specularColorFactor);

        if (!ok || !ofs.good()) {
            outMessage = "[HMAT] failed while writing: " + path.generic_string();
            return false;
        }

        outMessage = "[HMAT] wrote " + path.generic_string();
        return true;
    }

    bool ReadHmatFile(
        const std::filesystem::path& path,
        PbrMaterialAssetData& outMaterial,
        std::string& outMessage) {

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            outMessage = "[HMAT] failed to open: " + path.generic_string();
            return false;
        }

        HmatFileHeader header{};
        if (!ReadTrivial(ifs, header) ||
            header.magic != kHmatMagic ||
            header.version < 1u ||
            header.version > kHmatVersion ||
            header.headerSize != sizeof(HmatFileHeader)) {
            outMessage = "[HMAT] invalid or unsupported file: " + path.generic_string();
            return false;
        }

        PbrMaterialAssetData material{};
        const bool ok =
            ReadTrivial(ifs, material.version) &&
            ReadLengthPrefixedString32<kMaxStringBytes>(ifs, material.materialName) &&
            ReadSlot(ifs, material.baseColorTexture, header.version) &&
            ReadSlot(ifs, material.normalTexture, header.version) &&
            ReadSlot(ifs, material.metallicRoughnessTexture, header.version) &&
            ReadSlot(ifs, material.occlusionTexture, header.version) &&
            ReadSlot(ifs, material.emissiveTexture, header.version) &&
            ReadTrivial(ifs, material.baseColorFactor) &&
            ReadTrivial(ifs, material.metallicFactor) &&
            ReadTrivial(ifs, material.roughnessFactor) &&
            ReadTrivial(ifs, material.normalScale) &&
            ReadTrivial(ifs, material.occlusionStrength) &&
            ReadTrivial(ifs, material.emissiveFactor) &&
            ReadTrivial(ifs, material.emissiveStrength) &&
            ReadBoolean8(ifs, material.doubleSided) &&
            ReadBoolean8(ifs, material.unlit);

        if (ok && header.version >= 2u) {
            if (!ReadSlot(ifs, material.specularTexture, header.version) ||
                !ReadSlot(ifs, material.specularColorTexture, header.version) ||
                !ReadTrivial(ifs, material.specularFactor) ||
                !ReadTrivial(ifs, material.specularColorFactor)) {
                outMessage = "[HMAT] failed while reading v2 PBR specular fields: " + path.generic_string();
                return false;
            }
        }

        if (!ok || !ifs.good()) {
            outMessage = "[HMAT] failed while reading: " + path.generic_string();
            return false;
        }

        outMaterial = std::move(material);
        outMessage = "[HMAT] read " + path.generic_string();
        return true;
    }

} // namespace HIKARI
