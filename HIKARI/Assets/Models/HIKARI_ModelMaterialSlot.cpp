#include "Assets/Models/HIKARI_ModelMaterialSlot.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <unordered_set>

#include "Core/Hashing/HIKARI_FnvHash.h"

namespace HIKARI::ASSETS::MODELS {

namespace {
bool IsAsciiWhitespace(unsigned char value) noexcept {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\f' || value == '\v';
}

std::string NormalizeSourceMaterialIdentity(std::string_view value) {
    std::string normalized{};
    normalized.reserve(value.size());
    bool pendingWhitespace = false;
    for (const char character : value) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (IsAsciiWhitespace(byte)) {
            pendingWhitespace = !normalized.empty();
            continue;
        }
        if (pendingWhitespace) {
            normalized.push_back(' ');
            pendingWhitespace = false;
        }
        if (byte >= 'A' && byte <= 'Z') {
            normalized.push_back(static_cast<char>(byte - 'A' + 'a'));
        } else {
            normalized.push_back(character);
        }
    }
    return normalized;
}

bool IsLegacyGeneratedAssimpMaterialName(std::string_view value) noexcept {
    constexpr std::string_view kPrefix = "AssimpMaterial";
    if (!value.starts_with(kPrefix) || value.size() == kPrefix.size()) {
        return false;
    }
    return std::all_of(value.begin() + static_cast<std::string_view::difference_type>(kPrefix.size()), value.end(),
                       [](unsigned char character) { return character >= '0' && character <= '9'; });
}
} // namespace

MaterialSlotId MakeMaterialSlotId(MaterialSlotImporterSemantic importerSemantic,
                                  std::string_view sourceMaterialIdentity, uint32_t duplicateOrdinal) {
    const std::string normalizedIdentity = NormalizeSourceMaterialIdentity(sourceMaterialIdentity);
    uint64_t hash = HASH::kFnv1a64OffsetBasis;
    constexpr std::string_view kDomain = "hikari.model-material-slot.v1";
    hash = HASH::Fnv1aBytes(hash, kDomain.data(), kDomain.size());
    const unsigned char separator = 0xffu;
    hash = HASH::Fnv1aBytes(hash, &separator, sizeof(separator));
    const unsigned char semanticByte = static_cast<unsigned char>(importerSemantic);
    hash = HASH::Fnv1aBytes(hash, &semanticByte, sizeof(semanticByte));
    hash = HASH::Fnv1aBytes(hash, &separator, sizeof(separator));
    hash = HASH::Fnv1aBytes(hash, normalizedIdentity.data(), normalizedIdentity.size());
    hash = HASH::Fnv1aBytes(hash, &separator, sizeof(separator));
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
        const unsigned char byte = static_cast<unsigned char>((duplicateOrdinal >> shift) & 0xffu);
        hash = HASH::Fnv1aBytes(hash, &byte, sizeof(byte));
    }
    return {hash != 0u ? hash : 1u};
}

MaterialSlotIdBuilder::MaterialSlotIdBuilder(MaterialSlotImporterSemantic importerSemantic) noexcept
    : importerSemantic_(importerSemantic) {}

MaterialSlotId MaterialSlotIdBuilder::Next(std::string_view sourceMaterialIdentity) {
    const std::string normalizedIdentity = NormalizeSourceMaterialIdentity(sourceMaterialIdentity);
    uint32_t& count = duplicateCounts_[normalizedIdentity];
    const uint32_t duplicateOrdinal = count;
    if (count != (std::numeric_limits<uint32_t>::max)()) {
        ++count;
    }
    return MakeMaterialSlotId(importerSemantic_, normalizedIdentity, duplicateOrdinal);
}

MaterialSlotImporterSemantic ResolveMaterialSlotImporterSemantic(std::string_view sourcePath) {
    if (sourcePath.starts_with("procedural://")) {
        return MaterialSlotImporterSemantic::Procedural;
    }

    const size_t separator = sourcePath.find_last_of("/\\");
    const size_t dot = sourcePath.find_last_of('.');
    if (dot == std::string_view::npos || (separator != std::string_view::npos && dot < separator)) {
        return MaterialSlotImporterSemantic::LegacyHmodel;
    }

    std::string extension(sourcePath.substr(dot));
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : static_cast<char>(value);
    });
    if (extension == ".gltf" || extension == ".glb") {
        return MaterialSlotImporterSemantic::Gltf;
    }
    if (extension == ".obj") {
        return MaterialSlotImporterSemantic::Obj;
    }
    if (extension == ".fbx") {
        return MaterialSlotImporterSemantic::Assimp;
    }
    return MaterialSlotImporterSemantic::LegacyHmodel;
}

void AssignMaterialSlotIds(std::vector<MaterialAsset>& materials, MaterialSlotImporterSemantic importerSemantic) {
    MaterialSlotIdBuilder builder(importerSemantic);
    for (MaterialAsset& material : materials) {
        // HMODEL v1-v6 stored Assimp's display fallback but not the empty
        // source name. Recover the importer identity so a v7 recook keeps
        // the same unnamed-material ordinals. An old source material that
        // was literally named AssimpMaterial<number> is inherently
        // ambiguous and follows the legacy fallback interpretation.
        const std::string_view sourceIdentity = importerSemantic == MaterialSlotImporterSemantic::Assimp &&
                                                        IsLegacyGeneratedAssimpMaterialName(material.name)
                                                    ? std::string_view{}
                                                    : std::string_view{material.name};
        material.slotId = builder.Next(sourceIdentity);
    }
}

bool HaveValidUniqueMaterialSlotIds(const std::vector<MaterialAsset>& materials) {
    std::unordered_set<uint64_t> values{};
    values.reserve(materials.size());
    for (const MaterialAsset& material : materials) {
        if (!material.slotId.IsValid() || !values.insert(material.slotId.value).second) {
            return false;
        }
    }
    return true;
}

std::optional<uint32_t> FindMaterialSlotIndex(const std::vector<MaterialAsset>& materials,
                                              MaterialSlotId slotId) noexcept {
    if (!slotId.IsValid()) {
        return std::nullopt;
    }
    const auto found = std::find_if(materials.begin(), materials.end(),
                                    [slotId](const MaterialAsset& material) { return material.slotId == slotId; });
    const size_t index = static_cast<size_t>(std::distance(materials.begin(), found));
    if (found == materials.end() || index > (std::numeric_limits<uint32_t>::max)()) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(index);
}

std::string MaterialSlotIdToString(MaterialSlotId slotId) {
    std::array<char, 16u> text{};
    text.fill('0');
    std::array<char, 16u> digits{};
    const auto result = std::to_chars(digits.data(), digits.data() + digits.size(), slotId.value, 16);
    if (result.ec != std::errc{}) {
        return {};
    }
    const size_t digitCount = static_cast<size_t>(result.ptr - digits.data());
    std::copy_n(digits.data(), digitCount, text.data() + (text.size() - digitCount));
    return std::string(text.data(), text.size());
}

std::optional<MaterialSlotId> ParseMaterialSlotId(std::string_view text) noexcept {
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2u);
    }
    if (text.empty() || text.size() > 16u) {
        return std::nullopt;
    }
    uint64_t value = 0u;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0u) {
        return std::nullopt;
    }
    return MaterialSlotId{value};
}

} // namespace HIKARI::ASSETS::MODELS
