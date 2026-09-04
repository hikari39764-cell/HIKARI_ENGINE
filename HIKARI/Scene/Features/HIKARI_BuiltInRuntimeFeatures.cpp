#include "Scene/Features/HIKARI_BuiltInRuntimeFeatures.h"

#include <array>
#include <memory>

#include "Scene/Features/HIKARI_AnimationRuntimeFeature.h"
#include "Scene/Features/HIKARI_BuoyancyRuntimeFeature.h"
#include "Scene/Features/HIKARI_CameraRuntimeFeature.h"
#include "Scene/Features/HIKARI_GameplayRuntimeFeature.h"
#include "Scene/Features/HIKARI_LightingRuntimeFeature.h"
#include "Scene/Features/HIKARI_PhysicsRuntimeFeature.h"
#include "Scene/Features/HIKARI_RenderingRuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/Features/HIKARI_SequenceRuntimeFeature.h"
#include "Scene/Features/HIKARI_SurfaceInteractionRuntimeFeature.h"
#include "Scene/Features/HIKARI_TerrainRuntimeFeature.h"
#include "Scene/Features/HIKARI_UiRuntimeFeature.h"
#include "Scene/Features/HIKARI_VfxRuntimeFeature.h"
#include "Scene/Features/HIKARI_WaterRuntimeFeature.h"

namespace HIKARI {

    namespace {
        class NavigationRuntimeFeature final : public IRuntimeFeature {
        public:
            std::string_view GetFeatureId() const noexcept override {
                return RuntimeFeatureIds::Navigation;
            }

            std::string_view GetDisplayName() const noexcept override {
                return "Navigation";
            }

            std::string_view GetDescription() const noexcept override {
                return "Optional runtime navigation queries over available "
                    "scene geometry.";
            }

            bool Register(RuntimeFeatureContext&) override {
                // Navigation owns a World service instead of Scene component
                // or system registrations. Service composition is performed
                // after the active Feature set has committed.
                return true;
            }
        };

        class ActionPresentationRuntimeFeature final :
            public IRuntimeFeature {
        public:
            std::string_view GetFeatureId() const noexcept override {
                return RuntimeFeatureIds::ActionPresentation;
            }

            std::string_view GetDisplayName() const noexcept override {
                return "Action Presentation";
            }

            std::string_view GetDescription() const noexcept override {
                return "Optional combat trails, telegraphs, afterimages, "
                    "and screen reactions.";
            }

            std::span<const std::string_view>
                GetRequiredFeatureIds() const noexcept override {
                static constexpr std::array<std::string_view, 1> ids{
                    RuntimeFeatureIds::Vfx
                };
                return ids;
            }

            bool Register(RuntimeFeatureContext&) override {
                // This compatibility capability selects composition-root
                // World services. Projects that do not opt in use Vfx
                // directly and carry no action-specific presentation state.
                return true;
            }
        };
    } // namespace

    RuntimeFeatureCatalog CreateBuiltInRuntimeFeatureCatalog() {
        RuntimeFeatureCatalog catalog;
        (void)catalog.Add(CreateRenderingRuntimeFeature());
        (void)catalog.Add(CreateUiRuntimeFeature());
        (void)catalog.Add(CreateLightingRuntimeFeature());
        (void)catalog.Add(CreateTerrainRuntimeFeature());
        (void)catalog.Add(CreateWaterRuntimeFeature());
        (void)catalog.Add(CreateAnimationRuntimeFeature());
        (void)catalog.Add(CreateCameraRuntimeFeature());
        (void)catalog.Add(
            std::make_unique<NavigationRuntimeFeature>());
        (void)catalog.Add(CreatePhysicsRuntimeFeature());
        (void)catalog.Add(CreateSurfaceInteractionRuntimeFeature());
        (void)catalog.Add(CreateBuoyancyRuntimeFeature());
        (void)catalog.Add(CreateGameplayRuntimeFeature());
        (void)catalog.Add(CreateSequenceRuntimeFeature());
        (void)catalog.Add(CreateVfxRuntimeFeature());
        (void)catalog.Add(
            std::make_unique<ActionPresentationRuntimeFeature>());
        return catalog;
    }

} // namespace HIKARI
