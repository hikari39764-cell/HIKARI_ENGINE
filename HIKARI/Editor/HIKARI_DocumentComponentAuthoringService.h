#pragma once

#include <string>
#include <vector>

#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    struct ComponentAddResult {
        bool success = false;
        bool documentChanged = false;
        std::string message{};
        std::vector<std::string> addedComponents{};
        std::vector<std::string> autoAddedDependencies{};
    };

    struct ComponentEditResult {
        bool success = false;
        bool documentChanged = false;
        std::string componentType{};
        std::string message{};
    };

    class DocumentComponentAuthoringService {
    public:
        std::vector<std::string> GetMissingOptionalComponents(
            const ComponentRegistry& registry,
            const SceneObjectData& object) const;
        ComponentAddResult AddComponent(
            const ComponentRegistry& registry,
            SceneObjectData& object,
            std::string_view typeName) const;
        ComponentEditResult DuplicateComponent(
            const ComponentRegistry& registry,
            SceneObjectData& object,
            size_t componentIndex) const;
        ComponentEditResult RemoveComponent(
            const ComponentRegistry& registry,
            SceneDocument& document,
            SceneObjectData& object,
            size_t componentIndex) const;

    private:
        bool HasComponent(
            const SceneObjectData& object,
            std::string_view typeName) const;
        bool ContainsTypeName(
            const std::vector<std::string>& values,
            std::string_view typeName) const;
        bool AddSingleComponent(
            const ComponentRegistry& registry,
            SceneObjectData& object,
            std::string_view typeName,
            bool isDependency,
            std::vector<std::string>& dependencyPath,
            ComponentAddResult& result) const;
    };

} // namespace HIKARI
