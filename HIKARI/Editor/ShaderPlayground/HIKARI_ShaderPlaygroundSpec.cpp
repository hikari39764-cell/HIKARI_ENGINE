#include "Editor/ShaderPlayground/HIKARI_ShaderPlaygroundSpec.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <string_view>

#include <json.hpp>

#include "Assets/RenderStyle/HIKARI_RenderStyleAsset.h"
#include "Core/IO/HIKARI_PathNormalization.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Serialization/Json/HIKARI_JsonStrict.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "RenderContracts/RenderStyle/HIKARI_RenderStyleShading.h"

namespace HIKARI::EDITOR::PLAYGROUND {
namespace {
using Json = nlohmann::json;
using JSONSTRICT::Fail;

// タイル 1 枚の値。defaults、行の set、掃引の値の順に重ねる。
struct TileState {
    MATH::Vec3 baseColor{0.8f, 0.8f, 0.8f};
    float roughness = 0.45f;
    float metallic = 0.0f;
    float lightAzimuth = -40.0f;
    float lightElevation = 35.0f;
    float lightIntensity = 3.0f;
    MATH::Vec3 lightColor{1.0f, 1.0f, 1.0f};
    float ambient = 0.25f;
    float time = 0.0f;
    RENDERSTYLE::RenderStyleShading style{};
    std::array<MATH::Vec4, kParamSlots> params{};
};

constexpr auto kTileFields = [](auto&& field) {
    using JSONFIELDS::Nested;
    using JSONFIELDS::Range;
    using S = TileState;
    field("baseColor", &S::baseColor, Range{0.0, 1.0});
    field("roughness", &S::roughness, Range{0.0, 1.0});
    field("metallic", &S::metallic, Range{0.0, 1.0});
    field("lightAzimuth", &S::lightAzimuth, Range{-360.0, 360.0});
    field("lightElevation", &S::lightElevation, Range{-90.0, 90.0});
    field("lightIntensity", &S::lightIntensity, Range{0.0, 1000.0});
    field("lightColor", &S::lightColor, Range{0.0, 1.0});
    field("ambient", &S::ambient, Range{0.0, 100.0});
    field("time", &S::time, Range{0.0, 1.0e6});
    field("style", &S::style, Nested{RENDERSTYLE::kRenderStyleShadingFields});
};

constexpr std::string_view kSpecKeys[] = {"formatVersion", "name",       "source",       "compare",  "image",
                                          "scene",         "device",     "tileSize",     "columns",  "gap",
                                          "exposure",      "curveRange", "ambientColor", "maxValue", "parameters",
                                          "defaults",      "tiles",      "sweep"};

bool IsIdentifier(std::string_view text) {
    if (text.empty() || text.size() > 48u || (text[0] >= '0' && text[0] <= '9'))
        return false;
    return std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    });
}

bool IsTileField(std::string_view key) {
    bool found = false;
    kTileFields([&](const char* name, auto&&...) { found = found || key == name; });
    return found;
}

// 数か 1..4 個の数の配列を float4 へ（足りない成分は 0）。
bool ReadVector(const Json& node, std::string_view path, MATH::Vec4& out, std::string& error) {
    float values[4]{};
    if (node.is_number()) {
        values[0] = node.get<float>();
    } else if (node.is_array() && !node.empty() && node.size() <= 4u &&
               std::all_of(node.begin(), node.end(), [](const Json& item) { return item.is_number(); })) {
        for (size_t index = 0u; index < node.size(); ++index)
            values[index] = node[index].get<float>();
    } else {
        return Fail(error, std::string(path) + " must be a number or an array of 1 to 4 numbers");
    }
    if (!std::all_of(std::begin(values), std::end(values), [](float value) { return std::isfinite(value); }))
        return Fail(error, std::string(path) + " must be finite");
    out = {values[0], values[1], values[2], values[3]};
    return true;
}

bool ReadFloats(const Json& root, const char* key, float* values, size_t count, std::string& error) {
    const auto member = root.find(key);
    if (member == root.end())
        return true;
    if (!member->is_array() || member->size() != count ||
        !std::all_of(member->begin(), member->end(), [](const Json& item) { return item.is_number(); }))
        return Fail(error, std::string("root.") + key + " must be an array of " + std::to_string(count) + " numbers");
    for (size_t index = 0u; index < count; ++index)
        values[index] = (*member)[index].get<float>();
    return true;
}

