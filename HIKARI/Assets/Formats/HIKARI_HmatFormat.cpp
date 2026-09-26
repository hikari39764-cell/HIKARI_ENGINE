#include "HIKARI_HmatFormat.h"

#include <fstream>
#include <utility>

#include "Assets/Material/Compiler/HIKARI_MaterialProgramSerialization.h"
#include "Assets/Material/Serialization/HIKARI_MaterialSourceJson.h"
#include "Core/Serialization/Binary/HIKARI_BinaryStream.h"

namespace HIKARI {

namespace {

constexpr uint32_t kHmatMagic = 0x54414D48u; // 'HMAT'
constexpr uint32_t kHmatVersion = 6u;
constexpr uint32_t kMaxMaterialPayloadBytes = 16u * 1024u * 1024u;

struct HmatFileHeader {
    uint32_t magic = kHmatMagic;
    uint32_t version = kHmatVersion;
    uint32_t headerSize = sizeof(HmatFileHeader);
    uint32_t reserved = 0u;
};

using SERIALIZATION::BINARY::STREAM::ReadLengthPrefixedString32;
using SERIALIZATION::BINARY::STREAM::ReadTrivial;
using SERIALIZATION::BINARY::STREAM::WriteLengthPrefixedString32;
using SERIALIZATION::BINARY::STREAM::WriteTrivial;

bool OpenHmatForWrite(const std::filesystem::path& path, std::ofstream& outStream, std::string& outMessage) {
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

bool WriteHmatFile(const std::filesystem::path& path, const MaterialAssetData& material,
                   const MaterialCompileResult& compileResult, std::string& outMessage) {
    std::string sourceJson{};
    std::string programJson{};
    std::string serializationError{};
    if (!MATERIAL_SERIALIZATION::SerializeMaterialSourceJson(material, sourceJson, serializationError) ||
        !MATERIAL_SERIALIZATION::SerializeMaterialCompileResult(compileResult, programJson, serializationError)) {
        outMessage = "[HMAT] failed to serialize payload: " + serializationError;
        return false;
    }

    std::ofstream stream{};
    if (!OpenHmatForWrite(path, stream, outMessage)) {
        return false;
    }
    const HmatFileHeader header{};
    const bool ok = WriteTrivial(stream, header) &&
                    WriteLengthPrefixedString32<kMaxMaterialPayloadBytes>(stream, sourceJson) &&
                    WriteLengthPrefixedString32<kMaxMaterialPayloadBytes>(stream, programJson);
    if (!ok || !stream.good()) {
        outMessage = "[HMAT] failed while writing v6 payload: " + path.generic_string();
        return false;
    }
    outMessage = "[HMAT] wrote v6 " + path.generic_string();
    return true;
}

bool ReadHmatFile(const std::filesystem::path& path, MaterialAssetData& outMaterial,
                  MaterialCompileResult& outCompileResult, std::string& outMessage) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        outMessage = "[HMAT] failed to open: " + path.generic_string();
        return false;
    }

    HmatFileHeader header{};
    if (!ReadTrivial(stream, header) || header.magic != kHmatMagic || header.headerSize != sizeof(HmatFileHeader)) {
        outMessage = "[HMAT] invalid header: " + path.generic_string();
        return false;
    }
    if (header.version != kHmatVersion) {
        outMessage = "[HMAT] unsupported version " + std::to_string(header.version) +
                     " (supported: " + std::to_string(kHmatVersion) + "): " + path.generic_string();
        return false;
    }

    std::string sourceJson{};
    std::string programJson{};
    if (!ReadLengthPrefixedString32<kMaxMaterialPayloadBytes>(stream, sourceJson) ||
        !ReadLengthPrefixedString32<kMaxMaterialPayloadBytes>(stream, programJson)) {
        outMessage = "[HMAT] failed while reading payload: " + path.generic_string();
        return false;
    }
    std::string parseError{};
    if (!MATERIAL_SERIALIZATION::ParseMaterialSourceJson(sourceJson, outMaterial, parseError)) {
        outMessage = "[HMAT] invalid source payload: " + parseError;
        return false;
    }
    if (!MATERIAL_SERIALIZATION::ParseMaterialCompileResult(programJson, outCompileResult, parseError)) {
        outMessage = "[HMAT] invalid program payload: " + parseError;
        return false;
    }
    outMessage = "[HMAT] read " + path.generic_string();
    return true;
}

} // namespace HIKARI
