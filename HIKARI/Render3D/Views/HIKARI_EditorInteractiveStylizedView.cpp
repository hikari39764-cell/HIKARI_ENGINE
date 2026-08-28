#include "Render3D/Views/HIKARI_EditorInteractiveStylizedView.h"

#if defined(HIKARI_WITH_EDITOR)

#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Features/HIKARI_RenderFeaturePlanner.h"
#include "Render3D/Features/HIKARI_RenderFeatureSceneAnalysis.h"
#include "Render3D/Features/HIKARI_StylizedScreenFeatureRenderer.h"
#include "Render3D/Lighting/HIKARI_LocalLightList.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"

namespace HIKARI::RENDER3D::EDITORVIEW {

    namespace {
        constexpr DXGI_FORMAT kCompositeFormat =
            DXGI_FORMAT_R16G16B16A16_FLOAT;
        FEATURES::StylizedScreenFeatureRenderer gScreenFeatureRenderer{};

        bool RebindTarget(void* userData) {
            auto* target = static_cast<RenderTarget2D*>(userData);
            if (target == nullptr || !target->IsInitialized()) {
                return false;
            }
            target->Rebind();
            return true;
        }
    }

    EditorInteractiveStylizedView::~EditorInteractiveStylizedView() {
        Shutdown();
    }

    EditorInteractiveStylizedPreparation
        EditorInteractiveStylizedView::Prepare(
            const GPUDRIVEN::GpuDrivenSceneSource& sceneSource) const {

        EditorInteractiveStylizedPreparation result{};
        result.requests =
            FEATURES::AnalyzeRenderFeatureRequests(sceneSource);
        result.featureMaskBackendReady =
            MESHRENDERER::HasStylizedFeatureMaskBackend();
        result.geometryOutlineBackendReady =
            MESHRENDERER::HasGeometryOutlineFeatureBackend();
        result.visibilityOverlayBackendReady =
            MESHRENDERER::HasVisibilityOverlayFeatureBackend();
        result.renderGeometryOutline =
            result.geometryOutlineBackendReady &&
            result.requests.Requests(
                FEATURES::RenderFeatureKind::GeometryOutline);
        result.renderVisibilityOverlay =
            result.visibilityOverlayBackendReady &&
            result.requests.Requests(
                FEATURES::RenderFeatureKind::VisibilityOverlay);
        result.writeFeatureMask = result.featureMaskBackendReady &&
            (result.requests.RequestsOpaqueSurface(
                 FEATURES::RenderFeatureKind::ScreenOutline) ||
             result.requests.RequestsOpaqueSurface(
                 FEATURES::RenderFeatureKind::FaceHairShadow) ||
             (result.requests.RequestsOpaqueSurface(
                  FEATURES::RenderFeatureKind::GeometryOutline) &&
              !result.geometryOutlineBackendReady));
        return result;
    }

    bool EditorInteractiveStylizedView::EnsureCompositeTarget(
        uint32_t width,
        uint32_t height) {

        if (compositeTarget_ != nullptr &&
            compositeTarget_->IsInitialized() &&
            compositeTarget_->GetWidth() == static_cast<int>(width) &&
            compositeTarget_->GetHeight() == static_cast<int>(height) &&
            compositeTarget_->GetFormat() == kCompositeFormat &&
            !compositeTarget_->HasDepth()) {
            compositeTarget_->UpdateContext(GFX::GetCurrentContext());
            return true;
        }

        auto replacement = std::make_unique<RenderTarget2D>();
        replacement->UpdateContext(GFX::GetCurrentContext());
        if (!replacement->Init(
                static_cast<int>(width),
                static_cast<int>(height),
                kCompositeFormat,
                false,
                { 0.0f, 0.0f, 0.0f, 1.0f },
                false,
                false)) {
            return false;
        }
        replacement->SetDebugName(
            "EditorInteractiveView.StylizedComposite");
        compositeTarget_ = std::move(replacement);
        return true;
    }