// 設定のフォルダの中のファイル（相対パス、外へ出ない、拡張子が合う、存在する）。
bool ReadRelativeFile(const Json& root, const char* key, const std::filesystem::path& directory,
                      std::string_view extension, std::filesystem::path& out, std::string& error) {
    std::string text;
    if (!JSONSTRICT::ReadString(root, key, "", text, error, "root"))
        return false;
    if (text.empty())
        return true;
    const std::filesystem::path relative = TEXT::PathFromUtf8(text);
    if (!PATHS::IsSafeRelativePath(relative))
        return Fail(error, std::string("root.") + key + " must be a path inside the spec folder");
    std::string actual = TEXT::PathToUtf8(relative.extension());
    std::transform(actual.begin(), actual.end(), actual.begin(),
                   [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    if (!extension.empty() && actual != extension)
        return Fail(error, std::string("root.") + key + " must be a " + std::string(extension) + " file");
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(directory / relative, fileError))
        return Fail(error, std::string("root.") + key + " not found: " + text);
    out = relative;
    return true;
}

class TileBuilder {
public:
    explicit TileBuilder(const Spec& spec) : spec_(spec) {}

    // set の値を重ねる。parameters の名前は x..w、他は TileState の項目表で読む。
    bool Apply(const Json& set, std::string_view path, TileState& state, std::string& error) const {
        if (!set.is_object())
            return Fail(error, std::string(path) + " must be an object");
        for (auto it = set.begin(); it != set.end(); ++it) {
            const auto parameter = std::find(spec_.parameterNames.begin(), spec_.parameterNames.end(), it.key());
            if (parameter != spec_.parameterNames.end()) {
                const size_t slot = spec_.parameterBase + (parameter - spec_.parameterNames.begin());
                if (!ReadVector(it.value(), std::string(path) + "." + it.key(), state.params[slot], error))
                    return false;
            } else if (!IsTileField(it.key())) {
                return Fail(error, std::string(path) + "." + it.key() + " is not a known key or parameter");
            }
        }
        try {
            JSONFIELDS::Read(set, state, kTileFields);
        } catch (const std::exception& exception) {
            return Fail(error, std::string(path) + "." + exception.what());
        }
        return true;
    }

    Tile Pack(const TileState& input, std::string label, float sweep, float sweepT) const {
        const TileState state = JSONFIELDS::Sanitize(input, kTileFields);
        Tile tile;
        tile.label = std::move(label);
        for (uint32_t slot = 0u; slot < kParamSlots; ++slot)
            tile.slots[slot] = state.params[slot];
        if (spec_.source.empty())
            RENDERSTYLE::PackRenderStyleShading(state.style, tile.slots[0], tile.slots[1]);
        constexpr float kRadians = 3.14159265358979f / 180.0f;
        const float azimuth = state.lightAzimuth * kRadians, elevation = state.lightElevation * kRadians;
        const MATH::Vec3 radiance = state.lightColor * state.lightIntensity;
        tile.slots[12] = {state.baseColor.x, state.baseColor.y, state.baseColor.z, state.roughness};
        tile.slots[13] = {state.metallic, sweep, sweepT, state.time};
        tile.slots[14] = {std::cos(elevation) * std::sin(azimuth), std::sin(elevation),
                          std::cos(elevation) * std::cos(azimuth), state.ambient};
        tile.slots[15] = {radiance.x, radiance.y, radiance.z, 0.0f};
        return tile;
    }

private:
    const Spec& spec_;
};

bool ReadParameters(const Json& root, Spec& spec, TileState& defaults, std::string& error) {
    const auto member = root.find("parameters");
    if (member == root.end())
        return true;
    if (!member->is_array())
        return Fail(error, "root.parameters must be an array");
    if (member->size() > kParamSlots - spec.parameterBase)
        return Fail(error,
                    "root.parameters has more than " + std::to_string(kParamSlots - spec.parameterBase) + " entries");
    for (size_t index = 0u; index < member->size(); ++index) {
        const Json& item = (*member)[index];
        const std::string path = "root.parameters[" + std::to_string(index) + "]";
        std::string name;
        if (!item.is_object() || !JSONSTRICT::ReadString(item, "name", "", name, error, path))
            return error.empty() ? Fail(error, path + " must be an object") : false;
        if (!IsIdentifier(name) || IsTileField(name) ||
            std::find(spec.parameterNames.begin(), spec.parameterNames.end(), name) != spec.parameterNames.end())
            return Fail(error, path + ".name must be a new identifier (letters, digits, _)");
        const size_t slot = spec.parameterBase + spec.parameterNames.size();
        if (item.contains("value") && !ReadVector(item.at("value"), path + ".value", defaults.params[slot], error))
            return false;
        spec.parameterNames.push_back(std::move(name));
    }
    return true;
}

struct Row {
    std::string label;
    TileState state;
};

bool ReadRows(const Json& root, const TileBuilder& builder, const TileState& defaults, std::vector<Row>& rows,
              std::string& error) {
    const auto member = root.find("tiles");
    if (member == root.end()) {
        rows.push_back({"", defaults});
        return true;
    }
    if (!member->is_array() || member->empty())
        return Fail(error, "root.tiles must be a non-empty array");
    for (size_t index = 0u; index < member->size(); ++index) {
        const Json& item = (*member)[index];
        const std::string path = "root.tiles[" + std::to_string(index) + "]";
        Row row{"", defaults};
        if (!item.is_object() || !JSONSTRICT::ReadString(item, "label", "", row.label, error, path))
            return error.empty() ? Fail(error, path + " must be an object") : false;
        if (item.contains("set") && !builder.Apply(item.at("set"), path + ".set", row.state, error))
            return false;
        rows.push_back(std::move(row));
    }
    return true;
}

struct Sweep {
    std::string key;
    float from = 0.0f;
    float to = 1.0f;
    uint32_t count = 1u;
};

bool ReadSweep(const Json& root, Sweep& sweep, std::string& error) {
    const auto member = root.find("sweep");
    if (member == root.end())
        return true;
    const Json& node = *member;
    if (!node.is_object() || !JSONSTRICT::ReadString(node, "key", "", sweep.key, error, "root.sweep") ||
        !JSONSTRICT::ReadFloat(node, "from", 0.0f, sweep.from, error, "root.sweep") ||
        !JSONSTRICT::ReadFloat(node, "to", 1.0f, sweep.to, error, "root.sweep") ||
        !JSONSTRICT::ReadUint32(node, "count", 5u, sweep.count, error, "root.sweep"))
        return error.empty() ? Fail(error, "root.sweep must be an object") : false;
    if (sweep.key.empty() || sweep.count < 2u || sweep.count > kMaxTiles)
        return Fail(error, "root.sweep needs a key and a count of 2 to " + std::to_string(kMaxTiles));
    return true;
}

std::string FormatValue(float value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.3g", value);
    return text;
}

} // namespace

