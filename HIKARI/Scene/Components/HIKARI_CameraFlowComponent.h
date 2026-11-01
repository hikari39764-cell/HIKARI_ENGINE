#pragma once
#include "Core/HIKARI_AssetGuid.h"
#include "RenderContracts/Asset/HIKARI_AssetTypeEnum.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Scene/Camera/HIKARI_CameraRigTypes.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackTypes.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"
namespace HIKARI {
enum class CameraFlowStage { Armed, Playing, Returning, Completed, Interrupted, Invalid, Disabled };
const char* CameraFlowStageId(CameraFlowStage stage) noexcept;
struct CameraFlowSettings {
    bool enabled = true, repeatOnReentry = false, finishOnInterrupt = false, triggerOnExit = false;
    SceneObjectId subjectObjectId{}, cameraObjectId{};
    MATH::Vec3 halfExtents{2, 2, 2};
    AssetGuid sequence;
    std::string afterMode = "first_person", requiredMode;
    int priority = 50;
};
// 保存・読み取り・検査器・説明が共有する項目表。JSON のキー sequenceAssetGuid は sequence に対応する。
inline constexpr auto kCameraFlowFields = [](auto&& field) {
    using S = CameraFlowSettings;
    using namespace JSONFIELDS;
    field("enabled", &S::enabled, Label{"Enabled"}, Common{});
    field("subjectObjectId", &S::subjectObjectId, Label{"Follow target"}, ObjectRef{}, Common{});
    field("cameraObjectId", &S::cameraObjectId, Label{"Camera"}, ObjectRef{}, Common{});
    field("sequenceAssetGuid", &S::sequence, Label{"Cinematic"}, AssetRef{AssetType::Sequence}, Common{});
    field("triggerOnExit", &S::triggerOnExit, Label{"Trigger on exit"});
    field("requiredMode", &S::requiredMode, Label{"Only from mode"});
    field("afterMode", &S::afterMode, Label{"Gameplay after handoff"});
    field("halfExtents", &S::halfExtents, Range{0.01, 10000.0}, Label{"Half Extents"}, Unit{"m"},
          Help{"Half width (X), height (Y) and depth (Z) of the region."});
    field("priority", &S::priority, Label{"Priority"});
    field("repeatOnReentry", &S::repeatOnReentry, Label{"Repeat on re-entry"});
    field("finishOnInterrupt", &S::finishOnInterrupt, Label{"Use destination mode if interrupted"});
};
struct CameraFlowRuntime {
    CameraFlowStage stage = CameraFlowStage::Armed;
    bool inside = false, entered = false;
    unsigned runs = 0;
    float time = 0, duration = 0, returnRemaining = 0;
    std::string diagnostic;
    SEQUENCER::SequencePlaybackHandle playback{};
};
// シーンに属する領域と接続。再利用するレンズ/軌道は .hcamera、演出は .hsequence が所有する。
class CameraFlowComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "CameraFlowComponent"; }
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif
    bool RequestTrigger() noexcept {
        if (!settings_.enabled || runtime_.stage == CameraFlowStage::Playing ||
            runtime_.stage == CameraFlowStage::Returning)
            return false;
        triggerPending_ = true;
        return true;
    }
    bool RequestInterrupt() noexcept {
        if (runtime_.stage != CameraFlowStage::Playing)
            return false;
        interruptPending_ = true;
        return true;
    }
    const CameraFlowSettings& Settings() const { return settings_; }
    const CameraFlowRuntime& Runtime() const { return runtime_; }

private:
    friend class CameraFlowSystem;
    bool triggerPending_ = false, interruptPending_ = false;
    CameraFlowSettings settings_;
    CameraFlowRuntime runtime_;
};
} // namespace HIKARI
