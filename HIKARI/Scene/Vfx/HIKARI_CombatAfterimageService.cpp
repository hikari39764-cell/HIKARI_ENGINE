#include "Core/Math/HIKARI_MathValidation.h"
#include "Scene/Vfx/HIKARI_CombatAfterimageService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <utility>

#include "Animation/Runtime/HIKARI_AnimationPoseEvaluation.h"
#include "Animation/Runtime/HIKARI_AnimationPoseService.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Vfx/HIKARI_VfxSpecialOutputBridge.h"

namespace HIKARI::VFX {
    namespace {
        using AfterimageDraw =
            RENDER3D::COMBAT_VFX::CombatAfterimageDraw;

        template<class T>
        bool ProfileSilhouette(const T& profile) noexcept {
            if constexpr (requires { profile.silhouette; }) {
                return profile.silhouette;
            }
            return true;
        }

        // A draw record carries a bounded 128-joint palette.  Keep the CPU
        // capture budget deliberately small so a burst cannot consume an
        // unbounded amount of scene memory.
        constexpr size_t kMaximumActiveStates = 8u;
        constexpr size_t kMaximumHistoryPerState =
            RENDER3D::COMBAT_VFX::kMaximumCombatAfterimageSilhouettes;
        constexpr size_t kMaximumDrawsPerCapture = 32u;
        constexpr float kMinimumLifetime = 0.04f;
        constexpr float kMaximumLifetime = 1.0f;
        constexpr float kSyntheticAgeSpacing = 0.018f;

        bool IsSkinnedPrimitive(const MeshPrimitive& primitive) noexcept {
            // The glTF importer keeps the legacy StaticPNTT tag while it
            // retains a populated skinnedVertices stream for JOINTS_0 /
            // WEIGHTS_0 (the live RenderModelAsset path uses this same
            // semantic).  Layout alone would therefore freeze Summer's body
            // in bind pose and make the afterimage look like a T-pose.
            return (primitive.layout == VertexLayoutKind::SkinnedPNTTJW ||
                    !primitive.skinnedVertices.empty()) &&
                !primitive.skinnedVertices.empty() &&
                !primitive.indices.empty();
        }

        bool IsStaticPrimitive(const MeshPrimitive& primitive) noexcept {
            return primitive.layout == VertexLayoutKind::StaticPNTT &&
                primitive.skinnedVertices.empty() &&
                !primitive.staticVertices.empty() &&
                !primitive.indices.empty();
        }

        bool IsValidRequest(const CombatAfterimageRequest& request) noexcept {
            if (!request.ownerObject.IsValid() || request.sourceId == 0u ||
                request.silhouetteCount < 3u ||
                request.silhouetteCount >
                    RENDER3D::COMBAT_VFX::
                        kMaximumCombatAfterimageSilhouettes ||
                !MATH::IsFinite(request.intensity) ||
                !MATH::IsFinite(request.uniformScale) ||
                !MATH::IsFinite(request.lifetimeUnscaledSeconds) ||
                !MATH::IsFinite(request.opacity) ||
                !MATH::IsFinite(request.fresnelPower) ||
                !MATH::IsFinite(request.dissolveStrength) ||
                !MATH::IsFinite(request.shellOffsetWorld) ||
                !MATH::IsFinite(request.color)) {
                return false;
            }
            return request.intensity >= 0.0f &&
                request.uniformScale > 0.0f &&
                request.lifetimeUnscaledSeconds > 0.0f &&
                request.opacity >= 0.0f && request.fresnelPower >= 0.0f &&
                request.dissolveStrength >= 0.0f &&
                request.shellOffsetWorld >= 0.0f;
        }

        bool IsDuplicateCapture(
            const CombatAfterimageRequest& request,
            uint64_t lastFrameIndex,
            uint64_t lastSequence,
            uint32_t lastRandomSeed,
            bool hasLastCapture) noexcept {
            return hasLastCapture && request.frameIndex == lastFrameIndex &&
                request.sequence == lastSequence &&
                request.randomSeed == lastRandomSeed;
        }

        float SanitizePositive(
            float value,
            float lower,
            float upper) noexcept {
            return std::clamp(value, lower, upper);
        }

        void InitializeJointPalette(AfterimageDraw& draw) noexcept {
            for (MATH::Mat4& matrix : draw.jointPalette) {
                matrix = MATH::Mat4::Identity();
            }
        }

