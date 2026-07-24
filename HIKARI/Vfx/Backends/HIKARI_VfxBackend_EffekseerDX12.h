#pragma once

#include <string>

#include "Vfx/HIKARI_VfxTypes.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI::VFX::Backend {

bool Initialize(const GFX::Context& ctx);
void Shutdown();
void UpdateContext(const GFX::Context& ctx);
void BeginFrame(float dt);
void EndFrame();

bool LoadEffect(const std::string& assetId, const std::string& sourcePath);
void UnloadEffect(const std::string& assetId);
void ReloadEffect(const std::string& assetId, const std::string& sourcePath);

VfxHandle Play(const std::string& assetId);
VfxHandle PlayAt(const std::string& assetId, const MATH::Vec3& worldPos);
void Stop(VfxHandle handle);
void StopAll();
bool IsAlive(VfxHandle handle);

void SetVisible(VfxHandle handle, bool visible);
void SetPaused(VfxHandle handle, bool paused);
void SetTransform(VfxHandle handle, const Transform3D& transform);

void Render(const Camera3D& camera);

} // namespace HIKARI::VFX::Backend
