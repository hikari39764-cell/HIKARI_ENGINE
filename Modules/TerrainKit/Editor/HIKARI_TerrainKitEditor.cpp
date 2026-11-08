#include <algorithm>
#include <chrono>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include "HIKARI/Project/GameProjectEditorModule.h"
#include "HIKARI_TerrainKitStore.h"
#include "TerrainKit/Data/HIKARI_TerrainDataBrush.h"
#include "TerrainKit/Data/HIKARI_TerrainDataEdit.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace EDITOR_PROJECT;
using TERRAIN::DATA::HeightBrushOp;
using TERRAIN::DATA::HeightBrushSettings;

void ReplyFailure(IEditorOperationResultSink& result, std::string_view code, const std::string& message) {
    result.Receive(false, code, message, "{}");
}

// 操作の手続きの共通部分：JSON を読み、例外を結果に変える。
template <class Body>
class JsonOperation final : public IEditorOperationHandler {
public:
    explicit JsonOperation(Body body) : body_(std::move(body)) {}
    void Execute(std::string_view parametersJson, IEditorOperationResultSink& result) override {
        try {
            const nlohmann::json parameters =
                parametersJson.empty() ? nlohmann::json::object() : nlohmann::json::parse(parametersJson);
            body_(parameters, result);
        } catch (const std::exception& exception) {
            ReplyFailure(result, "INVALID_PARAMETERS", exception.what());
        }
    }

private:
    Body body_;
};

template <class Body>
std::unique_ptr<IEditorOperationHandler> MakeOperation(Body body) {
    return std::make_unique<JsonOperation<Body>>(std::move(body));
}

void DescribeTerrain(const nlohmann::json&, IEditorOperationResultSink& result) {
    const TerrainStore& store = TerrainStore::Get();
    if (!store.IsOpen()) {
        ReplyFailure(result, "NO_TERRAIN", "No terrain is open. Add a terrain with terrainkit.create.");
        return;
    }
    const auto& grid = store.Settings().grid;
    float minimum = std::numeric_limits<float>::max();
    float maximum = std::numeric_limits<float>::lowest();
    for (const auto& [tile, runtime] : store.Tiles()) {
        minimum = (std::min)(minimum, runtime.minHeight);
        maximum = (std::max)(maximum, runtime.maxHeight);
    }
    nlohmann::json layers = nlohmann::json::array();
    for (const auto& layer : store.Data().Stack().layers) {
        layers.push_back({{"id", layer.id}, {"name", layer.name}});
    }
    const nlohmann::json data{{"scene", store.ScenePath().generic_string()},
                              {"tileSizeMeters", grid.tileSizeMeters},
                              {"tileCells", grid.tileCells},
                              {"tiles", store.Tiles().size()},
                              {"tilesX", store.Settings().tilesX},
                              {"tilesZ", store.Settings().tilesZ},
                              {"minHeight", minimum},
                              {"maxHeight", maximum},
                              {"layers", std::move(layers)},
                              {"unsaved", store.HasUnsavedTiles()}};
    result.Receive(true, "OK", "Terrain description.", data.dump());
}

void GenerateTerrain(const nlohmann::json& parameters, IEditorOperationResultSink& result) {
    TerrainStore& store = TerrainStore::Get();
    if (!store.IsOpen()) {
        ReplyFailure(result, "NO_TERRAIN", "Add a Terrain component to an object first.");
        return;
    }
    TerrainSettings settings = store.Settings();
    TERRAIN::DATA::NoiseLayerSettings noise{};
    noise.amplitudeMeters = parameters.value("amplitudeMeters", 140.0f);
    noise.offsetMeters = parameters.value("offsetMeters", 60.0f);
    noise.featureSizeMeters = parameters.value("featureSizeMeters", 900.0f);
    noise.octaves = parameters.value("octaves", 6u);
    noise.seed = parameters.value("seed", 1337);
    noise.fractal = TERRAIN::DATA::NoiseFractal::FBm;
    std::string error{};
    if (!store.Generate(settings, noise, error)) {
        ReplyFailure(result, "GENERATE_FAILED", error);
        return;
    }
    result.Receive(true, "OK", "Terrain generated.", nlohmann::json{{"tiles", store.Tiles().size()}}.dump());
}

void StrokeTerrain(const nlohmann::json& parameters, IEditorOperationResultSink& result) {
    TerrainStore& store = TerrainStore::Get();
    if (!store.IsOpen() || store.SculptLayerId() == 0u) {
        ReplyFailure(result, "NO_TERRAIN", "No terrain with a sculpt layer is open.");
        return;
    }
    HeightBrushSettings brush{};
    brush.op = HeightBrushOp::Raise;
    const std::string op = parameters.value("op", std::string("raise"));
    for (const auto& name : TERRAIN::DATA::kHeightBrushOpNames) {
        if (op == name.name) {
            brush.op = name.value;
        }
    }
    brush.radiusMeters = parameters.value("radiusMeters", 16.0f);
    brush.strength = parameters.value("strength", 0.5f);
    brush.hardness = parameters.value("hardness", 0.5f);
    brush.targetHeightMeters = parameters.value("targetHeightMeters", 0.0f);
    std::vector<MATH::Vec2> path{};
    for (const auto& point : parameters.at("points")) {
        path.push_back({point.at(0).get<float>(), point.at(1).get<float>()});
    }
    const auto started = std::chrono::steady_clock::now();
    TERRAIN::DATA::BrushEditResult edit{};
    std::string error{};
    if (!TERRAIN::DATA::ApplyHeightBrush(store.Data(), store.SculptLayerId(), brush, path, nullptr, edit, error)) {
        ReplyFailure(result, "STROKE_FAILED", error);
        return;
    }
    store.RebakeRect(edit.touched);
    const double milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    result.Receive(true, "OK", "Stroke applied.",
                   nlohmann::json{{"dabs", edit.dabs}, {"milliseconds", milliseconds}}.dump());
}

