#include "HIKARI_DocumentComponentAuthoringService.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "Scene/HIKARI_SceneComponentId.h"

namespace HIKARI {

    ComponentAddResult DocumentComponentAuthoringService::AddComponent(
        const ComponentRegistry& registry,
        SceneObjectData& object,
        std::string_view typeName) const {

        SceneObjectData working = object;
        NormalizeSceneComponentIds(working);
        ComponentAddResult result{};
        std::vector<std::string> dependencyPath{};
        if (!AddSingleComponent(
                registry,
                working,
                typeName,
                false,
                dependencyPath,
                result)) {
            result.documentChanged = false;
            result.addedComponents.clear();
            result.autoAddedDependencies.clear();
            return result;
        }

        object = std::move(working);

        if (result.autoAddedDependencies.empty()) {
            result.message = "Added component: " + std::string(typeName);
        } else {
            std::ostringstream oss;
            oss << "Added component: " << typeName << " (auto-added dependencies: ";
            for (size_t i = 0; i < result.autoAddedDependencies.size(); ++i) {
                if (i > 0) {
                    oss << ", ";
                }
                oss << result.autoAddedDependencies[i];
            }
            oss << ")";
            result.message = oss.str();
        }
        return result;
    }

    ComponentEditResult
        DocumentComponentAuthoringService::DuplicateComponent(
            const ComponentRegistry& registry,
            SceneObjectData& object,
            size_t componentIndex) const {
        ComponentEditResult result{};
        if (componentIndex >= object.components.size()) {
            result.message = "Component no longer exists.";
            return result;
        }
        SceneComponentData source =
            object.components[componentIndex];
        source.id = AllocateSceneComponentId(object);
        result.componentType = source.type;
        const ComponentTypeInfo* info = registry.Find(source.type);
        if (info == nullptr) {
            result.message =
                "Unavailable component types cannot be duplicated.";
            return result;
        }
        if (!info->allowMultiple) {
            result.message =
                info->presentation.displayName +
                " allows only one instance.";
            return result;
        }
        object.components.insert(
            object.components.begin() +
                static_cast<std::ptrdiff_t>(componentIndex + 1u),
            std::move(source));
        result.success = true;
        result.documentChanged = true;
        result.message = "Duplicated " +
            info->presentation.displayName + ".";
        return result;
    }

    ComponentEditResult
        DocumentComponentAuthoringService::RemoveComponent(
            const ComponentRegistry& registry,
            SceneDocument& document,
            SceneObjectData& object,
            size_t componentIndex) const {
        ComponentEditResult result{};
        if (componentIndex >= object.components.size()) {
            result.message = "Component no longer exists.";
            return result;
        }
        const std::string removedType =
            object.components[componentIndex].type;
        result.componentType = removedType;
        const size_t remainingSameType = static_cast<size_t>(
            std::count_if(
                object.components.begin(),
                object.components.end(),
                [&removedType](const SceneComponentData& component) {
                    return component.type == removedType;
                })) - 1u;
        if (remainingSameType == 0u) {
            for (size_t index = 0;
                index < object.components.size();
                ++index) {
                if (index == componentIndex) {
                    continue;
                }
                const SceneComponentData& dependent =
                    object.components[index];
                const ComponentTypeInfo* dependentInfo =
                    registry.Find(dependent.type);
                if (dependentInfo == nullptr) {
                    continue;
                }
                if (std::find(
                        dependentInfo->requiredComponents.begin(),
                        dependentInfo->requiredComponents.end(),
                        removedType) !=
                    dependentInfo->requiredComponents.end()) {
                    result.message =
                        "Remove " +
                        dependentInfo->presentation.displayName +
                        " first.";
                    return result;
                }
            }
        }
        const ComponentTypeInfo* removedInfo =
            registry.Find(removedType);
        const std::string displayName =
            removedInfo != nullptr &&
            !removedInfo->presentation.displayName.empty()
                ? removedInfo->presentation.displayName
                : removedType;
        object.components.erase(
            object.components.begin() +
            static_cast<std::ptrdiff_t>(componentIndex));
        if (removedType == "CameraComponent" &&
            remainingSameType == 0u &&
            document.camera.defaultCameraObjectId == object.id) {

            document.camera.defaultCameraObjectId.reset();
        }
        result.success = true;
        result.documentChanged = true;
        result.message = "Removed " + displayName + ".";
        return result;
    }

