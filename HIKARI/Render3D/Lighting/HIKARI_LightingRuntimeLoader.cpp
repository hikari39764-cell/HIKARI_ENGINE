#include "Render3D/Lighting/HIKARI_LightingRuntimeLoader.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Render3D/Lighting/HIKARI_SkyManager.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::RENDER3D::LIGHTING {

    namespace {

        constexpr const char* kSharedBrdfLutPath =
            "Library/Generated/IBL/brdf_lut.dds";

        SceneLightingRuntimeData gLastLightingRuntimeData{};

        void AppendMessage(
            std::vector<std::string>& messages,
            std::string message) {
            messages.push_back(std::move(message));
        }

        bool HasAnyRuntimeResource(
            const SceneLightingRuntimeData& data) {
            return data.skyLoaded ||
                data.globalIblLoaded ||
                data.reflectionProbeLoaded ||
                data.lightProbeVolumeLoaded;
        }

        std::string ResolveSharedBrdfPath(
            const std::string& manifestBrdfPath) {
            return manifestBrdfPath.empty()
                ? std::string{ kSharedBrdfLutPath }
                : manifestBrdfPath;
        }

        const ReflectionProbeProvider* FindReflectionProvider(
            const SceneLightingSet& set,
            const std::string& id) {
            const auto found = std::find_if(
                set.reflectionProbes.begin(),
                set.reflectionProbes.end(),
                [&id](const ReflectionProbeProvider& provider) {
                    return provider.id == id;
                });
            return found == set.reflectionProbes.end()
                ? nullptr
                : &*found;
        }

        const IrradianceVolumeProvider* FindIrradianceProvider(
            const SceneLightingSet& set,
            const std::string& id) {
            const auto found = std::find_if(
                set.irradianceVolumes.begin(),
                set.irradianceVolumes.end(),
                [&id](const IrradianceVolumeProvider& provider) {
                    return provider.id == id;
                });
            return found == set.irradianceVolumes.end()
                ? nullptr
                : &*found;
        }

        bool HasValidReflectionProbe(
            const std::vector<
                REFLECTION::ReflectionProbeRuntimeData>& probes) {
            return std::any_of(
                probes.begin(),
                probes.end(),
                [](const auto& probe) {
                    return probe.valid;
                });
        }

        REFLECTION::ReflectionProbeRuntimeData
            BuildBakedReflectionProbe(
                const ASSETS::LIGHTING::
                    ReflectionProbeBakeRecord& record,
                const ReflectionProbeProvider& authored) {
            REFLECTION::ReflectionProbeRuntimeData probe{};
            probe.enabled = authored.enabled;
            probe.prefilteredResource = LoadCubemapResource(
                "reflection_probe/baked/" + record.id,
                record.prefilteredCubemapPath,
                TextureResourceColorSpace::Linear);
            probe.brdfLutPath =
                ResolveSharedBrdfPath(record.brdfLutPath);
            probe.brdfLutResource = LoadTextureResourceLinear(
                "reflection_probe/brdf_lut",
                probe.brdfLutPath);
            probe.requestedPrefilteredMipCount =
                record.prefilteredMipCount;
            probe.position = authored.position;
            probe.radius = authored.radius;
            probe.intensity = authored.intensity;
            probe.influenceShape = authored.influenceShape;
            probe.projectionShape = authored.projectionShape;
            probe.influenceBoxCenter =
                authored.influenceBoxCenter;
            probe.influenceBoxSize =
                authored.influenceBoxSize;
            probe.projectionBoxCenter =
                authored.projectionBoxCenter;
            probe.projectionBoxSize =
                authored.projectionBoxSize;
            probe.blendDistance = authored.blendDistance;
            probe.priority = authored.priority;
            probe.sourceAssetId = authored.id;
            probe.prefilteredPath =
                record.prefilteredCubemapPath;
            return probe;
        }

    } // namespace

    const char* ToString(LightingRuntimeSource source) {
        switch (source) {
        case LightingRuntimeSource::None:
            return "None";
        case LightingRuntimeSource::AuthoringFallback:
            return "AuthoringFallback";
        case LightingRuntimeSource::BakeManifestDiscovered:
            return "BakeManifestDiscovered";
        case LightingRuntimeSource::BakedRuntime:
            return "BakedRuntime";
        default:
            return "Unknown";
        }
    }

    const SceneLightingRuntimeData&
        GetLastLightingRuntimeData() {
        return gLastLightingRuntimeData;
    }

    void SetLastLightingRuntimeData(
        const SceneLightingRuntimeData& data) {
        gLastLightingRuntimeData = data;
    }

    LightingRuntimeLoadResult LightingRuntimeLoader::Load(
        const LightingRuntimeLoadRequest& request,
        const AssetRegistry& assetRegistry,
        SkyManager& skyManager) const {

        LightingRuntimeLoadResult result{};
        REFLECTION::Reset();
        LIGHTPROBE::Reset();
        result.runtimeData.activeScenario =
            request.lighting.activeScenario.empty()
            ? "Default"
            : request.lighting.activeScenario;

        const bool manifestLoaded = request.preferBakeManifest
            ? TryLoadBakeManifest(
                request,
                result.runtimeData,
                result.messages)
            : false;

        LoadSkyLightingFromAssets(
            request,
            assetRegistry,
            skyManager,
            result.runtimeData,
            result.messages);

        LoadReflectionProbesFromAuthoringSources(
            request,
            assetRegistry,
            result.runtimeData,
            result.messages);

        if (result.runtimeData.source !=
                LightingRuntimeSource::BakedRuntime &&
            result.runtimeData.reflectionProbeLoaded) {
            result.runtimeData.source =
                LightingRuntimeSource::AuthoringFallback;
        } else if (!result.runtimeData.reflectionProbeLoaded &&
            !result.runtimeData.lightProbeVolumeLoaded &&
            manifestLoaded) {
            result.runtimeData.source =
                LightingRuntimeSource::BakeManifestDiscovered;
        } else if (result.runtimeData.source ==
                LightingRuntimeSource::None &&
            HasAnyRuntimeResource(result.runtimeData)) {
            result.runtimeData.source =
                LightingRuntimeSource::AuthoringFallback;
        }

        SetLastLightingRuntimeData(result.runtimeData);
        return result;
    }

    bool LightingRuntimeLoader::TryLoadBakeManifest(
        const LightingRuntimeLoadRequest& request,
        SceneLightingRuntimeData& runtimeData,
        std::vector<std::string>& messages) const {

        if (request.sceneGuid.empty() ||
            request.projectRoot.empty()) {
            return false;
        }

        const std::filesystem::path manifestPath =
            ASSETS::LIGHTING::BuildLightingBakeManifestPath(
                request.projectRoot,
                request.sceneGuid);
        runtimeData.bakeManifestPath =
            manifestPath.generic_string();

        ASSETS::LIGHTING::LightingBakeManifest manifest{};
        std::string message{};
        if (!ASSETS::LIGHTING::LoadLightingBakeManifest(
                manifestPath,
                manifest,
                &message)) {
            AppendMessage(
                messages,
                "[LightingRuntimeLoader][BakeManifest] " +
                    message);
            return false;
        }

        runtimeData.bakeManifestLoaded = true;
        if (!request.expectedSceneHash.empty() &&
            manifest.sceneHash != request.expectedSceneHash) {
            runtimeData.bakeManifestStale = true;
            runtimeData.bakeManifestStaleReason =
                manifest.sceneHash.empty()
                ? "Bake manifest has no scene hash"
                : "Scene authoring changed after bake";
            runtimeData.source =
                LightingRuntimeSource::BakeManifestDiscovered;
            AppendMessage(
                messages,
                "[LightingRuntimeLoader][BakeManifest][STALE] " +
                    runtimeData.bakeManifestStaleReason +
                    "; baked providers are disabled until rebake.");
            return false;
        }
        runtimeData.activeScenario =
            manifest.activeScenario.empty()
            ? "Default"
            : manifest.activeScenario;
        runtimeData.bakedReflectionProbeCount =
            static_cast<uint32_t>(
                manifest.reflectionProbes.size());
        runtimeData.bakedLightProbeCount =
            static_cast<uint32_t>(
                manifest.lightProbes.size());
        runtimeData.bakedLightmapCount =
            static_cast<uint32_t>(
                manifest.lightmaps.size());

        std::vector<
            LIGHTPROBE::LightProbeVolumeLoadRequest>
            lightProbeRequests{};
        for (const ASSETS::LIGHTING::LightProbeBakeRecord&
             record : manifest.lightProbes) {
            const IrradianceVolumeProvider* authored =
                FindIrradianceProvider(
                    request.lighting,
                    record.id);
            if (authored == nullptr ||
                !authored->enabled ||
                record.shDataPath.empty() ||
                (!record.type.empty() &&
                    record.type != "VolumeGrid") ||
                (!record.scenario.empty() &&
                    record.scenario !=
                        runtimeData.activeScenario) ||
                (!authored->scenario.empty() &&
                    authored->scenario !=
                        runtimeData.activeScenario)) {
                continue;
            }
            lightProbeRequests.push_back(
                LIGHTPROBE::LightProbeVolumeLoadRequest{
                    record.id,
                    (request.projectRoot /
                        record.shDataPath).lexically_normal(),
                    authored->intensity,
                    authored->blendDistance,
                    authored->priority
                });
        }
        runtimeData.lightProbeVolumeLoaded =
            LIGHTPROBE::LoadLightProbeVolumeSet(
                std::move(lightProbeRequests),
                &messages);
        const auto& loadedVolumes =
            LIGHTPROBE::GetVolumeSet();
        runtimeData.loadedIrradianceVolumeCount =
            static_cast<uint32_t>(loadedVolumes.size());
        if (!loadedVolumes.empty()) {
            runtimeData.activeLightProbeVolumePath =
                loadedVolumes.front().sourcePath;
        }

        std::vector<
            REFLECTION::ReflectionProbeRuntimeData>
            bakedProbes{};
        for (const ASSETS::LIGHTING::
             ReflectionProbeBakeRecord& record :
             manifest.reflectionProbes) {
            const ReflectionProbeProvider* authored =
                FindReflectionProvider(
                    request.lighting,
                    record.id);
            if (authored == nullptr ||
                !authored->enabled ||
                record.prefilteredCubemapPath.empty() ||
                (!record.scenario.empty() &&
                    record.scenario !=
                        runtimeData.activeScenario) ||
                (!authored->scenario.empty() &&
                    authored->scenario !=
                        runtimeData.activeScenario)) {
                continue;
            }
            bakedProbes.push_back(
                BuildBakedReflectionProbe(
                    record,
                    *authored));
        }
        REFLECTION::SetProbeSet(std::move(bakedProbes));
        const auto& resolvedProbes =
            REFLECTION::GetProbeSet();
        runtimeData.reflectionProbeLoaded =
            HasValidReflectionProbe(resolvedProbes);
        runtimeData.loadedReflectionProbeCount =
            static_cast<uint32_t>(resolvedProbes.size());
        const auto firstValid = std::find_if(
            resolvedProbes.begin(),
            resolvedProbes.end(),
            [](const auto& probe) {
                return probe.valid;
            });
        if (firstValid != resolvedProbes.end()) {
            runtimeData.activeReflectionProbeSourceAssetId =
                firstValid->sourceAssetId;
        }

        if (runtimeData.reflectionProbeLoaded ||
            runtimeData.lightProbeVolumeLoaded) {
            runtimeData.source =
                LightingRuntimeSource::BakedRuntime;
        } else {
            runtimeData.source =
                LightingRuntimeSource::BakeManifestDiscovered;
        }

        AppendMessage(
            messages,
            "[LightingRuntimeLoader][BakeManifest] providers reflection=" +
                std::to_string(
                    runtimeData.loadedReflectionProbeCount) +
                " irradiance=" +
                std::to_string(
                    runtimeData.loadedIrradianceVolumeCount));
        return true;
    }

    void LightingRuntimeLoader::LoadSkyLightingFromAssets(
        const LightingRuntimeLoadRequest& request,
        const AssetRegistry& assetRegistry,
        SkyManager& skyManager,
        SceneLightingRuntimeData& runtimeData,
        std::vector<std::string>& messages) const {

        if (request.skyAssetIds.empty()) {
            IBL::Reset();
            return;
        }

        for (const std::string& skyId :
             request.skyAssetIds) {
            const auto* descriptor =
                assetRegistry.FindAs<SkyAssetDescriptor>(
                    AssetId{ skyId });
            if (descriptor == nullptr) {
                IBL::Reset();
                AppendMessage(
                    messages,
                    "[LightingRuntimeLoader][Sky] descriptor missing: " +
                        skyId);
                continue;
            }

            std::string texturePath =
                descriptor->sourcePath;
            if (!descriptor->textureAssetId.empty()) {
                if (const auto* texture =
                        assetRegistry.FindAs<
                            TextureAssetDescriptor>(
                            AssetId{
                                descriptor->
                                    textureAssetId })) {
                    texturePath = texture->sourcePath;
                }
            }
            skyManager.RegisterOrUpdateAsset(SkyAsset{
                descriptor->id.value,
                descriptor->meshAssetId,
                texturePath,
                descriptor->preferredMode
            });
            runtimeData.skyLoaded = true;
            runtimeData.activeSkyAssetId =
                descriptor->id.value;

            if (!descriptor->hasIbl) {
                IBL::Reset();
                AppendMessage(
                    messages,
                    "[LightingRuntimeLoader][IBL] sky has no IBL artifacts: " +
                        descriptor->id.value);
                continue;
            }

            TextureResourceHandle irradiance{};
            TextureResourceHandle prefiltered{};
            TextureResourceHandle brdf{};
            if (!descriptor->irradiancePath.empty()) {
                irradiance = LoadCubemapResource(
                    "ibl/irradiance/" +
                        descriptor->id.value,
                    descriptor->irradiancePath,
                    TextureResourceColorSpace::Linear);
            }
            if (!descriptor->prefilteredPath.empty()) {
                prefiltered = LoadCubemapResource(
                    "ibl/prefiltered/" +
                        descriptor->id.value,
                    descriptor->prefilteredPath,
                    TextureResourceColorSpace::Linear);
            }
            const std::string brdfLutPath =
                descriptor->brdfLutPath.empty()
                ? std::string{ kSharedBrdfLutPath }
                : descriptor->brdfLutPath;
            if (!brdfLutPath.empty()) {
                brdf = LoadTextureResourceLinear(
                    "ibl/brdf_lut",
                    brdfLutPath);
            }
            IBL::SetFromTextureResources(
                irradiance,
                prefiltered,
                brdf,
                descriptor->prefilteredMipCount);
            runtimeData.globalIblLoaded =
                IsTextureResourceValid(irradiance) ||
                IsTextureResourceValid(prefiltered) ||
                IsTextureResourceValid(brdf);
        }
    }

    void LightingRuntimeLoader::
        LoadReflectionProbesFromAuthoringSources(
            const LightingRuntimeLoadRequest& request,
            const AssetRegistry& assetRegistry,
            SceneLightingRuntimeData& runtimeData,
            std::vector<std::string>& messages) const {

        if (!request.allowAuthoringReflectionProbeFallback) {
            return;
        }

        std::vector<
            REFLECTION::ReflectionProbeRuntimeData> probes =
            REFLECTION::GetProbeSet();
        std::unordered_set<std::string> loadedProviderIds{};
        for (const auto& probe : probes) {
            loadedProviderIds.insert(probe.sourceAssetId);
        }

        for (const ReflectionProbeProvider& authored :
             request.lighting.reflectionProbes) {
            if (!authored.enabled ||
                (!authored.scenario.empty() &&
                    authored.scenario !=
                        runtimeData.activeScenario) ||
                authored.sourceCubemapAssetId.empty() ||
                loadedProviderIds.contains(authored.id)) {
                continue;
            }

            const auto* descriptor =
                assetRegistry.FindAs<SkyAssetDescriptor>(
                    AssetId{
                        authored.sourceCubemapAssetId });
            if (descriptor == nullptr) {
                AppendMessage(
                    messages,
                    "[LightingRuntimeLoader][ReflectionProbeSet] source missing provider=" +
                        authored.id +
                        " asset=" +
                        authored.sourceCubemapAssetId);
                continue;
            }

            REFLECTION::ReflectionProbeRuntimeData probe{};
            probe.enabled = true;
            if (!descriptor->prefilteredPath.empty()) {
                probe.prefilteredResource =
                    LoadCubemapResource(
                        "reflection_probe/prefiltered/" +
                            descriptor->id.value,
                        descriptor->prefilteredPath,
                        TextureResourceColorSpace::Linear);
            }
            probe.brdfLutPath =
                descriptor->brdfLutPath.empty()
                ? std::string{ kSharedBrdfLutPath }
                : descriptor->brdfLutPath;
            if (!probe.brdfLutPath.empty()) {
                probe.brdfLutResource =
                    LoadTextureResourceLinear(
                        "reflection_probe/brdf_lut",
                        probe.brdfLutPath);
            }
            probe.requestedPrefilteredMipCount =
                descriptor->prefilteredMipCount;
            probe.position = authored.position;
            probe.radius = authored.radius;
            probe.intensity = authored.intensity;
            probe.influenceShape = authored.influenceShape;
            probe.projectionShape = authored.projectionShape;
            probe.influenceBoxCenter =
                authored.influenceBoxCenter;
            probe.influenceBoxSize =
                authored.influenceBoxSize;
            probe.projectionBoxCenter =
                authored.projectionBoxCenter;
            probe.projectionBoxSize =
                authored.projectionBoxSize;
            probe.blendDistance = authored.blendDistance;
            probe.priority = authored.priority;
            probe.sourceAssetId = authored.id;
            probe.prefilteredPath =
                descriptor->prefilteredPath;
            probes.push_back(std::move(probe));
        }

        REFLECTION::SetProbeSet(std::move(probes));
        const auto& resolvedProbes =
            REFLECTION::GetProbeSet();
        runtimeData.reflectionProbeLoaded =
            HasValidReflectionProbe(resolvedProbes);
        runtimeData.loadedReflectionProbeCount =
            static_cast<uint32_t>(resolvedProbes.size());
        if (runtimeData.reflectionProbeLoaded &&
            runtimeData.activeReflectionProbeSourceAssetId.empty()) {
            const auto firstValid = std::find_if(
                resolvedProbes.begin(),
                resolvedProbes.end(),
                [](const auto& probe) {
                    return probe.valid;
                });
            if (firstValid != resolvedProbes.end()) {
                runtimeData.activeReflectionProbeSourceAssetId =
                    firstValid->sourceAssetId;
            }
        }
    }

} // namespace HIKARI::RENDER3D::LIGHTING
