#pragma once

#include "HIKARI/Project/FrameContext.h"
#include "HIKARI/Project/PropertyArchive.h"
#include "HIKARI/Project/WorldView.h"

namespace HIKARI {

    class IGameSystem {
    public:
        virtual ~IGameSystem() = default;

        virtual void LoadSettings(const PropertyReader&) {}
        virtual void SaveSettings(PropertyWriter&) const {}

        virtual void OnWorldAttached(WorldView&) {}
        virtual void OnWorldDetached(WorldView&) {}
        virtual void PreUpdate(WorldView&, const FrameContext&) {}
        virtual void PreFixedUpdate(WorldView&, const FrameContext&) {}
        virtual void FixedUpdate(WorldView&, const FrameContext&) {}
        virtual void PostFixedUpdate(WorldView&, const FrameContext&) {}
        virtual void Update(WorldView&, const FrameContext&) {}
        virtual void LateUpdate(WorldView&, const FrameContext&) {}
        virtual void PreRender(WorldView&, const FrameContext&) {}
        virtual void Render(WorldView&, const FrameContext&) {}
        virtual void PostRender(WorldView&, const FrameContext&) {}
    };

} // namespace HIKARI
