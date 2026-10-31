#pragma once

#include <string>
#include <utility>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "HIKARI_IComponent.h"

namespace HIKARI {

// 作者値（シーン文書の properties と同じ形）。項目表（kSpawnPointFields）が保存・読み取り・検査器・説明を決める。
struct SpawnPointSettings {
    bool enabled = true;
    std::string spawnPointId{"DefaultSpawn"};
};

inline constexpr auto kSpawnPointFields = [](auto&& field) {
    using S = SpawnPointSettings;
    using namespace JSONFIELDS;
    field("spawnPointId", &S::spawnPointId, Label{"Spawn Point ID"});
    field("enabled", &S::enabled, Label{"Enabled"});
};

class SpawnPointComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "SpawnPointComponent"; }

    void Serialize(nlohmann::json& out) const override {
        out = nlohmann::json::object();
        JSONFIELDS::Write(out, settings_, kSpawnPointFields);
    }
    void Deserialize(const nlohmann::json& in) override {
        SpawnPointSettings decoded{};
        JSONFIELDS::Read(in, decoded, kSpawnPointFields);
        settings_ = std::move(decoded);
    }
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    const std::string& GetSpawnPointId() const { return settings_.spawnPointId; }
    bool IsEnabled() const { return settings_.enabled; }

private:
    SpawnPointSettings settings_{};
};

} // namespace HIKARI