    bool DocumentComponentAuthoringService::HasComponent(const SceneObjectData& object, std::string_view typeName) const {
        for (const SceneComponentData& component : object.components) {
            if (component.type == typeName) {
                return true;
            }
        }
        return false;
    }

    bool DocumentComponentAuthoringService::ContainsTypeName(const std::vector<std::string>& values, std::string_view typeName) const {
        for (const std::string& value : values) {
            if (value == typeName) {
                return true;
            }
        }
        return false;
    }

    bool DocumentComponentAuthoringService::AddSingleComponent(
        const ComponentRegistry& registry,
        SceneObjectData& object,
        std::string_view typeName,
        bool isDependency,
        std::vector<std::string>& dependencyPath,
        ComponentAddResult& result) const {

        const auto cycleBegin = std::find(
            dependencyPath.begin(),
            dependencyPath.end(),
            typeName);
        if (cycleBegin != dependencyPath.end()) {
            std::ostringstream oss;
            oss << "Component dependency cycle: ";
            for (auto component = cycleBegin;
                component != dependencyPath.end();
                ++component) {
                if (component != cycleBegin) {
                    oss << " -> ";
                }
                oss << *component;
            }
            oss << " -> " << typeName;
            result.success = false;
            result.message = oss.str();
            return false;
        }

        const ComponentTypeInfo* info = registry.Find(typeName);
        if (!info) {
            result.success = false;
            result.message = "Unknown component type: " + std::string(typeName);
            return false;
        }

        if (!info->allowMultiple && HasComponent(object, typeName)) {
            result.success = false;
            result.message = "Component already exists and does not allow multiple: " + std::string(typeName);
            return false;
        }

        for (const std::string& incompatibleType : info->incompatibleComponents) {
            if (HasComponent(object, incompatibleType)) {
                result.success = false;
                result.message = "Component conflict: " + std::string(typeName) + " is incompatible with " + incompatibleType;
                return false;
            }
        }

        for (const SceneComponentData& existing : object.components) {
            const ComponentTypeInfo* existingInfo = registry.Find(existing.type);
            if (!existingInfo) {
                continue;
            }
            if (ContainsTypeName(existingInfo->incompatibleComponents, typeName)) {
                result.success = false;
                result.message = "Component conflict: " + existing.type + " is incompatible with " + std::string(typeName);
                return false;
            }
        }

        dependencyPath.emplace_back(typeName);
        const size_t dependenciesBegin = result.addedComponents.size();
        for (const std::string& dependencyType : info->requiredComponents) {
            if (HasComponent(object, dependencyType)) {
                continue;
            }

            if (!AddSingleComponent(
                    registry,
                    object,
                    dependencyType,
                    true,
                    dependencyPath,
                    result)) {
                dependencyPath.pop_back();
                if (result.message.empty()) {
                    result.message = "Failed to add required component: " + dependencyType;
                }
                return false;
            }
        }
        dependencyPath.pop_back();

        if (info->configureDependencies) {
            std::vector<std::string> newlyAddedDependencies(
                result.addedComponents.begin() +
                    static_cast<std::ptrdiff_t>(dependenciesBegin),
                result.addedComponents.end());
            info->configureDependencies(
                object,
                newlyAddedDependencies);
        }

        nlohmann::json properties = nlohmann::json::object();
        if (info->initializeDefaults) {
            info->initializeDefaults(object, properties);
        }

        SceneComponentData component{
            info->typeName,
            std::move(properties)
        };
        component.id = AllocateSceneComponentId(object);
        object.components.push_back(std::move(component));
        result.documentChanged = true;
        result.addedComponents.push_back(info->typeName);
        if (isDependency) {
            result.autoAddedDependencies.push_back(info->typeName);
        }
        result.success = true;
        return true;
    }

} // namespace HIKARI
