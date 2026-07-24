#include "HIKARI_ValidationLabPanel.h"

#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"

#include <algorithm>
#include <cstdio>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

#if defined(HIKARI_WITH_EDITOR)
    namespace {

        struct RendererHealthSnapshot {
            RenderSubmissionDebugStats submission{};
            RENDER3D::RUNTIME::SceneRenderCache::Stats scene{};
            RENDER3D::GPUDRIVEN::GpuSceneRegistryStats registry{};
            MESHRENDERER::MeshRendererDebugStats mesh{};
            SHADOW::ShadowMapDebugStats shadow{};
            RENDER3D::ClusterGeometryResourceSystemStats clusterResources{};
            RENDER3D::RenderResourceDescriptorPoolStats descriptorPool{};
        };

        int ClampScore(int score) {
            return (std::max)(0, (std::min)(100, score));
        }

        const char* ScoreBandText(int score) {
            if (score >= 85) {
                return "Good";
            }
            if (score >= 60) {
                return "Watch";
            }
            return "Risk";
        }

        ImVec4 ScoreBandColor(int score) {
            if (score >= 85) {
                return ImVec4(0.28f, 0.82f, 0.45f, 1.0f);
            }
            if (score >= 60) {
                return ImVec4(0.95f, 0.72f, 0.25f, 1.0f);
            }
            return ImVec4(0.95f, 0.34f, 0.32f, 1.0f);
        }

        RendererHealthSnapshot BuildSnapshot() {
            RendererHealthSnapshot out{};
            out.submission = RenderSubmissionSystem::GetDebugStats();
            out.scene = RenderSubmissionSystem::GetSceneRenderCacheStats();
            out.registry = RenderSubmissionSystem::GetGpuSceneRegistryStats();
            out.mesh = MESHRENDERER::GetDebugStats();
            out.shadow = SHADOW::GetDebugStats();
            out.clusterResources = RENDER3D::GetClusterGeometryResourceSystemStats();
            out.descriptorPool = RENDER3D::GetRenderResourceDescriptorPoolStats();
            return out;
        }

        void DrawHealthCard(const char* label, int score, const char* detail) {
            const ImVec4 color = ScoreBandColor(score);
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            ImGui::TextColored(color, "%d / 100 (%s)", score, ScoreBandText(score));

            char overlay[32]{};
            std::snprintf(overlay, sizeof(overlay), "%d", score);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
            ImGui::ProgressBar(static_cast<float>(score) / 100.0f, ImVec2(-1.0f, 0.0f), overlay);
            ImGui::PopStyleColor();
            ImGui::TextDisabled("%s", detail);
        }

        template <typename... Args>
        void MetricRow(const char* label, const char* fmt, Args... args) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text(fmt, args...);
        }

        int ComputeGpuDrivenScore(const RendererHealthSnapshot& s) {
            int score = 100;
            if (!s.submission.gpuDrivenMainRouteActive) {
                score -= 15;
            }
            if (!s.mesh.surfaceGpuSceneSrvValid || !s.mesh.surfaceGpuSceneBufferReady) {
                score -= 35;
            }
            if (s.mesh.surfaceGpuSceneOverflowInstanceCount > 0) {
                score -= 25;
            }
            if (s.registry.unsupportedForwardRecordCount > 0) {
                score -= 10;
            }
            if (s.registry.forwardOpaqueGpuSceneStats.missingResourceHandleInstanceCount > 0) {
                score -= 15;
            }
            return ClampScore(score);
        }

        int ComputeMeshletScore(const RendererHealthSnapshot& s) {
            int score = 100;
            if (!s.mesh.clusterGpuCullReady) {
                score -= 25;
            }
            if (!s.mesh.meshletBackendPipelineReady) {
                score -= 30;
            }
            if (!s.mesh.meshletBackendDispatchArgumentBufferReady ||
                !s.mesh.meshletBackendDispatchCommandSignatureReady) {
                score -= 25;
            }
            if (s.mesh.clusterGpuCullGpuOverflowCount +
                    s.mesh.clusterGpuCullGpuDrawCommandOverflowCount >
                0) {
                score -= 20;
            }
            return ClampScore(score);
        }

        int ComputeResourceScore(const RendererHealthSnapshot& s) {
            int score = 100;
            if (!s.clusterResources.initialized || s.clusterResources.readyResourceCount == 0) {
                score -= 30;
            }
            if (s.clusterResources.failedCount > 0) {
                score -= 20;
            }
            if (s.clusterResources.missingDescriptorCount > 0 ||
                s.clusterResources.descriptorAllocationFailedCount > 0) {
                score -= 25;
            }
            if (!s.descriptorPool.initialized || s.descriptorPool.failedAllocationCount > 0) {
                score -= 25;
            }
            return ClampScore(score);
        }

        int ComputeShadowScore(const RendererHealthSnapshot& s) {
            if (!s.shadow.enabled) {
                return 100;
            }

            int score = 100;
            if (!s.shadow.shadowCacheValid) {
                score -= 20;
            }
            if (s.shadow.shadowGpuSceneOverflowInstanceCount > 0) {
                score -= 20;
            }
            if (s.shadow.shadowStaticSourceInstanceCount > 0 && s.shadow.shadowCacheMissCount > 0 &&
                s.shadow.shadowCacheHitCount == 0) {
                score -= 20;
            }
            if (s.shadow.shadowMeshletRequestedDispatchCount > 0 &&
                s.shadow.shadowMeshletSubmittedDispatchCount == 0) {
                score -= 25;
            }
            return ClampScore(score);
        }

        void DrawHealthOverview(const RendererHealthSnapshot& s) {
            if (ImGui::BeginTable("RendererHealthCards", 2, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                DrawHealthCard(
                    "GPU Driven Path",
                    ComputeGpuDrivenScore(s),
                    "Route, GPU scene residency, upload overflow, and unsupported records.");
                ImGui::TableNextColumn();
                DrawHealthCard(
                    "Meshlet Pipeline",
                    ComputeMeshletScore(s),
                    "Cluster culling, meshlet PSO readiness, indirect args, and overflow state.");
                ImGui::TableNextColumn();
                DrawHealthCard(
                    "Render Resources",
                    ComputeResourceScore(s),
                    "Cluster resources, shader-visible descriptors, and descriptor pool health.");
                ImGui::TableNextColumn();
                DrawHealthCard(
                    "Shadow Path",
                    ComputeShadowScore(s),
                    "Shadow cache health, GPU scene overflow, and meshlet shadow submission.");
                ImGui::EndTable();
            }
        }

        void DrawIssueSummary(const RendererHealthSnapshot& s) {
            ImGui::SeparatorText("Issue Summary");
            if (EDITOR::BeginMetricTable("RendererHealthIssueSummary", 230.0f)) {
                MetricRow("Route / Mainline", "%s / %s",
                    ToString(s.submission.routeMode),
                    s.submission.gpuDrivenMainRouteActive ? "on" : "off");
                MetricRow("GPU Scene Missing Resources / Overflow", "%u / %zu",
                    s.registry.forwardOpaqueGpuSceneStats.missingResourceHandleInstanceCount,
                    s.mesh.surfaceGpuSceneOverflowInstanceCount);
                MetricRow("Cluster Resources Ready / Failed / Missing SRV", "%u / %u / %u",
                    s.clusterResources.readyResourceCount,
                    s.clusterResources.failedCount,
                    s.clusterResources.missingDescriptorCount);
                MetricRow("Meshlet Requested / Submitted / Empty Buckets", "%zu / %zu / %zu",
                    s.mesh.meshletBackendRequestedDispatchCount,
                    s.mesh.meshletBackendSubmittedDispatchCount,
                    s.mesh.meshletBackendSkippedBucketCount);
                MetricRow("Shadow Cache Valid / Hits / Misses / Updates", "%s / %zu / %zu / %zu",
                    s.shadow.shadowCacheValid ? "yes" : "no",
                    s.shadow.shadowCacheHitCount,
                    s.shadow.shadowCacheMissCount,
                    s.shadow.shadowStaticCacheUpdateCount);
                MetricRow("Scene Objects Visible / Hidden / Dirty", "%u / %u / %u",
                    s.scene.visibleObjectCount,
                    s.scene.hiddenObjectCount,
                    s.scene.dirtyObjectCount);
                MetricRow("Scene Surfaces Static / Dynamic / Skinned", "%u / %u / %u",
                    s.scene.staticSurfaceInstanceCount,
                    s.scene.dynamicSurfaceInstanceCount,
                    s.scene.skinnedSurfaceInstanceCount);
                ImGui::EndTable();
            }
        }

        void DrawResourceSummary(const RendererHealthSnapshot& s) {
            ImGui::SeparatorText("Resource Summary");
            if (EDITOR::BeginMetricTable("RendererHealthResourceSummary", 230.0f)) {
                MetricRow("Cluster Surfaces / LOD Ranges / Clusters / Pages", "%u / %u / %u / %u",
                    s.clusterResources.surfaceCount,
                    s.clusterResources.surfaceLodRangeCount,
                    s.clusterResources.clusterCount,
                    s.clusterResources.pageCount);
                MetricRow("Cluster Vertices / Indices / Primitives", "%u / %u / %u",
                    s.clusterResources.vertexCount,
                    s.clusterResources.indexCount,
                    s.clusterResources.meshletPrimitiveCount);
                MetricRow("Cluster GPU Buffer", "%.2f MB",
                    static_cast<double>(s.clusterResources.gpuBufferBytes) / (1024.0 * 1024.0));
                MetricRow("Descriptor Pool Used / Capacity / Failed", "%u / %u / %u",
                    s.descriptorPool.used,
                    s.descriptorPool.capacity,
                    s.descriptorPool.failedAllocationCount);
                ImGui::EndTable();
            }
        }

    } // namespace
#endif

    void ValidationLabPanel::Draw(EditorContext& context, bool& open) const {
#if defined(HIKARI_WITH_EDITOR)
        if (!open) {
            return;
        }
        if (!ImGui::Begin("Renderer Health", &open)) {
            ImGui::End();
            return;
        }

        DrawContents(context);

        ImGui::End();
#else
        (void)context;
        (void)open;
#endif
    }

    void ValidationLabPanel::DrawContents(EditorContext& context) const {
#if defined(HIKARI_WITH_EDITOR)
        (void)context;
        const RendererHealthSnapshot snapshot = BuildSnapshot();
        DrawHealthOverview(snapshot);
        DrawIssueSummary(snapshot);
        DrawResourceSummary(snapshot);
#else
        (void)context;
#endif
    }

} // namespace HIKARI
