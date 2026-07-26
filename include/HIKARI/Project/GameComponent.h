#pragma once

#include "HIKARI/Project/Inspector.h"
#include "HIKARI/Project/PropertyArchive.h"
#include "HIKARI/Project/WorldView.h"

namespace HIKARI {

    class IGameComponent {
    public:
        virtual ~IGameComponent() = default;

        virtual void OnAttach(const ObjectView&) {}
        virtual void Update(float deltaTime) {
            (void)deltaTime;
        }
        virtual void LoadProperties(const PropertyReader&) {}
        virtual void SaveProperties(PropertyWriter&) const {}
        virtual void BuildInspector(GameProjectInspector&) {}
    };

} // namespace HIKARI
