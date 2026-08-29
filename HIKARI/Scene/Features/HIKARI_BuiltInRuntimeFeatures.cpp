#include "Scene/Features/HIKARI_BuiltInRuntimeFeatures.h"

#include "Scene/Features/HIKARI_AnimationRuntimeFeature.h"
#include "Scene/Features/HIKARI_BuoyancyRuntimeFeature.h"
#include "Scene/Features/HIKARI_CameraRuntimeFeature.h"
#include "Scene/Features/HIKARI_GameplayRuntimeFeature.h"
#include "Scene/Features/HIKARI_LightingRuntimeFeature.h"
#include "Scene/Features/HIKARI_PhysicsRuntimeFeature.h"
#include "Scene/Features/HIKARI_RenderingRuntimeFeature.h"
#include "Scene/Features/HIKARI_SequenceRuntimeFeature.h"
#include "Scene/Features/HIKARI_SurfaceInteractionRuntimeFeature.h"
#include "Scene/Features/HIKARI_TerrainRuntimeFeature.h"
#include "Scene/Features/HIKARI_VfxRuntimeFeature.h"
#include "Scene/Features/HIKARI_WaterRuntimeFeature.h"

namespace HIKARI {

    RuntimeFeatureCatalog CreateBuiltInRuntimeFeatureCatalog() {
        RuntimeFeatureCatalog catalog;
        (void)catalog.Add(CreateRenderingRuntimeFeature());
        (void)catalog.Add(CreateLightingRuntimeFeature());
        (void)catalog.Add(CreateTerrainRuntimeFeature());
        (void)catalog.Add(CreateWaterRuntimeFeature());
        (void)catalog.Add(CreateAnimationRuntimeFeature());
        (void)catalog.Add(CreateCameraRuntimeFeature());
        (void)catalog.Add(CreatePhysicsRuntimeFeature());
        (void)catalog.Add(CreateSurfaceInteractionRuntimeFeature());
        (void)catalog.Add(CreateBuoyancyRuntimeFeature());
        (void)catalog.Add(CreateGameplayRuntimeFeature());
        (void)catalog.Add(CreateSequenceRuntimeFeature());
        (void)catalog.Add(CreateVfxRuntimeFeature());
        return catalog;
    }

} // namespace HIKARI