bool ReadSpec(const std::filesystem::path& path, Spec& outSpec, std::string& outError) {
    Json root;
    std::string message;
    if (!SERIALIZATION::JSON::ReadJsonFile(path, root, &message))
        return Fail(outError, "could not read " + TEXT::PathToUtf8(path) + ": " + message);
    if (!JSONSTRICT::RequireFormatVersion(root, kSpecFormatVersion, outError))
        return false;
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (std::find(std::begin(kSpecKeys), std::end(kSpecKeys), it.key()) == std::end(kSpecKeys))
            return Fail(outError, "root." + it.key() + " is not a known key");
    }

    Spec spec;
    spec.directory = PATHS::MakeAbsoluteNormalized(path).parent_path();
    std::string sceneName, deviceName;
    if (!JSONSTRICT::ReadString(root, "name", TEXT::PathToUtf8(path.stem()), spec.name, outError, "root") ||
        !ReadRelativeFile(root, "source", spec.directory, ".hlsl", spec.source, outError) ||
        !ReadRelativeFile(root, "compare", spec.directory, ".hlsl", spec.compare, outError) ||
        !ReadRelativeFile(root, "image", spec.directory, "", spec.image, outError) ||
        !JSONSTRICT::ReadString(root, "scene", "sphere", sceneName, outError, "root") ||
        !JSONSTRICT::ReadString(root, "device", "gpu", deviceName, outError, "root") ||
        !JSONSTRICT::ReadUint32(root, "tileSize", spec.tileSize, spec.tileSize, outError, "root") ||
        !JSONSTRICT::ReadUint32(root, "columns", spec.columns, spec.columns, outError, "root") ||
        !JSONSTRICT::ReadUint32(root, "gap", spec.gap, spec.gap, outError, "root") ||
        !JSONSTRICT::ReadFloat(root, "exposure", spec.exposure, spec.exposure, outError, "root") ||
        !ReadFloats(root, "curveRange", &spec.curveRange.x, 2u, outError) ||
        !ReadFloats(root, "ambientColor", &spec.ambientColor.x, 3u, outError))
        return false;
    if (spec.name.empty() || spec.name.size() > 64u || TEXT::IsWindowsReservedFileName(spec.name) ||
        spec.name.find_first_of("/\\:*?\"<>|") != std::string::npos || spec.name.front() == '.')
        return Fail(outError, "root.name must be a plain folder name of up to 64 bytes");
    if (!TEXT::TryParseEnum(kSceneNames, sceneName, spec.scene))
        return Fail(outError, "root.scene must be one of sphere, furnace, uv, image, curve, ramp");
    if (!TEXT::TryParseEnum(kDeviceNames, deviceName, spec.device))
        return Fail(outError, "root.device must be gpu or warp");
    if (spec.tileSize < 16u || spec.tileSize > 1024u || spec.columns > kMaxTiles || spec.gap > 64u ||
        !(spec.exposure > 0.0f) || !(spec.curveRange.y > spec.curveRange.x))
        return Fail(outError, "root.tileSize (16..1024), columns (0..64), gap (0..64), exposure (> 0) or "
                              "curveRange (min < max) is out of range");
    if (!JSONSTRICT::ReadFloat(root, "maxValue", spec.scene == Scene::Furnace ? 1.001f : 0.0f, spec.maxValue, outError,
                               "root"))
        return false;
    spec.parameterBase = spec.source.empty() ? kStyleParamSlots : 0u;

    TileState defaults;
    const TileBuilder builder(spec);
    if (!ReadParameters(root, spec, defaults, outError) ||
        (root.contains("defaults") && !builder.Apply(root.at("defaults"), "root.defaults", defaults, outError)))
        return false;
    std::vector<Row> rows;
    Sweep sweep;
    if (!ReadRows(root, builder, defaults, rows, outError) || !ReadSweep(root, sweep, outError))
        return false;
    if (rows.size() * sweep.count > kMaxTiles)
        return Fail(outError, "tiles x sweep.count must be at most " + std::to_string(kMaxTiles));

    for (size_t row = 0u; row < rows.size(); ++row) {
        const std::string rowLabel =
            rows[row].label.empty() && rows.size() > 1u ? "row " + std::to_string(row + 1u) : rows[row].label;
        for (uint32_t column = 0u; column < sweep.count; ++column) {
            TileState state = rows[row].state;
            const float t = sweep.count > 1u ? static_cast<float>(column) / static_cast<float>(sweep.count - 1u) : 0.0f;
            const float value = sweep.from + (sweep.to - sweep.from) * t;
            std::string label = rowLabel;
            if (!sweep.key.empty()) {
                Json set = Json::object();
                if (sweep.key.rfind("style.", 0u) == 0u)
                    set["style"][sweep.key.substr(6u)] = value;
                else
                    set[sweep.key] = value;
                if (!builder.Apply(set, "root.sweep.key", state, outError))
                    return false;
                label += (label.empty() ? "" : " / ") + sweep.key + "=" + FormatValue(value);
            }
            spec.tiles.push_back(builder.Pack(state, std::move(label), value, t));
        }
    }
    if (spec.columns == 0u)
        spec.columns = sweep.key.empty() ? (std::min)(static_cast<uint32_t>(spec.tiles.size()), 6u) : sweep.count;
    outSpec = std::move(spec);
    outError.clear();
    return true;
}

} // namespace HIKARI::EDITOR::PLAYGROUND
