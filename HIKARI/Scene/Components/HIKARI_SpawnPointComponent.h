#pragma once

#include <string>

#include "HIKARI_IComponent.h"

namespace HIKARI {

class SpawnPointComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "SpawnPointComponent"; }

    void Serialize(nlohmann::json& out) const override {
        out["spawnPointId"] = spawnPointId_;
        out["enabled"] = enabled_;
    }
    void Deserialize(const nlohmann::json& in) override {
        spawnPointId_ = in.value("spawnPointId", spawnPointId_);
        enabled_ = in.value("enabled", enabled_);
    }
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    const std::string& GetSpawnPointId() const { return spawnPointId_; }
    bool IsEnabled() const { return enabled_; }

private:
    std::string spawnPointId_{"DefaultSpawn"};
    bool enabled_ = true;
};

} // namespace HIKARI
