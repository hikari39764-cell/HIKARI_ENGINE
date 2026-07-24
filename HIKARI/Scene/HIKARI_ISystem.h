#pragma once

#include <string_view>

namespace HIKARI {

class World;
struct FrameContext;

class ISystem {
public:
    virtual ~ISystem() = default;

    virtual std::string_view GetName() const = 0;

    virtual void OnWorldAttached(World&) {}
    virtual void OnWorldDetached(World&) {}

    virtual void PreUpdate(World&, const FrameContext&) {}
    virtual void PreFixedUpdate(World&, const FrameContext&) {}
    virtual void FixedUpdate(World&, const FrameContext&) {}
    virtual void PostFixedUpdate(World&, const FrameContext&) {}
    virtual void Update(World&, const FrameContext&) {}
    virtual void LateUpdate(World&, const FrameContext&) {}

    virtual void PreRender(World&, const FrameContext&) {}
    virtual void Render(World&, const FrameContext&) {}
    virtual void PostRender(World&, const FrameContext&) {}
};

} // namespace HIKARI
