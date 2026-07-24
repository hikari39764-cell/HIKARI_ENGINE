#include "HIKARI_ComponentRegistry.h"

#include "HIKARI_GameObject.h"

namespace HIKARI {

    bool ComponentRegistry::Register(ComponentTypeInfo info) {
        if (info.typeName.empty() || !info.factory) {
            return false;
        }
        if (info.presentation.displayName.empty()) {
            info.presentation.displayName = info.typeName;
        }
        const std::string typeName = info.typeName;
        return byName_.emplace(typeName, std::move(info)).second;
    }

    void ComponentRegistry::Clear() noexcept {
        byName_.clear();
    }

    const ComponentTypeInfo* ComponentRegistry::Find(std::string_view typeName) const {
        const auto it = byName_.find(std::string(typeName));
        if (it == byName_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    std::vector<std::string> ComponentRegistry::GetTypeNames() const {
        std::vector<std::string> names;
        names.reserve(byName_.size());
        for (const auto& [name, _] : byName_) {
            names.push_back(name);
        }
        return names;
    }

    std::vector<const ComponentTypeInfo*> ComponentRegistry::GetTypeInfos() const {
        std::vector<const ComponentTypeInfo*> infos;
        infos.reserve(byName_.size());
        for (const auto& [_, info] : byName_) {
            infos.push_back(&info);
        }
        return infos;
    }

    IComponent* ComponentRegistry::AddComponentToObject(GameObject& object, std::string_view typeName) const {
        const ComponentTypeInfo* info = Find(typeName);
        if (!info) {
            return nullptr;
        }

        std::unique_ptr<IComponent> component = info->factory();
        return object.AddComponentInstance(std::move(component));
    }

} // namespace HIKARI
