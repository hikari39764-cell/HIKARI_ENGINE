#include <algorithm>
#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

#include <json.hpp>

#include "HIKARI_TerrainKitEditorCommon.h"
#include "TerrainKit/Data/HIKARI_TerrainDataImage.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace EDITOR_PROJECT;
using namespace TERRAIN::DATA;
using Sink = IEditorOperationResultSink;
using Json = nlohmann::json;
using Type = EditorOperationParameterType;

void Fail(Sink& result, std::string_view code, const std::string& message) {
    result.Receive(false, code, message, "{}");
}
void Ok(Sink& result, std::string_view message, const Json& data) { result.Receive(true, "OK", message, data.dump()); }

// 操作の手続きの共通部分：JSON を読み、例外を結果に変える。
template <class Body>
class JsonOperation final : public IEditorOperationHandler {
public:
    explicit JsonOperation(Body body) : body_(std::move(body)) {}
    void Execute(std::string_view parametersJson, Sink& result) override {
        try {
            const Json parameters = parametersJson.empty() ? Json::object() : Json::parse(parametersJson);
            body_(parameters, result);
        } catch (const std::exception& exception) {
            Fail(result, "INVALID_PARAMETERS", exception.what());
        }
    }

private:
    Body body_;
};

struct Parameter {
    const char* name = nullptr;
    EditorOperationParameterType type = EditorOperationParameterType::Unspecified;
    const char* description = nullptr;
};

class Registration {
public:
    Registration(IEditorOperationRegistrar& registrar, std::vector<std::unique_ptr<IEditorOperationHandler>>& handlers)
        : registrar_(registrar), handlers_(handlers) {}

    template <class Body>
    void Add(const char* id, const char* summary, EditorOperationKind kind, EditorOperationPermission permission,
             std::initializer_list<Parameter> parameters, Body body) {
        handlers_.push_back(std::make_unique<JsonOperation<Body>>(std::move(body)));
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
        (void)registrar_.RegisterOperation(descriptor);
    }

    template <class Body>
    void Query(const char* id, const char* summary, std::initializer_list<Parameter> parameters, Body body) {
        Add(id, summary, EditorOperationKind::Query, EditorOperationPermission::ReadEditorState, parameters,
            std::move(body));
    }

    template <class Body>
    void Mutation(const char* id, const char* summary, std::initializer_list<Parameter> parameters, Body body) {
        Add(id, summary, EditorOperationKind::Mutation, EditorOperationPermission::ModifyEditorState, parameters,
            std::move(body));
    }

private:
    IEditorOperationRegistrar& registrar_;
    std::vector<std::unique_ptr<IEditorOperationHandler>>& handlers_;
};

bool RequireTerrain(Sink& result) {
    if (TerrainStore::Get().IsOpen()) {
        return true;
    }
    Fail(result, "NO_TERRAIN", "No terrain is open. Add the Terrain component to an object.");
    return false;
}

Json LayerToJson(const TerrainData& data, const HeightLayer& layer) {
    Json out{};
    JSONFIELDS::Write(out, layer, kHeightLayerFields);
    const auto* values = data.LayerValues(layer.id);
    const auto* mask = data.LayerMask(layer.id);
    out["valueTiles"] = values != nullptr ? values->TileCount() : 0u;
    out["maskTiles"] = mask != nullptr ? mask->TileCount() : 0u;
    return out;
}

