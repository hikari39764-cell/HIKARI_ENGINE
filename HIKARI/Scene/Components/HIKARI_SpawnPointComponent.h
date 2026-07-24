#pragma once

#include <string>

#include "HIKARI_IComponent.h"

namespace HIKARI {

    class SpawnPointComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override { return "SpawnPointComponent"; }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        const std::string& GetSpawnPointId() const;
        bool IsEnabled() const;

    private:
        std::string spawnPointId_{ "DefaultSpawn" };
        bool enabled_ = true;
    };

} // namespace HIKARI