        bool BuildCapturedDraws(
            const MATH::Mat4& objectWorld,
            const ModelAsset& model,
            const ANIMATION::AnimationLocalPose* pose,
            std::vector<AfterimageDraw>& outDraws) {
            if (!model.HasSkinnedMesh() || model.nodes.empty()) {
                return false;
            }

            std::vector<MATH::Mat4> nodeGlobals;
            if (!ANIMATION::POSE_EVALUATION::BuildNodeGlobalMatrices(
                    model,
                    pose,
                    nodeGlobals) ||
                nodeGlobals.size() != model.nodes.size()) {
                return false;
            }
            for (const MATH::Mat4& matrix : nodeGlobals) {
                if (!MATH::IsFinite(matrix)) {
                    return false;
                }
            }

            if (!MATH::IsFinite(objectWorld)) {
                return false;
            }

            outDraws.clear();
            outDraws.reserve(kMaximumDrawsPerCapture);
            for (size_t nodeIndex = 0u;
                nodeIndex < model.nodes.size();
                ++nodeIndex) {
                const ModelNode& node = model.nodes[nodeIndex];
                if (node.meshIndex < 0 ||
                    node.meshIndex >= static_cast<int>(model.meshes.size())) {
                    continue;
                }
                if (nodeIndex >= nodeGlobals.size()) {
                    continue;
                }
                const MATH::Mat4 drawWorld =
                    objectWorld * nodeGlobals[nodeIndex];
                if (!MATH::IsFinite(drawWorld)) {
                    continue;
                }

                const MeshAsset& mesh =
                    model.meshes[static_cast<size_t>(node.meshIndex)];
                const SkeletonAsset* skin = model.FindSkin(node.skinIndex);
                std::array<MATH::Mat4,
                    RENDER3D::COMBAT_VFX::
                        kMaximumCombatAfterimageJoints> palette{};
                bool validPalette = skin != nullptr &&
                    !skin->joints.empty() &&
                    skin->joints.size() <=
                        RENDER3D::COMBAT_VFX::
                            kMaximumCombatAfterimageJoints;
                if (validPalette) {
                    for (size_t jointIndex = 0u;
                        jointIndex < skin->joints.size();
                        ++jointIndex) {
                        const SkeletonJoint& joint =
                            skin->joints[jointIndex];
                        if (joint.nodeIndex < 0 ||
                            joint.nodeIndex >=
                                static_cast<int>(nodeGlobals.size())) {
                            validPalette = false;
                            break;
                        }
                        palette[jointIndex] =
                            nodeGlobals[
                                static_cast<size_t>(joint.nodeIndex)] *
                            joint.inverseBindMatrix;
                        if (!MATH::IsFinite(palette[jointIndex])) {
                            validPalette = false;
                            break;
                        }
                    }
                }

                for (const MeshPrimitive& primitive : mesh.primitives) {
                    const bool skinned = IsSkinnedPrimitive(primitive);
                    const bool staticPrimitive =
                        IsStaticPrimitive(primitive);
                    if ((!skinned && !staticPrimitive) ||
                        (skinned && !validPalette)) {
                        continue;
                    }
                    if (outDraws.size() >= kMaximumDrawsPerCapture) {
                        break;
                    }
                    AfterimageDraw draw{};
                    draw.primitive = &primitive;
                    draw.world = drawWorld;
                    draw.jointCount = skinned
                        ? static_cast<uint32_t>(skin->joints.size())
                        : 0u;
                    draw.skinned = skinned ? 1u : 0u;
                    draw.silhouetteIndex = 0u;
                    InitializeJointPalette(draw);
                    if (skinned) {
                        for (size_t jointIndex = 0u;
                            jointIndex < skin->joints.size();
                            ++jointIndex) {
                            draw.jointPalette[jointIndex] =
                                palette[jointIndex];
                        }
                    }
                    outDraws.push_back(std::move(draw));
                }
            }
            return !outDraws.empty();
        }
    }

    struct CombatAfterimageService::AfterimageState {
        struct Snapshot {
            std::vector<AfterimageDraw> draws{};
            float ageSeconds = 0.0f;
        };

        RuntimeObjectHandle ownerObject{};
        uint64_t sourceId = 0u;
        uint32_t randomSeed = 0u;
        uint64_t sequence = 0u;
        uint64_t lastCaptureFrame = 0u;
        bool hasLastCapture = false;

