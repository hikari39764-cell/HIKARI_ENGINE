#include <algorithm>
#include "Assets/Vfx/HIKARI_VfxLayerOrder.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include "Assets/Formats/HIKARI_HvfxcFormat.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Render3D/Material/Program/HIKARI_MaterialExpressionProgram.h"
#if defined(HIKARI_WITH_EDITOR)
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#endif
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>

#include "Assets/Vfx/Compiler/HIKARI_VfxCompiler.h"
#include "Assets/Vfx/HIKARI_VfxBuiltinTextureCatalog.h"
#include "Assets/Vfx/HIKARI_VfxPresetRegistry.h"
#include "Assets/Vfx/HIKARI_VfxValidation.h"
#include "Assets/Vfx/HIKARI_VfxLayerAnimation.h"
#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJson.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxPreviewBounds.h"
#include "Render3D/Vfx/HIKARI_ParticleCpuSimulation.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"
#include "Scene/Vfx/HIKARI_VfxRenderInputMailbox.h"
#include "Scene/Vfx/HIKARI_VfxLayerHistory.h"
#include "Scene/Vfx/HIKARI_CombatPresentationService.h"
#include "Render3D/Vfx/HIKARI_GpuParticleParameterPacking.h"
#include "Internal/HIKARI_VfxFlowContracts.h"
#include "Internal/HIKARI_MaterialRegisterContracts.h"

