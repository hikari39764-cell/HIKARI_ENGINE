#include "HIKARI_VfxBackend_EffekseerDX12.h"

#include <unordered_map>

#include "Effekseer.h"
#include "EffekseerRendererDX12.h"

namespace HIKARI::VFX::Backend {

namespace {
    Effekseer::ManagerRef gManager;
    Effekseer::Backend::GraphicsDeviceRef gGraphicsDevice;
    EffekseerRenderer::RendererRef gRenderer;
    Effekseer::RefPtr<EffekseerRenderer::SingleFrameMemoryPool> gMemoryPool;
    Effekseer::RefPtr<EffekseerRenderer::CommandList> gCommandList;
    GFX::Context gCtx{};
    bool gInitialized = false;
    float gTime = 0.0f;
    uint32_t gNextHandle = 1;

    std::unordered_map<std::string, Effekseer::EffectRef> gEffects{};
    std::unordered_map<VfxHandle, Effekseer::Handle> gInstanceMap{};

    Effekseer::Matrix44 ToEffekseer(const MATH::Mat4& m) {
        Effekseer::Matrix44 out{};
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                out.Values[r][c] = m.m[r][c];
            }
        }
        return out;
    }
}

bool Initialize(const GFX::Context& ctx) {
    if (gInitialized) {
        return true;
    }

    if (!ctx.device || !ctx.queue || !ctx.cmdList) {
        return false;
    }

    gCtx = ctx;
    gManager = Effekseer::Manager::Create(8000);
    if (gManager == nullptr) return false;

    gGraphicsDevice = EffekseerRendererDX12::CreateGraphicsDevice(ctx.device, ctx.queue, 3);
    if (gGraphicsDevice == nullptr) {
        Shutdown();
        return false;
    }

    DXGI_FORMAT colorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    gRenderer = EffekseerRendererDX12::Create(gGraphicsDevice, &colorFormat, 1, DXGI_FORMAT_D32_FLOAT, false, 8000);
    if (gRenderer == nullptr) {
        Shutdown();
        return false;
    }

    gMemoryPool = EffekseerRenderer::CreateSingleFrameMemoryPool(gRenderer->GetGraphicsDevice());
    gCommandList = EffekseerRenderer::CreateCommandList(gRenderer->GetGraphicsDevice(), gMemoryPool);
    if (gMemoryPool == nullptr || gCommandList == nullptr) {
        Shutdown();
        return false;
    }

    gManager->SetSpriteRenderer(gRenderer->CreateSpriteRenderer());
    gManager->SetRibbonRenderer(gRenderer->CreateRibbonRenderer());
    gManager->SetRingRenderer(gRenderer->CreateRingRenderer());
    gManager->SetTrackRenderer(gRenderer->CreateTrackRenderer());
    gManager->SetModelRenderer(gRenderer->CreateModelRenderer());
    gManager->SetTextureLoader(
        EffekseerRenderer::CreateTextureLoader(
            gGraphicsDevice,
            nullptr,
            Effekseer::ColorSpaceType::Linear
        )
    );
    gManager->SetModelLoader(gRenderer->CreateModelLoader());
    gManager->SetMaterialLoader(gRenderer->CreateMaterialLoader());
    gManager->SetCurveLoader(Effekseer::MakeRefPtr<Effekseer::CurveLoader>());
    gManager->SetCoordinateSystem(Effekseer::CoordinateSystem::RH);
    

    gInitialized = true;
    return true;
}

void Shutdown() {
    if (!gInitialized) {
        return;
    }

    if (gManager != nullptr) gManager->StopAllEffects();
    if (gRenderer!= nullptr) gRenderer->SetCommandList(nullptr);

    gEffects.clear();
    gInstanceMap.clear();
    gCommandList = nullptr;
    gMemoryPool = nullptr;
    gRenderer = nullptr;
    gGraphicsDevice = nullptr;
    gManager = nullptr;
    gTime = 0.0f;
    gNextHandle = 1;
    gInitialized = false;
}

void UpdateContext(const GFX::Context& ctx) {
    gCtx = ctx;
}

void BeginFrame(float dt) {
    if (!gInitialized || gManager == nullptr) {
        return;
    }

    gTime += dt;

    Effekseer::Manager::UpdateParameter p{};
    p.DeltaFrame = dt * 60.0f;
    gManager->Update(p);

    for (auto it = gInstanceMap.begin(); it != gInstanceMap.end();) {
        if (!gManager->Exists(it->second)) {
            it = gInstanceMap.erase(it);
        } else {
            ++it;
        }
    }
}

void EndFrame() {
}

