#include "Assets/Vfx/HIKARI_VfxBudgets.h"
#include "Assets/Formats/HIKARI_HvfxcFormat.h"
#include "RenderContracts/Asset/HIKARI_AssetTypes.h"
#include "Assets/Material/Serialization/HIKARI_MaterialSourceJson.h"
#include "Assets/Material/Compiler/HIKARI_MaterialProgramSerialization.h"

#include "Render3D/Material/Program/HIKARI_MaterialExpressionProgram.h"
#include <atomic>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "Core/IO/HIKARI_FileReplacementTransaction.h"

namespace HIKARI::VFX {
namespace {
using namespace RENDER3D::PARTICLES;

inline constexpr uint32_t kHvfxcMagic = 0x58465648u;
inline constexpr uint32_t kVfxProgramPayloadMagic = 0x32505648u;
inline constexpr size_t kMinimumSerializedInputBytes =
    sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(int64_t) + sizeof(float) * 4u;
inline constexpr size_t kMinimumSerializedEmitterBytes = sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t);
inline constexpr size_t kSerializedInputLinkBytes =
    sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(float) * 2u;
inline constexpr size_t kMinimumSerializedDependencyBytes = sizeof(uint32_t);

struct HvfxcHeader {
    uint32_t magic = kHvfxcMagic;
    uint32_t formatVersion = kHvfxcFormatVersion;
    uint32_t headerSize = sizeof(HvfxcHeader);
    uint32_t payloadSize = 0u;
    uint64_t sourceHash = 0u;
};

static_assert(sizeof(HvfxcHeader) == 24u);

class BinaryWriter final {
public:
    template <class T>
    void Write(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        if (!CanAppend(sizeof(T))) {
            failed_ = true;
            return;
        }
        const size_t offset = bytes_.size();
        bytes_.resize(offset + sizeof(T));
        std::memcpy(bytes_.data() + offset, &value, sizeof(T));
    }

    template <class Enum>
    void WriteEnum(Enum value) {
        Write(static_cast<std::underlying_type_t<Enum>>(value));
    }

    void WriteBool(bool value) { Write(static_cast<uint8_t>(value ? 1u : 0u)); }

    void WriteString(std::string_view value) {
        if (value.size() > kMaximumVfxProgramStringBytes ||
            value.size() > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
            failed_ = true;
            return;
        }
        Write(static_cast<uint32_t>(value.size()));
        if (failed_ || !CanAppend(value.size())) {
            failed_ = true;
            return;
        }
        bytes_.append(value.data(), value.size());
    }

    bool Failed() const noexcept { return failed_; }
    void Fail() noexcept { failed_ = true; }

    std::string Take() noexcept { return std::move(bytes_); }

private:
    bool CanAppend(size_t byteCount) const noexcept {
        return byteCount <= kMaximumHvfxcPayloadBytes && bytes_.size() <= kMaximumHvfxcPayloadBytes - byteCount;
    }

    std::string bytes_{};
    bool failed_ = false;
};

class BinaryReader final {
public:
    explicit BinaryReader(std::string_view bytes) : bytes_(bytes) {}

    template <class T>
    bool Read(T& outValue) {
        static_assert(std::is_trivially_copyable_v<T>);
        if (bytes_.size() - offset_ < sizeof(T)) {
            return false;
        }
        std::memcpy(&outValue, bytes_.data() + offset_, sizeof(T));
        offset_ += sizeof(T);
        return true;
    }

    template <class Enum>
    bool ReadEnum(Enum& outValue) {
        std::underlying_type_t<Enum> value{};
        if (!Read(value))
            return false;
        outValue = static_cast<Enum>(value);
        return true;
    }

    bool ReadBool(bool& outValue) {
        uint8_t value = 0u;
        if (!Read(value) || value > 1u)
            return false;
        outValue = value != 0u;
        return true;
    }

    bool ReadString(std::string& outValue) {
        uint32_t size = 0u;
        if (!Read(size) || size > kMaximumVfxProgramStringBytes || bytes_.size() - offset_ < size) {
            return false;
        }
        outValue.assign(bytes_.data() + offset_, size);
        offset_ += size;
        return true;
    }

    bool IsAtEnd() const noexcept { return offset_ == bytes_.size(); }

