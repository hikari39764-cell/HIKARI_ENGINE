#include "HIKARI_SpawnPointComponent.h"

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

    void SpawnPointComponent::Serialize(nlohmann::json& out) const {
        out["spawnPointId"] = spawnPointId_;
        out["enabled"] = enabled_;
    }

    void SpawnPointComponent::Deserialize(const nlohmann::json& in) {
        spawnPointId_ = in.value("spawnPointId", spawnPointId_);
        enabled_ = in.value("enabled", enabled_);
    }

    void SpawnPointComponent::BuildInspector(IInspectorBuilder& builder) {
        builder.String("Spawn Point ID", spawnPointId_);
        builder.Bool("Enabled", enabled_);
    }

    const std::string& SpawnPointComponent::GetSpawnPointId() const {
        return spawnPointId_;
    }

    bool SpawnPointComponent::IsEnabled() const {
        return enabled_;
    }

} // namespace HIKARI