        uint32_t silhouetteCount = 4u;
        bool silhouette = true;
        float lifetimeSeconds = 0.28f;
        float opacity = 0.62f;
        float intensity = 1.0f;
        float uniformScale = 1.0f;
        float fresnelPower = 3.0f;
        float dissolveStrength = 1.0f;
        float shellOffsetWorld = 0.008f;
        MATH::Vec3 color{ 0.10f, 0.72f, 1.0f };
        std::vector<Snapshot> history{};
    };

    CombatAfterimageService::CombatAfterimageService() = default;
    CombatAfterimageService::~CombatAfterimageService() = default;

    bool CombatAfterimageService::Capture(
        World& world,
        const CombatAfterimageRequest& request) noexcept {
        try {
            if (!IsValidRequest(request)) {
                return false;
            }

            GameObject* object = world.FindObject(request.ownerObject);
            if (object == nullptr) {
                return false;
            }
            const ModelComponent* modelComponent =
                object->GetComponent<ModelComponent>();
            const ModelAsset* model = modelComponent != nullptr
                ? modelComponent->GetModelAsset()
                : nullptr;
            if (model == nullptr) {
                return false;
            }

            const ANIMATION::AnimationPoseService* poses =
                world.Services().Find<ANIMATION::AnimationPoseService>();
            if (poses == nullptr) {
                poseRejectedThisFrame_ = true;
                return false;
            }
            const std::shared_ptr<const ANIMATION::AnimationPoseSnapshot>
                poseSnapshot = poses->Find(request.ownerObject);
            if (poseSnapshot == nullptr || !poseSnapshot->valid ||
                !poseSnapshot->localPose.IsValidFor(model->nodes.size())) {
                // A bind-pose fallback would turn a transient animation
                // delivery failure into a visible T-pose silhouette.  The
                // effect is presentation-only, so fail closed instead.
                poseRejectedThisFrame_ = true;
                if (!poseRejectLogged_) {
                    DEBUGLOG::PushRenderError(
                        "[CombatAfterimage][WARN] capture rejected: evaluated animation pose is missing or invalid.");
                    poseRejectLogged_ = true;
                }
                return false;
            }

            MATH::Mat4 captureWorld =
                object->GetTransform().GetWorldMatrix();
            if (const PresentationTransformService* presentation =
                    world.Services().Find<PresentationTransformService>()) {
                MATH::Mat4 presentationWorld{};
                if (presentation->TryGetWorldMatrix(
                        request.ownerObject,
                        presentationWorld)) {
                    captureWorld = presentationWorld;
                }
            }
            if (!MATH::IsFinite(captureWorld)) {
                return false;
            }

            std::vector<AfterimageDraw> capturedDraws;
            if (!BuildCapturedDraws(
                    captureWorld,
                    *model,
                    &poseSnapshot->localPose,
                    capturedDraws)) {
                return false;
            }

            auto found = std::find_if(
                states_.begin(),
                states_.end(),
                [&request](const AfterimageState& state) {
                    return state.ownerObject == request.ownerObject &&
                        state.sourceId == request.sourceId;
                });
            if (found == states_.end()) {
                if (states_.size() >= kMaximumActiveStates) {
                    return false;
                }
                AfterimageState state{};
                state.ownerObject = request.ownerObject;
                state.sourceId = request.sourceId;
                state.history.reserve(kMaximumHistoryPerState);
                states_.push_back(std::move(state));
                found = std::prev(states_.end());
            }

            AfterimageState& state = *found;
            if (IsDuplicateCapture(
                    request,
                    state.lastCaptureFrame,
                    state.sequence,
                    state.randomSeed,
                    state.hasLastCapture)) {
                return false;
            }

            state.silhouetteCount = request.silhouetteCount;
            state.silhouette = request.silhouette;
            state.lifetimeSeconds = SanitizePositive(
                request.lifetimeUnscaledSeconds,
                kMinimumLifetime,
                kMaximumLifetime);
            state.opacity = std::clamp(request.opacity, 0.01f, 1.0f);
            state.intensity = SanitizePositive(
                request.intensity,
                0.05f,
                4.0f);
            state.uniformScale = SanitizePositive(
                request.uniformScale,
                0.05f,
                4.0f);
            state.fresnelPower = SanitizePositive(
                request.fresnelPower,
                0.5f,
                8.0f);
            state.dissolveStrength = SanitizePositive(
                request.dissolveStrength,
                0.1f,
                4.0f);
            state.shellOffsetWorld = std::clamp(
                request.shellOffsetWorld,
                0.0f,
                0.08f);
            state.color = {
                (std::max)(0.0f, request.color.x),
                (std::max)(0.0f, request.color.y),
                (std::max)(0.0f, request.color.z) };

            AfterimageState::Snapshot snapshot{};
            snapshot.draws = std::move(capturedDraws);
            snapshot.ageSeconds = 0.0f;
            if (state.history.size() >= kMaximumHistoryPerState) {
                state.history.erase(state.history.begin());
            }
            state.history.push_back(std::move(snapshot));
            if (state.history.size() > state.silhouetteCount) {
                state.history.erase(
                    state.history.begin(),
                    state.history.begin() +
                        (state.history.size() - state.silhouetteCount));
            }
            state.lastCaptureFrame = request.frameIndex;
            state.randomSeed = request.randomSeed;
            state.sequence = request.sequence;
            state.hasLastCapture = true;
            poseRejectLogged_ = false;
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool CombatAfterimageService::Capture(
        World& world,
        const CombatAfterimageRequest& request,
        const VfxSpecialOutputData& output) noexcept {

        if (!output.enabled ||
            output.type != VfxSpecialOutputType::SkinnedAfterimage) {
            return false;
        }
        const VfxSkinnedAfterimageProfile& profile =
            output.skinnedAfterimage;
        CombatAfterimageRequest configured = request;
        const float intensityScale = std::clamp(
            request.intensity, 0.0f, 8.0f);
        const float opacityScale = std::clamp(
            request.opacity, 0.0f, 1.0f);
        const float colorAlpha = std::clamp(
            std::isfinite(profile.color.w) ? profile.color.w : 1.0f,
            0.0f,
            1.0f);
        configured.intensity = profile.intensity * intensityScale;
        configured.silhouette = ProfileSilhouette(profile);
        configured.lifetimeUnscaledSeconds = profile.lifetimeSeconds;
        // The existing afterimage frame carries RGB; use authored color A as
        // the profile's visibility multiplier so it is not a dead Inspector
        // field while preserving the renderer's compact frame contract.
        configured.opacity = profile.opacity * opacityScale * colorAlpha;
        configured.silhouetteCount = std::clamp(
            profile.sampleCount,
            3u,
            RENDER3D::COMBAT_VFX::kMaximumCombatAfterimageSilhouettes);
        configured.fresnelPower = profile.fresnel;
        configured.dissolveStrength = profile.dissolve;
        configured.shellOffsetWorld = profile.shellOffset;
        configured.color = {
            profile.color.x,
            profile.color.y,
            profile.color.z };
        return Capture(world, configured);
    }

    bool CombatAfterimageService::Capture(
        World& world,
        const CombatAfterimageRequest& request,
        const VfxSkinnedAfterimageProfile& profile) noexcept {

        VfxSpecialOutputData output{};
        output.type = VfxSpecialOutputType::SkinnedAfterimage;
        output.skinnedAfterimage = profile;
        return Capture(world, request, output);
    }

    bool CombatAfterimageService::Capture(
        World& world,
        const CombatAfterimageRequest& request,
        const VfxProgram& program) noexcept {

        return Capture(world, request, program, {});
    }

    bool CombatAfterimageService::Capture(
        World& world,
        const CombatAfterimageRequest& request,
        const VfxProgram& program,
        std::string_view outputId) noexcept {

        const VfxSpecialOutputData* output = FindVfxSpecialOutput(
            program,
            outputId,
            VfxSpecialOutputType::SkinnedAfterimage);
        if (output == nullptr) {
            return outputId.empty()
                ? Capture(world, request)
                : false;
        }
        return Capture(world, request, *output);
    }

    void CombatAfterimageService::Tick(
        uint64_t frameIndex,
        float unscaledDeltaSeconds) noexcept {
        try {
            if (hasTicked_ && frameIndex <= lastTickFrame_) {
                return;
            }
            hasTicked_ = true;
            lastTickFrame_ = frameIndex;
            const float delta = MATH::IsFinite(unscaledDeltaSeconds)
                ? std::clamp(unscaledDeltaSeconds, 0.0f, 0.25f)
                : 0.0f;
            for (AfterimageState& state : states_) {
                for (AfterimageState::Snapshot& snapshot : state.history) {
                    snapshot.ageSeconds += delta;
                }
                std::erase_if(
                    state.history,
                    [&state](const AfterimageState::Snapshot& snapshot) {
                        return snapshot.ageSeconds >= state.lifetimeSeconds;
                    });
            }
            std::erase_if(
                states_,
                [](const AfterimageState& state) {
                    return state.history.empty();
                });

            BuildFrameSource(frameIndex);
        }
        catch (...) {
            // Presentation must fail closed.  Preserve ownership state, but
            // publish an empty frame if allocation/building failed.
            drawRecords_.clear();
            frameSource_.draws = nullptr;
            frameSource_.drawCount = 0u;
            frameSource_.silhouetteCount = 0u;
            frameSource_.missingPose = poseRejectedThisFrame_;
            poseRejectedThisFrame_ = false;
            frameSource_.frameIndex = frameIndex;
        }
    }

    void CombatAfterimageService::BuildFrameSource(
        uint64_t frameIndex) noexcept {
        drawRecords_.clear();
        drawRecords_.reserve(
            RENDER3D::COMBAT_VFX::kMaximumCombatAfterimageDraws);
        uint32_t silhouetteCount = 0u;

        for (const AfterimageState& state : states_) {
            const uint32_t requestedSilhouettes = std::clamp(
                state.silhouetteCount,
                3u,
                RENDER3D::COMBAT_VFX::
                    kMaximumCombatAfterimageSilhouettes);
            const size_t historySize = state.history.size();
            if (historySize == 0u) {
                continue;
            }

            uint32_t emittedForState = 0u;
            for (uint32_t silhouetteIndex = 0u;
                silhouetteIndex < requestedSilhouettes;
                ++silhouetteIndex) {
                if (drawRecords_.size() >=
                    RENDER3D::COMBAT_VFX::kMaximumCombatAfterimageDraws) {
                    break;
                }
                const size_t historyIndex = silhouetteIndex < historySize
                    ? historySize - 1u - silhouetteIndex
                    : 0u;
                const AfterimageState::Snapshot& snapshot =
                    state.history[historyIndex];
                const float age = (std::max)(
                    snapshot.ageSeconds,
                    static_cast<float>(silhouetteIndex) *
                        kSyntheticAgeSpacing);
                if (age >= state.lifetimeSeconds) {
                    continue;
                }
                const float remaining = std::clamp(
                    1.0f - age / (std::max)(state.lifetimeSeconds, 0.001f),
                    0.0f,
                    1.0f);
                const float silhouetteOpacity =
                    state.opacity * remaining * remaining *
                    (1.0f - 0.08f *
                        static_cast<float>(silhouetteIndex));
                for (const AfterimageDraw& captured : snapshot.draws) {
                    if (drawRecords_.size() >=
                        RENDER3D::COMBAT_VFX::
                            kMaximumCombatAfterimageDraws) {
                        break;
                    }
                    AfterimageDraw draw = captured;
                    draw.silhouetteIndex = silhouetteIndex;
                    draw.silhouette = state.silhouette ? 1u : 0u;
                    draw.ageSeconds = age;
                    draw.lifetimeSeconds = state.lifetimeSeconds;
                    draw.opacity = std::clamp(
                        silhouetteOpacity,
                        0.0f,
                        1.0f);
                    draw.intensity = state.intensity;
                    draw.uniformScale = state.uniformScale;
                    draw.fresnelPower = state.fresnelPower;
                    draw.dissolveStrength = state.dissolveStrength;
                    draw.shellOffsetWorld = state.shellOffsetWorld *
                        static_cast<float>(silhouetteIndex);
                    draw.color = state.color;
                    drawRecords_.push_back(std::move(draw));
                }
                ++emittedForState;
            }
            silhouetteCount = (std::max)(silhouetteCount, emittedForState);
        }

        ++revision_;
        if (revision_ == 0u) {
            revision_ = 1u;
        }
        frameSource_.draws = drawRecords_.empty()
            ? nullptr
            : drawRecords_.data();
        frameSource_.drawCount = static_cast<uint32_t>(
            (std::min)(
                drawRecords_.size(),
                static_cast<size_t>(
                    (std::numeric_limits<uint32_t>::max)())));
        frameSource_.silhouetteCount = silhouetteCount;
        frameSource_.missingPose = poseRejectedThisFrame_;
        poseRejectedThisFrame_ = false;
        frameSource_.frameIndex = frameIndex;
        frameSource_.revision = revision_;
    }

    void CombatAfterimageService::Reset() noexcept {
        states_.clear();
        drawRecords_.clear();
        frameSource_ = {};
        revision_ = 0u;
        lastTickFrame_ = 0u;
        hasTicked_ = false;
        poseRejectLogged_ = false;
        poseRejectedThisFrame_ = false;
    }

    size_t CombatAfterimageService::GetActiveCount() const noexcept {
        return states_.size();
    }

} // namespace HIKARI::VFX
