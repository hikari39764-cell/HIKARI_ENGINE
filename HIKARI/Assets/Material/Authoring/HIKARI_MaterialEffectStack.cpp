#include "Assets/Material/Authoring/HIKARI_MaterialEffectStack.h"
#include "Assets/Material/Authoring/Internal/HIKARI_MaterialEffectSampling.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace HIKARI {
    namespace {
        using Kind = MaterialNodeKind;
        using Type = MaterialValueType;
        constexpr std::array<std::string_view, 21> names{
            "UvFlow", "UvDistortion", "Gradient", "Fade", "Wipe", "Dissolve", "Emission", "Fresnel",
            "Opacity", "Glow", "Blur", "LightSweep", "LuminanceMask",
            "RadialBlur", "TurbulentDisplace", "ColorCorrect", "Streak", "Pixelate", "Posterize", "Ink", "FlowMap"};
        using namespace MATERIAL_EFFECT_INTERNAL;
    }

    std::span<const std::string_view> GetMaterialEffectTextureTargets() noexcept {
        static constexpr std::array<std::string_view, 6> targets{"Shared", "BaseColor", "Mask", "Noise", "DetailNoise", "FlowMap"};
        return targets;
    }
    std::string_view MaterialEffectTextureTargetName(MaterialEffectTextureTarget target) noexcept {
        const auto targets = GetMaterialEffectTextureTargets();
        return static_cast<size_t>(target) < targets.size() ? targets[static_cast<size_t>(target)] : "Invalid";
    }
    bool ParseMaterialEffectTextureTarget(std::string_view name, MaterialEffectTextureTarget& target) noexcept {
        const auto targets = GetMaterialEffectTextureTargets();
        for (size_t i = 0; i < targets.size(); ++i) {
            if (targets[i] == name) { target = static_cast<MaterialEffectTextureTarget>(i); return true; }
        }
        return false;
    }
    MaterialEffectStage GetMaterialEffectStage(MaterialEffectKind kind) noexcept {
        switch (kind) {
        case MaterialEffectKind::FlowMap:
        case MaterialEffectKind::UvFlow: case MaterialEffectKind::UvDistortion:
        case MaterialEffectKind::TurbulentDisplace: return MaterialEffectStage::Uv;
        case MaterialEffectKind::Blur: return MaterialEffectStage::Blur;
        case MaterialEffectKind::Streak: return MaterialEffectStage::Streak;
        case MaterialEffectKind::RadialBlur: return MaterialEffectStage::RadialBlur;
        case MaterialEffectKind::Pixelate: case MaterialEffectKind::Posterize:
        case MaterialEffectKind::Ink: return MaterialEffectStage::Composite;
        default: return MaterialEffectStage::Surface;
        }
    }
    std::string_view MaterialEffectStageName(MaterialEffectStage stage) noexcept {
        switch (stage) {
        case MaterialEffectStage::Uv: return "UV mapping";
        case MaterialEffectStage::Blur: return "Texture blur";
        case MaterialEffectStage::Streak: return "Texture streak";
        case MaterialEffectStage::RadialBlur: return "Texture radial blur";
        case MaterialEffectStage::Surface: return "Color, coverage & emission";
        default: return "Composition";
        }
    }
    bool IsCompositeOnlyMaterialEffect(MaterialEffectKind kind) noexcept {
        return kind == MaterialEffectKind::Pixelate || kind == MaterialEffectKind::Posterize || kind == MaterialEffectKind::Ink;
    }
    bool IsCompositeMaterialEffect(MaterialEffectKind kind) noexcept {
        return IsCompositeOnlyMaterialEffect(kind) || kind == MaterialEffectKind::ColorCorrect ||
            kind == MaterialEffectKind::Opacity || kind == MaterialEffectKind::Blur || kind == MaterialEffectKind::Glow;
    }
    std::array<const char*, 4> MaterialEffectParameterNames(MaterialEffectKind kind, bool secondary) noexcept {
        if (secondary) {
            switch (kind) {
            case MaterialEffectKind::UvFlow: return {"rotation", "swapAxes", "pivotU", "pivotV"};
            case MaterialEffectKind::Glow: return {"farRadius", "farWeight"};
            case MaterialEffectKind::RadialBlur: return {"direction"};
            case MaterialEffectKind::ColorCorrect: return {"hue"};
            default: return {};
            }
        }
        switch (kind) {
        case MaterialEffectKind::UvFlow: return {"speedU", "speedV", "repeatU", "repeatV"};
        case MaterialEffectKind::FlowMap: return {"strength", "speed", "repeat", "mix"};
        case MaterialEffectKind::UvDistortion: return {"strength", "noiseScale", "speed"};
        case MaterialEffectKind::Fade: return {"fadeIn", "fadeOut"};
        case MaterialEffectKind::Wipe: return {"start", "end", "softness", "axis"};
        case MaterialEffectKind::Dissolve: return {"start", "end", "noiseScale", "edgeWidth"};
        case MaterialEffectKind::Emission: return {"intensity", "pulse", "frequency"};
        case MaterialEffectKind::Fresnel: return {"power", "intensity", "rimOpacity"};
        case MaterialEffectKind::Opacity: return {"amount"};
        case MaterialEffectKind::Glow: return {"threshold", "intensity", "softness"};
        case MaterialEffectKind::Blur: return {"radius", "angle", "taps"};
        case MaterialEffectKind::LightSweep: return {"center", "width", "angle", "intensity"};
        case MaterialEffectKind::LuminanceMask: return {"threshold", "softness", "replace"};
        case MaterialEffectKind::RadialBlur: return {"radius", "centerU", "centerV", "taps"};
        case MaterialEffectKind::TurbulentDisplace: return {"amount", "frequency", "speed", "detail"};
        case MaterialEffectKind::ColorCorrect: return {"exposure", "contrast", "saturation", "blackPoint"};
        case MaterialEffectKind::Streak: return {"length", "angle", "taps", "decay"};
        case MaterialEffectKind::Pixelate: return {"pixelSize", "mix"};
        case MaterialEffectKind::Posterize: return {"levels", "mix"};
        case MaterialEffectKind::Ink: return {"edgeWidth", "intensity", "detail", "mix"};
        default: return {};
        }
    }

    std::array<const char*, 4> CompositeEffectParameterNames(MaterialEffectKind kind, bool secondary) noexcept {
        if (secondary) return kind == MaterialEffectKind::ColorCorrect ? std::array<const char*,4>{"hue"} : std::array<const char*,4>{};
        if (kind == MaterialEffectKind::Blur) return {"radius"};
        if (kind == MaterialEffectKind::Glow) return {"threshold", "intensity", "radius"};
        return MaterialEffectParameterNames(kind);
    }
    std::string_view MaterialEffectName(MaterialEffectKind kind) noexcept {
        const size_t index = static_cast<size_t>(kind);
        return index < names.size() ? names[index] : "Invalid";
    }
    std::span<const std::string_view> GetMaterialEffectKindNames() noexcept {
        return names;
    }
    std::string MaterialEffectKindList() {
        std::string result;
        for (const std::string_view name : names) {
            if (!result.empty()) result += ", ";
            result += name;
        }
        return result;
    }
    bool ParseMaterialEffectKind(std::string_view text, MaterialEffectKind& kind) noexcept {
        for (size_t i = 0; i < names.size(); ++i) if (names[i] == text) {
            kind = static_cast<MaterialEffectKind>(i); return true;
        }
        return false;
    }
    MaterialEffectStep MakeMaterialEffect(MaterialEffectKind kind, std::string id) {
        MaterialEffectStep step; step.id = std::move(id); step.kind = kind;
        switch (kind) {
        case MaterialEffectKind::FlowMap:
            step.parameters = {0.15f, 0.8f, 1, 1};
            step.textureTarget = MaterialEffectTextureTarget::BaseColor;
            break;
        case MaterialEffectKind::Pixelate: step.parameters = {6,1,0,0}; break;
        case MaterialEffectKind::Posterize: step.parameters = {5,1,0,0}; break;
        case MaterialEffectKind::Ink: step.parameters = {1.5f,1,0.3f,1}; step.colorA = {0.015f,0.02f,0.03f,1}; break;
        // UvFlow: x/y=フロー速度, z/w=タイル, parameters2=回転/軸交換/ピボット。
        case MaterialEffectKind::UvFlow: step.parameters = {0.5f,0,1,1}; break;
        case MaterialEffectKind::UvDistortion: step.parameters = {0.06f,5,0.2f,0}; break;
        case MaterialEffectKind::Gradient:
            step.colorA = {0.05f,0.15f,0.8f,1}; step.colorB = {0.4f,1.5f,3,1};
            // 既定は 2 ストップ。増やすと多段ランプとして評価される。
            step.colorStops.push_back({step.id + ".start", 0.0f, step.colorA});
            step.colorStops.push_back({step.id + ".end", 1.0f, step.colorB});
            break;
        case MaterialEffectKind::Fade: step.parameters = {0.08f,0.35f,0,0}; break;
        case MaterialEffectKind::Wipe: step.parameters = {0,1,0.06f,0}; break;
        case MaterialEffectKind::Dissolve: step.parameters = {0,1,8,0.08f}; step.colorA = {0.4f,2,4,1}; break;
        case MaterialEffectKind::Emission: step.parameters = {1.5f,0.15f,2,0}; break;
        case MaterialEffectKind::Fresnel: step.parameters = {3,2,0.8f,0}; step.colorA = {0.1f,0.7f,2,1}; break;
        case MaterialEffectKind::Opacity: step.parameters = {1,0,0,0}; break;
        // Glow: x=しきい値, y=強度, z=軟らかさ。ぼかしは Blur が担当する。
        case MaterialEffectKind::Glow:
            // x=しきい値, y=強度, z=軟らかさ, w=ぼかし半径(UV)。
            // w=0 は加算のみ（従来動作）。w>0 で十字 4 タップのぼかしが入るが、
            // マテリアル式の命令/レジスタ予算を消費するので既定では切っておく。
            // 光輪が要る層だけ明示的に有効化する。
            step.parameters = {0.75f,1.5f,0.15f,0.0f}; step.colorA = {1,0.85f,0.6f,1}; break;
        // Blur: x=半径(UV), y=方向(度), z=片側タップ数(1..4)。
        case MaterialEffectKind::Blur: step.parameters = {0.02f,0,2,0}; break;
        // LightSweep: x=中心, y=幅, z=方向(度), w=強度。
        case MaterialEffectKind::LightSweep:
            step.parameters = {0.5f,0.35f,0,1.5f}; step.colorA = {1,0.95f,0.8f,1}; break;
        // LuminanceMask: x=しきい値, y=軟らかさ, z=0 乗算 / 1 置換。
        case MaterialEffectKind::LuminanceMask: step.parameters = {0.05f,0.1f,0,0}; break;
        // RadialBlur: x=半径, y/z=中心 UV, w=片側タップ数。parameters2.x は
        // 正=外側へ / 負=中心へ引く方向。
        case MaterialEffectKind::RadialBlur:
            step.parameters = {0.06f, 0.5f, 0.5f, 2.0f};
            step.parameters2 = {1.0f, 0, 0, 0};
            break;
        // TurbulentDisplace: x=振幅, y=周波数, z=進化速度, w=第 2 オクターブの比率。
        case MaterialEffectKind::TurbulentDisplace:
            step.parameters = {0.04f, 6.0f, 0.6f, 0.45f};
            break;
        // ColorCorrect: x=露出(乗算), y=コントラスト, z=彩度, w=黒レベル。
        case MaterialEffectKind::ColorCorrect:
            step.parameters = {1.0f, 1.0f, 1.0f, 0.0f};
            break;
        // Streak: x=長さ(UV), y=角度(度), z=タップ数, w=減衰(0..1)。
        case MaterialEffectKind::Streak:
            step.parameters = {0.06f, 0.0f, 4.0f, 0.6f};
            break;
        }
        return step;
    }

    MaterialAssetData BuildMaterialEffectStack(const MaterialEffectTextureSet& textures,
        std::span<const MaterialEffectStep> effects, bool lit,
        uint32_t atlasColumns, uint32_t atlasRows, std::span<const std::string> animatedParameters) {
        EffectGraph g;
        g.animated=animatedParameters;
        g.data.materialName = "VFX Effect Stack";
        g.data.surface.doubleSided = true;
        g.data.surface.unlit = !lit;
        g.data.pbr.unlit = !lit;
        const auto state = g.Node(Kind::ParticleState, Type::Float1, "Age");
        const Output seconds{state.node, "Seconds", Type::Float1};
        auto uv = g.Node(Kind::ParticleUv, Type::Float2, "Value");
        const auto localUv = g.Node(Kind::TextureCoordinate, Type::Float2, "UV1");
        // 追加スロットは先に資源として登録し、参照ノードは必要になった時点で
        // 作る。遮罩は最終 UV、ノイズは歪み/溶解の段の UV を使う。
        std::string maskResourceId{};
        std::string noiseResourceId{};
        std::string detailNoiseResourceId{};
        if (textures.mask.IsValid()) {
            MaterialGraphResourceData resource;
            resource.id = "effect.mask"; resource.assetGuid = textures.mask;
            resource.semantic = MaterialResourceSemantic::Mask; resource.enabled = true;
            g.data.graph.resources.push_back(resource);
            maskResourceId = resource.id;
        }
        if (textures.noise.IsValid()) {
            MaterialGraphResourceData resource;
            resource.id = "effect.noise"; resource.assetGuid = textures.noise;
            resource.semantic = MaterialResourceSemantic::Noise; resource.enabled = true;
            g.data.graph.resources.push_back(resource);
            noiseResourceId = resource.id;
        }
        if (textures.detailNoise.IsValid()) {
            MaterialGraphResourceData resource;
            resource.id = "effect.detailnoise";
            resource.assetGuid = textures.detailNoise;
            resource.semantic = MaterialResourceSemantic::DetailNoise;
            resource.enabled = true;
            g.data.graph.resources.push_back(resource);
            detailNoiseResourceId = resource.id;
        }
        if (textures.flowMap.IsValid()) {
            MaterialGraphResourceData resource;
            resource.id = "effect.flowMap";
            resource.assetGuid = textures.flowMap;
            resource.semantic = MaterialResourceSemantic::FlowMap;
            resource.enabled = true;
            g.data.graph.resources.push_back(resource);
        }
        EffectTextureSampling sampling{g, effects, seconds, textures.flowMap.IsValid()};
        const auto sampleResource = [&](const std::string& id, Output at) {
            const auto target = id == maskResourceId ? MaterialEffectTextureTarget::Mask :
                id == detailNoiseResourceId ? MaterialEffectTextureTarget::DetailNoise : MaterialEffectTextureTarget::Noise;
            return sampling.Sample(id, at, target);
        };
        for (const auto& step : effects) {
            if (!step.enabled) continue;
            const auto& p = step.parameters;
            if (step.kind == MaterialEffectKind::UvFlow) {
                if (step.textureTarget == MaterialEffectTextureTarget::Shared)
                    uv = ApplyUvFlow(g, uv, step, seconds);
            } else if (step.kind == MaterialEffectKind::UvDistortion) {
                auto offset = g.Node(Kind::MakeFloat2, Type::Float2, "Value");
                if (!noiseResourceId.empty()) {
                    // 貼図ノイズを使う場合、R と G をそのまま XY にすると
                    // グレースケール (R==G) のノイズでは対角線方向にしか
                    // ずれない。2 つ目の成分は別スケールで引き直して
                    // 去相関させる。これで任意のグレースケールノイズが
                    // 2 次元の変位場として使える。
                    const auto moving = g.Binary(Kind::Add,
                        g.Binary(Kind::Multiply, uv, g.C((std::max)(p.y, 0.01f))),
                        g.Binary(Kind::Multiply, seconds, g.C(p.z)));
                    const auto decorrelated = g.Binary(Kind::Add,
                        g.Binary(Kind::Multiply, moving,
                            g.Constant({1.37f, 1.37f, 0, 0}, Type::Float2)),
                        g.Constant({0.31f, 0.17f, 0, 0}, Type::Float2));
                    const auto sample =
                        g.Split(sampleResource(noiseResourceId, moving), Type::Float4);
                    // 2 つ目のノイズ場が指定されていれば、その Y をそのまま
                    // 使う。無い場合だけ同じ貼図を別スケールで引き直す。
                    const std::string& secondNoise =
                        detailNoiseResourceId.empty() ? noiseResourceId
                                                      : detailNoiseResourceId;
                    const auto sampleB =
                        g.Split(sampleResource(secondNoise, decorrelated), Type::Float4);
                    g.Link(sample.x, offset, "X"); g.Link(sampleB.y, offset, "Y");
                } else {
                    const auto moving = g.Binary(Kind::Add, uv, g.Binary(Kind::Multiply, seconds, g.C(p.z)));
                    const auto nx = g.Noise(moving, (std::max)(p.y, 0.01f));
                    const auto ny = g.Noise(g.Binary(Kind::Add, moving, g.Constant({17.1f,53.4f,0,0}, Type::Float2)), (std::max)(p.y, 0.01f));
                    g.Link(nx, offset, "X"); g.Link(ny, offset, "Y");
                }
                offset = g.Binary(Kind::Multiply, g.Binary(Kind::Subtract, offset, g.C(0.5f)), g.Parameter(step,"parameters.x",p.x));
                uv = g.Binary(Kind::Add, uv, offset);
            } else if (step.kind == MaterialEffectKind::TurbulentDisplace) {
                // 2 オクターブのノイズで場を作る。UvDistortion より粗く、
                // 進化速度で流れるので「煮立つ」感じが出る。
                const auto base = g.Binary(Kind::Maximum, g.Parameter(step, "parameters.y", p.y), g.C(0.01f));
                const auto moving = g.Binary(Kind::Add,
                    g.Binary(Kind::Multiply, uv, base),
                    g.Flow(step, "parameters.z", p.z, seconds));
                Output fieldX{};
                Output fieldY{};
                if (!noiseResourceId.empty()) {
                    // 上と同じ理由で 2 成分目は別スケールから取る。
                    // グレースケールノイズでも 2 次元の場になる。
                    const auto decorrelated = g.Binary(Kind::Add,
                        g.Binary(Kind::Multiply, moving,
                            g.Constant({1.37f, 1.37f, 0, 0}, Type::Float2)),
                        g.Constant({0.31f, 0.17f, 0, 0}, Type::Float2));
                    const auto sample = g.Split(
                        sampleResource(noiseResourceId, moving), Type::Float4);
                    const auto sampleB = g.Split(
                        sampleResource(noiseResourceId, decorrelated), Type::Float4);
                    fieldX = sample.x;
                    fieldY = sampleB.y;
                } else {
                    fieldX = g.Noise(moving, 1.0f);
                    fieldY = g.Noise(g.Binary(Kind::Add, moving,
                        g.Constant({19.3f, 7.7f, 0, 0}, Type::Float2)), 1.0f);
                }
                // 第 2 オクターブを足して細部を作る。比率 0 なら従来の粗さ。
                const auto detailX = g.Noise(
                    g.Binary(Kind::Multiply, moving, g.C(2.7f)), 2.0f);
                const auto detailY = g.Noise(g.Binary(Kind::Add,
                    g.Binary(Kind::Multiply, moving, g.C(2.7f)),
                    g.Constant({31.7f, 53.1f, 0, 0}, Type::Float2)), 2.0f);
                fieldX = g.Binary(Kind::Add, fieldX,
                    g.Binary(Kind::Multiply,
                        g.Binary(Kind::Subtract, detailX, g.C(0.5f)),
                        g.C((std::max)(p.w, 0.0f))));
                fieldY = g.Binary(Kind::Add, fieldY,
                    g.Binary(Kind::Multiply,
                        g.Binary(Kind::Subtract, detailY, g.C(0.5f)),
                        g.C((std::max)(p.w, 0.0f))));
                auto offset = g.Node(Kind::MakeFloat2, Type::Float2, "Value");
                g.Link(fieldX, offset, "X");
                g.Link(fieldY, offset, "Y");
                offset = g.Binary(Kind::Multiply,
                    g.Binary(Kind::Subtract, offset, g.C(0.5f)),
                    g.Parameter(step, "parameters.x", p.x));
                uv = g.Binary(Kind::Add, uv, offset);
            }
        }
        auto color = g.Constant({1,1,1,1}, Type::Float4);
        auto alpha = g.C(1);
        auto mask = g.C(1);
        auto emissive = g.Constant({}, Type::Float3);
        const AssetGuid& texture = textures.baseColor;
        const bool hasBaseTexture = texture.IsValid();
        if (hasBaseTexture) {
            MaterialGraphResourceData resource;
            resource.id = "effect.texture"; resource.assetGuid = texture;
            resource.semantic = MaterialResourceSemantic::BaseColor; resource.enabled = true;
            g.data.graph.resources.push_back(resource);
        }
        // 発光のぼかしは基準貼図を複数の UV で引き直して広がりを作る。
        // 効果スタックの後段（Glow）からも使えるよう、ここで持ち上げておく。
        const auto atlasUv = g.Node(Kind::TextureCoordinate, Type::Float2, "UV0");
        const auto sampleBaseAt = [&](Output at) {
            return sampling.SampleBase(at, atlasUv, localUv, atlasColumns, atlasRows);
        };
        // アトラス（フリップブック）ではタップがセル外へ出ないようグリッドで
        // 正規化する。ぼかし系が後段から参照するのでここで定義する。
        const float atlasU = atlasColumns > 1 ? float(atlasColumns) : 1.0f;
        const float atlasV = atlasRows > 1 ? float(atlasRows) : 1.0f;
        Output baseUv = uv;
        if (hasBaseTexture) {
            if (atlasColumns > 1 || atlasRows > 1) {
                // 描画側で解決済みのフレームを再利用し、流れ/歪みはセル内で適用。
                const auto inverseGrid = g.Constant({1.0f / (std::max)(atlasColumns, 1u),
                    1.0f / (std::max)(atlasRows, 1u), 0, 0}, Type::Float2);
                const auto atlasUv = g.Node(Kind::TextureCoordinate, Type::Float2, "UV0");
                const auto bounded = g.Binary(Kind::Add,
                    g.Binary(Kind::Multiply, g.Unary(Kind::Saturate, uv), g.C(0.998f)), g.C(0.001f));
                baseUv = g.Binary(Kind::Add, atlasUv,
                    g.Binary(Kind::Multiply, g.Binary(Kind::Subtract, bounded, localUv), inverseGrid));
            }
            color = sampleBaseAt(baseUv);
            // ぼかしは UV 段の確定後に、方向別の複数タップ平均として適用する。
            // 空間方向のサンプリングを所有するのは Blur だけ。
            for (const auto& step : effects) {
                if (!step.enabled || step.kind != MaterialEffectKind::Blur) continue;
                const auto& blurParameters = step.parameters;
                if (blurParameters.x <= 0.0f && !g.Animated(step, "parameters.x")) continue;
                const auto radius = g.Binary(Kind::Maximum, g.Parameter(step, "parameters.x", blurParameters.x), g.C(0));
                // アトラスではタップがセル外へ出ないようグリッドで正規化する。
                const int taps = std::clamp(static_cast<int>(blurParameters.z + 0.5f), 1, 6);
                const float angle = blurParameters.y * 0.01745329252f;
                const float tapU = std::cos(angle) / atlasU;
                const float tapV = std::sin(angle) / atlasV;
                // w>0.5 で直交方向にも伸ばす（十字 2D）。1 パスで 4 方向を
                // 平均するので、分離 2 パスのような中間バッファは要らない。
                // タップ数が 4 倍になるため既定では切っておく。
                const bool crossBlur = blurParameters.w > 0.5f;
                const float crossU = -std::sin(angle) / atlasU;
                const float crossV = std::cos(angle) / atlasV;
                auto sum = color;
                for (int tap = 1; tap <= taps; ++tap) {
                    const auto offset = g.Binary(Kind::Multiply, radius, g.Constant({tapU * tap, tapV * tap, 0, 0}, Type::Float2));
                    sum = g.Binary(Kind::Add, sum, sampleBaseAt(g.Binary(Kind::Add, baseUv, offset)));
                    sum = g.Binary(Kind::Add, sum, sampleBaseAt(g.Binary(Kind::Subtract, baseUv, offset)));
                    if (crossBlur) {
                        const auto crossOffset = g.Binary(Kind::Multiply, radius, g.Constant(
                            {crossU * tap, crossV * tap, 0, 0}, Type::Float2));
                        sum = g.Binary(Kind::Add, sum,
                            sampleBaseAt(g.Binary(Kind::Add, baseUv, crossOffset)));
                        sum = g.Binary(Kind::Add, sum,
                            sampleBaseAt(g.Binary(Kind::Subtract, baseUv, crossOffset)));
                    }
                }
                const float weight = 1.0f / float((crossBlur ? 4 : 2) * taps + 1);
                color = g.Binary(Kind::Multiply, sum,
                    g.Constant({weight, weight, weight, weight}, Type::Float4));
            }
            // 拖尾: 片側へ減衰しながらサンプルを伸ばす。対称平均の Blur とは
            // 別物で、進行方向の後ろだけが伸びるので「尾」に見える。
            for (const auto& step : effects) {
                if (!step.enabled || step.kind != MaterialEffectKind::Streak) continue;
                const auto& streak = step.parameters;
                const auto length = g.Binary(Kind::Maximum, g.Parameter(step, "parameters.x", streak.x), g.C(0));
                const int taps =
                    std::clamp(static_cast<int>(streak.z + 0.5f), 1, 8);
                const auto decay = g.Unary(Kind::Saturate, g.Parameter(step, "parameters.w", streak.w));
                const auto angle = g.Binary(Kind::Multiply, g.Parameter(step, "parameters.y", streak.y), g.C(0.01745329252f));
                const auto direction = g.Pair(
                    g.Binary(Kind::Multiply, g.Unary(Kind::Cosine, angle), g.C(-1.0f / atlasU / taps)),
                    g.Binary(Kind::Multiply, g.Unary(Kind::Sine, angle), g.C(-1.0f / atlasV / taps)));
                const auto stepOffset = g.Binary(Kind::Multiply, direction, length);
                auto sum = color;
                auto weightTotal = g.C(1);
                auto weight = g.C(1);
                for (int tap = 1; tap <= taps; ++tap) {
                    weight = g.Binary(Kind::Multiply, weight, g.Unary(Kind::OneMinus, decay));
                    const auto offset = g.Binary(Kind::Multiply, stepOffset, g.C(float(tap)));
                    sum = g.Binary(Kind::Add, sum,
                        g.Binary(Kind::Multiply,
                            sampleBaseAt(g.Binary(Kind::Add, baseUv, offset)),
                            weight));
                    weightTotal = g.Binary(Kind::Add, weightTotal, weight);
                }
                color = g.Binary(Kind::Divide, sum, weightTotal);
            }
            // 放射ブラーは中心から（または中心へ）向かう方向へタップを伸ばす。
            // 方向別 Blur と同じく、UV 確定後の複数タップ平均だけで作る。
            for (const auto& step : effects) {
                if (!step.enabled || step.kind != MaterialEffectKind::RadialBlur) continue;
                const auto& p = step.parameters;
                const auto radius = g.Binary(Kind::Maximum, g.Parameter(step, "parameters.x", p.x), g.C(0));
                const int taps = std::clamp(static_cast<int>(p.w + 0.5f), 1, 4);
                const float sign = step.parameters2.x < 0.0f ? -1.0f : 1.0f;
                // ズーム式の放射サンプリング: 中心へ向かう線分上を割合で刻む。
                // 正規化ノードに依存しないので Float2 のまま扱える。
                const auto centered = g.Binary(Kind::Subtract, baseUv,
                    g.Pair(g.Parameter(step, "parameters.y", p.y), g.Parameter(step, "parameters.z", p.z)));
                auto sum = color;
                for (int tap = 1; tap <= taps; ++tap) {
                    const auto scale = g.Binary(Kind::Multiply, radius, g.C(sign * float(tap) / float(taps)));
                    const auto offset = g.Binary(Kind::Multiply,
                        centered,
                        g.Binary(Kind::Multiply, scale, g.Constant({1.0f / atlasU, 1.0f / atlasV, 0, 0}, Type::Float2)));
                    sum = g.Binary(Kind::Add, sum,
                        sampleBaseAt(g.Binary(Kind::Subtract, baseUv, offset)));
                }
                const float weight = 1.0f / float(taps + 1);
                color = g.Binary(Kind::Multiply, sum,
                    g.Constant({weight, weight, weight, weight}, Type::Float4));
            }
            // 拡張した輪郭の alpha もフィルタ結果から取る。
            alpha = g.Split(color, Type::Float4).w;
            mask = g.Luminance(color);
        }
        // 遮罩は不透明度へ掛ける。底色の解決直後に適用し、以降の効果が
        // 発光や階調を自由に足せるようにする。
        if (!maskResourceId.empty()) {
            const auto maskSample =
                g.Split(sampleResource(maskResourceId, localUv), Type::Float4);
            // 色のにじみ防止用 RGB 拡張は透明画素にも値を持つ。遮罩では
            // 被覆率も掛け、白黒画像 (A=1) と透明縁の画像を同じ契約で扱う。
            alpha = g.Binary(Kind::Multiply, alpha,
                g.Binary(Kind::Multiply, maskSample.x, maskSample.w));
        }
        for (const auto& step : effects) {
            if (!step.enabled) continue;
            const auto& p = step.parameters;
            switch (step.kind) {
            case MaterialEffectKind::Gradient:
                if (step.colorStops.size() >= 2u) {
                    // 多段ランプは区間ごとの smoothstep を重ねて作る。ノードは
                    // 既存の Lerp/SmoothStep だけを使い、専用の解釈器は持たない。
                    auto stops = step.colorStops;
                    std::sort(stops.begin(), stops.end(),
                        [](const auto& left, const auto& right) {
                            return left.time < right.time;
                        });
                    auto ramp = g.Constant(stops.front().color, Type::Float4);
                    for (size_t index = 1u; index < stops.size(); ++index) {
                        const float span = (std::max)(
                            stops[index].time - stops[index - 1u].time, 1.0e-4f);
                        const auto t = g.Smooth(
                            g.C(stops[index - 1u].time),
                            g.C(stops[index - 1u].time + span), mask);
                        ramp = g.Lerp(ramp,
                            g.Constant(stops[index].color, Type::Float4), t);
                    }
                    color = ramp;
                } else {
                    color = g.Lerp(g.Constant(step.colorA, Type::Float4), g.Constant(step.colorB, Type::Float4), mask);
                }
                break;
            case MaterialEffectKind::Fade: {
                const auto fadeIn = g.Smooth(g.C(0), g.C((std::max)(p.x,0.0001f)), state);
                const auto fadeOut = g.Unary(Kind::OneMinus, g.Smooth(g.C(1-(std::max)(p.y,0.0001f)), g.C(1), state));
                alpha = g.Binary(Kind::Multiply, alpha, g.Binary(Kind::Multiply, fadeIn, fadeOut));
                break;
            }
            case MaterialEffectKind::Wipe:
            case MaterialEffectKind::Dissolve: {
                const auto progressId=step.id+".progress";
                const auto threshold=std::find(animatedParameters.begin(),animatedParameters.end(),progressId)!=animatedParameters.end() ?
                    g.Parameter(step,"progress",p.x) : g.Lerp(g.C(p.x),g.C(p.y),state);
                Output field;
                const bool dissolve = step.kind == MaterialEffectKind::Dissolve;
                if (dissolve && !noiseResourceId.empty()) {
                    // ノイズ貼図があれば溶解の場もそれに合わせる。
                    field = g.Split(sampleResource(noiseResourceId,
                        g.Binary(Kind::Multiply, uv, g.C((std::max)(p.z,0.01f)))), Type::Float4).x;
                }
                else if (dissolve) field = g.Noise(uv, (std::max)(p.z,0.01f));
                else {
                    const auto split = g.Split(localUv, Type::Float2);
                    field = p.w < 0.5f ? split.x : split.y;
                }
                const float width = (std::max)(dissolve ? p.w : p.z, 0.0001f);
                const auto edgeEnd = g.Binary(Kind::Add, threshold, g.C(width));
                const auto keep = g.Smooth(threshold, edgeEnd, field);
                if (dissolve) {
                    const auto outer = g.Smooth(edgeEnd, g.Binary(Kind::Add, edgeEnd, g.C(width)), field);
                    emissive = g.Binary(Kind::Add, emissive, g.Binary(Kind::Multiply,
                        g.Binary(Kind::Subtract, keep, outer), g.Constant(step.colorA, Type::Float3)));
                }
                alpha = g.Binary(Kind::Multiply, alpha, keep);
                break;
            }
            case MaterialEffectKind::Emission: {
                auto pulse = g.Unary(Kind::Sine, g.Binary(Kind::Multiply, seconds, g.C(p.z * 6.2831853f)));
                pulse = g.Binary(Kind::Multiply, g.Binary(Kind::Add, g.C(1),
                    g.Binary(Kind::Multiply, pulse, g.C(p.y))), g.Parameter(step,"parameters.x",p.x));
                emissive = g.Binary(Kind::Add, emissive, g.Binary(Kind::Multiply,
                    g.Constant(step.colorA, Type::Float3), g.Binary(Kind::Multiply, mask, pulse)));
                break;
            }
            case MaterialEffectKind::Fresnel: {
                auto rim = g.Node(Kind::Fresnel, Type::Float1);
                g.Link(g.C(0), rim, "F0"); g.Link(g.C((std::max)(p.x,0.01f)), rim, "Power");
                emissive = g.Binary(Kind::Add, emissive, g.Binary(Kind::Multiply,
                    g.Constant(step.colorA, Type::Float3), g.Binary(Kind::Multiply, rim, g.Parameter(step,"parameters.y",p.y))));
                alpha = g.Binary(Kind::Multiply, alpha, g.Lerp(g.C(1), rim, g.C(std::clamp(p.z,0.0f,1.0f))));
                break;
            }
            case MaterialEffectKind::Opacity:
                // スタック内の任意位置で効く統一の不透明度。
                alpha = g.Binary(Kind::Multiply, alpha,
                    g.Unary(Kind::Saturate, g.Parameter(step,"parameters.x",p.x)));
                break;
            case MaterialEffectKind::Glow: {
                // 明部抽出に加えて、基準貼図を半径違いで何度も引き直して
                // ぼかしを作る。UV 空間で広がるので輪郭の外側にも光輪が出る。
                // parameters.w が半径 (UV)。0 なら従来どおり加算のみ。
                const auto softness = (std::max)(p.z, 0.0001f);
                const auto intensity = g.Parameter(step,"parameters.y",p.y);
                const auto threshold = g.Parameter(step, "parameters.x", p.x);
                const auto thresholdEnd = g.Binary(Kind::Add, threshold, g.C(softness));
                const auto brightCore =
                    g.Smooth(threshold, thresholdEnd, g.Luminance(color));
                auto brightTotal = brightCore;
                const float radius = (std::max)(p.w, 0.0f);
                // 2 段目 (遠い半径) の多級ぼかし。parameters2.x が半径、
                // parameters2.y が重み (0 なら 0.5)。0 のときは従来動作のまま。
                const float farRadius = (std::max)(step.parameters2.x, 0.0f);
                const float farWeight = step.parameters2.y > 0.0f
                    ? step.parameters2.y
                    : 0.5f;
                if (hasBaseTexture && radius > 0.0f) {
                    // 十字 4 タップ。タップごとに輝度変換としきい値を計算すると
                    // 式の値スロット (48) を使い切り、コンパイルに失敗する。
                    // 先に平均を作り、輝度変換としきい値は 1 回だけ通す。
                    const auto offsetU = g.Constant(
                        {radius / atlasU, 0.0f, 0.0f, 0.0f}, Type::Float2);
                    const auto offsetV = g.Constant(
                        {0.0f, radius / atlasV, 0.0f, 0.0f}, Type::Float2);
                    auto averaged = sampleBaseAt(
                        g.Binary(Kind::Add, baseUv, offsetU));
                    averaged = g.Binary(Kind::Add, averaged, sampleBaseAt(
                        g.Binary(Kind::Subtract, baseUv, offsetU)));
                    averaged = g.Binary(Kind::Add, averaged, sampleBaseAt(
                        g.Binary(Kind::Add, baseUv, offsetV)));
                    averaged = g.Binary(Kind::Add, averaged, sampleBaseAt(
                        g.Binary(Kind::Subtract, baseUv, offsetV)));
                    averaged = g.Binary(Kind::Multiply, averaged,
                        g.Constant({0.25f, 0.25f, 0.25f, 0.25f}, Type::Float4));
                    const auto blurredBright = g.Smooth(
                        threshold, thresholdEnd, g.Luminance(averaged));
                    brightTotal = g.Binary(Kind::Add, brightCore, blurredBright);
                }
                if (hasBaseTexture && farRadius > radius) {
                    // 遠い段は V 軸 (リングでは帯幅 = 半径方向) の外側 1 タップのみ。
                    // タップを増やすと 128 レジスタ予算を超え、アニメーション付き
                    // レイヤーのマテリアルがコンパイルできなくなる。
                    const auto farOffsetV = g.Constant(
                        {0.0f, farRadius / atlasV, 0.0f, 0.0f}, Type::Float2);
                    auto far = sampleBaseAt(
                        g.Binary(Kind::Add, baseUv, farOffsetV));
                    const auto farBright = g.Smooth(
                        threshold, thresholdEnd, g.Luminance(far));
                    brightTotal = g.Binary(Kind::Add, brightTotal,
                        g.Binary(Kind::Multiply, farBright, g.C(farWeight)));
                }
                emissive = g.Binary(Kind::Add, emissive, g.Binary(Kind::Multiply,
                    g.Constant(step.colorA, Type::Float3),
                    g.Binary(Kind::Multiply, brightTotal, intensity)));
                break;
            }
            case MaterialEffectKind::ColorCorrect: {
                // x=露出(乗算), y=コントラスト(0.5 中心), z=彩度, w=黒レベル。
                // 追加サンプルを持たない四則演算だけなので、命令/レジスタ
                // 予算をほとんど消費しない。
                const auto exposure = g.Parameter(step, "parameters.x", p.x);
                const auto contrast = g.Parameter(step, "parameters.y", p.y);
                const auto saturation = g.Parameter(step, "parameters.z", p.z);
                const auto black = g.Parameter(step, "parameters.w", p.w);
                const auto rgba = g.Split(color, Type::Float4);
                const auto grade = [&](Output channel) {
                    auto value = g.Binary(Kind::Multiply,
                        g.Binary(Kind::Subtract, channel, black), exposure);
                    return g.Binary(Kind::Add,
                        g.Binary(Kind::Multiply,
                            g.Binary(Kind::Subtract, value, g.C(0.5f)), contrast),
                        g.C(0.5f));
                };
                const auto luminance = grade(g.Luminance(color));
                const auto graded = [&](Output channel) {
                    // 彩度は輝度へ寄せる/離す。1 でそのまま、0 で灰色。
                    return g.Lerp(luminance, grade(channel), saturation);
                };
                auto red = graded(rgba.x);
                auto green = graded(rgba.y);
                auto blue = graded(rgba.z);
                // 灰色軸 (1,1,1) 周りの回転。無彩色と HDR 強度を保ち、
                // 旧 R/B 平面回転のように白が負の赤成分を持たない。
                if (std::abs(step.parameters2.x) > 1.0e-4f || g.Animated(step, "parameters2.x")) {
                    const auto angle = g.Binary(Kind::Multiply,
                        g.Parameter(step, "parameters2.x", step.parameters2.x),
                        g.C(0.01745329252f));
                    const auto cosine = g.Unary(Kind::Cosine, angle);
                    const auto sine = g.Unary(Kind::Sine, angle);
                    const auto diagonal = g.Binary(Kind::Multiply,
                        g.Binary(Kind::Add, g.C(1), g.Binary(Kind::Multiply, cosine, g.C(2))), g.C(1.0f / 3.0f));
                    const auto base = g.Binary(Kind::Multiply,
                        g.Unary(Kind::OneMinus, cosine), g.C(1.0f / 3.0f));
                    const auto rotation = g.Binary(Kind::Multiply, sine, g.C(0.57735026919f));
                    const auto forward = g.Binary(Kind::Subtract, base, rotation);
                    const auto backward = g.Binary(Kind::Add, base, rotation);
                    const auto rotate = [&](Output a, Output b, Output c) {
                        return g.Binary(Kind::Maximum, g.C(0), g.Binary(Kind::Add,
                            g.Binary(Kind::Multiply, a, diagonal), g.Binary(Kind::Add,
                                g.Binary(Kind::Multiply, b, forward),
                                g.Binary(Kind::Multiply, c, backward))));
                    };
                    const auto oldRed = red;
                    const auto oldGreen = green;
                    red = rotate(oldRed, oldGreen, blue);
                    green = rotate(oldGreen, blue, oldRed);
                    blue = rotate(blue, oldRed, oldGreen);
                }
                auto node = g.Node(Kind::MakeFloat4, Type::Float4, "Value");
                g.Link(red, node, "X");
                g.Link(green, node, "Y");
                g.Link(blue, node, "Z");
                g.Link(rgba.w, node, "W");
                color = node;
                break;
            }
            case MaterialEffectKind::LuminanceMask: {
                const auto softness = g.Binary(Kind::Maximum, g.Parameter(step, "parameters.y", p.y), g.C(0.0001f));
                const auto threshold = g.Parameter(step, "parameters.x", p.x);
                const auto luminanceMask =
                    g.Smooth(threshold, g.Binary(Kind::Add, threshold, softness), g.Luminance(color));
                alpha = p.z > 0.5f ? luminanceMask
                                   : g.Binary(Kind::Multiply, alpha, luminanceMask);
                break;
            }
            case MaterialEffectKind::LightSweep: {
                // 方向に沿った帯を加算する AE のライトスイープ相当。
                const float sweepAngle = p.z * 0.01745329252f;
                const auto axis = g.Split(localUv, Type::Float2);
                const auto projection = g.Binary(Kind::Add,
                    g.Binary(Kind::Multiply, axis.x, g.C(std::cos(sweepAngle))),
                    g.Binary(Kind::Multiply, axis.y, g.C(std::sin(sweepAngle))));
                const auto center = g.Parameter(step, "parameters.x", p.x);
                const auto halfWidth = g.Binary(Kind::Multiply,
                    g.Binary(Kind::Maximum, g.Parameter(step, "parameters.y", p.y), g.C(0.0001f)), g.C(0.5f));
                const auto leading = g.Smooth(g.Binary(Kind::Subtract, center, halfWidth), center, projection);
                const auto trailing = g.Unary(Kind::OneMinus,
                    g.Smooth(center, g.Binary(Kind::Add, center, halfWidth), projection));
                emissive = g.Binary(Kind::Add, emissive, g.Binary(Kind::Multiply,
                    g.Constant(step.colorA, Type::Float3),
                    g.Binary(Kind::Multiply, g.Binary(Kind::Multiply, leading, trailing),
                        g.Parameter(step,"parameters.w",p.w))));
                break;
            }
            default: break;
            }
        }
        const auto output = g.Node(Kind::ParticleOutput, Type::Float4);
        g.Link(color, output, "Color"); g.Link(alpha, output, "Opacity"); g.Link(emissive, output, "Emissive");
        g.data.graph.outputNodeId = output.node;
        return std::move(g.data);
    }
}