namespace {
// 材料グラフの配線エラーを端点つきで出す。原因の切り分けに必要な情報だけ。
void DumpMaterialEdges(const HIKARI::MaterialAssetData& source) {
    const auto kindOf = [&source](const std::string& id) {
        const auto found = std::find_if(source.graph.nodes.begin(), source.graph.nodes.end(),
                                        [&id](const HIKARI::MaterialGraphNodeData& node) { return node.id == id; });
        return found == source.graph.nodes.end() ? std::string{"?"} : std::string{HIKARI::ToString(found->kind)};
    };
    for (const auto& edge : source.graph.edges) {
        std::cerr << "  edge " << edge.id << ": " << kindOf(edge.sourceNodeId) << "." << edge.sourcePin << " -> "
                  << kindOf(edge.targetNodeId) << "." << edge.targetPin << '\n';
    }
}

void ReportMaterialDiagnostics(const HIKARI::MaterialCompileResult& compiled, const HIKARI::MaterialAssetData& source) {
    bool failed = false;
    for (const auto& diagnostic : compiled.diagnostics) {
        if (diagnostic.severity != HIKARI::MaterialCompileDiagnosticSeverity::Error)
            continue;
        failed = true;
        std::cerr << diagnostic.code << " [" << diagnostic.subjectId << "] " << diagnostic.message << '\n';
    }
    if (failed)
        DumpMaterialEdges(source);
}
int ValidateRenderInputMailbox() {
    using HIKARI::RENDER3D::PARTICLES::ParticleEmitterSubmission;
    using HIKARI::RENDER3D::PARTICLES::ParticleRenderInput;
    using HIKARI::VFX::VfxRenderInputMailbox;
    using HIKARI::VFX::VfxRenderInputPublishMode;

    ParticleRenderInput first{};
    first.frameIndex = 10u;
    ParticleEmitterSubmission firstEmitter{};
    firstEmitter.resetSimulation = true;
    firstEmitter.spawnCount = 7u;
    first.emitters.push_back(firstEmitter);

    VfxRenderInputMailbox mailbox{};
    const uint64_t firstSequence = mailbox.Publish(first);
    if (firstSequence == 0u || !mailbox.HasPending() || mailbox.GetCurrent() == nullptr) {
        std::printf("[mailbox] first event frame was not retained\n");
        return 1;
    }

    ParticleRenderInput later{};
    later.frameIndex = 11u;
    if (mailbox.Publish(later) != 0u || mailbox.GetCurrent()->emitters.empty() ||
        !mailbox.GetCurrent()->emitters.front().resetSimulation ||
        mailbox.GetCurrent()->emitters.front().spawnCount != 7u) {
        std::printf("[mailbox] unacknowledged burst was replaced\n");
        return 1;
    }
    if (mailbox.Acknowledge(firstSequence + 1u) || !mailbox.HasPending()) {
        std::printf("[mailbox] stale acknowledgement consumed a frame\n");
        return 1;
    }
    if (!mailbox.Acknowledge(firstSequence) || mailbox.HasPending() || mailbox.GetCurrent() == nullptr) {
        std::printf("[mailbox] exact acknowledgement lost draw state\n");
        return 1;
    }

    const uint64_t laterSequence = mailbox.Publish(later);
    ParticleRenderInput replacement{};
    replacement.frameIndex = 12u;
    replacement.retiredEmitters.push_back({9u, 1u, 2u, 3u});
    const uint64_t replacementSequence = mailbox.Publish(replacement, VfxRenderInputPublishMode::SupersedePending);
    if (laterSequence == 0u || replacementSequence == 0u || replacementSequence == laterSequence ||
        mailbox.Acknowledge(laterSequence) || !mailbox.HasPending() || mailbox.GetCurrent() == nullptr ||
        mailbox.GetCurrent()->retiredEmitters.empty()) {
        std::printf("[mailbox] playback epoch replacement is unsafe\n");
        return 1;
    }
    return mailbox.Acknowledge(replacementSequence) ? 0 : 1;
}

bool IsFiniteRenderableParticle(const HIKARI::RENDER3D::PARTICLES::CpuParticleState& particle) noexcept {
    return std::isfinite(particle.position.x) && std::isfinite(particle.position.y) &&
           std::isfinite(particle.position.z) && std::isfinite(particle.scale.x) && std::isfinite(particle.scale.y) &&
           particle.scale.x > 0.0f && particle.scale.y > 0.0f && std::isfinite(particle.color.x) &&
           std::isfinite(particle.color.y) && std::isfinite(particle.color.z) && std::isfinite(particle.color.w) &&
           particle.color.w > 0.0f;
}

int ValidateDefaultAssetPlayback() {
    using HIKARI::RENDER3D::PARTICLES::ParticleCpuSimulation;

    const HIKARI::VFX::VfxAssetData asset = HIKARI::VFX::MakeDefaultVfxAssetData();
    const auto& authoredEmitter = asset.emitters.front();
    if (authoredEmitter.renderCommon.materialMode != HIKARI::VFX::VfxMaterialMode::StandardUnlit ||
        authoredEmitter.renderCommon.colorTextureAssetGuid.value != "engine.vfx.texture.soft_disc") {
        std::printf("[default][material] default must use the built-in soft disc\n");
        return 1;
    }
    const HIKARI::VFX::VfxCompileResult compiled = HIKARI::VFX::CompileVfxAsset(asset);
    if (!compiled.success || compiled.program.emitters.empty()) {
        std::printf("[default][compile] default VFX did not compile\n");
        return 1;
    }
    const auto& compiledEmitter = compiled.program.emitters.front();
    if (compiledEmitter.render.materialMode != HIKARI::RENDER3D::PARTICLES::ParticleMaterialMode::StandardUnlit ||
        compiledEmitter.render.colorTextureAssetGuid != "engine.vfx.texture.soft_disc" ||
        compiledEmitter.simulation.capacity == 0u) {
        std::printf("[default][compile] soft-disc render contract was lost\n");
        return 1;
    }

    HIKARI::VFX::VfxPlaybackService playback{};
    HIKARI::VFX::VfxPlayRequest request{};
    request.assetGuid = HIKARI::GenerateAssetGuid();
    request.program = std::make_shared<const HIKARI::VFX::VfxProgram>(compiled.program);
    const HIKARI::VFX::VfxPlayResult result = playback.PlayDetailed(request);
    if (!result.IsAccepted()) {
        std::printf("[default][playback] play request was rejected\n");
        return 1;
    }
    playback.Update(1u, 1.0f / 60.0f);
    const auto& input = playback.GetRenderInput();
    if (input.emitters.size() != 1u || input.emitters.front().spawnCount == 0u ||
        !input.emitters.front().resetSimulation) {
        std::printf("[default][playback] first frame did not reset and spawn\n");
        return 1;
    }
    ParticleCpuSimulation simulation{};
    simulation.Apply(input);
    const auto* state = simulation.Find(input.emitters.front().key);
    if (state == nullptr || simulation.GetAliveParticleCount(input.emitters.front().key) == 0u ||
        std::none_of(state->particles.begin(), state->particles.end(),
                     [](const auto& particle) { return particle.alive && IsFiniteRenderableParticle(particle); })) {
        std::printf("[default][simulation] first particle is not renderable\n");
        return 1;
    }
    return 0;
}

int ValidateReliableRuntimeDelivery() {
    const HIKARI::VFX::VfxCompileResult compiled = HIKARI::VFX::CompileVfxAsset(HIKARI::VFX::MakeDefaultVfxAssetData());
    if (!compiled.success) {
        std::printf("[delivery][compile] default VFX did not compile\n");
        return 1;
    }

    HIKARI::VFX::VfxPlaybackService playback{0x48494b41u};
    playback.SetReliableRenderDeliveryEnabled(true);
    HIKARI::VFX::VfxPlayRequest request{};
    request.assetGuid = HIKARI::GenerateAssetGuid();
    request.program = std::make_shared<const HIKARI::VFX::VfxProgram>(compiled.program);
    if (!playback.PlayDetailed(request).IsAccepted()) {
        std::printf("[delivery][playback] reliable play request was rejected\n");
        return 1;
    }

    playback.Update(10u, 1.0f / 60.0f);
    const auto first = playback.GetRenderInput();
    const auto firstToken = first.GetDeliveryToken();
    if (!firstToken.IsValid() || !playback.HasPendingRenderInput() || first.emitters.empty() ||
        !first.emitters.front().resetSimulation || first.emitters.front().spawnCount == 0u) {
        std::printf("[delivery][publish] initial burst has no retained token\n");
        return 1;
    }

    playback.Update(11u, 1.0f / 60.0f);
    const auto held = playback.GetRenderInput();
    if (held.GetDeliveryToken() != firstToken || held.frameIndex != first.frameIndex || held.emitters.empty() ||
        !held.emitters.front().resetSimulation ||
        held.emitters.front().spawnCount != first.emitters.front().spawnCount) {
        std::printf("[delivery][retain] unacknowledged burst was overwritten\n");
        return 1;
    }

    if (playback.PublishFrozenRenderInput() || playback.GetRenderInput().GetDeliveryToken() != firstToken ||
        playback.GetRenderInput().emitters.front().spawnCount != first.emitters.front().spawnCount) {
        std::printf("[delivery][freeze] pause discarded an unacknowledged burst\n");
        return 1;
    }
    auto wrongToken = firstToken;
    ++wrongToken.sequence;
    if (playback.AcknowledgeRenderInput(wrongToken) || !playback.HasPendingRenderInput() ||
        !playback.AcknowledgeRenderInput(firstToken) || playback.HasPendingRenderInput()) {
        std::printf("[delivery][ack] exact receipt contract was not enforced\n");
        return 1;
    }

    playback.Update(12u, 1.0f / 60.0f);
    const auto& next = playback.GetRenderInput();
    if (next.GetDeliveryToken() == firstToken || next.frameIndex != 12u || next.emitters.empty() ||
        next.emitters.front().resetSimulation || next.emitters.front().deltaSeconds <= 1.0f / 60.0f) {
        std::printf("[delivery][resume] acknowledged playback did not catch up\n");
        return 1;
    }
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    using namespace HIKARI;
    using namespace HIKARI::VFX;
    const auto require = [](bool value, const std::string& message) {
        if (!value)
            throw std::runtime_error(message);
    };
    try {
        ValidateVfxFlowContracts();
        ValidateMaterialRegisterContracts();
        std::cerr << "[contracts] mailbox\n";
        require(ValidateRenderInputMailbox() == 0, "Mailbox regression");
        std::cerr << "[contracts] default playback\n";
        require(ValidateDefaultAssetPlayback() == 0, "Default playback regression");
        std::cerr << "[contracts] reliable delivery\n";
        require(ValidateReliableRuntimeDelivery() == 0, "Reliable delivery regression");
        std::cerr << "[contracts] preset prewarm\n";
        std::string text, error;
        require(CombatPresentationService{}.Prewarm(), "Combat presets diverged from the canonical VFX catalog");
        std::cerr << "[contracts] history\n";
        {
            using namespace RENDER3D::PARTICLES;
            PLAYBACK::LayerHistory history;
            VfxHistoryData settings;
            settings.mode = VfxHistoryMode::Afterimage;
            settings.samples = 8;
            ParticleEmitterSubmission submission;
            submission.directShape = true;
            submission.simulation.capacity = settings.samples;
            for (int i = 0; i < 40; ++i) {
                submission.shapePoses.clear();
                submission.overrides.enabled = 1;
                submission.worldPosition.x = i * 0.02f;
                history.Project(settings, i / 60.0f, submission);
            }
            require(submission.shapePoses.size() > 1 && submission.shapePoses.size() <= settings.samples,
                    "History must retain bounded real world poses");
            const auto packed = INTERNAL::BuildGpuParticleEmitterParameters(submission);
            require(packed.values[1].x < submission.worldPosition.x, "History packing lost old world position");
            submission.shapePoses.clear();
            submission.worldPosition.x = 100;
            history.Project(settings, 1, submission);
            require(submission.shapePoses.size() == 1, "Teleport must break history");
            settings.mode = VfxHistoryMode::Trail;
            history.Clear();
            for (int i = 0; i < 10; ++i) {
                submission.shapePoses.clear();
                submission.overrides.enabled = 1;
                submission.worldPosition = {i * 0.1f, i * 0.05f, 0};
                history.Project(settings, i / 60.0f, submission);
            }
            require(submission.render.shape == ParticleRenderShape::HistoryRibbon && !submission.shapePoses.empty(),
                    "Trail did not connect world samples");
            for (const auto& pose : submission.shapePoses)
                for (const auto& corner : pose.corners)
                    require(MATH::Length(corner - pose.position) <= pose.scale.x * 0.5001f,
                            "Trail AS bound discarded a connected corner");
            submission.shapePoses.clear();
            submission.overrides.enabled = 1;
            history.Project(settings, 0, submission);
            require(submission.shapePoses.empty(), "Rewind connected to stale world-space history");
        }
        // ぼかし付き Glow は十字 4 タップを追加するため、式の値スロットを
        std::cerr << "[contracts] glow\n";
        // 使い切ってコンパイルに失敗しやすい。既定値 (半径 0) の一巡では
        // この経路を通らないので、明示的に 1 件コンパイルしておく。
        {
            auto glow = MakeMaterialEffect(MaterialEffectKind::Glow, "glow.blur");
            glow.parameters = {0.5f, 1.2f, 0.25f, 0.05f};
            MaterialEffectTextureSet textures{};
            textures.baseColor = AssetGuid{"engine.vfx.texture.flame_mask"};
            textures.noise = AssetGuid{"engine.vfx.texture.noise_fractal_1"};
            textures.detailNoise = AssetGuid{"engine.vfx.texture.noise_fractal_2"};
            const auto source = BuildMaterialEffectStack(textures, std::span(&glow, 1));
            const auto compiled = CompileMaterialAsset(source);
            if (!compiled.success)
                ReportMaterialDiagnostics(compiled, source);
            require(compiled.success, "Material effect compile: Glow with blur radius");
        }
        // 全ての効果種を 1 つずつ展開・コンパイルする（追加漏れをここで止める）。
        // 貼図ありの経路も必ず通す。Blur/RadialBlur 系は貼図がある時だけ
        // サンプリングノードを作るため、貼図なしだけでは配線ミスを見逃す。
        const auto effectKindNames = GetMaterialEffectKindNames();
        // UI が公開するすべての材質キーを source -> compiler -> runtime binding へ通す。
        for (size_t i = 0; i < effectKindNames.size(); ++i) {
            auto source = MakeDefaultVfxAssetData();
            auto& layer = source.emitters.front();
            layer.effects = {MakeMaterialEffect(static_cast<MaterialEffectKind>(i), "animated")};
            layer.renderCommon.colorTextureAssetGuid = AssetGuid{"engine.vfx.texture.soft_disc"};
            layer.renderCommon.flowMapTextureAssetGuid = AssetGuid{"engine.vfx.texture.flow_stream"};
            for (const auto& parameter : GetVfxAnimatableParameters(layer)) {
                if (parameter.channel != VfxLayerChannel::MaterialParameter)
                    continue;
                VfxParameterTrackData track{};
                track.id = parameter.targetId + "." + parameter.propertyId;
                track.targetId = parameter.targetId;
                track.propertyId = parameter.propertyId;
                track.curve.keys = {{track.id + ".a", 0, parameter.value}, {track.id + ".b", 1, parameter.value}};
                layer.parameterTracks.push_back(std::move(track));
            }
            const auto compiled = CompileVfxAsset(source);
            if (!compiled.success)
                for (const auto& diagnostic : compiled.diagnostics)
                    std::cerr << diagnostic.message << '\n';
            require(compiled.success, "Animated effect bindings: " + std::string(effectKindNames[i]));
        }
        for (size_t i = 0; i < effectKindNames.size(); ++i) {
            const auto effect = MakeMaterialEffect(static_cast<MaterialEffectKind>(i), "test");
            MaterialEffectTextureSet bareTextures{};
            MaterialEffectTextureSet textured{};
            textured.baseColor = AssetGuid{"engine.vfx.texture.flame_mask"};
            textured.mask = AssetGuid{"engine.vfx.texture.gradient_falloff"};
            textured.noise = AssetGuid{"engine.vfx.texture.noise_fractal_1"};
            // 2 つ目のノイズ場も必ず通す。資源スロットの配線漏れをここで止める。
            textured.detailNoise = AssetGuid{"engine.vfx.texture.noise_fractal_2"};
            textured.flowMap = AssetGuid{"engine.vfx.texture.flow_stream"};
            for (const MaterialEffectTextureSet& textures : {bareTextures, textured}) {
                const auto source = BuildMaterialEffectStack(textures, std::span(&effect, 1));
                const auto compiled = CompileMaterialAsset(source);
                if (!compiled.success)
                    ReportMaterialDiagnostics(compiled, source);
                require(compiled.success, "Material effect compile: " + std::string(MaterialEffectName(effect.kind)));
                RENDER3D::MATERIAL::MaterialExpressionRuntimeProgram expression;
                require(RENDER3D::MATERIAL::BuildMaterialExpressionRuntimeProgram(compiled.program, expression, error),
                        error);
            }
        }
        {
            // 追加貼図スロット: 底色/遮罩/ノイズが別資源として展開され、
            // 材質プログラムへ 3 枚とも載ることを契約にする。
            MaterialEffectTextureSet textures{};
            textures.baseColor = AssetGuid{"engine.vfx.texture.flame_mask"};
            textures.mask = AssetGuid{"engine.vfx.texture.gradient_falloff"};
            textures.noise = AssetGuid{"engine.vfx.texture.noise_fractal_3"};
            const auto dissolve = MakeMaterialEffect(MaterialEffectKind::Dissolve, "slot.test");
            const auto source = BuildMaterialEffectStack(textures, std::span(&dissolve, 1));
            const auto findSemantic = [&source](MaterialResourceSemantic semantic) {
                return std::find_if(
                    source.graph.resources.begin(), source.graph.resources.end(),
                    [semantic](const MaterialGraphResourceData& resource) { return resource.semantic == semantic; });
            };
            require(source.graph.resources.size() == 3u, "Effect stack did not create the three texture slots");
            require(findSemantic(MaterialResourceSemantic::BaseColor) != source.graph.resources.end() &&
                        findSemantic(MaterialResourceSemantic::Mask) != source.graph.resources.end() &&
                        findSemantic(MaterialResourceSemantic::Noise) != source.graph.resources.end(),
                    "Effect stack lost a texture semantic");

            const auto compiled = CompileMaterialAsset(source);
            if (!compiled.success)
                for (const auto& diagnostic : compiled.diagnostics)
                    std::cerr << diagnostic.message << '\n';
            require(compiled.success, "Masked effect stack did not compile");
            size_t textureBindings = 0u;
            for (const auto& binding : compiled.program.resources) {
                if (binding.type == MaterialResourceType::Texture2D)
                    ++textureBindings;
            }
            require(textureBindings == 3u, "Compiled effect stack did not keep the three texture bindings");

            // 追加スロットが空なら従来どおり 1 枚だけ。既存資産を変えない。
            MaterialEffectTextureSet baseOnly{};
            baseOnly.baseColor = textures.baseColor;
            const auto legacy = BuildMaterialEffectStack(baseOnly, std::span(&dissolve, 1));
            require(legacy.graph.resources.size() == 1u, "Texture-less effect stack changed its resource layout");
        }
        {
            // 効果の組み合わせ: 実際の資産のように複数を積んだ状態でも配線が
            // 成立すること（ぼかし系は color を合成ノードへ差し替えるため、
            // 後段の alpha 参照と衝突しやすい）。
            std::vector<MaterialEffectStep> stack{};
            for (const MaterialEffectKind kind :
                 {MaterialEffectKind::UvFlow, MaterialEffectKind::RadialBlur, MaterialEffectKind::Fade,
                  MaterialEffectKind::Emission, MaterialEffectKind::Blur, MaterialEffectKind::Dissolve}) {
                stack.push_back(
                    MakeMaterialEffect(kind, std::string("stack.") + std::string(MaterialEffectName(kind))));
            }
            MaterialEffectTextureSet stackTextures{};
            stackTextures.baseColor = AssetGuid{"engine.vfx.texture.flame_mask"};
            stackTextures.mask = AssetGuid{"engine.vfx.texture.gradient_falloff"};
            stackTextures.noise = AssetGuid{"engine.vfx.texture.noise_fractal_1"};
            // 資産と同じ並びも試す（ぼかし系を最後に置いた形）。
            std::vector<MaterialEffectStep> shipped{};
            {
                auto flow = MakeMaterialEffect(MaterialEffectKind::UvFlow, "shipped.flow");
                flow.parameters = {0.0f, -0.35f, 1.0f, 1.0f};
                auto fade = MakeMaterialEffect(MaterialEffectKind::Fade, "shipped.fade");
                fade.parameters = {0.04f, 0.42f, 0.0f, 0.0f};
                auto emission = MakeMaterialEffect(MaterialEffectKind::Emission, "shipped.emission");
                emission.parameters = {1.5f, 0.10f, 3.0f, 0.0f};
                emission.colorA = {0.55f, 1.55f, 3.60f, 1.0f};
                auto radial = MakeMaterialEffect(MaterialEffectKind::RadialBlur, "shipped.radial");
                radial.parameters = {0.12f, 0.5f, 0.5f, 3.0f};
                radial.parameters2 = {1.0f, 0.0f, 0.0f, 0.0f};
                shipped = {flow, fade, emission, radial};
            }
            MaterialEffectTextureSet shippedTextures{};
            shippedTextures.baseColor = AssetGuid{"engine.vfx.texture.shockwave_double"};
            const auto shippedSource = BuildMaterialEffectStack(shippedTextures, shipped);
            const auto shippedCompiled = CompileMaterialAsset(shippedSource);
            if (!shippedCompiled.success)
                ReportMaterialDiagnostics(shippedCompiled, shippedSource);
            require(shippedCompiled.success, "Shipped-style effect stack failed to compile");

            const auto stackSource = BuildMaterialEffectStack(stackTextures, stack);
            const auto stackCompiled = CompileMaterialAsset(stackSource);
            if (!stackCompiled.success)
                for (const auto& diagnostic : stackCompiled.diagnostics)
                    std::cerr << diagnostic.code << " [" << diagnostic.subjectId << "] " << diagnostic.message << '\n';
            require(stackCompiled.success, "Combined effect stack failed to compile");
        }
        {
            // 多段グラデーション: ストップを増やすと補間ノードが増え、JSON 往復
            // でもストップが保たれる（旧資産は 2 色のまま読める）。
            auto gradient = MakeMaterialEffect(MaterialEffectKind::Gradient, "ramp.test");
            require(gradient.colorStops.size() == 2u, "Gradient did not seed its two default stops");
            MaterialEffectTextureSet textures{};
            const auto twoStop = BuildMaterialEffectStack(textures, std::span(&gradient, 1));
            const size_t twoStopNodes = twoStop.graph.nodes.size();
            require(CompileMaterialAsset(twoStop).success, "Two-stop gradient did not compile");

            gradient.colorStops.push_back({"ramp.mid", 0.35f, {1.0f, 0.2f, 0.1f, 1.0f}});
            gradient.colorStops.push_back({"ramp.hot", 0.70f, {0.2f, 0.6f, 3.0f, 1.0f}});
            const auto fourStop = BuildMaterialEffectStack(textures, std::span(&gradient, 1));
            require(fourStop.graph.nodes.size() > twoStopNodes, "Multi-stop ramp did not expand into more nodes");
            require(CompileMaterialAsset(fourStop).success, "Multi-stop ramp did not compile");

            VfxAssetData asset = MakeDefaultVfxAssetData();
            asset.emitters.front().renderCommon.materialMode = VfxMaterialMode::StandardUnlit;
            asset.emitters.front().effects.push_back(gradient);
            require(SERIALIZATION::SerializeVfxSourceJson(asset, text, error), error);
            VfxAssetData restored{};
            require(SERIALIZATION::ParseVfxSourceJson(text, restored, error), error);
            require(restored.emitters.front().effects.size() == 1u, "Effect stack was lost in the round trip");
            const auto& restoredStops = restored.emitters.front().effects.front().colorStops;
            // 保存順は挿入順のまま。並べ替えは評価側の責務なので、
            // 往復では「同じストップが同じ値で戻る」ことだけを見る。
            const auto hasStop = [&restoredStops](const char* id, float time, float blue) {
                return std::any_of(restoredStops.begin(), restoredStops.end(),
                                   [id, time, blue](const MaterialEffectStep::ColorStop& stop) {
                                       return stop.id == id && std::abs(stop.time - time) < 1.0e-5f &&
                                              std::abs(stop.color.z - blue) < 1.0e-5f;
                                   });
            };
            require(restoredStops.size() == 4u && hasStop("ramp.mid", 0.35f, 0.1f) && hasStop("ramp.hot", 0.70f, 3.0f),
                    "Gradient stops were not preserved by the source JSON");
        }
        {
            // Mesh 出力: モデル参照レイヤーは粒子 pass へ渡さず、モデル描画
            // 側のサンプルとしてだけ公開される。
            VfxAssetData asset = MakeDefaultVfxAssetData();
            asset.name = "Mesh output contract";
            VfxEmitterData& layer = asset.emitters.front();
            layer.meshOutput.enabled = true;
            layer.meshOutput.meshAssetGuid = AssetGuid{"model.crystal.contract"};
            layer.meshOutput.castShadows = true;
            require(HasVfxErrors(ValidateVfxAsset(asset)), "Particle mesh output must not silently discard simulation");
            layer.kind = VfxLayerKind::Shape;
            require(!HasVfxErrors(ValidateVfxAsset(asset)), "Mesh output layer failed validation");
            const auto compiled = CompileVfxAsset(asset);
            if (!compiled.success)
                for (const auto& diagnostic : compiled.diagnostics)
                    std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
            require(compiled.success, "Mesh output asset did not compile");
            require(compiled.program.emitters.front().render.meshAssetGuid == "model.crystal.contract" &&
                        (compiled.program.emitters.front().render.meshOutputFlags & 1u) != 0u,
                    "Mesh output reference was lost during compile");

            VfxPlaybackService playback{};
            VfxPlayRequest request{};
            request.assetGuid = GenerateAssetGuid();
            request.program = std::make_shared<VfxProgram>(compiled.program);
            require(playback.PlayDetailed(request).IsAccepted(), "Mesh output playback was rejected");
            playback.Update(1u, 1.0f / 60.0f);
            require(playback.GetRenderInput().emitters.empty(),
                    "Mesh output layer was still submitted to the particle pass");
            std::vector<VfxLayerMeshSample> meshSamples{};
            require(playback.CollectMeshLayers(meshSamples) == 1u &&
                        meshSamples.front().meshAssetGuid == "model.crystal.contract" &&
                        (meshSamples.front().flags & 1u) != 0u,
                    "Mesh output layer did not publish a mesh sample");

            // 参照が欠けたまま有効化すると検証で止まる。
            VfxAssetData missing = asset;
            missing.emitters.front().meshOutput.meshAssetGuid = {};
            require(HasVfxErrors(ValidateVfxAsset(missing)), "Mesh output without a model asset was accepted");
        }
        {
            // 接続ターゲット: 2 層目の幾何が「自分の原点 → 1 層目の原点」へ
            // 実際に張られることを、再生結果の submission で確かめる。
            VfxAssetData asset = MakeDefaultVfxAssetData();
            asset.durationSeconds = 1.0f;
            asset.emitters.resize(2u);
            VfxEmitterData& anchor = asset.emitters[0];
            anchor.id = "emitter.anchor";
            anchor.kind = VfxLayerKind::Shape;
            anchor.startDelay = 0.0f;
            anchor.activeDuration = 0.0f;
            anchor.emitterTransform.transform.position = {0.0f, 0.0f, 0.0f};
            anchor.scale.value.mode = VfxAnimatedValueMode::Fixed;
            anchor.scale.value.fixed = {VfxDistributionMode::Constant, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, 0u};
            VfxEmitterData& linked = asset.emitters[1];
            linked = anchor;
            linked.id = "emitter.linked";
            linked.name = "linked";
            // 既定資産が持つ安定 ID を複製先で振り直す。重複 ID は検証で落ちる。
            linked.color.id = "module.color.linked";
            linked.renderCommon.id = "module.render_common.linked";
            linked.renderShape.id = "module.render_shape.linked";
            linked.basic.id = "module.basic.linked";
            linked.emitterTransform.id = "module.emitter_transform.linked";
            linked.attachment.id = "module.attachment.linked";
            linked.spawn.id = "module.spawn.linked";
            linked.shape.id = "module.shape.linked";
            linked.position.id = "module.position.linked";
            linked.rotation.id = "module.rotation.linked";
            linked.scale.id = "module.scale.linked";
            linked.forces.id = "module.forces.linked";
            for (size_t stopIndex = 0u; stopIndex < linked.color.value.gradient.stops.size(); ++stopIndex) {
                linked.color.value.gradient.stops[stopIndex].id = "color.linked." + std::to_string(stopIndex);
            }
            linked.emitterTransform.transform.position = {0.0f, 0.0f, 0.0f};
            linked.renderShape.type = VfxRenderShapeType::MeshCylinder;
            linked.renderShape.billboardAxis = VfxBillboardAxis::LocalY;
            linked.connectTarget.mode = VfxConnectTargetMode::LayerOrigin;
            linked.connectTarget.targetLayerId = anchor.id;
            linked.connectTarget.pivot = 0.5f;
            linked.connectTarget.lengthScale = 1.0f;
            linked.connectTarget.orient = true;
            linked.connectTarget.stretch = true;
            // 参照先を 4 単位上へ動かし、そこへ張られるか見る。
            anchor.emitterTransform.transform.position = {0.0f, 4.0f, 0.0f};

            const auto compiledLink = CompileVfxAsset(asset);
            if (!compiledLink.success)
                for (const auto& diagnostic : compiledLink.diagnostics)
                    std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
            require(compiledLink.success, "Connect target asset did not compile");
            require(compiledLink.program.emitters[1].render.connectTargetMode ==
                            static_cast<uint32_t>(VfxConnectTargetMode::LayerOrigin) &&
                        compiledLink.program.emitters[1].render.connectTargetLayerIndex == 0u,
                    "Connect target reference was not resolved to a layer index");

            VfxPlaybackService playback{};
            VfxPlayRequest request{};
            request.assetGuid = GenerateAssetGuid();
            request.program = std::make_shared<VfxProgram>(compiledLink.program);
            require(playback.PlayDetailed(request).IsAccepted(), "Connect target playback was rejected");
            playback.Update(1u, 1.0f / 60.0f);
            const auto& input = playback.GetRenderInput();
            const auto submission =
                std::find_if(input.emitters.begin(), input.emitters.end(),
                             [](const auto& candidate) { return candidate.key.emitterIndex == 1u; });
            require(submission != input.emitters.end(), "Linked layer produced no submission");
            // 原点 (0,0,0) から (0,4,0) へ張るので、中心は y=2、長さ軸 y は 4。
            require(std::abs(submission->worldPosition.y - 2.0f) < 1.0e-3f &&
                        std::abs(submission->worldScale.y - 4.0f) < 1.0e-3f,
                    "Connect target did not place and stretch the linked layer");
        }
        for (const auto& preset : PRESETS::GetVfxPresetCatalog()) {
            auto source = PRESETS::MakeVfxPresetAsset(preset.id);
            require(SERIALIZATION::SerializeVfxSourceJson(source, text, error), error);
            if (argc == 2) {
                const std::filesystem::path folder(argv[1]);
                std::filesystem::create_directories(folder);
                std::ofstream output(folder / (preset.id + ".hvfx"), std::ios::binary);
                output << text;
                require(static_cast<bool>(output), "Cannot write VFX sample");
            }
            VfxAssetData restored;
            require(SERIALIZATION::ParseVfxSourceJson(text, restored, error), error);
            const auto compiled = CompileVfxAsset(restored);
            if (!compiled.success)
                for (const auto& diagnostic : compiled.diagnostics)
                    std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
            require(compiled.success, "Preset compile: " + preset.id);
            require(SerializeVfxProgram(compiled.program, text, error), error);
            VfxProgram cooked;
            require(DeserializeVfxProgram(text, cooked, error), error);
            require(cooked.emitters.front().kind == restored.emitters.front().kind, "Cooked layer kind changed");
            for (size_t i = 0; i < compiled.program.emitters.size(); ++i) {
                const auto& original = compiled.program.emitters[i];
                const auto& material = cooked.emitters[i].render.effectMaterial;
                require(bool(original.render.effectMaterial) == bool(material),
                        "Cook changed material ownership: " + preset.id);
                if (material)
                    require(material->compileResult.success, "Cook lost embedded material program: " + preset.id);
                if (original.kind == VfxLayerKind::Adjustment)
                    require(!material, "Adjustment owns geometry material instead of applying to lower content");
            }
            VfxPlaybackService playback;
            VfxPlayRequest request;
            request.assetGuid = GenerateAssetGuid();
            request.program = std::make_shared<VfxProgram>(std::move(cooked));
            const auto play = playback.PlayDetailed(request);
            require(play.IsAccepted(), "Play rejected: " + preset.id);
            if (argc == 2 && preset.id == "slash") {
                require(playback.Seek(play.handle, 1, 0.24f).complete, "Slash history seek");
                std::ofstream trace(std::filesystem::path(argv[1]) / "slash-history.csv");
                trace << "segment,corner,x,y,z\n";
                for (const auto& submission : playback.GetRenderInput().emitters) {
                    if (!submission.drawAfterSimulation || submission.shapePoses.empty())
                        continue;
                    for (size_t i = 0; i < submission.shapePoses.size(); ++i)
                        for (size_t c = 0; c < 4; ++c) {
                            const auto p = submission.shapePoses[i].corners[c];
                            trace << i << ',' << c << ',' << p.x << ',' << p.y << ',' << p.z << '\n';
                        }
                }
            }
            require(playback.Seek(play.handle, 1, source.durationSeconds * 0.5f).complete, "Seek failed: " + preset.id);
            RENDER3D::PARTICLES::ParticleCpuSimulation cpu;
            cpu.Apply(playback.GetRenderInput());
            bool visible = false;
            for (const auto& submission : playback.GetRenderInput().emitters) {
                if (!submission.drawAfterSimulation)
                    continue;
                visible |= cpu.GetAliveParticleCount(submission.key) > 0;
                // 窓の外のレイヤーも submission は出る（enabled=0）。時間の
                // 契約は「その時点で有効なレイヤー」に対してだけ見る。
                if (submission.directShape && submission.overrides.enabled != 0u) {
                    require(submission.spawnCount == 0 && submission.simulation.capacity <= 16,
                            "Shape entered the particle spawn scheduler");
                    require(submission.shapeAgeSeconds > 0, "Shape lost composition time: " + preset.id);
                }
            }
            const bool hasContent = std::any_of(source.emitters.begin(), source.emitters.end(), [](const auto& layer) {
                return layer.kind == VfxLayerKind::Shape || layer.kind == VfxLayerKind::Particles;
            });
            require(visible == hasContent, "Preset content visibility mismatch at mid-time: " + preset.id);
            if (!hasContent)
                require(playback.GetRenderInput().emitters.empty() && playback.GetRenderInput().compositions.empty(),
                        "Adjustment-only module allocated rendering work without lower content");
        }
        // 全プリセットとスタイルの実制作の組合せを検証。
        for (const auto& preset : PRESETS::GetVfxPresetCatalog()) {
            if (preset.category == "Styles")
                continue;
            for (const auto style :
                 {"style_ink", "style_comic", "style_pixel", "style_hologram", "style_heat", "style_glow"}) {
                auto source = PRESETS::MakeVfxPresetAsset(preset.id);
                for (auto& layer : source.emitters)
                    ++layer.order;
                auto adjustment = PRESETS::MakeVfxPresetAsset(style).emitters.front();
                adjustment.order = 0;
                source.emitters.push_back(adjustment);
                const auto compiled = CompileVfxAsset(source);
                require(compiled.success, preset.id + " + " + style + " failed material budget or runtime validation");
                require(SerializeVfxProgram(compiled.program, text, error), error);
                VfxProgram cooked;
                require(DeserializeVfxProgram(text, cooked, error), error);
            }
        }
        {
            auto source = PRESETS::MakeVfxPresetAsset("slash");
            auto& group = source.emitters.front();
            group.kind = VfxLayerKind::Group;
            group.history.mode = VfxHistoryMode::None;
            group.activeDuration = 0.3f;
            const auto compiled = CompileVfxAsset(source);
            require(compiled.success, "Group compile failed");
            VfxPlaybackService playback;
            VfxPlayRequest request;
            request.program = std::make_shared<VfxProgram>(compiled.program);
            const auto handle = playback.PlayDetailed(request).handle;
            require(playback.Seek(handle, 1, 0.5f).complete, "Group seek failed");
            bool childChecked = false;
            for (const auto& submission : playback.GetRenderInput().emitters) {
                if (!submission.drawAfterSimulation ||
                    compiled.program.emitters[submission.key.emitterIndex].sourceEmitterId != source.emitters[1].id)
                    continue;
                require(!submission.overrides.enabled, "A child remained visible outside the group time window");
                childChecked = true;
            }
            require(childChecked, "The group child draw was not checked");
            VfxWorldTransform world, frame;
            require(playback.GetLayerPose(handle, group.id, world, frame), "Group pose unavailable for authoring");
        }
#if defined(HIKARI_WITH_EDITOR)
        EDITOR::VfxWorkspaceController editor;
        editor.NewAsset("Composition contract");
        const std::string layer = editor.GetAsset()->emitters.front().id;
        require(editor.GetAsset()->emitters.front().kind == VfxLayerKind::Shape,
                "New composition must start with a shape");
        require(editor.SetLayerTransformKey(layer, "position", 0, {0, 0, 0}, error), error);
        require(editor.SetLayerTransformKey(layer, "position", 0.5f, {2, 0, 0}, error), error);
        require(editor.AddLayerEffect(layer, MaterialEffectKind::Dissolve, error), error);
        require(editor.AddLayerEffect(layer, MaterialEffectKind::Gradient, error), error);
        require(editor.AddLayerEffect(layer, MaterialEffectKind::Gradient, error),
                "Multiple gradients reused stop IDs: " + error);
        std::string copy;
        require(editor.DuplicateEmitter(layer, copy, error), error);
        require(editor.GetAsset()->emitters.size() == 2 &&
                    editor.GetAsset()->emitters[0].effects[0].id != editor.GetAsset()->emitters[1].effects[0].id,
                "Duplicate reused effect identity");
        require(editor.Undo(error) && editor.GetAsset()->emitters.size() == 1, "Undo failed");
        require(editor.Redo(error) && editor.GetAsset()->emitters.size() == 2, "Redo failed");
        require(!editor.SetLayerTransformKey(layer, "position", -1, {}, error), "Negative key time accepted");
        for (const auto& preset : PRESETS::GetVfxPresetCatalog()) {
            require(editor.NewAssetFromPreset(preset.id, error), error);
            const auto count = editor.GetAsset()->emitters.size();
            const float duration = editor.GetAsset()->durationSeconds;
            if (!editor.AddPresetLayers("smoke", error))
                throw std::runtime_error(preset.id + ": add smoke: " + error);
            require(editor.GetAsset()->emitters.size() == count + 1, "Template layer count");
            require(editor.Undo(error) && editor.GetAsset()->emitters.size() == count, "Template undo: " + preset.id);
            require(editor.GetAsset()->durationSeconds == duration, "Template undo did not restore timeline duration");
            require(editor.Redo(error) && editor.GetAsset()->emitters.size() == count + 1,
                    "Template redo: " + preset.id);
        }
        {
            auto beam = PRESETS::MakeVfxPresetAsset("beam");
            const auto bounds = EDITOR::EstimateVfxPreviewBounds(beam, beam.emitters[4].id);
            require(bounds.valid && bounds.maximum.x >= 2.6f && bounds.minimum.x <= -2.6f,
                    "Anchored beam bounds miss an endpoint");
            require(bounds.maximum.y - bounds.minimum.y < 0.3f,
                    "Static beam framing inflated its narrow axis into a sphere");
        }
        {
            EDITOR::VfxWorkspaceController styles;
            styles.NewAsset("Adjustment contract");
            require(styles.SetProperty(styles.GetAsset()->emitters.front().renderCommon.id, "colorTextureAssetGuid",
                                       "engine.vfx.texture.soft_disc", error),
                    error);
            std::string adjustment;
            require(styles.AddEmitter({}, "Style", {}, adjustment, error, VfxLayerKind::Adjustment), error);
            require(CollectVfxLayerTree(*styles.GetAsset()).front()->id == adjustment,
                    "Timeline hierarchy order differs from adjustment composition order");
            require(styles.SetProperty(adjustment, "name", "Style renamed", error), error);
            require(styles.GetDocument().GetLastImpact() == EDITOR::VfxDocumentImpact::Metadata,
                    "Rename incorrectly invalidates simulation");
            require(styles.Undo(error) && styles.GetDocument().GetLastImpact() == EDITOR::VfxDocumentImpact::Metadata,
                    "Rename undo lost its impact");
            require(styles.Redo(error), error);
            require(styles.AddLayerEffect(adjustment, MaterialEffectKind::Pixelate, error), error);
            const auto adjustmentLayer = [&]() -> const VfxEmitterData& {
                const auto& layers = styles.GetAsset()->emitters;
                const auto found = std::find_if(layers.begin(), layers.end(),
                                                [&](const auto& candidate) { return candidate.id == adjustment; });
                require(found != layers.end(), "Adjustment identity lost");
                return *found;
            };
            require(adjustmentLayer().effects.size() == 1, "Pixelate effect missing");
            const auto effectId = adjustmentLayer().effects.front().id;
            require(styles.SetLayerParameterKey(adjustment, effectId, "parameters.x", 0, 0.01f, error), error);
            require(styles.SetLayerParameterKey(adjustment, effectId, "parameters.x", 1, 0.05f, error), error);
            require(styles.AddLayerEffect(adjustment, MaterialEffectKind::ColorCorrect, error), error);
            require(styles.CopyLayerEffects(adjustment), "Copy adjustment stack");
            require(styles.PasteLayerEffects(adjustment, error), error);
            require(adjustmentLayer().effects.size() == 4, "Paste adjustment stack");
            require(styles.Undo(error), error);
            const auto source = *styles.GetAsset();
            require(SERIALIZATION::SerializeVfxSourceJson(source, text, error), error);
            VfxAssetData parsed;
            require(SERIALIZATION::ParseVfxSourceJson(text, parsed, error), error);
            const auto compiled = CompileVfxAsset(parsed);
            if (!compiled.success)
                for (const auto& diagnostic : compiled.diagnostics)
                    std::cerr << diagnostic.message << '\n';
            require(compiled.success, "Adjustment compile");
            require(compiled.program.emitters.front().kind == VfxLayerKind::Adjustment,
                    "Adjustment must be above content");
            require(SerializeVfxProgram(compiled.program, text, error), error);
            VfxProgram cooked;
            require(DeserializeVfxProgram(text, cooked, error), error);
            require(cooked.emitters.front().runtimeEffects.size() == 2, "Cooked composite style stack missing");
            VfxPlaybackService playback;
            VfxPlayRequest request;
            request.assetGuid = GenerateAssetGuid();
            request.program = std::make_shared<VfxProgram>(cooked);
            require(playback.PlayDetailed(request).IsAccepted(), "Cooked adjustment play rejected");
            playback.Update(1, 0.5f);
            const auto& input = playback.GetRenderInput();
            require(input.compositions.size() == 1, "Adjustment did not allocate its lower-layer composition");
            require(input.emitters.size() == 1, "Adjustment was submitted as geometry");
            require(std::any_of(input.compositions.front().layers.front().effects.begin(),
                                input.compositions.front().layers.front().effects.end(),
                                [](const auto& effect) {
                                    return effect.kind == MaterialEffectKind::Pixelate && effect.parameters.x > 0.01f &&
                                           effect.parameters.x < 0.05f;
                                }),
                    "Adjustment animation did not reach composition");
            auto invalid = cooked;
            invalid.emitters.back().parentEmitterIndex = 0;
            require(!ValidateVfxProgram(invalid, error), "Cooked adjustment accepted children");
            invalid = cooked;
            invalid.emitters.back().render.meshAssetGuid = "model.uncomposited";
            require(!ValidateVfxProgram(invalid, error), "Model output silently escaped composition");
            auto disabled = cooked;
            disabled.emitters.front().authoredOverrides.enabled = 0;
            VfxPlaybackService bypassed;
            request.program = std::make_shared<VfxProgram>(disabled);
            require(bypassed.PlayDetailed(request).IsAccepted(), "Disabled adjustment play rejected");
            bypassed.Update(1, 0.5f);
            require(bypassed.GetRenderInput().compositions.empty() && bypassed.GetRenderInput().emitters.size() == 1,
                    "Disabled adjustments allocated a composition or removed content");
            VfxPlaybackService instances;
            request.program = std::make_shared<VfxProgram>(cooked);
            require(instances.PlayDetailed(request).IsAccepted() && instances.PlayDetailed(request).IsAccepted(),
                    "Multiple compositions rejected");
            instances.Update(1, 0.25f);
            const auto& combined = instances.GetRenderInput().emitters;
            require(combined.size() == 2 && !(combined[0].key == combined[1].key),
                    "Independent effect instances shared an adjustment scope");
        }
#endif
        std::cout << "VFX composition, material, cooked playback and editor contracts passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