void HeightAt(const nlohmann::json& parameters, IEditorOperationResultSink& result) {
    const auto height = TerrainStore::Get().HeightAt(parameters.at("x").get<float>(), parameters.at("z").get<float>());
    if (!height.has_value()) {
        ReplyFailure(result, "NO_TILE", "No terrain tile at that position.");
        return;
    }
    result.Receive(true, "OK", "Height.", nlohmann::json{{"height", *height}}.dump());
}

class TerrainKitEditorModule final : public IGameProjectEditorModule {
public:
    std::string_view GetModuleId() const override { return "HIKARI.TerrainKit"; }
    std::vector<std::unique_ptr<IEditorWorkspace>> CreateEditorWorkspaces() const override { return {}; }

    void RegisterEditorOperations(IEditorOperationRegistrar& registrar) override {
        using Type = EditorOperationParameterType;
        Register(registrar, "terrainkit.describe", "Describe the open terrain: tiles, height range and layers.",
                 EditorOperationKind::Query, EditorOperationPermission::ReadEditorState, {}, DescribeTerrain);
        Register(registrar, "terrainkit.generate",
                 "Replace the terrain with a noise layer and an empty sculpt layer. Parameters: amplitudeMeters, "
                 "offsetMeters, featureSizeMeters, octaves, seed.",
                 EditorOperationKind::Mutation, EditorOperationPermission::ModifyEditorState,
                 {{"amplitudeMeters", Type::Number, "Height of noise value 1."},
                  {"offsetMeters", Type::Number, "Height added to the noise."},
                  {"featureSizeMeters", Type::Number, "World size of the largest noise feature."},
                  {"octaves", Type::Integer, "Noise octaves."},
                  {"seed", Type::Integer, "Noise seed."}},
                 GenerateTerrain);
        Register(registrar, "terrainkit.stroke",
                 "Apply a height brush along a world-space path. Parameters: op (raise, lower, smooth, flatten, "
                 "noise), radiusMeters, strength, hardness, targetHeightMeters, points [[x,z],...].",
                 EditorOperationKind::Mutation, EditorOperationPermission::ModifyEditorState,
                 {{"op", Type::String, "raise, lower, smooth, flatten or noise."},
                  {"radiusMeters", Type::Number, "Brush radius."},
                  {"strength", Type::Number, "Meters per dab, or a 0..1 fraction for smooth and flatten."},
                  {"hardness", Type::Number, "Fraction of the radius at full weight."},
                  {"targetHeightMeters", Type::Number, "Target height for flatten."},
                  {"points", Type::Json, "World-space path [[x,z],...]."}},
                 StrokeTerrain);
        Register(registrar, "terrainkit.height_at", "Read the terrain height at x, z.", EditorOperationKind::Query,
                 EditorOperationPermission::ReadEditorState,
                 {{"x", Type::Number, "World X."}, {"z", Type::Number, "World Z."}}, HeightAt);
    }

private:
    struct Parameter {
        const char* name = nullptr;
        EditorOperationParameterType type = EditorOperationParameterType::Unspecified;
        const char* description = nullptr;
    };

    template <class Body>
    void Register(IEditorOperationRegistrar& registrar, const char* id, const char* summary, EditorOperationKind kind,
                  EditorOperationPermission permission, std::initializer_list<Parameter> parameters, Body body) {
        handlers_.push_back(MakeOperation(std::move(body)));
        std::vector<EditorOperationParameterDescriptor> descriptors{};
        for (const Parameter& parameter : parameters) {
            EditorOperationParameterDescriptor entry{};
            entry.name = parameter.name;
            entry.type = parameter.type;
            entry.description = parameter.description;
            descriptors.push_back(entry);
        }
        EditorOperationDescriptor descriptor{};
        descriptor.id = id;
        descriptor.summary = summary;
        descriptor.workspace = "general";
        descriptor.kind = kind;
        descriptor.permission = permission;
        descriptor.exposureMask = EditorOperationExposureMask::Human | EditorOperationExposureMask::Agent;
        descriptor.handler = handlers_.back().get();
        descriptor.parameters = descriptors.data();
        descriptor.parameterCount = descriptors.size();
        (void)registrar.RegisterOperation(descriptor);
    }

    std::vector<std::unique_ptr<IEditorOperationHandler>> handlers_{};
};

} // namespace
} // namespace HIKARI::TERRAINKIT

namespace HIKARI::EDITOR_PROJECT {
std::unique_ptr<IGameProjectEditorModule> CreateGameProjectEditorModule() {
    return std::make_unique<::HIKARI::TERRAINKIT::TerrainKitEditorModule>();
}
} // namespace HIKARI::EDITOR_PROJECT