    bool EditorInteractiveStylizedView::PublishSceneSurfaces(
        RenderTarget2D& sceneTarget,
        uint64_t targetGeneration,
        RenderViewId viewId,
        SURFACE::SceneSurfaceSet& outSurfaces) {

        const GFX::Context& graphics = GFX::GetCurrentContext();
        if (sceneTarget.GetResource() == nullptr ||
            sceneTarget.GetDepthResource() == nullptr ||
            graphics.device == nullptr) {
            return false;
        }
        UpdateRenderResourceDescriptorPoolContext(graphics);
        const uint32_t frameSlot =
            graphics.frameIndex % GFX::kFrameResourceCount;
        RenderResourceView& colorView = sceneColorSrvs_[frameSlot];
        RenderResourceView& depthView = sceneDepthSrvs_[frameSlot];
        if (!colorView.IsValid()) {
            colorView = AllocateRenderResourceDescriptor();
        }
        if (!depthView.IsValid()) {
            depthView = AllocateRenderResourceDescriptor();
        }
        if (!colorView.IsValid() || !depthView.IsValid()) {
            return false;
        }
        if (sceneSrvGenerations_[frameSlot] != targetGeneration) {
            D3D12_SHADER_RESOURCE_VIEW_DESC colorDesc{};
            colorDesc.Shader4ComponentMapping =
                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            colorDesc.Format = sceneTarget.GetFormat();
            colorDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            colorDesc.Texture2D.MipLevels = 1u;
            graphics.device->CreateShaderResourceView(
                sceneTarget.GetResource(), &colorDesc, colorView.cpu);

            D3D12_SHADER_RESOURCE_VIEW_DESC depthDesc{};
            depthDesc.Shader4ComponentMapping =
                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            depthDesc.Format = DXGI_FORMAT_R32_FLOAT;
            depthDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            depthDesc.Texture2D.MipLevels = 1u;
            graphics.device->CreateShaderResourceView(
                sceneTarget.GetDepthResource(), &depthDesc, depthView.cpu);
            sceneSrvGenerations_[frameSlot] = targetGeneration;
        }

        const uint32_t width =
            static_cast<uint32_t>(sceneTarget.GetWidth());
        const uint32_t height =
            static_cast<uint32_t>(sceneTarget.GetHeight());
        outSurfaces.frameIndex = graphics.frameIndex;
        outSurfaces.viewId = viewId;
        outSurfaces.sceneColorOpaque = {
            sceneTarget.GetResource(), colorView.gpu, sceneTarget.GetFormat(),
            width, height, 1u, sceneTarget.GetColorState(), true
        };
        outSurfaces.sceneColorPostOpaque =
            outSurfaces.sceneColorOpaque;
        outSurfaces.sceneDepth = {
            sceneTarget.GetDepthResource(), depthView.gpu,
            DXGI_FORMAT_R32_FLOAT, width, height, 1u,
            sceneTarget.GetDepthState(), true
        };
        outSurfaces.packedSurface = surfaceBuffer_.GetPackedSurfaceView();
        outSurfaces.surfaceFeatureMask = surfaceBuffer_.GetFeatureMaskView();
        outSurfaces.stylizedOutlineStyle =
            surfaceBuffer_.GetStylizedOutlineStyleView();
        return outSurfaces.HasOpaqueSurface() &&
            outSurfaces.HasStylizedFeatureMask() &&
            outSurfaces.HasStylizedOutlineStyle();
    }

