#include "HIKARI_VfxSystem.h"

#include "Vfx/Backends/HIKARI_VfxBackend_EffekseerDX12.h"
#include "Vfx/Runtime/HIKARI_VfxAsset.h"

namespace HIKARI::VFX {

bool Initialize(const GFX::Context& ctx) {
    return Backend::Initialize(ctx);
}

void Shutdown() {
    Backend::Shutdown();
}

void UpdateContext(const GFX::Context& ctx) {
    Backend::UpdateContext(ctx);
}

void BeginFrame(float dt) {
    Backend::BeginFrame(dt);
}

void EndFrame() {
    Backend::EndFrame();
}

bool LoadEffect(const std::string& assetId) {
    return Backend::LoadEffect(assetId, ResolveEffectPath(assetId));
}

void UnloadEffect(const std::string& assetId) {
    Backend::UnloadEffect(assetId);
}

void ReloadEffect(const std::string& assetId) {
    Backend::ReloadEffect(assetId, ResolveEffectPath(assetId));
}

VfxHandle Play(const std::string& assetId) {
    LoadEffect(assetId);
    return Backend::Play(assetId);
}

VfxHandle PlayAt(const std::string& assetId, const MATH::Vec3& worldPos) {
    LoadEffect(assetId);
    return Backend::PlayAt(assetId, worldPos);
}

VfxHandle PlayAttached(const std::string& assetId, SceneObjectId objectId) {
    (void)objectId;
    return Play(assetId);
}

void Stop(VfxHandle handle) {
    Backend::Stop(handle);
}

void StopAll() {
    Backend::StopAll();
}

bool IsAlive(VfxHandle handle) {
    return Backend::IsAlive(handle);
}

void SetVisible(VfxHandle handle, bool visible) {
    Backend::SetVisible(handle, visible);
}

void SetPaused(VfxHandle handle, bool paused) {
    Backend::SetPaused(handle, paused);
}

void SetTransform(VfxHandle handle, const Transform3D& transform) {
    Backend::SetTransform(handle, transform);
}

void Render(const Camera3D& camera) {
    Backend::Render(camera);
}

} // namespace HIKARI::VFX