    size_t Remaining() const noexcept { return bytes_.size() - offset_; }

private:
    std::string_view bytes_{};
    size_t offset_ = 0u;
};

void WriteVec3(BinaryWriter& writer, const MATH::Vec3& value) {
    writer.Write(value.x);
    writer.Write(value.y);
    writer.Write(value.z);
}

void WriteVec4(BinaryWriter& writer, const MATH::Vec4& value) {
    writer.Write(value.x);
    writer.Write(value.y);
    writer.Write(value.z);
    writer.Write(value.w);
}

bool ReadVec3(BinaryReader& reader, MATH::Vec3& value) {
    return reader.Read(value.x) && reader.Read(value.y) && reader.Read(value.z);
}

bool ReadVec4(BinaryReader& reader, MATH::Vec4& value) {
    return reader.Read(value.x) && reader.Read(value.y) && reader.Read(value.z) && reader.Read(value.w);
}

void WriteTransform(BinaryWriter& writer, const VfxTransformData& value) {
    WriteVec3(writer, value.position);
    WriteVec3(writer, value.rotationEulerDegrees);
    WriteVec3(writer, value.scale);
}

bool ReadTransform(BinaryReader& reader, VfxTransformData& value) {
    return ReadVec3(reader, value.position) && ReadVec3(reader, value.rotationEulerDegrees) &&
           ReadVec3(reader, value.scale);
}

void WriteAttachment(BinaryWriter& writer, const VfxAttachmentData& value) {
    writer.WriteString(value.id);
    writer.WriteEnum(value.mode);
    writer.WriteEnum(value.follow);
    writer.WriteString(value.socketName);
    WriteTransform(writer, value.offset);
}

bool ReadAttachment(BinaryReader& reader, VfxAttachmentData& value) {
    return reader.ReadString(value.id) && reader.ReadEnum(value.mode) && reader.ReadEnum(value.follow) &&
           reader.ReadString(value.socketName) && ReadTransform(reader, value.offset);
}

void WriteSpecialOutput(BinaryWriter& writer, const VfxSpecialOutputData& value) {
    writer.WriteString(value.id);
    writer.WriteEnum(value.type);
    writer.WriteBool(value.enabled);
    const auto& weapon = value.weaponPathDistortion;
    writer.Write(weapon.distortion);
    writer.Write(weapon.blur);
    writer.Write(weapon.opacity);
    writer.Write(weapon.emissive);
    writer.Write(weapon.flow);
    writer.Write(weapon.width);
    writer.Write(weapon.lifetimeSeconds);
    WriteVec4(writer, weapon.bodyColor);
    WriteVec4(writer, weapon.edgeColor);
    const auto& screen = value.screenImpulse;
    writer.Write(screen.durationSeconds);
    writer.Write(screen.maxRadius);
    writer.Write(screen.ringThickness);
    writer.Write(screen.distortion);
    writer.Write(screen.chromaticAberration);
    writer.Write(screen.flash);
    writer.Write(screen.intensity);
    WriteVec4(writer, screen.color);
    writer.Write(screen.waveFrequency);
    writer.Write(screen.waveAmplitude);
    writer.Write(screen.waveDamping);
    writer.Write(screen.waveCount);
    const auto& afterimage = value.skinnedAfterimage;
    writer.Write(afterimage.sampleCount);
    writer.Write(afterimage.lifetimeSeconds);
    writer.Write(afterimage.opacity);
    writer.Write(afterimage.fresnel);
    writer.Write(afterimage.dissolve);
    writer.Write(afterimage.shellOffset);
    WriteVec4(writer, afterimage.color);
    writer.Write(afterimage.intensity);
    const auto& light = value.light;
    WriteVec4(writer, light.color);
    writer.Write(light.luminousFlux);
    writer.Write(light.range);
    writer.Write(light.sourceRadius);
    writer.Write(light.flickerFrequency);
    writer.Write(light.flickerAmplitude);
    writer.Write(light.priority);
    writer.WriteBool(light.castsShadows);
    writer.WriteBool(light.followFluidGlow);
    writer.Write(light.glowShare);
}

bool ReadSpecialOutput(BinaryReader& reader, VfxSpecialOutputData& value) {
    auto& weapon = value.weaponPathDistortion;
    auto& screen = value.screenImpulse;
    auto& afterimage = value.skinnedAfterimage;
    auto& light = value.light;
    return reader.ReadString(value.id) && reader.ReadEnum(value.type) && reader.ReadBool(value.enabled) &&
           reader.Read(weapon.distortion) && reader.Read(weapon.blur) && reader.Read(weapon.opacity) &&
           reader.Read(weapon.emissive) && reader.Read(weapon.flow) && reader.Read(weapon.width) &&
           reader.Read(weapon.lifetimeSeconds) && ReadVec4(reader, weapon.bodyColor) &&
           ReadVec4(reader, weapon.edgeColor) && reader.Read(screen.durationSeconds) && reader.Read(screen.maxRadius) &&
           reader.Read(screen.ringThickness) && reader.Read(screen.distortion) &&
           reader.Read(screen.chromaticAberration) && reader.Read(screen.flash) && reader.Read(screen.intensity) &&
           ReadVec4(reader, screen.color) && reader.Read(screen.waveFrequency) && reader.Read(screen.waveAmplitude) &&
           reader.Read(screen.waveDamping) && reader.Read(screen.waveCount) && reader.Read(afterimage.sampleCount) &&
           reader.Read(afterimage.lifetimeSeconds) && reader.Read(afterimage.opacity) &&
           reader.Read(afterimage.fresnel) && reader.Read(afterimage.dissolve) && reader.Read(afterimage.shellOffset) &&
           ReadVec4(reader, afterimage.color) && reader.Read(afterimage.intensity) && ReadVec4(reader, light.color) &&
           reader.Read(light.luminousFlux) && reader.Read(light.range) && reader.Read(light.sourceRadius) &&
           reader.Read(light.flickerFrequency) && reader.Read(light.flickerAmplitude) && reader.Read(light.priority) &&
           reader.ReadBool(light.castsShadows) && reader.ReadBool(light.followFluidGlow) &&
           reader.Read(light.glowShare);
}

void WriteValue(BinaryWriter& writer, const VfxValue& value) {
    writer.WriteEnum(value.type);
    writer.WriteBool(value.boolean);
    writer.Write(value.integer);
    WriteVec4(writer, value.numeric);
}

bool ReadValue(BinaryReader& reader, VfxValue& value) {
    return reader.ReadEnum(value.type) && reader.ReadBool(value.boolean) && reader.Read(value.integer) &&
           ReadVec4(reader, value.numeric);
}

void WriteDistribution(BinaryWriter& writer, const ParticleDistributionFloat& value) {
    writer.WriteEnum(value.mode);
    writer.Write(value.minimum);
    writer.Write(value.maximum);
    writer.Write(value.randomGroup);
}

void WriteDistribution(BinaryWriter& writer, const ParticleDistributionFloat3& value) {
    writer.WriteEnum(value.mode);
    WriteVec3(writer, value.minimum);
    WriteVec3(writer, value.maximum);
    writer.Write(value.randomGroup);
}

void WriteDistribution(BinaryWriter& writer, const ParticleDistributionFloat4& value) {
    writer.WriteEnum(value.mode);
    WriteVec4(writer, value.minimum);
    WriteVec4(writer, value.maximum);
    writer.Write(value.randomGroup);
}

bool ReadDistribution(BinaryReader& reader, ParticleDistributionFloat& value) {
    return reader.ReadEnum(value.mode) && reader.Read(value.minimum) && reader.Read(value.maximum) &&
           reader.Read(value.randomGroup);
}

bool ReadDistribution(BinaryReader& reader, ParticleDistributionFloat3& value) {
    return reader.ReadEnum(value.mode) && ReadVec3(reader, value.minimum) && ReadVec3(reader, value.maximum) &&
           reader.Read(value.randomGroup);
}

bool ReadDistribution(BinaryReader& reader, ParticleDistributionFloat4& value) {
    return reader.ReadEnum(value.mode) && ReadVec4(reader, value.minimum) && ReadVec4(reader, value.maximum) &&
           reader.Read(value.randomGroup);
}

void WriteCurve(BinaryWriter& writer, const ParticleCurve& curve) {
    writer.WriteEnum(curve.interpolation);
    WriteDistribution(writer, curve.randomOffset);
    writer.Write(curve.keyCount);
    for (uint32_t index = 0u; index < curve.keyCount; ++index) {
        const ParticleCurveKey& key = curve.keys[index];
        writer.Write(key.time);
        writer.Write(key.value);
        writer.Write(key.inTangent);
        writer.Write(key.outTangent);
    }
}

bool ReadCurve(BinaryReader& reader, ParticleCurve& curve) {
    if (!reader.ReadEnum(curve.interpolation) || !ReadDistribution(reader, curve.randomOffset) ||
        !reader.Read(curve.keyCount) || curve.keyCount > kMaxParticleCurveKeys) {
        return false;
    }
    for (uint32_t index = 0u; index < curve.keyCount; ++index) {
        ParticleCurveKey& key = curve.keys[index];
        if (!reader.Read(key.time) || !reader.Read(key.value) || !reader.Read(key.inTangent) ||
            !reader.Read(key.outTangent)) {
            return false;
        }
    }
    return true;
}

void WriteRuntimeEffects(BinaryWriter& writer, const VfxProgramEmitter& emitter) {
    writer.Write(uint32_t(emitter.runtimeEffects.size()));
    for (const auto& e : emitter.runtimeEffects) {
        writer.WriteString(e.id);
        writer.WriteEnum(e.kind);
        writer.WriteEnum(e.space);
        writer.WriteBool(e.enabled);
        writer.Write(e.mix);
        writer.WriteEnum(e.mask);
        writer.WriteBool(e.maskInvert);
        WriteVec4(writer, e.maskParameters);
        WriteVec4(writer, e.parameters);
        WriteVec4(writer, e.parameters2);
        WriteVec4(writer, e.colorA);
        WriteVec4(writer, e.colorB);
        writer.Write(uint32_t(e.colorStops.size()));
        for (const auto& stop : e.colorStops) {
            writer.WriteString(stop.id);
            writer.Write(stop.time);
            WriteVec4(writer, stop.color);
        }
    }
    writer.Write(uint32_t(emitter.spatialOwners.size()));
    for (const auto owner : emitter.spatialOwners)
        writer.Write(owner);
}
bool ReadRuntimeEffects(BinaryReader& reader, VfxProgramEmitter& emitter) {
    uint32_t count = 0;
    if (!reader.Read(count) || count > kMaximumVfxEffectsPerLayer)
        return false;
    emitter.runtimeEffects.resize(count);
    for (auto& e : emitter.runtimeEffects) {
        uint32_t stops = 0;
        if (!reader.ReadString(e.id) || !reader.ReadEnum(e.kind) || !reader.ReadEnum(e.space) ||
            !reader.ReadBool(e.enabled) || !reader.Read(e.mix) || !reader.ReadEnum(e.mask) ||
            !reader.ReadBool(e.maskInvert) || !ReadVec4(reader, e.maskParameters) || !ReadVec4(reader, e.parameters) ||
            !ReadVec4(reader, e.parameters2) || !ReadVec4(reader, e.colorA) || !ReadVec4(reader, e.colorB) ||
            !reader.Read(stops) || stops > kMaximumMaterialEffectColorStops)
            return false;
        e.colorStops.resize(stops);
        for (auto& stop : e.colorStops)
            if (!reader.ReadString(stop.id) || !reader.Read(stop.time) || !ReadVec4(reader, stop.color))
                return false;
    }
    if (!reader.Read(count) || count > kMaximumVfxProgramEmitters)
        return false;
    emitter.spatialOwners.resize(count);
    for (auto& owner : emitter.spatialOwners)
        if (!reader.Read(owner))
            return false;
    return true;
}

void WriteLayerCurves(BinaryWriter& writer, const std::vector<VfxProgramLayerCurve>& curves) {
    writer.Write(static_cast<uint32_t>(curves.size()));
    for (const auto& curve : curves) {
        writer.WriteEnum(curve.channel);
        writer.Write(curve.materialSlot);
        writer.Write(curve.ownerEmitterIndex);
        writer.WriteEnum(curve.interpolation);
        writer.Write(curve.minimum);
        writer.Write(curve.maximum);
        writer.Write(static_cast<uint32_t>(curve.keys.size()));
        for (const auto& key : curve.keys) {
            writer.Write(key.time);
            writer.Write(key.value);
            writer.Write(key.inTangent);
            writer.Write(key.outTangent);
        }
    }
}
bool ReadLayerCurves(BinaryReader& reader, std::vector<VfxProgramLayerCurve>& curves) {
    uint32_t count = 0;
    if (!reader.Read(count) ||
        count > RENDER3D::MATERIAL::kGpuMaterialProgramParameterSlotCount + 18 + kMaximumVfxRuntimeEffectCurves)
        return false;
    for (uint32_t i = 0; i < count; ++i) {
        VfxProgramLayerCurve curve;
        uint32_t keys = 0;
        if (!reader.ReadEnum(curve.channel) || !reader.Read(curve.materialSlot) ||
            !reader.Read(curve.ownerEmitterIndex) || !reader.ReadEnum(curve.interpolation) ||
            !reader.Read(curve.minimum) || !reader.Read(curve.maximum) || !reader.Read(keys) || keys == 0 ||
            keys > kMaxVfxLayerCurveKeys || keys > reader.Remaining() / (4 * sizeof(float)))
            return false;
        curve.keys.resize(keys);
        for (auto& key : curve.keys)
            if (!reader.Read(key.time) || !reader.Read(key.value) || !reader.Read(key.inTangent) ||
                !reader.Read(key.outTangent))
                return false;
        curves.push_back(std::move(curve));
    }
    return true;
}

void WriteAnimated(BinaryWriter& writer, const ParticleAnimatedFloat3& value) {
    writer.WriteEnum(value.mode);
    WriteDistribution(writer, value.initial);
    WriteDistribution(writer, value.velocity);
    WriteDistribution(writer, value.acceleration);
    WriteDistribution(writer, value.easingStart);
    WriteDistribution(writer, value.easingMiddle);
    WriteDistribution(writer, value.easingEnd);
    writer.WriteEnum(value.easingFunction);
    writer.Write(value.hasEasingMiddle);
    for (const ParticleCurve& curve : value.curves) {
        WriteCurve(writer, curve);
    }
}

bool ReadAnimated(BinaryReader& reader, ParticleAnimatedFloat3& value) {
    if (!reader.ReadEnum(value.mode) || !ReadDistribution(reader, value.initial) ||
        !ReadDistribution(reader, value.velocity) || !ReadDistribution(reader, value.acceleration) ||
        !ReadDistribution(reader, value.easingStart) || !ReadDistribution(reader, value.easingMiddle) ||
        !ReadDistribution(reader, value.easingEnd) || !reader.ReadEnum(value.easingFunction) ||
        !reader.Read(value.hasEasingMiddle)) {
        return false;
    }
    for (ParticleCurve& curve : value.curves) {
        if (!ReadCurve(reader, curve))
            return false;
    }
    return true;
}

void WriteAnimated(BinaryWriter& writer, const ParticleAnimatedColor& value) {
    writer.WriteEnum(value.mode);
    WriteDistribution(writer, value.fixed);
    WriteDistribution(writer, value.easingStart);
    WriteDistribution(writer, value.easingMiddle);
    WriteDistribution(writer, value.easingEnd);
    writer.WriteEnum(value.easingFunction);
    writer.Write(value.hasEasingMiddle);
    for (const ParticleCurve& curve : value.curves) {
        WriteCurve(writer, curve);
    }
    writer.Write(value.gradient.stopCount);
    for (uint32_t index = 0u; index < value.gradient.stopCount; ++index) {
        writer.Write(value.gradient.stops[index].time);
        WriteVec4(writer, value.gradient.stops[index].color);
    }
}

bool ReadAnimated(BinaryReader& reader, ParticleAnimatedColor& value) {
    if (!reader.ReadEnum(value.mode) || !ReadDistribution(reader, value.fixed) ||
        !ReadDistribution(reader, value.easingStart) || !ReadDistribution(reader, value.easingMiddle) ||
        !ReadDistribution(reader, value.easingEnd) || !reader.ReadEnum(value.easingFunction) ||
        !reader.Read(value.hasEasingMiddle)) {
        return false;
    }
    for (ParticleCurve& curve : value.curves) {
        if (!ReadCurve(reader, curve))
            return false;
    }
    if (!reader.Read(value.gradient.stopCount) || value.gradient.stopCount > kMaxParticleColorStops) {
        return false;
    }
    for (uint32_t index = 0u; index < value.gradient.stopCount; ++index) {
        if (!reader.Read(value.gradient.stops[index].time) || !ReadVec4(reader, value.gradient.stops[index].color)) {
            return false;
        }
    }
    return true;
}

// 流体層の設定は字段表（唯一の宣言）で JSON にして文字列で持つ。種類の違う層には書かない。
template <class Settings, class Fields>
void WriteFieldTable(BinaryWriter& writer, const Settings& settings, const Fields& fields) {
    nlohmann::json json;
    JSONFIELDS::Write(json, settings, fields);
    writer.WriteString(json.dump());
}

template <class Settings, class Fields>
bool ReadFieldTable(BinaryReader& reader, Settings& settings, const Fields& fields) {
    std::string text;
    if (!reader.ReadString(text))
        return false;
    try {
        JSONFIELDS::Read(nlohmann::json::parse(text), settings, fields);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

void WriteFluidSettings(BinaryWriter& writer, const VfxProgramEmitter& emitter) {
    if (emitter.kind == VfxLayerKind::Fluid)
        WriteFieldTable(writer, emitter.fluidDomain, RENDER3D::FLUID::kFluidDomainSettingsFields);
    if (emitter.kind == VfxLayerKind::FluidSource) {
        WriteFieldTable(writer, emitter.fluidSource, RENDER3D::FLUID::kFluidSourceSettingsFields);
        writer.Write(emitter.fluidDomainIndex);
    }
}

bool ReadFluidSettings(BinaryReader& reader, VfxProgramEmitter& emitter) {
    if (emitter.kind == VfxLayerKind::Fluid)
        return ReadFieldTable(reader, emitter.fluidDomain, RENDER3D::FLUID::kFluidDomainSettingsFields);
    if (emitter.kind == VfxLayerKind::FluidSource)
        return ReadFieldTable(reader, emitter.fluidSource, RENDER3D::FLUID::kFluidSourceSettingsFields) &&
               reader.Read(emitter.fluidDomainIndex);
    return true;
}

void WriteProgram(BinaryWriter& writer, const VfxProgram& program) {
    writer.Write(kVfxProgramPayloadMagic);
    writer.Write(program.version);
    writer.Write(program.sourceVersion);
    writer.Write(program.sourceHash);
    writer.WriteString(program.programId);
    writer.WriteString(program.name);
    writer.Write(program.durationSeconds);
    writer.Write(program.maximumParticleLifetimeSeconds);
    writer.WriteBool(program.loop);

    writer.Write(static_cast<uint32_t>(program.inputs.size()));
    for (const VfxProgramInput& input : program.inputs) {
        writer.WriteString(input.id);
        writer.WriteEnum(input.type);
        WriteValue(writer, input.defaultValue);
    }

    writer.Write(static_cast<uint32_t>(program.emitters.size()));
    for (const VfxProgramEmitter& emitter : program.emitters) {
        writer.WriteString(emitter.sourceEmitterId);
        writer.Write(emitter.emitterIndex);
        writer.Write(emitter.parentEmitterIndex);
        writer.Write(emitter.matteSourceIndex);
        writer.Write(emitter.matteLuminance);
        writer.Write(emitter.matteInverted);
        writer.Write(emitter.matteOnly);
        writer.Write(emitter.orientAlongPath);
        WriteVec3(writer, emitter.transform.position);
        WriteVec3(writer, emitter.transform.rotationEulerDegrees);
        WriteVec3(writer, emitter.transform.scale);
        writer.Write(emitter.startDelay);
        writer.Write(emitter.activeDuration);
        writer.WriteEnum(emitter.kind);
        writer.WriteEnum(emitter.history.mode);
        writer.Write(emitter.history.duration);
        writer.Write(emitter.history.samples);
        writer.Write(emitter.history.breakDistance);
        writer.Write(emitter.history.tailWidth);
        writer.Write(emitter.history.fadePower);
        WriteVec4(writer, emitter.history.tailColor);
        WriteLayerCurves(writer, emitter.layerCurves);
        WriteRuntimeEffects(writer, emitter);
        writer.Write(emitter.animationTiming.sourceDuration);
        writer.Write(emitter.animationTiming.offset);
        writer.Write(emitter.animationTiming.rate);
        WriteAttachment(writer, emitter.attachment);
        writer.Write(emitter.spawnRatePerSecond);
        writer.Write(static_cast<uint32_t>(emitter.burstTimeline.size()));
        for (const VfxBurstKeyData& key : emitter.burstTimeline) {
            writer.WriteString(key.id);
            writer.Write(key.time);
            writer.Write(key.count);
        }
        writer.Write(emitter.authoredOverrides.enabled);
        writer.Write(emitter.authoredOverrides.spawnRateMultiplier);
        writer.Write(emitter.authoredOverrides.radialSpeedMultiplier);
        writer.Write(emitter.authoredOverrides.scaleMultiplier);
        writer.Write(emitter.authoredOverrides.gravityMultiplier);
        WriteVec4(writer, emitter.authoredOverrides.colorMultiplier);

        const ParticleEmitterSimulationParameters& simulation = emitter.simulation;
        writer.Write(simulation.capacity);
        WriteDistribution(writer, simulation.lifetime);
        writer.WriteEnum(simulation.shape);
        WriteDistribution(writer, simulation.shapeRadius);
        writer.Write(simulation.coneAngleRadians);
        WriteVec3(writer, simulation.shapeBoxHalfExtents);
        writer.Write(simulation.shapeCylinderHeight);
        writer.Write(simulation.shapeCircleInnerRadius);
        writer.Write(simulation.shapeCircleArcDegrees);
        writer.Write(simulation.shapeCircleEmitOnEdge);
        writer.Write(simulation.shapeEdgeLength);
        WriteDistribution(writer, simulation.radialSpeed);
        WriteAnimated(writer, simulation.position);
        WriteAnimated(writer, simulation.rotationRadians);
        WriteAnimated(writer, simulation.scale);
        WriteVec3(writer, simulation.gravity);
        writer.Write(simulation.drag);
        writer.Write(simulation.fluidFollow);
        writer.Write(simulation.fluidSmoke);
        writer.Write(simulation.fluidTemperature);
        writer.Write(simulation.fluidCoupling);
        writer.Write(simulation.fluidRadius);
        writer.Write(simulation.localSimulation);
        writer.Write(simulation.collisionPlaneEnabled);
        writer.Write(simulation.collisionPlaneHeight);
        writer.Write(simulation.sceneDepthCollision);
        writer.Write(simulation.collisionThickness);
        writer.Write(simulation.restitution);
        writer.Write(simulation.friction);
        writer.Write(simulation.eventKinds);
        writer.Write(simulation.eventImpactThreshold);
        writer.Write(simulation.eventSourceEmitter);
        writer.Write(simulation.eventKind);
        writer.Write(simulation.particlesPerEvent);
        writer.Write(simulation.inheritVelocity);
        writer.Write(simulation.minimumImpactSpeed);
        WriteVec3(writer, simulation.field.center);
        writer.Write(simulation.field.radius);
        WriteVec3(writer, simulation.field.axis);
        writer.Write(simulation.field.vortex);
        writer.Write(simulation.field.turbulence);
        writer.Write(simulation.field.frequency);
        writer.Write(simulation.field.evolution);
        writer.Write(simulation.field.attraction);
        WriteAnimated(writer, simulation.color);

        writer.WriteEnum(emitter.render.materialMode);
        writer.WriteString(emitter.render.colorTextureAssetGuid);
        writer.WriteString(emitter.render.maskTextureAssetGuid);
        writer.WriteString(emitter.render.noiseTextureAssetGuid);
        writer.WriteString(emitter.render.materialAssetGuid);
        writer.WriteEnum(emitter.render.blendMode);
        writer.WriteEnum(emitter.render.postProcessMode);
        writer.WriteEnum(emitter.render.shape);
        writer.WriteEnum(emitter.render.billboardMode);
        writer.WriteEnum(emitter.render.depthMode);
        writer.Write(emitter.render.softIntersectionDistance);
        writer.Write(emitter.render.depthBias);
        writer.Write(emitter.render.flipbookColumns);
        writer.Write(emitter.render.flipbookRows);
        writer.Write(emitter.render.flipbookFrameCount);
        writer.Write(emitter.render.flipbookFramesPerSecond);
        writer.WriteBool(emitter.render.flipbookLoop);
        writer.WriteBool(emitter.render.flipbookRandomStart);
        writer.Write(emitter.render.ribbonSegmentCount);
        writer.Write(emitter.render.ribbonWidth);
        writer.Write(emitter.render.ringOuterRadius);
        writer.Write(emitter.render.ringInnerRadius);
        writer.Write(emitter.render.ringSegmentCount);
        writer.Write(emitter.render.ringStartAngleRadians);
        writer.Write(emitter.render.ringEndAngleRadians);
        writer.Write(emitter.render.uvMode);
        writer.Write(emitter.render.uvTileLength);
        writer.Write(emitter.render.uvTilingU);
        writer.Write(emitter.render.uvTilingV);
        writer.Write(emitter.render.uvRotationRadians);
        writer.Write(emitter.render.uvSwapAxes);
        writer.Write(emitter.render.anchor.x);
        writer.Write(emitter.render.anchor.y);
        writer.Write(emitter.render.anchor.z);
        writer.Write(emitter.render.connectTargetMode);
        writer.Write(emitter.render.connectTargetLayerIndex);
        writer.Write(emitter.render.connectLocalPosition.x);
        writer.Write(emitter.render.connectLocalPosition.y);
        writer.Write(emitter.render.connectLocalPosition.z);
        writer.Write(emitter.render.connectPivot);
        writer.Write(emitter.render.connectLengthScale);
        writer.Write(emitter.render.connectStartOffset);
        writer.Write(emitter.render.connectEndOffset);
        writer.Write(emitter.render.connectFlags);
        writer.WriteString(emitter.render.meshAssetGuid);
        writer.Write(emitter.render.meshOutputFlags);
        // 形状パラメータは資産JSON・本フォーマット・GPU定数の
        // 3箇所すべてへ通す必要がある。欠けると既定値のままになる。
        writer.Write(emitter.render.taperStart);
        writer.Write(emitter.render.taperEnd);
        writer.Write(emitter.render.cylinderTopRadius);
        writer.Write(emitter.render.cylinderBottomRadius);
        writer.Write(emitter.render.coneBaseRadius);
        writer.Write(emitter.render.bendAmount);
        writer.Write(emitter.render.twistRadians);
        writer.Write(emitter.render.torusMajorRadius);
        writer.Write(emitter.render.torusMinorRadius);
        writer.Write(emitter.render.capsuleRadius);
        writer.Write(emitter.render.billboardAxis);
        writer.WriteBool(emitter.render.effectMaterialIncludesParticleColor);
        writer.WriteBool(emitter.render.effectMaterial != nullptr);
        if (const auto& material = emitter.render.effectMaterial) {
            std::string source, programIr, error;
            if (!MATERIAL_SERIALIZATION::SerializeMaterialSourceJson(material->sourceData, source, error) ||
                !MATERIAL_SERIALIZATION::SerializeMaterialCompileResult(material->compileResult, programIr, error)) {
                writer.Fail();
                return;
            }
            writer.WriteString(source);
            writer.WriteString(programIr);
        }

        writer.Write(static_cast<uint32_t>(emitter.specialOutputs.size()));
        for (const VfxSpecialOutputData& output : emitter.specialOutputs) {
            WriteSpecialOutput(writer, output);
        }

        writer.Write(static_cast<uint32_t>(emitter.inputLinks.size()));
        for (const VfxProgramInputLink& link : emitter.inputLinks) {
            writer.Write(link.inputIndex);
            writer.WriteEnum(link.semantic);
            writer.Write(link.byteOffset);
            writer.WriteEnum(link.storageType);
            writer.WriteEnum(link.inputType);
            writer.Write(link.scale);
            writer.Write(link.bias);
        }
        WriteFluidSettings(writer, emitter);
    }

    writer.Write(static_cast<uint32_t>(program.dependencyGuids.size()));
    for (const AssetGuid& dependency : program.dependencyGuids) {
        writer.WriteString(dependency.value);
    }
}

bool ReadProgram(BinaryReader& reader, VfxProgram& program) {
    uint32_t magic = 0u;
    if (!reader.Read(magic) || magic != kVfxProgramPayloadMagic || !reader.Read(program.version) ||
        !reader.Read(program.sourceVersion) || !reader.Read(program.sourceHash) ||
        !reader.ReadString(program.programId) || !reader.ReadString(program.name) ||
        !reader.Read(program.durationSeconds) || !reader.Read(program.maximumParticleLifetimeSeconds) ||
        !reader.ReadBool(program.loop)) {
        return false;
    }

    uint32_t inputCount = 0u;
    if (!reader.Read(inputCount) || inputCount > kMaximumVfxProgramInputs ||
        inputCount > reader.Remaining() / kMinimumSerializedInputBytes) {
        return false;
    }
    program.inputs.reserve(inputCount);
    for (uint32_t index = 0u; index < inputCount; ++index) {
        VfxProgramInput input{};
        if (!reader.ReadString(input.id) || !reader.ReadEnum(input.type) || !ReadValue(reader, input.defaultValue)) {
            return false;
        }
        program.inputs.push_back(std::move(input));
    }

    uint32_t emitterCount = 0u;
    if (!reader.Read(emitterCount) || emitterCount == 0u || emitterCount > kMaximumVfxProgramEmitters ||
        emitterCount > reader.Remaining() / kMinimumSerializedEmitterBytes) {
        return false;
    }
    program.emitters.reserve(emitterCount);
    uint64_t totalInputLinks = 0u;
    for (uint32_t index = 0u; index < emitterCount; ++index) {
        VfxProgramEmitter emitter{};
        uint32_t burstCount = 0u;
        if (!reader.ReadString(emitter.sourceEmitterId) || !reader.Read(emitter.emitterIndex) ||
            !reader.Read(emitter.parentEmitterIndex) || !reader.Read(emitter.matteSourceIndex) ||
            !reader.Read(emitter.matteLuminance) || !reader.Read(emitter.matteInverted) ||
            !reader.Read(emitter.matteOnly) || !reader.Read(emitter.orientAlongPath) ||
            !ReadVec3(reader, emitter.transform.position) ||
            !ReadVec3(reader, emitter.transform.rotationEulerDegrees) || !ReadVec3(reader, emitter.transform.scale) ||
            !reader.Read(emitter.startDelay) || !reader.Read(emitter.activeDuration) ||
            !reader.ReadEnum(emitter.kind) || !reader.ReadEnum(emitter.history.mode) ||
            !reader.Read(emitter.history.duration) || !reader.Read(emitter.history.samples) ||
            !reader.Read(emitter.history.breakDistance) || !reader.Read(emitter.history.tailWidth) ||
            !reader.Read(emitter.history.fadePower) || !ReadVec4(reader, emitter.history.tailColor) ||
            !ReadLayerCurves(reader, emitter.layerCurves) || !ReadRuntimeEffects(reader, emitter) ||
            !reader.Read(emitter.animationTiming.sourceDuration) || !reader.Read(emitter.animationTiming.offset) ||
            !reader.Read(emitter.animationTiming.rate) || !ReadAttachment(reader, emitter.attachment) ||
            !reader.Read(emitter.spawnRatePerSecond) || !reader.Read(burstCount)) {
            return false;
        }
        if (burstCount > kMaximumVfxProgramBurstKeys ||
            burstCount > reader.Remaining() / (sizeof(float) + sizeof(uint32_t) + sizeof(uint32_t))) {
            return false;
        }
        emitter.burstTimeline.reserve(burstCount);
        for (uint32_t burstIndex = 0u; burstIndex < burstCount; ++burstIndex) {
            VfxBurstKeyData key{};
            if (!reader.ReadString(key.id) || !reader.Read(key.time) || !reader.Read(key.count)) {
                return false;
            }
            emitter.burstTimeline.push_back(std::move(key));
        }
        if (!reader.Read(emitter.authoredOverrides.enabled) ||
            !reader.Read(emitter.authoredOverrides.spawnRateMultiplier) ||
            !reader.Read(emitter.authoredOverrides.radialSpeedMultiplier) ||
            !reader.Read(emitter.authoredOverrides.scaleMultiplier) ||
            !reader.Read(emitter.authoredOverrides.gravityMultiplier) ||
            !ReadVec4(reader, emitter.authoredOverrides.colorMultiplier)) {
            return false;
        }

        ParticleEmitterSimulationParameters& simulation = emitter.simulation;
        if (!reader.Read(simulation.capacity) || !ReadDistribution(reader, simulation.lifetime) ||
            !reader.ReadEnum(simulation.shape) || !ReadDistribution(reader, simulation.shapeRadius) ||
            !reader.Read(simulation.coneAngleRadians) || !ReadVec3(reader, simulation.shapeBoxHalfExtents) ||
            !reader.Read(simulation.shapeCylinderHeight) || !reader.Read(simulation.shapeCircleInnerRadius) ||
            !reader.Read(simulation.shapeCircleArcDegrees) || !reader.Read(simulation.shapeCircleEmitOnEdge) ||
            !reader.Read(simulation.shapeEdgeLength) || !ReadDistribution(reader, simulation.radialSpeed) ||
            !ReadAnimated(reader, simulation.position) || !ReadAnimated(reader, simulation.rotationRadians) ||
            !ReadAnimated(reader, simulation.scale) || !ReadVec3(reader, simulation.gravity) ||
            !reader.Read(simulation.drag) || !reader.Read(simulation.fluidFollow) ||
            !reader.Read(simulation.fluidSmoke) || !reader.Read(simulation.fluidTemperature) ||
            !reader.Read(simulation.fluidCoupling) || !reader.Read(simulation.fluidRadius) ||
            !reader.Read(simulation.localSimulation) || !reader.Read(simulation.collisionPlaneEnabled) ||
            !reader.Read(simulation.collisionPlaneHeight) || !reader.Read(simulation.sceneDepthCollision) ||
            !reader.Read(simulation.collisionThickness) || !reader.Read(simulation.restitution) ||
            !reader.Read(simulation.friction) || !reader.Read(simulation.eventKinds) ||
            !reader.Read(simulation.eventImpactThreshold) || !reader.Read(simulation.eventSourceEmitter) ||
            !reader.Read(simulation.eventKind) || !reader.Read(simulation.particlesPerEvent) ||
            !reader.Read(simulation.inheritVelocity) || !reader.Read(simulation.minimumImpactSpeed) ||
            !ReadVec3(reader, simulation.field.center) || !reader.Read(simulation.field.radius) ||
            !ReadVec3(reader, simulation.field.axis) || !reader.Read(simulation.field.vortex) ||
            !reader.Read(simulation.field.turbulence) || !reader.Read(simulation.field.frequency) ||
            !reader.Read(simulation.field.evolution) || !reader.Read(simulation.field.attraction) ||
            !ReadAnimated(reader, simulation.color) || !reader.ReadEnum(emitter.render.materialMode) ||
            !reader.ReadString(emitter.render.colorTextureAssetGuid) ||
            !reader.ReadString(emitter.render.maskTextureAssetGuid) ||
            !reader.ReadString(emitter.render.noiseTextureAssetGuid) ||
            !reader.ReadString(emitter.render.materialAssetGuid) || !reader.ReadEnum(emitter.render.blendMode) ||
            !reader.ReadEnum(emitter.render.postProcessMode) || !reader.ReadEnum(emitter.render.shape) ||
            !reader.ReadEnum(emitter.render.billboardMode) || !reader.ReadEnum(emitter.render.depthMode) ||
            !reader.Read(emitter.render.softIntersectionDistance) || !reader.Read(emitter.render.depthBias) ||
            !reader.Read(emitter.render.flipbookColumns) || !reader.Read(emitter.render.flipbookRows) ||
            !reader.Read(emitter.render.flipbookFrameCount) || !reader.Read(emitter.render.flipbookFramesPerSecond) ||
            !reader.ReadBool(emitter.render.flipbookLoop) || !reader.ReadBool(emitter.render.flipbookRandomStart) ||
            !reader.Read(emitter.render.ribbonSegmentCount) || !reader.Read(emitter.render.ribbonWidth) ||
            !reader.Read(emitter.render.ringOuterRadius) || !reader.Read(emitter.render.ringInnerRadius) ||
            !reader.Read(emitter.render.ringSegmentCount) || !reader.Read(emitter.render.ringStartAngleRadians) ||
            !reader.Read(emitter.render.ringEndAngleRadians) || !reader.Read(emitter.render.uvMode) ||
            !reader.Read(emitter.render.uvTileLength) || !reader.Read(emitter.render.uvTilingU) ||
            !reader.Read(emitter.render.uvTilingV) || !reader.Read(emitter.render.uvRotationRadians) ||
            !reader.Read(emitter.render.uvSwapAxes) || !reader.Read(emitter.render.anchor.x) ||
            !reader.Read(emitter.render.anchor.y) || !reader.Read(emitter.render.anchor.z) ||
            !reader.Read(emitter.render.connectTargetMode) || !reader.Read(emitter.render.connectTargetLayerIndex) ||
            !reader.Read(emitter.render.connectLocalPosition.x) ||
            !reader.Read(emitter.render.connectLocalPosition.y) ||
            !reader.Read(emitter.render.connectLocalPosition.z) || !reader.Read(emitter.render.connectPivot) ||
            !reader.Read(emitter.render.connectLengthScale) || !reader.Read(emitter.render.connectStartOffset) ||
            !reader.Read(emitter.render.connectEndOffset) || !reader.Read(emitter.render.connectFlags) ||
            !reader.ReadString(emitter.render.meshAssetGuid) || !reader.Read(emitter.render.meshOutputFlags) ||
            !reader.Read(emitter.render.taperStart) || !reader.Read(emitter.render.taperEnd) ||
            !reader.Read(emitter.render.cylinderTopRadius) || !reader.Read(emitter.render.cylinderBottomRadius) ||
            !reader.Read(emitter.render.coneBaseRadius)) {
            return false;
        }
        if (!reader.Read(emitter.render.bendAmount) || !reader.Read(emitter.render.twistRadians)) {
            return false;
        }
        if (!reader.Read(emitter.render.torusMajorRadius) || !reader.Read(emitter.render.torusMinorRadius) ||
            !reader.Read(emitter.render.capsuleRadius)) {
            return false;
        }
        if (!reader.Read(emitter.render.billboardAxis)) {
            return false;
        }

        bool hasEffectMaterial = false;
        if (!reader.ReadBool(emitter.render.effectMaterialIncludesParticleColor) || !reader.ReadBool(hasEffectMaterial))
            return false;
        if (hasEffectMaterial) {
            std::string source, programIr, error;
            auto material = std::make_shared<MaterialAssetDescriptor>();
            if (!reader.ReadString(source) || !reader.ReadString(programIr) ||
                !MATERIAL_SERIALIZATION::ParseMaterialSourceJson(source, material->sourceData, error) ||
                !MATERIAL_SERIALIZATION::ParseMaterialCompileResult(programIr, material->compileResult, error) ||
                !material->compileResult.success || material->sourceData.domain != MaterialDomain::Particle)
                return false;
            emitter.render.effectMaterial = std::move(material);
            emitter.render.effectMaterialKey = program.programId + ":" + emitter.sourceEmitterId;
        }
        uint32_t specialOutputCount = 0u;
        if (!reader.Read(specialOutputCount) || specialOutputCount > kMaximumVfxProgramSpecialOutputs) {
            return false;
        }
        emitter.specialOutputs.reserve(specialOutputCount);
        for (uint32_t outputIndex = 0u; outputIndex < specialOutputCount; ++outputIndex) {
            VfxSpecialOutputData output{};
            if (!ReadSpecialOutput(reader, output))
                return false;
            emitter.specialOutputs.push_back(std::move(output));
        }

        uint32_t linkCount = 0u;
        if (!reader.Read(linkCount) || linkCount > kMaximumVfxProgramLinksPerEmitter ||
            linkCount > reader.Remaining() / kSerializedInputLinkBytes) {
            return false;
        }
        totalInputLinks += linkCount;
        if (totalInputLinks > kMaximumVfxProgramTableItems) {
            return false;
        }
        emitter.inputLinks.reserve(linkCount);
        for (uint32_t linkIndex = 0u; linkIndex < linkCount; ++linkIndex) {
            VfxProgramInputLink link{};
            if (!reader.Read(link.inputIndex) || !reader.ReadEnum(link.semantic) || !reader.Read(link.byteOffset) ||
                !reader.ReadEnum(link.storageType) || !reader.ReadEnum(link.inputType) || !reader.Read(link.scale) ||
                !reader.Read(link.bias)) {
                return false;
            }
            emitter.inputLinks.push_back(link);
        }
        if (!ReadFluidSettings(reader, emitter)) {
            return false;
        }
        program.emitters.push_back(std::move(emitter));
    }

    uint32_t dependencyCount = 0u;
    if (!reader.Read(dependencyCount) || dependencyCount > kMaximumVfxProgramDependencies ||
        dependencyCount > reader.Remaining() / kMinimumSerializedDependencyBytes) {
        return false;
    }
    program.dependencyGuids.reserve(dependencyCount);
    for (uint32_t index = 0u; index < dependencyCount; ++index) {
        std::string value{};
        if (!reader.ReadString(value))
            return false;
        program.dependencyGuids.push_back(AssetGuid{std::move(value)});
    }
    return true;
}

std::filesystem::path MakeStagedPath(const std::filesystem::path& finalPath) {
    static std::atomic_uint64_t sequence{0u};
    std::filesystem::path path = finalPath;
    path += ".hikari-hvfxc-" + std::to_string(sequence.fetch_add(1u, std::memory_order_relaxed));
    return path;
}
} // namespace

bool SerializeVfxProgram(const VfxProgram& program, std::string& outPayload, std::string& outError) {
    try {
        if (!ValidateVfxProgram(program, outError)) {
            return false;
        }
        BinaryWriter writer{};
        WriteProgram(writer, program);
        if (writer.Failed()) {
            outError = "VFX program payload exceeds the cooked limit";
            return false;
        }
        std::string payload = writer.Take();
        outPayload = std::move(payload);
        outError.clear();
        return true;
    } catch (const std::bad_alloc&) {
        outError = "VFX program serialization allocation failed";
        return false;
    } catch (const std::length_error&) {
        outError = "VFX program serialization size is invalid";
        return false;
    }
}

bool DeserializeVfxProgram(std::string_view payload, VfxProgram& outProgram, std::string& outError) {
    if (payload.empty() || payload.size() > kMaximumHvfxcPayloadBytes) {
        outError = "VFX program payload size is invalid";
        return false;
    }
    try {
        BinaryReader reader(payload);
        VfxProgram program{};
        if (!ReadProgram(reader, program) || !reader.IsAtEnd()) {
            outError = "invalid or truncated VFX program payload";
            return false;
        }
        if (!ValidateVfxProgram(program, outError)) {
            return false;
        }
        outProgram = std::move(program);
        outError.clear();
        return true;
    } catch (const std::bad_alloc&) {
        outError = "VFX program deserialization allocation failed";
        return false;
    } catch (const std::length_error&) {
        outError = "VFX program deserialization size is invalid";
        return false;
    }
}

bool WriteHvfxcFile(const std::filesystem::path& path, const VfxProgram& program, std::string& outError) {
    std::string payload{};
    if (!SerializeVfxProgram(program, payload, outError)) {
        return false;
    }
    if (payload.empty() || payload.size() > kMaximumHvfxcPayloadBytes) {
        outError = "VFX program payload is too large";
        return false;
    }

    std::error_code ec{};
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    if (ec) {
        outError = "failed to create HVFXC directory: " + ec.message();
        return false;
    }

    const HvfxcHeader header{
        kHvfxcMagic,        kHvfxcFormatVersion, sizeof(HvfxcHeader), static_cast<uint32_t>(payload.size()),
        program.sourceHash,
    };
    const std::filesystem::path stagedPath = MakeStagedPath(path);
    std::ofstream output(stagedPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        outError = "failed to open staged HVFXC file";
        return false;
    }
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    output.close();
    if (output.fail()) {
        std::filesystem::remove(stagedPath, ec);
        outError = "failed while writing HVFXC file";
        return false;
    }

    std::string commitMessage{};
    if (!IO::CommitStagedFile(stagedPath, path, commitMessage)) {
        std::filesystem::remove(stagedPath, ec);
        outError = "failed to commit HVFXC file: " + commitMessage;
        return false;
    }
    outError.clear();
    return true;
}

bool ReadHvfxcFile(const std::filesystem::path& path, VfxProgram& outProgram, std::string& outError) {
    std::error_code fileSizeError{};
    const uintmax_t fileSize = std::filesystem::file_size(path, fileSizeError);
    if (fileSizeError) {
        outError = "failed to query HVFXC file size: " + fileSizeError.message();
        return false;
    }
    if (fileSize < sizeof(HvfxcHeader) ||
        fileSize > sizeof(HvfxcHeader) + static_cast<uintmax_t>(kMaximumHvfxcPayloadBytes)) {
        outError = "HVFXC file size is outside the supported limit";
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        outError = "failed to open HVFXC file";
        return false;
    }
    HvfxcHeader header{};
    input.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!input || header.magic != kHvfxcMagic || header.formatVersion != kHvfxcFormatVersion ||
        header.headerSize != sizeof(HvfxcHeader) || header.payloadSize == 0u ||
        header.payloadSize > kMaximumHvfxcPayloadBytes) {
        outError = "invalid or unsupported HVFXC header";
        return false;
    }
    const uintmax_t expectedFileSize = sizeof(HvfxcHeader) + static_cast<uintmax_t>(header.payloadSize);
    if (fileSize != expectedFileSize) {
        outError = "HVFXC payload size does not match the file";
        return false;
    }

    std::string payload{};
    try {
        payload.resize(header.payloadSize);
    } catch (const std::bad_alloc&) {
        outError = "HVFXC payload allocation failed";
        return false;
    } catch (const std::length_error&) {
        outError = "HVFXC payload size is invalid";
        return false;
    }
    input.read(payload.data(), header.payloadSize);
    if (!input || input.peek() != std::char_traits<char>::eof()) {
        outError = "HVFXC payload size does not match the file";
        return false;
    }
    VfxProgram program{};
    if (!DeserializeVfxProgram(payload, program, outError)) {
        return false;
    }
    if (program.sourceHash != header.sourceHash) {
        outError = "HVFXC source hash does not match its payload";
        return false;
    }
    outProgram = std::move(program);
    outError.clear();
    return true;
}

} // namespace HIKARI::VFX
