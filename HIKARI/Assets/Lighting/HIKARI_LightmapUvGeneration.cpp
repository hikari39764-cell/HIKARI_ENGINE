#include "Assets/Lighting/HIKARI_LightmapUvGeneration.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(HIKARI_WITH_EDITOR)
#include <xatlas.h>
#endif

#include "Assets/Lighting/HIKARI_LightmapSurfaceAnalysis.h"
#include "RenderContracts/Lighting/HIKARI_LightingBakePolicy.h"
#include "Assets/Lighting/HIKARI_LightmapUvGenerationInput.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"

namespace HIKARI::ASSETS::LIGHTING {

namespace {

enum class PrimitiveGenerationResult {
    Succeeded,
    Failed,
    Canceled,
};

bool IsGenerationCandidate(const MeshPrimitive& primitive, AlphaMode alphaMode) {
    return primitive.layout == VertexLayoutKind::StaticPNTT && !primitive.hasMorphTargets &&
           alphaMode == AlphaMode::Opaque && !primitive.staticVertices.empty() && primitive.indices.size() >= 3u &&
           primitive.indices.size() % 3u == 0u;
}

#if defined(HIKARI_WITH_EDITOR)
bool IsCancellationRequested(const LightmapUvGenerationCallbacks* callbacks) {
    return callbacks != nullptr && callbacks->isCancellationRequested && callbacks->isCancellationRequested();
}

bool XatlasProgressCallback(xatlas::ProgressCategory, int, void* userData) {
    const auto* callbacks = static_cast<const LightmapUvGenerationCallbacks*>(userData);
    return !IsCancellationRequested(callbacks);
}

bool ValidateGeneratedTopology(const xatlas::Mesh& generated, const INTERNAL::XatlasInputGeometry& input,
                               std::string& outMessage) {
    if (generated.indexCount != input.indices.size() || generated.indexArray == nullptr ||
        generated.vertexArray == nullptr) {
        outMessage = "xatlas changed the source triangle count";
        return false;
    }
    for (uint32_t index = 0u; index < generated.indexCount; ++index) {
        const uint32_t generatedVertexIndex = generated.indexArray[index];
        if (generatedVertexIndex >= generated.vertexCount) {
            outMessage = "xatlas produced an out-of-range index";
            return false;
        }
        if (generated.vertexArray[generatedVertexIndex].xref != input.indices[index]) {
            outMessage = "xatlas changed source triangle topology";
            return false;
        }
    }
    return true;
}

PrimitiveGenerationResult GeneratePrimitiveLightmapUv(MeshPrimitive& primitive,
                                                      const LightmapUvGenerationSettings& settings,
                                                      const LightmapUvGenerationCallbacks* callbacks,
                                                      std::string& outMessage) {
    if (IsCancellationRequested(callbacks)) {
        outMessage = "lightmap UV generation canceled";
        return PrimitiveGenerationResult::Canceled;
    }
    if (primitive.staticVertices.size() > std::numeric_limits<uint32_t>::max() ||
        primitive.indices.size() > std::numeric_limits<uint32_t>::max()) {
        outMessage = "mesh exceeds xatlas 32-bit limits";
        return PrimitiveGenerationResult::Failed;
    }

    INTERNAL::XatlasInputGeometry input = INTERNAL::PrepareXatlasInputGeometry(primitive);
    if (!input.CanGenerate()) {
        outMessage = INTERNAL::DescribeXatlasInputRejection(input.analysis);
        return PrimitiveGenerationResult::Failed;
    }
    const std::string repairMessage = INTERNAL::DescribeXatlasInputRepair(input.analysis);

    xatlas::Atlas* atlas = xatlas::Create();
    if (atlas == nullptr) {
        outMessage = "xatlas allocation failed";
        return PrimitiveGenerationResult::Failed;
    }
    xatlas::SetProgressCallback(atlas, XatlasProgressCallback, const_cast<LightmapUvGenerationCallbacks*>(callbacks));

    xatlas::MeshDecl declaration{};
    declaration.vertexPositionData = &primitive.staticVertices.front().position;
    declaration.vertexPositionStride = sizeof(Vertex3D);
    if (input.analysis.useVertexNormals) {
        declaration.vertexNormalData = &primitive.staticVertices.front().normal;
        declaration.vertexNormalStride = sizeof(Vertex3D);
    }
    declaration.vertexCount = static_cast<uint32_t>(primitive.staticVertices.size());
    declaration.indexData = input.indices.data();
    declaration.indexCount = static_cast<uint32_t>(input.indices.size());
    declaration.indexFormat = xatlas::IndexFormat::UInt32;

    const xatlas::AddMeshError addError = xatlas::AddMesh(atlas, declaration, 1u);
    if (addError != xatlas::AddMeshError::Success) {
        outMessage = "xatlas rejected mesh: " + std::string(xatlas::StringForEnum(addError));
        xatlas::Destroy(atlas);
        return PrimitiveGenerationResult::Failed;
    }

    xatlas::ChartOptions chartOptions{};
    chartOptions.fixWinding = true;

    xatlas::PackOptions packOptions{};
    packOptions.padding = std::clamp(settings.padding, 1u, 64u);
    packOptions.bilinear = true;
    packOptions.blockAlign = true;
    packOptions.bruteForce = false;
    packOptions.createImage = false;
    // 参照解像度に chart が収まらないと xatlas は atlas を複数に分ける。一つに収まるまで解像度を倍にする
    // （64 texel では暖房器のように chart の多い形状が収まらず、自動展開に失敗して焼き込みから外れていた）。
    xatlas::ComputeCharts(atlas, chartOptions);
    for (uint32_t resolution = std::clamp(settings.resolution, 64u, 8192u);; resolution *= 2u) {
        packOptions.resolution = resolution;
        xatlas::PackCharts(atlas, packOptions);
        if (atlas->atlasCount <= 1u || resolution >= 8192u || IsCancellationRequested(callbacks))
            break;
    }
    if (IsCancellationRequested(callbacks)) {
        outMessage = "lightmap UV generation canceled";
        xatlas::Destroy(atlas);
        return PrimitiveGenerationResult::Canceled;
    }

    if (atlas->meshCount != 1u || atlas->meshes == nullptr || atlas->width == 0u || atlas->height == 0u ||
        atlas->atlasCount > 1u) {
        outMessage = "xatlas produced an unsupported atlas layout";
        xatlas::Destroy(atlas);
        return PrimitiveGenerationResult::Failed;
    }

    const xatlas::Mesh& generated = atlas->meshes[0];
    if (!ValidateGeneratedTopology(generated, input, outMessage)) {
        xatlas::Destroy(atlas);
        return PrimitiveGenerationResult::Failed;
    }
    std::vector<Vertex3D> vertices{};
    vertices.reserve(generated.vertexCount);
    // xatlas は細い単一 chart の外周を切り詰める。参照解像度に padding の
    // 両側分を確保し、UV だけを中央へ移す（UV0 と描画形状は変更しない）。
    const uint32_t minimumExtent = packOptions.padding * 2u + 1u;
    const uint32_t width = (std::max)(atlas->width, minimumExtent);
    const uint32_t height = (std::max)(atlas->height, minimumExtent);
    const float offsetX = static_cast<float>(width - atlas->width) * 0.5f;
    const float offsetY = static_cast<float>(height - atlas->height) * 0.5f;
    const float invWidth = 1.0f / static_cast<float>(width);
    const float invHeight = 1.0f / static_cast<float>(height);
    for (uint32_t index = 0u; index < generated.vertexCount; ++index) {
        const xatlas::Vertex& generatedVertex = generated.vertexArray[index];
        if (generatedVertex.xref >= primitive.staticVertices.size() || generatedVertex.atlasIndex != 0) {
            outMessage = "xatlas produced an invalid vertex mapping";
            xatlas::Destroy(atlas);
            return PrimitiveGenerationResult::Failed;
        }
        Vertex3D vertex = primitive.staticVertices[generatedVertex.xref];
        vertex.uv1 = {(generatedVertex.uv[0] + offsetX) * invWidth, (generatedVertex.uv[1] + offsetY) * invHeight};
        vertices.push_back(vertex);
    }

    std::vector<uint32_t> indices(generated.indexArray, generated.indexArray + generated.indexCount);
    MeshPrimitive candidate = primitive;
    candidate.staticVertices = std::move(vertices);
    candidate.indices = std::move(indices);
    candidate.hasLightmapUv = true;
    candidate.lightmapUvLayout = {width, height, packOptions.padding};

    const LightmapSurfaceAnalysis validation = AnalyzeLightmapSurface(candidate, AlphaMode::Opaque);
    if (!validation.IsEligible()) {
        outMessage = "generated UV validation failed: " + DescribeLightmapSurfaceAnalysis(validation);
        xatlas::Destroy(atlas);
        return PrimitiveGenerationResult::Failed;
    }

    primitive.staticVertices = std::move(candidate.staticVertices);
    primitive.indices = std::move(candidate.indices);
    primitive.hasLightmapUv = true;
    primitive.lightmapUvLayout = candidate.lightmapUvLayout;
    xatlas::Destroy(atlas);
    outMessage = repairMessage;
    return PrimitiveGenerationResult::Succeeded;
}
#endif

uint32_t CountModelPrimitives(const ModelAsset& model) {
    uint64_t primitiveCount = 0u;
    for (const MeshAsset& mesh : model.meshes) {
        primitiveCount += mesh.primitives.size();
    }
    return static_cast<uint32_t>(
        (std::min)(primitiveCount, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
}

void ReportProgress(const LightmapUvGenerationCallbacks* callbacks, uint32_t currentPrimitive, uint32_t primitiveCount,
                    std::string_view primitiveName) {
    if (callbacks != nullptr && callbacks->reportProgress) {
        callbacks->reportProgress(currentPrimitive, primitiveCount, primitiveName);
    }
}

} // namespace

bool PrepareModelLightmapUvs(ModelAsset& model, const LightmapUvGenerationSettings& settings,
                             LightmapUvGenerationReport& outReport, const LightmapUvGenerationCallbacks* callbacks) {
    outReport = {};
    const uint32_t primitiveCount = CountModelPrimitives(model);
    uint32_t currentPrimitive = 0u;
    for (MeshAsset& mesh : model.meshes) {
        for (MeshPrimitive& primitive : mesh.primitives) {
            ++outReport.primitiveCount;
            ++currentPrimitive;
            ReportProgress(callbacks, currentPrimitive, primitiveCount, primitive.name);
            if (callbacks != nullptr && callbacks->isCancellationRequested && callbacks->isCancellationRequested()) {
                outReport.canceled = true;
                return false;
            }

            const AlphaMode alphaMode = primitive.materialIndex < model.materials.size()
                                            ? model.materials[primitive.materialIndex].alphaMode
                                            : AlphaMode::Opaque;
            const LightmapSurfaceAnalysis analysis = AnalyzeLightmapSurface(primitive, alphaMode);
            if (analysis.IsEligible()) {
                ++outReport.authoredValidCount;
            } else if (!primitive.hasLightmapUv) {
                ++outReport.missingCount;
            } else {
                ++outReport.invalidCount;
            }

            const bool force = settings.mode == LightmapUvGenerationMode::ForceRegenerate;
            const bool repair =
                settings.mode == LightmapUvGenerationMode::GenerateIfMissingOrInvalid && !analysis.IsEligible();
            if (!force && !repair) {
                continue;
            }
            if (!IsGenerationCandidate(primitive, alphaMode)) {
                ++outReport.skippedUnsupportedCount;
                continue;
            }

#if defined(HIKARI_WITH_EDITOR)
            std::string message{};
            const std::string label = mesh.name + "/" + primitive.name;
            // UV1 を材質が参照する場合、Lightmap 用の展開で著者の座標を上書きしない。
            if (primitive.materialIndex < model.materials.size() &&
                MATERIAL_POLICY::UsesTextureCoordinateSet(model.materials[primitive.materialIndex],
                                                          static_cast<int>(kSurfaceLightmapUvSet))) {
                ++outReport.skippedUnsupportedCount;
                if (outReport.messages.size() < 16u) {
                    outReport.messages.push_back(
                        label +
                        ": lightmap UV generation skipped because a material texture uses TEXCOORD_1. "
                        "Preserve the authored UV1; provide non-overlapping UV1 or move material textures to UV0.");
                }
                continue;
            }
            const PrimitiveGenerationResult generationResult =
                GeneratePrimitiveLightmapUv(primitive, settings, callbacks, message);
            if (generationResult == PrimitiveGenerationResult::Succeeded) {
                ++outReport.generatedCount;
                if (!message.empty() && outReport.messages.size() < 16u) {
                    outReport.messages.push_back(label + ": " + message);
                }
            } else if (generationResult == PrimitiveGenerationResult::Canceled) {
                outReport.canceled = true;
                return false;
            } else {
                // 失敗は修復の報告より先に並べ、件数の上限で落とさない（どの形状が焼き込みから外れたかが分かるように）。
                ++outReport.generationFailedCount;
                outReport.messages.insert(outReport.messages.begin() + (outReport.generationFailedCount - 1u),
                                          label + ": FAILED " + message);
            }
#else
            ++outReport.generationFailedCount;
            if (outReport.messages.empty()) {
                outReport.messages.push_back("automatic lightmap UV generation is Editor/Cook only");
            }
#endif
        }
    }
    return outReport.generationFailedCount == 0u;
}

const char* ToString(LightmapUvGenerationMode mode) noexcept {
    switch (mode) {
    case LightmapUvGenerationMode::Preserve: return "Preserve";
    case LightmapUvGenerationMode::ForceRegenerate: return "ForceRegenerate";
    case LightmapUvGenerationMode::GenerateIfMissingOrInvalid:
    default: return "GenerateIfMissingOrInvalid";
    }
}

} // namespace HIKARI::ASSETS::LIGHTING