bool LoadEffect(const std::string& assetId, const std::string& sourcePath) {
    if (!gInitialized || assetId.empty() || sourcePath.empty()) {
        return false;
    }

    if (gEffects.contains(assetId)) {
        return true;
    }

    std::u16string path(sourcePath.begin(), sourcePath.end());
    Effekseer::EffectRef effect = Effekseer::Effect::Create(gManager, reinterpret_cast<const char16_t*>(path.c_str()));
    if (effect == nullptr) {
        return false;
    }

    gEffects[assetId] = effect;
    return true;
}

void UnloadEffect(const std::string& assetId) {
    gEffects.erase(assetId);
}

void ReloadEffect(const std::string& assetId, const std::string& sourcePath) {
    UnloadEffect(assetId);
    LoadEffect(assetId, sourcePath);
}

static VfxHandle PlayImpl(const std::string& assetId, const MATH::Vec3* pos) {
    if (!gInitialized || gManager == nullptr) {
        return kInvalidVfxHandle;
    }
    auto it = gEffects.find(assetId);
    if (it == gEffects.end() || it->second == nullptr) {
        return kInvalidVfxHandle;
    }
    const float x = pos ? pos->x : 0.0f;
    const float y = pos ? pos->y : 0.0f;
    const float z = pos ? pos->z : 0.0f;
    const Effekseer::Handle raw = gManager->Play(it->second, x, y, z);
    if (raw < 0) {
        return kInvalidVfxHandle;
    }

    const VfxHandle handle = gNextHandle++;
    gInstanceMap[handle] = raw;
    return handle;
}

VfxHandle Play(const std::string& assetId) {
    return PlayImpl(assetId, nullptr);
}

VfxHandle PlayAt(const std::string& assetId, const MATH::Vec3& worldPos) {
    return PlayImpl(assetId, &worldPos);
}

void Stop(VfxHandle handle) {
    auto it = gInstanceMap.find(handle);
    if (it == gInstanceMap.end() || gManager == nullptr) return;
    gManager->StopEffect(it->second);
    gInstanceMap.erase(it);
}

void StopAll() {
    if (gManager != nullptr) gManager->StopAllEffects();
    gInstanceMap.clear();
}

bool IsAlive(VfxHandle handle) {
    auto it = gInstanceMap.find(handle);
    if (it == gInstanceMap.end() || gManager == nullptr) return false;
    return gManager->Exists(it->second);
}

void SetVisible(VfxHandle handle, bool visible) {
    auto it = gInstanceMap.find(handle);
    if (it == gInstanceMap.end() || gManager == nullptr) return;
    gManager->SetShown(it->second, visible);
}

void SetPaused(VfxHandle handle, bool paused) {
    auto it = gInstanceMap.find(handle);
    if (it == gInstanceMap.end() || gManager == nullptr) return;
    gManager->SetPaused(it->second, paused);
}

void SetTransform(VfxHandle handle, const Transform3D& transform) {
    auto it = gInstanceMap.find(handle);
    if (it == gInstanceMap.end() || gManager == nullptr) return;
    const auto& p = transform.position;
	const auto& s = transform.scale;
    gManager->SetLocation(it->second, p.x, p.y, p.z);
    gManager->SetScale(it->second, s.x, s.y, s.z);
}

void Render(const Camera3D& camera) {
    if (!gInitialized || gManager == nullptr || gRenderer == nullptr || gMemoryPool == nullptr || gCommandList == nullptr || gCtx.cmdList == nullptr) return;

    gMemoryPool->NewFrame();
    EffekseerRendererDX12::BeginCommandList(gCommandList, gCtx.cmdList);
    gRenderer->SetCommandList(gCommandList);

    Effekseer::Manager::LayerParameter layer{};
    auto eye = camera.GetPosition();
    layer.ViewerPosition = Effekseer::Vector3D(eye.x, eye.y, eye.z);
    gManager->SetLayerParameter(0, layer);

    gRenderer->SetTime(gTime);
    gRenderer->SetProjectionMatrix(ToEffekseer(camera.GetProj()));
    gRenderer->SetCameraMatrix(ToEffekseer(camera.GetView()));

    gRenderer->BeginRendering();
    Effekseer::Manager::DrawParameter draw{};
    draw.ZNear = 0.1f;
    draw.ZFar = 1000.0f;
    draw.ViewProjectionMatrix = gRenderer->GetCameraProjectionMatrix();
    gManager->Draw(draw);
    gRenderer->EndRendering();

    gRenderer->SetCommandList(nullptr);
    EffekseerRendererDX12::EndCommandList(gCommandList);
}

} // namespace HIKARI::VFX::Backend
