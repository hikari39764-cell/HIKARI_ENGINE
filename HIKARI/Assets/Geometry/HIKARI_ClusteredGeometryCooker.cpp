#include "Assets/Geometry/HIKARI_ClusteredGeometryCooker.h"

#include <algorithm>
#include <string>
#include <vector>

#include "Assets/Geometry/Cooking/Internal/HIKARI_ClusteredGeometryCookInternal.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::ASSETS::GEOMETRY {

    using namespace COOKING;

    namespace {
        void CookPrimitive(
            const ModelAsset& model,
            const MeshPrimitive& primitive,
            uint32_t nodeIndex,
            int nodeSkinIndex,
            uint32_t meshIndex,
            uint32_t primitiveIndex,
            const MATH::Mat4& matrix,
            const ClusteredGeometryCookSettings& settings,
            ClusteredGeometryAsset& asset,
            ClusteredGeometryBuildReport& report) {

            SurfaceCookInput work{};
            if (!BuildSurfaceCookInput(
                    model,
                    primitive,
                    nodeIndex,
                    nodeSkinIndex,
                    meshIndex,
                    primitiveIndex,
                    matrix,
                    settings,
                    work)) {
                if (primitive.hasMorphTargets) {
                    ++report.skippedMorphPrimitiveCount;
                } else if (primitive.layout == VertexLayoutKind::SkinnedPNTTJW ||
                    !primitive.skinnedVertices.empty() ||
                    nodeSkinIndex >= 0) {
                    ++report.skippedSkinnedPrimitiveCount;
                } else {
                    ++report.skippedInvalidPrimitiveCount;
                }
                return;
            }

            (void)AssembleClusteredSurface(
                work,
                settings,
                asset,
                report);
        }

    }

    bool CookClusteredGeometryFromModel(
        const ModelAsset& model,
        const AssetGuid& sourceGuid,
        const ClusteredGeometryCookSettings& settings,
        ClusteredGeometryAsset& outAsset,
        ClusteredGeometryBuildReport& outReport) {

        outAsset = {};
        outReport = {};
        outAsset.sourceModelGuid = sourceGuid;
        outAsset.sourceModelPath = model.sourcePath;
        const SourceGeometryCounts sourceCounts = CountSourceGeometry(model);
        outReport.sourceStaticTriangleCount = sourceCounts.staticTriangleCount;
        outReport.sourceStaticVertexCount = sourceCounts.staticVertexCount;
        // Static surfaces bake the node transform; skinned surfaces retain bind-pose
        // model-space vertices and are identified by their per-surface flag.
        RENDER3D::CLUSTER::AddFlag(
            outAsset.flags,
            RENDER3D::CLUSTER::ClusteredGeometryFlags::NodeTransformBaked);
        RENDER3D::CLUSTER::AddFlag(
            outAsset.flags,
            RENDER3D::CLUSTER::ClusteredGeometryFlags::ClusterLocalIndices);
        RENDER3D::CLUSTER::AddFlag(
            outAsset.flags,
            RENDER3D::CLUSTER::ClusteredGeometryFlags::SourceMapping);
        RENDER3D::CLUSTER::AddFlag(
            outAsset.flags,
            RENDER3D::CLUSTER::ClusteredGeometryFlags::MeshletPrimitiveTable);
        RENDER3D::CLUSTER::AddFlag(
            outAsset.flags,
            RENDER3D::CLUSTER::ClusteredGeometryFlags::MeshletReady);
        RENDER3D::CLUSTER::AddFlag(
            outAsset.flags,
            RENDER3D::CLUSTER::ClusteredGeometryFlags::LodRanges);
        AppendMaterialSlots(model, outAsset);

        if (model.meshes.empty()) {
            outReport.messages.push_back("model has no mesh");
            outAsset.valid = false;
            return false;
        }

        const std::vector<MATH::Mat4> nodeGlobals = BOUNDS::BuildModelNodeGlobals(model);
        if (!model.nodes.empty()) {
            for (uint32_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
                const ModelNode& node = model.nodes[nodeIndex];
                if (node.meshIndex < 0 || node.meshIndex >= static_cast<int>(model.meshes.size())) {
                    continue;
                }
                const MeshAsset& mesh = model.meshes[static_cast<size_t>(node.meshIndex)];
                for (uint32_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex) {
                    CookPrimitive(
                        model,
                        mesh.primitives[primitiveIndex],
                        nodeIndex,
                        node.skinIndex,
                        static_cast<uint32_t>(node.meshIndex),
                        primitiveIndex,
                        nodeGlobals[static_cast<size_t>(nodeIndex)],
                        settings,
                        outAsset,
                        outReport);
                }
            }
        } else {
            for (uint32_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex) {
                const MeshAsset& mesh = model.meshes[meshIndex];
                for (uint32_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex) {
                    CookPrimitive(
                        model,
                        mesh.primitives[primitiveIndex],
                        RENDER3D::CLUSTER::kInvalidClusterIndex,
                        -1,
                        meshIndex,
                        primitiveIndex,
                        MATH::Mat4::Identity(),
                        settings,
                        outAsset,
                        outReport);
                }
            }
        }

        outAsset.skippedSkinnedPrimitiveCount = outReport.skippedSkinnedPrimitiveCount;
        outAsset.skippedMorphPrimitiveCount = outReport.skippedMorphPrimitiveCount;
        outAsset.skippedInvalidPrimitiveCount = outReport.skippedInvalidPrimitiveCount;
        outAsset.skippedPrimitiveCount =
            outAsset.skippedSkinnedPrimitiveCount +
            outAsset.skippedMorphPrimitiveCount +
            outAsset.skippedInvalidPrimitiveCount;
        outAsset.unsupportedPrimitiveModeCount = outReport.unsupportedPrimitiveModeCount;
        outAsset.unsupportedFeatureCount = outReport.unsupportedFeatureCount;
        outAsset.skippedMorphPrimitiveCount += model.importDiagnostics.skippedMorphPrimitiveCount;
        outAsset.unsupportedPrimitiveModeCount += model.importDiagnostics.unsupportedPrimitiveModeCount;
        outAsset.unsupportedFeatureCount += model.importDiagnostics.unsupportedFeatureCount;
        outAsset.skippedPrimitiveCount =
            outAsset.skippedSkinnedPrimitiveCount +
            outAsset.skippedMorphPrimitiveCount +
            outAsset.skippedInvalidPrimitiveCount;
        outAsset.totalTriangleCount = RENDER3D::CLUSTER::CountClusterTriangles(outAsset);
        outAsset.totalVertexCount = static_cast<uint32_t>(outAsset.packedVertices.size());
        outAsset.localBounds = ComputeVertexBounds(outAsset.packedVertices);
        const bool hasSkinningData = std::any_of(
            outAsset.surfaces.begin(),
            outAsset.surfaces.end(),
            [](const ClusterSurface& surface) {
                return RENDER3D::CLUSTER::HasFlag(
                    surface.flags,
                    ClusterSurfaceFlags::Skinned);
            });
        if (hasSkinningData) {
            RENDER3D::CLUSTER::AddFlag(
                outAsset.flags,
                RENDER3D::CLUSTER::ClusteredGeometryFlags::SkinningData);
            outAsset.packedSkinningVertices.resize(outAsset.packedVertices.size());
            for (size_t vertexIndex = 0;
                 vertexIndex < outAsset.packedVertices.size();
                 ++vertexIndex) {
                const ClusterVertex& source = outAsset.packedVertices[vertexIndex];
                ClusterSkinVertex& destination =
                    outAsset.packedSkinningVertices[vertexIndex];
                for (size_t influence = 0; influence < 4u; ++influence) {
                    destination.joints[influence] = source.joints[influence];
                    destination.weights[influence] = source.weights[influence];
                }
            }
        }
        FinalizeAssetLodMetrics(outAsset, settings);
        outAsset.valid =
            !outAsset.surfaces.empty() &&
            !outAsset.surfaceLodRanges.empty() &&
            !outAsset.surfaceSections.empty() &&
            !outAsset.clusters.empty() &&
            !outAsset.packedVertices.empty() &&
            !outAsset.packedIndices.empty() &&
            !outAsset.meshletPrimitives.empty();

        FillCookReportFromAsset(outAsset, outReport);
        if (outAsset.valid) {
            FillPackedGeometryByteReport(outAsset, outReport);
            ApplyCookBudgetReport(settings, outReport);
        }
        outReport.skippedMorphPrimitiveCount = outAsset.skippedMorphPrimitiveCount;
        outReport.unsupportedPrimitiveModeCount = outAsset.unsupportedPrimitiveModeCount;
        outReport.unsupportedFeatureCount = outAsset.unsupportedFeatureCount;
        for (const std::string& message : model.importDiagnostics.messages) {
            outReport.messages.push_back(message);
        }
        if (!outAsset.valid) {
            outReport.messages.push_back("no clusterable static primitive");
        }
        return outAsset.valid;
    }

} // namespace HIKARI::ASSETS::GEOMETRY