// patch を層の記述へ重ねて検証する。id と種類は変えられない。
bool PatchLayer(const HeightLayer& layer, const Json& patch, HeightLayer& out, std::string& error) {
    if (!patch.is_object()) {
        error = "patch must be an object.";
        return false;
    }
    Json merged{};
    JSONFIELDS::Write(merged, layer, kHeightLayerFields);
    merged.merge_patch(patch);
    HeightLayer next = layer;
    try {
        JSONFIELDS::Read(merged, next, kHeightLayerFields);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    next.id = layer.id;
    next.kind = layer.kind;
    out = next;
    return true;
}

void ReadBrushPatch(const Json& parameters, HeightBrushSettings& brush) {
    Json merged{};
    JSONFIELDS::Write(merged, brush, kHeightBrushFields);
    merged.merge_patch(parameters);
    merged.erase("points");
    merged.erase("layer");
    JSONFIELDS::Read(merged, brush, kHeightBrushFields);
}

std::vector<MATH::Vec2> ReadPath(const Json& points) {
    std::vector<MATH::Vec2> path{};
    for (const auto& point : points) {
        path.push_back({point.at(0).get<float>(), point.at(1).get<float>()});
    }
    return path;
}

void Describe(const std::shared_ptr<EditorShared>& shared, Sink& result) {
    const TerrainStore& store = TerrainStore::Get();
    const auto& grid = store.Settings().grid;
    float minimum = std::numeric_limits<float>::max();
    float maximum = std::numeric_limits<float>::lowest();
    for (const auto& [tile, runtime] : store.Tiles()) {
        minimum = (std::min)(minimum, runtime.minHeight);
        maximum = (std::max)(maximum, runtime.maxHeight);
    }
    Json layers = Json::array();
    for (const auto& layer : store.Data().Stack().layers) {
        layers.push_back({{"id", layer.id}, {"name", layer.name}});
    }
    const double frames = (std::max)(shared->brushFrames, 1u);
    Ok(result, "Terrain description.",
       {{"scene", store.ScenePath().generic_string()},
        {"tileSizeMeters", grid.tileSizeMeters},
        {"tileCells", grid.tileCells},
        {"tiles", store.Tiles().size()},
        {"tilesX", store.Settings().tilesX},
        {"tilesZ", store.Settings().tilesZ},
        {"firstTileX", store.Settings().firstTileX},
        {"firstTileZ", store.Settings().firstTileZ},
        {"minHeight", minimum},
        {"maxHeight", maximum},
        {"layers", std::move(layers)},
        {"selectedLayer", shared->selectedLayer},
        {"strokes", shared->strokes},
        {"brushFrames", shared->brushFrames},
        {"brushFrameMeanMs", shared->brushFrameTotalMs / frames},
        {"brushFrameMaxMs", shared->brushFrameMaxMs},
        {"unsaved", store.HasUnsavedTiles()}});
}

void WriteMap(const std::filesystem::path& directory, const char* name, const SampleRect& rect,
              const std::vector<float>& values, Json& out) {
    if (values.size() != rect.Count()) {
        return;
    }
    const std::filesystem::path png = directory / (std::string("erosion_") + name + ".png");
    const std::filesystem::path exr = directory / (std::string("erosion_") + name + ".exr");
    float minimum = 0.0f;
    float maximum = 0.0f;
    if (!WriteGrayPng(png, rect.width, rect.height, values, minimum, maximum)) {
        return;
    }
    HeightImage image{rect.width, rect.height, values};
    std::string error{};
    (void)SaveHeightImageExr(exr, image, error);
    out[name] = {{"png", png.generic_string()}, {"exr", exr.generic_string()}, {"min", minimum}, {"max", maximum}};
}

} // namespace

