#pragma once

#include <string_view>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

// 地面への置き方。scene.drop_to_ground の既定と GroundAnchorComponent の保存形が同じ表を使う。
struct GroundPlacementSettings {
    float alignToGround = 1.0f;
    float maxTiltDegrees = 30.0f;
    float sinkMeters = 0.0f;

    bool operator==(const GroundPlacementSettings&) const = default;
};

inline constexpr auto kGroundPlacementFields = [](auto&& field) {
    using S = GroundPlacementSettings;
    using namespace JSONFIELDS;
    field("alignToGround", &S::alignToGround, Label{"Align To Slope"}, Slider(0.0, 1.0),
          Help{"0 keeps the object upright, 1 tilts it to the ground slope."}, Common{});
    field("maxTiltDegrees", &S::maxTiltDegrees, Label{"Max Tilt"}, Range{0.0, 90.0}, Unit{"deg"},
          Help{"Largest tilt away from upright."});
    field("sinkMeters", &S::sinkMeters, Label{"Sink Into Ground"}, Range{-10.0, 10.0}, Unit{"m"},
          Help{"Pushes the object down into the ground; negative lifts it."});
};

// 地面に従う印。地形を削ったり盛ったりすると、編集器がこの物体を真下の地面へ置き直す（向きは保つ）。遊びの中では何もしない。
class GroundAnchorComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "GroundAnchorComponent"; }

    void Serialize(nlohmann::json& out) const override {
        out = nlohmann::json::object();
        JSONFIELDS::Write(out, settings_, kGroundPlacementFields);
    }
    void Deserialize(const nlohmann::json& in) override {
        GroundPlacementSettings decoded{};
        JSONFIELDS::ReadLenient(in, decoded, kGroundPlacementFields);
        settings_ = decoded;
    }
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

private:
    GroundPlacementSettings settings_{};
};

} // namespace HIKARI
