#include "Scene/Features/HIKARI_BuiltInRuntimeFeatures.h"

#include "Scene/Features/HIKARI_AnimationRuntimeFeature.h"
#include "Scene/Features/HIKARI_CameraRuntimeFeature.h"
#include "Scene/Features/HIKARI_GameplayRuntimeFeature.h"
#include "Scene/Features/HIKARI_PhysicsRuntimeFeature.h"
#include "Scene/Features/HIKARI_RenderingRuntimeFeature.h"
#include "Scene/Features/HIKARI_SequenceRuntimeFeature.h"

namespace HIKARI {

    RuntimeFeatureCatalog CreateBuiltInRuntimeFeatureCatalog() {
        RuntimeFeatureCatalog catalog;
        (void)catalog.Add(CreateRenderingRuntimeFeature());
        (void)catalog.Add(CreateAnimationRuntimeFeature());
        (void)catalog.Add(CreateCameraRuntimeFeature());
        (void)catalog.Add(CreatePhysicsRuntimeFeature());
        (void)catalog.Add(CreateGameplayRuntimeFeature());
        (void)catalog.Add(CreateSequenceRuntimeFeature());
        return catalog;
    }

} // namespace HIKARI