void RegisterTerrainOperations(IEditorOperationRegistrar& registrar, const std::shared_ptr<EditorShared>& shared,
                               std::vector<std::unique_ptr<IEditorOperationHandler>>& handlers) {
    Registration add(registrar, handlers);

    add.Query("terrainkit.describe", "Describe the open terrain: tiles, height range, layers and brush timing.", {},
              [shared](const Json&, Sink& result) {
                  if (RequireTerrain(result)) {
                      Describe(shared, result);
                  }
              });
    add.Mutation("terrainkit.generate",
                 "Replace the terrain with a noise layer and an empty sculpt layer. One undo step.",
                 {{"amplitudeMeters", Type::Number, "Height of noise value 1."},
                  {"offsetMeters", Type::Number, "Height added to the noise."},
                  {"featureSizeMeters", Type::Number, "World size of the largest noise feature."},
                  {"octaves", Type::Integer, "Noise octaves."},
                  {"seed", Type::Integer, "Noise seed."}},
                 [shared](const Json& parameters, Sink& result) {
                     if (!RequireTerrain(result)) {
                         return;
                     }
                     NoiseLayerSettings noise = DefaultNoise();
                     noise.amplitudeMeters = parameters.value("amplitudeMeters", noise.amplitudeMeters);
                     noise.offsetMeters = parameters.value("offsetMeters", noise.offsetMeters);
                     noise.featureSizeMeters = parameters.value("featureSizeMeters", noise.featureSizeMeters);
                     noise.octaves = parameters.value("octaves", noise.octaves);
                     noise.seed = parameters.value("seed", noise.seed);
                     std::string error{};
                     if (!RegenerateTerrain(*shared, noise, error)) {
                         Fail(result, "GENERATE_FAILED", error);
                         return;
                     }
                     Ok(result, "Terrain generated.", {{"tiles", TerrainStore::Get().Tiles().size()}});
                 });

    add.Query("terrainkit.layer.list", "List the terrain layers from bottom to top with all their settings.", {},
              [](const Json&, Sink& result) {
                  if (!RequireTerrain(result)) {
                      return;
                  }
                  const TerrainData& data = TerrainStore::Get().Data();
                  Json layers = Json::array();
                  for (const HeightLayer& layer : data.Stack().layers) {
                      layers.push_back(LayerToJson(data, layer));
                  }
                  Ok(result, "Terrain layers.", {{"layers", std::move(layers)}});
              });
    add.Mutation("terrainkit.layer.add", "Add a terrain layer. One undo step.",
                 {{"kind", Type::String, "noise, image or sculpt."},
                  {"name", Type::String, "Layer name."},
                  {"index", Type::Integer, "Position counted from the bottom; omit for the top."},
                  {"patch", Type::Json, "Layer fields as in terrainkit.layer.list: blend, opacity, noise, image."}},
                 [shared](const Json& parameters, Sink& result) {
                     if (!RequireTerrain(result)) {
                         return;
                     }
                     const std::string kind = parameters.value("kind", std::string("sculpt"));
                     HeightLayer layer{};
                     bool known = false;
                     for (const auto& entry : kHeightLayerKindNames) {
                         if (kind == entry.name) {
                             layer.kind = entry.value;
                             known = true;
                         }
                     }
                     if (!known) {
                         Fail(result, "UNKNOWN_KIND", "kind must be noise, image or sculpt.");
                         return;
                     }
                     layer.name = parameters.value("name", kind);
                     layer.noise = DefaultNoise();
                     std::string error{};
                     if (parameters.contains("patch") && !PatchLayer(layer, parameters.at("patch"), layer, error)) {
                         Fail(result, "INVALID_PATCH", error);
                         return;
                     }
                     const size_t index = parameters.value("index", TerrainData::kAppend);
                     uint32_t id = 0u;
                     if (!AddTerrainLayer(*shared, std::move(layer), index, id, error)) {
                         Fail(result, "ADD_FAILED", error);
                         return;
                     }
                     shared->selectedLayer = id;
                     Ok(result, "Layer added.", {{"id", id}});
                 });
    add.Mutation("terrainkit.layer.remove", "Remove a terrain layer and its pixels. One undo step.",
                 {{"id", Type::Integer, "Layer id."}}, [shared](const Json& parameters, Sink& result) {
                     if (!RequireTerrain(result)) {
                         return;
                     }
                     std::string error{};
                     if (!RemoveTerrainLayer(*shared, parameters.at("id").get<uint32_t>(), error)) {
                         Fail(result, "REMOVE_FAILED", error);
                         return;
                     }
                     Ok(result, "Layer removed.", Json::object());
                 });
    add.Mutation("terrainkit.layer.move", "Move a terrain layer. One undo step.",
                 {{"id", Type::Integer, "Layer id."}, {"index", Type::Integer, "Position counted from the bottom."}},
                 [shared](const Json& parameters, Sink& result) {
                     if (!RequireTerrain(result)) {
                         return;
                     }
                     std::string error{};
                     if (!MoveTerrainLayer(*shared, parameters.at("id").get<uint32_t>(),
                                           parameters.at("index").get<size_t>(), error)) {
                         Fail(result, "MOVE_FAILED", error);
                         return;
                     }
                     Ok(result, "Layer moved.", Json::object());
                 });
    add.Mutation(
        "terrainkit.layer.set",
        "Merge a patch into a terrain layer (enabled, opacity, blend, masked, name, noise, image). Replacing a "
        "base image or noise keeps the sculpt layers above it. One undo step.",
        {{"id", Type::Integer, "Layer id."}, {"patch", Type::Json, "Partial layer."}},
        [shared](const Json& parameters, Sink& result) {
            if (!RequireTerrain(result)) {
                return;
            }
            const HeightLayer* layer = TerrainStore::Get().Data().FindLayer(parameters.at("id").get<uint32_t>());
            if (layer == nullptr) {
                Fail(result, "UNKNOWN_LAYER", "No terrain layer with that id.");
                return;
            }
            HeightLayer next{};
            std::string error{};
            if (!PatchLayer(*layer, parameters.at("patch"), next, error) || !UpdateTerrainLayer(*shared, next, error)) {
                Fail(result, "SET_FAILED", error);
                return;
            }
            Ok(result, "Layer updated.", LayerToJson(TerrainStore::Get().Data(), next));
        });
    add.Mutation("terrainkit.layer.select", "Select the layer brushes, stamps and erosion write to.",
                 {{"id", Type::Integer, "Layer id."}}, [shared](const Json& parameters, Sink& result) {
                     shared->selectedLayer = parameters.at("id").get<uint32_t>();
                     Ok(result, "Layer selected.", {{"writesTo", TargetSculptLayer(*shared)}});
                 });

    add.Mutation("terrainkit.brush.set", "Change the settings the Sculpt tool uses (same fields as terrainkit.stroke).",
                 {{"patch", Type::Json, "Partial brush settings."}}, [shared](const Json& parameters, Sink& result) {
                     ReadBrushPatch(parameters.at("patch"), shared->brush);
                     Json out{};
                     JSONFIELDS::Write(out, shared->brush, kHeightBrushFields);
                     Ok(result, "Brush updated.", out);
                 });
    add.Mutation("terrainkit.stroke",
                 "Apply a height brush along a world-space path, the same way the Sculpt tool does. One undo step.",
                 {{"op", Type::String, "raise, lower, smooth, flatten, noise, ramp or terrace."},
                  {"radiusMeters", Type::Number, "Brush radius."},
                  {"strength", Type::Number, "Meters per dab, or a 0..1 fraction for smooth, flatten, ramp, terrace."},
                  {"hardness", Type::Number, "Fraction of the radius at full weight."},
                  {"spacing", Type::Number, "Dab spacing as a fraction of the radius."},
                  {"targetHeightMeters", Type::Number, "Target height for flatten."},
                  {"terraceStepMeters", Type::Number, "Step height for terrace."},
                  {"rampSlopeDegrees", Type::Number, "Fixed slope for ramp; 0 joins the ground at the end."},
                  {"noiseScaleMeters", Type::Number, "Noise size for noise."},
                  {"limitMinSlopeDegrees", Type::Number, "Only where the ground is steeper than this."},
                  {"limitMaxSlopeDegrees", Type::Number, "Only where the ground is flatter than this."},
                  {"limitMinHeightMeters", Type::Number, "Only above this height."},
                  {"limitMaxHeightMeters", Type::Number, "Only below this height."},
                  {"points", Type::Json, "World-space path [[x,z],...]."}},
                 [shared](const Json& parameters, Sink& result) {
                     TerrainStore& store = TerrainStore::Get();
                     const uint32_t layer = TargetSculptLayer(*shared);
                     if (!store.IsOpen() || layer == 0u) {
                         Fail(result, "NO_TERRAIN", "No terrain with a sculpt layer is open.");
                         return;
                     }
                     HeightBrushSettings brush = shared->brush;
                     ReadBrushPatch(parameters, brush);
                     const std::vector<MATH::Vec2> path = ReadPath(parameters.at("points"));
                     const auto started = std::chrono::steady_clock::now();
                     TerrainEditRecorder recorder(store.Data());
                     BrushEditResult edit{};
                     std::string error{};
                     if (!ApplyHeightBrush(store.Data(), layer, brush, path, &recorder, edit, error)) {
                         Fail(result, "STROKE_FAILED", error);
                         return;
                     }
                     store.RebakeRect(edit.touched);
                     TerrainEdit undo = recorder.Finish("Sculpt Terrain");
                     const size_t changedTiles = undo.ChangedTiles().size();
                     RecordTerrainEdit(*shared, std::move(undo));
                     const double milliseconds =
                         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
                     Ok(result, "Stroke applied.",
                        {{"dabs", edit.dabs}, {"changedTiles", changedTiles}, {"milliseconds", milliseconds}});
                 });
    add.Mutation(
        "terrainkit.mask.stroke", "Paint a layer's mask along a world-space path. One undo step.",
        {{"id", Type::Integer, "Layer id."},
         {"op", Type::String, "erase hides the layer, paint brings it back."},
         {"radiusMeters", Type::Number, "Brush radius."},
         {"strength", Type::Number, "0..1 per dab."},
         {"hardness", Type::Number, "Fraction of the radius at full weight."},
         {"points", Type::Json, "World-space path [[x,z],...]."}},
        [shared](const Json& parameters, Sink& result) {
            if (!RequireTerrain(result)) {
                return;
            }
            TerrainStore& store = TerrainStore::Get();
            const uint32_t id = parameters.at("id").get<uint32_t>();
            MaskBrushSettings brush = shared->maskBrush;
            Json merged{};
            JSONFIELDS::Write(merged, brush, kMaskBrushFields);
            merged.merge_patch(parameters);
            merged.erase("points");
            merged.erase("id");
            JSONFIELDS::Read(merged, brush, kMaskBrushFields);
            const HeightLayer* layer = store.Data().FindLayer(id);
            const bool wasMasked = layer != nullptr && layer->masked;
            TerrainEditRecorder recorder(store.Data());
            BrushEditResult edit{};
            std::string error{};
            if (!ApplyMaskBrush(store.Data(), id, brush, ReadPath(parameters.at("points")), &recorder, edit, error)) {
                Fail(result, "MASK_FAILED", error);
                return;
            }
            if (wasMasked) {
                store.RebakeRect(edit.touched);
            } else {
                store.RebakeAfterStackChange();
            }
            RecordTerrainEdit(*shared, recorder.Finish("Paint Terrain Mask"));
            Ok(result, "Mask painted.", {{"dabs", edit.dabs}});
        });
    add.Mutation("terrainkit.mask.import",
                 "Load a project image (.exr .r16 .raw, values 0..1) into a layer's mask. One undo step.",
                 {{"id", Type::Integer, "Layer id."},
                  {"source", Type::String, "Project-relative image path."},
                  {"rawWidth", Type::Integer, "Pixel width of an R16 image; 0 means square."},
                  {"originMeters", Type::Json, "World [x, z] of the first pixel."},
                  {"sizeMeters", Type::Json, "World [x, z] span of the image."}},
                 [shared](const Json& parameters, Sink& result) {
                     if (!RequireTerrain(result)) {
                         return;
                     }
                     TerrainStore& store = TerrainStore::Get();
                     HeightImage image{};
                     std::string error{};
                     const std::string source = parameters.at("source").get<std::string>();
                     if (!LoadHeightImage(store.ProjectRoot() /
                                              std::filesystem::path(std::u8string_view(
                                                  reinterpret_cast<const char8_t*>(source.data()), source.size())),
                                          parameters.value("rawWidth", 0u), image, error)) {
                         Fail(result, "IMAGE_FAILED", error);
                         return;
                     }
                     const Json origin = parameters.at("originMeters");
                     const Json size = parameters.at("sizeMeters");
                     TerrainEditRecorder recorder(store.Data());
                     BrushEditResult edit{};
                     if (!ImportLayerMask(store.Data(), parameters.at("id").get<uint32_t>(), image,
                                          {origin.at(0).get<float>(), origin.at(1).get<float>()},
                                          {size.at(0).get<float>(), size.at(1).get<float>()}, &recorder, edit, error)) {
                         Fail(result, "MASK_FAILED", error);
                         return;
                     }
                     store.RebakeAfterStackChange();
                     RecordTerrainEdit(*shared, recorder.Finish("Import Terrain Mask"));
                     Ok(result, "Mask imported.", {{"samples", edit.touched.Count()}});
                 });
    add.Query(
        "terrainkit.mask.export", "Write a layer's mask over the whole terrain to Library/TerrainKit as EXR and PNG.",
        {{"id", Type::Integer, "Layer id."}}, [](const Json& parameters, Sink& result) {
            if (!RequireTerrain(result)) {
                return;
            }
            const TerrainStore& store = TerrainStore::Get();
            HeightImage image{};
            std::string error{};
            const uint32_t id = parameters.at("id").get<uint32_t>();
            if (!ExportLayerMask(store.Data(), id, WholeTerrainRect(store), image, error)) {
                Fail(result, "MASK_FAILED", error);
                return;
            }
            const std::filesystem::path directory = TerrainOutputDirectory(store);
            const std::string stem = "mask_" + std::to_string(id);
            float minimum = 0.0f;
            float maximum = 0.0f;
            if (!WriteGrayPng(directory / (stem + ".png"), image.width, image.height, image.values, minimum, maximum) ||
                !SaveHeightImageExr(directory / (stem + ".exr"), image, error)) {
                Fail(result, "WRITE_FAILED", error);
                return;
            }
            Ok(result, "Mask written.",
               {{"png", (directory / (stem + ".png")).generic_string()},
                {"exr", (directory / (stem + ".exr")).generic_string()},
                {"width", image.width},
                {"height", image.height},
                {"min", minimum},
                {"max", maximum}});
        });

    add.Query("terrainkit.stamp.list", "List the stamps: the built-in ones and the images in Assets/TerrainStamps.", {},
              [](const Json&, Sink& result) { Ok(result, "Stamps.", {{"stamps", ListStampNames()}}); });
    add.Mutation(
        "terrainkit.stamp",
        "Press a stamp into the selected sculpt layer at a world position, like the Stamp tool. One undo step.",
        {{"stamp", Type::String, "Stamp name from terrainkit.stamp.list."},
         {"x", Type::Number, "World X of the center."},
         {"z", Type::Number, "World Z of the center."},
         {"radiusMeters", Type::Number, "Stamp radius."},
         {"heightMeters", Type::Number, "Height of image value 1."},
         {"rotationDegrees", Type::Number, "Rotation."},
         {"edgeFalloff", Type::Number, "Fraction of the radius that fades to 0 at the edge."},
         {"blend", Type::String, "add, max or min."}},
        [shared](const Json& parameters, Sink& result) {
            if (!RequireTerrain(result)) {
                return;
            }
            StampSettings stamp = shared->stamp;
            Json merged{};
            JSONFIELDS::Write(merged, stamp, kStampFields);
            for (const char* key : {"radiusMeters", "heightMeters", "rotationDegrees", "edgeFalloff", "blend"}) {
                if (parameters.contains(key)) {
                    merged[key] = parameters.at(key);
                }
            }
            JSONFIELDS::Read(merged, stamp, kStampFields);
            stamp.center = MATH::Vec2{parameters.at("x").get<float>(), parameters.at("z").get<float>()};
            std::string error{};
            const auto started = std::chrono::steady_clock::now();
            if (!RunStamp(*shared, parameters.value("stamp", shared->stampName), stamp, error)) {
                Fail(result, "STAMP_FAILED", error);
                return;
            }
            const double milliseconds =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            Ok(result, "Stamp applied.", {{"milliseconds", milliseconds}});
        });

    add.Mutation(
        "terrainkit.erode",
        "Run water and thermal erosion over the terrain or a circle, write the result to the selected sculpt "
        "layer and export the flow, deposition and wear maps to Library/TerrainKit. One undo step.",
        {{"x", Type::Number, "World X of the center; omit x and z for the whole terrain."},
         {"z", Type::Number, "World Z of the center."},
         {"radiusMeters", Type::Number, "Half size of the square area around the center."},
         {"hydraulic", Type::Boolean, "Run water erosion."},
         {"thermal", Type::Boolean, "Run thermal erosion."},
         {"iterations", Type::Integer, "Water iterations."},
         {"rainRate", Type::Number, "Rain per second."},
         {"talusAngleDegrees", Type::Number, "Thermal talus angle."},
         {"featherSamples", Type::Integer, "Samples blended at the edge of the area."}},
        [shared](const Json& parameters, Sink& result) {
            if (!RequireTerrain(result)) {
                return;
            }
            TerrainStore& store = TerrainStore::Get();
            ErosionRequest request = shared->erosion;
            request.hydraulic = parameters.value("hydraulic", request.hydraulic);
            request.thermal = parameters.value("thermal", request.thermal);
            request.featherSamples = parameters.value("featherSamples", request.featherSamples);
            request.hydraulicSettings.iterations = parameters.value("iterations", request.hydraulicSettings.iterations);
            request.hydraulicSettings.rainRate = parameters.value("rainRate", request.hydraulicSettings.rainRate);
            request.thermalSettings.talusAngleDegrees =
                parameters.value("talusAngleDegrees", request.thermalSettings.talusAngleDegrees);
            SampleRect rect = WholeTerrainRect(store);
            if (parameters.contains("x") && parameters.contains("z")) {
                rect = IntersectRects(rect, SampleRectAround(store.Settings().grid, parameters.at("x").get<float>(),
                                                             parameters.at("z").get<float>(),
                                                             parameters.value("radiusMeters", 256.0f)));
            }
            ErosionMaps maps{};
            std::string error{};
            const auto started = std::chrono::steady_clock::now();
            if (!RunErosion(*shared, rect, request, &maps, error)) {
                Fail(result, "EROSION_FAILED", error);
                return;
            }
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            Json outputs = Json::object();
            const std::filesystem::path directory = TerrainOutputDirectory(store);
            WriteMap(directory, "flow", rect, maps.flow, outputs);
            WriteMap(directory, "deposition", rect, maps.deposition, outputs);
            WriteMap(directory, "wear", rect, maps.wear, outputs);
            Ok(result, "Erosion applied.",
               {{"seconds", seconds},
                {"samples", rect.Count()},
                {"squareKilometers", static_cast<double>(rect.Count()) * SampleSpacing(store.Settings().grid) *
                                         SampleSpacing(store.Settings().grid) / 1.0e6},
                {"maps", std::move(outputs)}});
        });

    add.Query("terrainkit.height_at", "Read the terrain height at x, z.",
              {{"x", Type::Number, "World X."}, {"z", Type::Number, "World Z."}},
              [](const Json& parameters, Sink& result) {
                  const auto height =
                      TerrainStore::Get().HeightAt(parameters.at("x").get<float>(), parameters.at("z").get<float>());
                  if (!height.has_value()) {
                      Fail(result, "NO_TILE", "No terrain tile at that position.");
                      return;
                  }
                  Ok(result, "Height.", {{"height", *height}});
              });
    add.Query("terrainkit.export_overview",
              "Write a shaded top-down picture of the whole terrain to Library/TerrainKit/overview.png.",
              {{"size", Type::Integer, "Picture width and height in pixels."}},
              [](const Json& parameters, Sink& result) {
                  if (!RequireTerrain(result)) {
                      return;
                  }
                  const uint32_t size = std::clamp(parameters.value("size", 1024u), 16u, 8192u);
                  std::filesystem::path output{};
                  float metersPerPixel = 0.0f;
                  std::string error{};
                  if (!WriteOverviewPng(TerrainStore::Get(), size, output, metersPerPixel, error)) {
                      Fail(result, "WRITE_FAILED", error);
                      return;
                  }
                  Ok(result, "Overview written.",
                     {{"path", output.generic_string()}, {"size", size}, {"metersPerPixel", metersPerPixel}});
              });
    add.Query("terrainkit.export_height",
              "Write the baked heights of the whole terrain to Library/TerrainKit as height.exr (meters) and "
              "height.r16 (0..1 over the grid's height range).",
              {}, [](const Json&, Sink& result) {
                  if (!RequireTerrain(result)) {
                      return;
                  }
                  std::filesystem::path exr{};
                  std::filesystem::path raw{};
                  uint32_t width = 0u;
                  std::string error{};
                  if (!ExportBakedHeight(TerrainStore::Get(), exr, raw, width, error)) {
                      Fail(result, "WRITE_FAILED", error);
                      return;
                  }
                  const auto& grid = TerrainStore::Get().Settings().grid;
                  Ok(result, "Height written.",
                     {{"exr", exr.generic_string()},
                      {"r16", raw.generic_string()},
                      {"width", width},
                      {"heightMin", grid.heightMin},
                      {"heightRange", grid.heightRange}});
              });
}

} // namespace HIKARI::TERRAINKIT