    bool EditorInteractiveStylizedView::Resolve(
        RenderTarget2D& sceneTarget,
        uint64_t targetGeneration,
        RenderViewId viewId,
        const Camera3D& camera,
        const SceneEnvironment& environment,
        const LIGHTING::SceneLightingSet& lighting,
        uint32_t width,
        uint32_t height,
        const EditorInteractiveStylizedPreparation& preparation,
        RenderTarget2D*& outDisplaySource) {

        outDisplaySource = &sceneTarget;
        FEATURES::RenderFeatureAvailability availability{};
        availability.width = width;
        availability.height = height;
        if (preparation.geometryOutlineBackendReady) {
            availability.inputMask |= FEATURES::RenderFeatureInputBit(
                FEATURES::RenderFeatureInput::GeometryBackend);
        }
        if (preparation.visibilityOverlayBackendReady) {
            availability.inputMask |= FEATURES::RenderFeatureInputBit(
                FEATURES::RenderFeatureInput::VisibilityBackend);
        }

        SURFACE::SceneSurfaceSet surfaces{};
        bool inputsReady = false;
        if (preparation.writeFeatureMask) {
            inputsReady = PublishSceneSurfaces(
                sceneTarget, targetGeneration, viewId, surfaces);
            if (surfaces.sceneColorPostOpaque.IsSampleable()) {
                availability.inputMask |= FEATURES::RenderFeatureInputBit(
                    FEATURES::RenderFeatureInput::SceneColor);
            }
            if (surfaces.sceneDepth.IsSampleable()) {
                availability.inputMask |= FEATURES::RenderFeatureInputBit(
                    FEATURES::RenderFeatureInput::SceneDepth);
            }
            if (surfaces.packedSurface.IsSampleable()) {
                availability.inputMask |= FEATURES::RenderFeatureInputBit(
                    FEATURES::RenderFeatureInput::SurfaceNormals);
            }
            if (surfaces.surfaceFeatureMask.IsSampleable()) {
                availability.inputMask |= FEATURES::RenderFeatureInputBit(
                    FEATURES::RenderFeatureInput::SurfaceFeatureMask);
            }
        }
        plan_ = FEATURES::BuildRenderFeaturePlan(
            preparation.requests, availability);
        if (!plan_.screenCompositePassRequired) {
            compositeTarget_.reset();
            return !preparation.writeFeatureMask || inputsReady;
        }
        if (!inputsReady || !EnsureCompositeTarget(width, height) ||
            !sceneTarget.BeginDepthShaderRead()) {
            return false;
        }

        compositeTarget_->BeginCapture(0.0f, 0.0f, 0.0f, 1.0f);
        PIPELINE::RenderTargetAccess targetAccess{};
        targetAccess.userData = compositeTarget_.get();
        targetAccess.rebind = &RebindTarget;
        FEATURES::StylizedScreenFeatureSettings settings{};
        settings.viewProj = camera.GetViewProj();
        if (const LIGHTING::SceneLightProvider* directional =
                LIGHTING::SelectDirectionalLight(lighting)) {
            settings.directionalLightWS[0] = directional->direction.x;
            settings.directionalLightWS[1] = directional->direction.y;
            settings.directionalLightWS[2] = directional->direction.z;
        } else {
            settings.directionalLightWS[0] =
                environment.directional.direction.x;
            settings.directionalLightWS[1] =
                environment.directional.direction.y;
            settings.directionalLightWS[2] =
                environment.directional.direction.z;
        }
        const bool rendered = gScreenFeatureRenderer.Render(
            GFX::GetCurrentContext(),
            targetAccess,
            surfaces,
            plan_,
            settings);
        compositeTarget_->EndCapture();
        sceneTarget.EndDepthShaderRead();
        if (!rendered) {
            return false;
        }
        outDisplaySource = compositeTarget_.get();
        return true;
    }

    void EditorInteractiveStylizedView::Shutdown() {
        for (RenderResourceView& view : sceneColorSrvs_) {
            if (view.IsValid()) {
                (void)ReleaseRenderResourceDescriptor(view);
                view = {};
            }
        }
        for (RenderResourceView& view : sceneDepthSrvs_) {
            if (view.IsValid()) {
                (void)ReleaseRenderResourceDescriptor(view);
                view = {};
            }
        }
        sceneSrvGenerations_.fill(0u);
        surfaceBuffer_.Release();
        compositeTarget_.reset();
        plan_ = {};
    }

    void ShutdownEditorInteractiveStylizedSharedResources() {
        gScreenFeatureRenderer.Release();
    }

} // namespace HIKARI::RENDER3D::EDITORVIEW

#endif
