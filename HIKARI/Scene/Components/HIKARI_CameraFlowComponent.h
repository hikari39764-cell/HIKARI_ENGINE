#pragma once
#include "Scene/Camera/HIKARI_CameraRigTypes.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackTypes.h"
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
    void BuildInspector(IInspectorBuilder& builder) override;
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
