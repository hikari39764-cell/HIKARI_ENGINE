#pragma once

#include <string_view>

namespace HIKARI {

class World;
struct FrameContext;
struct GameProjectMassSystemScheduleDesc;
struct GameProjectMassPartitionInput;
struct GameProjectMassPartitionOutput;
struct GameProjectMassCommandWriter;
struct GameProjectRenderObserverDesc;
struct GameProjectRenderFrameView;

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

// Opt-in sibling interface. Existing ISystem vtables and serial behavior stay
// unchanged; SystemScheduler discovers only explicit Mass worker adapters.
class IMassWorkerSystem {
public:
    virtual ~IMassWorkerSystem() = default;

    virtual const GameProjectMassSystemScheduleDesc&
        GetMassSystemSchedule() const noexcept = 0;
    virtual bool ExecuteMassPartition(
        const GameProjectMassPartitionInput& input,
        GameProjectMassPartitionOutput& output,
        const GameProjectMassCommandWriter& commands,
        const FrameContext& frame) const noexcept = 0;
};

// RenderGraph calls this sibling only at its stable owner-thread observation
// anchor. It does not change the existing ISystem or IGameSystem vtables.
class IRenderFrameObserverSystem {
public:
    virtual ~IRenderFrameObserverSystem() = default;

    virtual const GameProjectRenderObserverDesc&
        GetRenderObserverDesc() const noexcept = 0;
    virtual bool ObserveRenderFrame(
        const GameProjectRenderFrameView& frame) const noexcept = 0;
};

} // namespace HIKARI
