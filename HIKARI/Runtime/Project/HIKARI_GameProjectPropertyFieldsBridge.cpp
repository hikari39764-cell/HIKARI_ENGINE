#include "HIKARI/Project/PropertyFields.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "RenderContracts/Asset/HIKARI_AssetTypes.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Input/Runtime/HIKARI_InputTypes.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectBindingConversion.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectFixedText.h"
#include "Scene/HIKARI_SceneObjectId.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

namespace HIKARI {
using namespace PROJECT::DETAIL;

namespace {

const nlohmann::json* FindProperty(const void* archive, std::string_view id) noexcept {
    const auto* object = static_cast<const nlohmann::json*>(archive);
    if (object == nullptr || !object->is_object()) {
        return nullptr;
    }
    const auto found = object->find(std::string(id));
    return found != object->end() ? &*found : nullptr;
}

nlohmann::json& PropertySlot(void* archive, std::string_view id) {
    auto& object = *static_cast<nlohmann::json*>(archive);
    if (!object.is_object()) {
        object = nlohmann::json::object();
    }
    return object[std::string(id)];
}

AuthoringFieldDescriptor MakeField(std::string_view id, std::string_view label, std::string_view description = {}) {
    return AuthoringFieldDescriptor{id, label.empty() ? id : label, description};
}

// 一つの項目宣言を、現在の用途（読込・保存・Inspector）だけ評価する。
template <class Load, class Save, class Inspect>
bool Visit(PropertyFieldsMode mode, std::string_view id, void* archive, void* inspector, Load&& load, Save&& save,
           Inspect&& inspect) noexcept {
    if (id.empty()) {
        return false;
    }
    try {
        switch (mode) {
        case PropertyFieldsMode::Load: return archive != nullptr && load(FindProperty(archive, id));
        case PropertyFieldsMode::Save:
            if (archive != nullptr) {
                save(PropertySlot(archive, id));
            }
            return false;
        case PropertyFieldsMode::Inspect:
            return inspector != nullptr && inspect(*static_cast<IInspectorBuilder*>(inspector));
        }
    } catch (...) {
    }
    return false;
}

template <class Value>
bool Assign(Value& target, const Value& next) {
    if (target == next) {
        return false;
    }
    target = next;
    return true;
}

bool ReadFiniteFloat(const nlohmann::json* value, float& outValue) {
    if (value == nullptr || !value->is_number()) {
        return false;
    }
    outValue = value->get<float>();
    return std::isfinite(outValue);
}

// 3 要素の有限値配列だけを受け付ける（Float3 と Color3 の保存形式）。
bool LoadFloat3(const nlohmann::json* stored, GameProjectFloat3& value) {
    if (stored == nullptr || !stored->is_array() || stored->size() != 3u) {
        return false;
    }
    GameProjectFloat3 next{};
    if (!ReadFiniteFloat(&(*stored)[0], next.x) || !ReadFiniteFloat(&(*stored)[1], next.y) ||
        !ReadFiniteFloat(&(*stored)[2], next.z)) {
        return false;
    }
    const bool changed = next.x != value.x || next.y != value.y || next.z != value.z;
    value = next;
    return changed;
}

AssetType ToAssetType(GameProjectAssetKind kind) noexcept {
    switch (kind) {
    case GameProjectAssetKind::Scene: return AssetType::Scene;
    case GameProjectAssetKind::Prefab: return AssetType::Prefab;
    case GameProjectAssetKind::Model: return AssetType::Model;
    case GameProjectAssetKind::ModelMaterialSet: return AssetType::ModelMaterialBinding;
    case GameProjectAssetKind::Material: return AssetType::Material;
    case GameProjectAssetKind::Texture: return AssetType::Texture;
    case GameProjectAssetKind::Sky: return AssetType::Sky;
    case GameProjectAssetKind::Sprite: return AssetType::Sprite;
    case GameProjectAssetKind::SpriteMaterial: return AssetType::SpriteMaterial;
    case GameProjectAssetKind::Animation: return AssetType::Animation;
    case GameProjectAssetKind::AnimationStateMachine: return AssetType::AnimationStateMachine;
    case GameProjectAssetKind::Sequence: return AssetType::Sequence;
    case GameProjectAssetKind::Dialogue: return AssetType::Dialogue;
    case GameProjectAssetKind::GameplayCamera: return AssetType::GameplayCamera;
    case GameProjectAssetKind::VfxEffect: return AssetType::VfxEffect;
    case GameProjectAssetKind::Shader: return AssetType::Shader;
    case GameProjectAssetKind::PostEffect: return AssetType::PostEffect;
    case GameProjectAssetKind::PostProfile: return AssetType::PostProfile;
    case GameProjectAssetKind::Font: return AssetType::Font;
    case GameProjectAssetKind::TextStyle: return AssetType::TextStyle;
    case GameProjectAssetKind::UiAnimation: return AssetType::UiAnimation;
    case GameProjectAssetKind::Environment: return AssetType::Environment;
    case GameProjectAssetKind::Weather: return AssetType::Weather;
    }
    return AssetType::Unknown;
}

bool ToInputValueType(GameProjectInputValueType type, INPUT::InputActionValueType& outType) noexcept {
    switch (type) {
    case GameProjectInputValueType::Button: outType = INPUT::InputActionValueType::Button; return true;
    case GameProjectInputValueType::Axis1D: outType = INPUT::InputActionValueType::Axis1D; return true;
    case GameProjectInputValueType::Axis2D: outType = INPUT::InputActionValueType::Axis2D; return true;
    }
    return false;
}

// Asset は $hikari.assetGuid 印付きオブジェクト、AssetId を含むその他の文字列項目は素の文字列で保存する。
const std::string* StoredText(const nlohmann::json* value, GameProjectTextFieldKind kind) noexcept {
    if (value == nullptr) {
        return nullptr;
    }
    if (kind == GameProjectTextFieldKind::Asset) {
        if (!value->is_object()) {
            return nullptr;
        }
        const auto marker = value->find(std::string(kGameProjectAssetGuidArchiveMarker));
        return marker != value->end() && marker->is_string() ? &marker->get_ref<const std::string&>() : nullptr;
    }
    return value->is_string() ? &value->get_ref<const std::string&>() : nullptr;
}

bool InspectText(IInspectorBuilder& builder, const GameProjectTextFieldDesc& desc, std::string& editable) {
    const AuthoringFieldDescriptor field = MakeField(desc.id, desc.label);
    switch (desc.kind) {
    case GameProjectTextFieldKind::Plain: return builder.String(field, editable);
    case GameProjectTextFieldKind::Asset:
    case GameProjectTextFieldKind::AssetId:
        return desc.assetKind == GameProjectAssetKind::Scene
                   ? builder.SceneIdPicker(field, editable)
                   : builder.AssetIdPicker(field, ToAssetType(desc.assetKind), editable);
    case GameProjectTextFieldKind::InputAction: {
        INPUT::InputActionValueType type{};
        return ToInputValueType(desc.inputType, type) && builder.InputActionIdPicker(field, type, editable);
    }
    case GameProjectTextFieldKind::Variable:
        return builder.RuntimeVariableIdPicker(field, ToRuntimeBindingType(desc.valueType), editable);
    case GameProjectTextFieldKind::Event:
        return builder.RuntimeEventIdPicker(field, desc.eventHasPayload, ToRuntimeBindingType(desc.valueType),
                                            editable);
    }
    return false;
}

// VFX とテクスチャの参照は GUID と元パスの組。パスだけの旧参照は GUID を補い、GUID 変更時はパスを追随させる。
template <class Reference>
bool InspectAssetReference(IInspectorBuilder& builder, std::string_view id, std::string_view label, AssetType type,
                           std::string_view description, Reference& reference) {
    const std::string previousGuid = ReadFixedString(reference.assetGuid);
    const std::string previousPath = ReadFixedString(reference.assetPath);
    const Reference original = reference;
    if ((reference.assetGuid[0] != '\0' && previousGuid.empty()) ||
        (reference.assetPath[0] != '\0' && previousPath.empty())) {
        return false;
    }
    const InspectorContext& context = builder.GetContext();
    if (context.assetDatabase != nullptr && reference.assetGuid[0] == '\0' && reference.assetPath[0] != '\0') {
        const AssetRecord* record = context.assetDatabase->FindByPath(std::filesystem::path(previousPath));
        if (record != nullptr && record->type == type &&
            !CopyStringToReference(record->guid.value, reference.assetGuid, sizeof(reference.assetGuid))) {
            reference = original;
            return false;
        }
    }
    std::string guid = ReadFixedString(reference.assetGuid);
    const bool pickerChanged = builder.AssetIdPicker(MakeField(id, label, description), type, guid);
    if (!CopyStringToReference(guid, reference.assetGuid, sizeof(reference.assetGuid))) {
        reference = original;
        return false;
    }
    if (context.assetDatabase != nullptr && reference.assetGuid[0] != '\0') {
        const AssetRecord* record = context.assetDatabase->FindByGuid(AssetGuid{guid});
        if (record != nullptr && record->type == type) {
            if (!CopyStringToReference(record->sourcePath.generic_string(), reference.assetPath,
                                       sizeof(reference.assetPath))) {
                reference = original;
                return false;
            }
        } else if (previousGuid != guid) {
            reference.assetPath[0] = '\0';
        }
    } else if (reference.assetGuid[0] == '\0') {
        reference.assetPath[0] = '\0';
    }
    return pickerChanged || previousGuid != ReadFixedString(reference.assetGuid) ||
           previousPath != ReadFixedString(reference.assetPath);
}

template <class Reference>
bool LoadAssetReference(const nlohmann::json* value, Reference& reference) {
    if (value == nullptr || !value->is_object()) {
        return false;
    }
    Reference next{};
    const auto guid = value->find("assetGuid");
    const auto path = value->find("assetPath");
    if ((guid != value->end() &&
         (!guid->is_string() ||
          !CopyStringToReference(guid->get_ref<const std::string&>(), next.assetGuid, sizeof(next.assetGuid)))) ||
        (path != value->end() &&
         (!path->is_string() ||
          !CopyStringToReference(path->get_ref<const std::string&>(), next.assetPath, sizeof(next.assetPath))))) {
        return false;
    }
    const bool changed = std::memcmp(&next, &reference, sizeof(Reference)) != 0;
    reference = next;
    return changed;
}

template <class Reference>
void SaveAssetReference(nlohmann::json& slot, const Reference& reference) {
    slot = {{"assetGuid", ReadFixedString(reference.assetGuid)}, {"assetPath", ReadFixedString(reference.assetPath)}};
}

// 選択肢の名前は保存値なので、空・重複を許さない。
bool IsValidChoiceTable(std::span<const std::string_view> names) noexcept {
    if (names.empty() || names.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    for (auto name = names.begin(); name != names.end(); ++name) {
        if (name->empty() || std::find(names.begin(), name, *name) != name) {
            return false;
        }
    }
    return true;
}

} // namespace

PropertyFields GameProjectSdkAccess::MakePropertyFields(PropertyFieldsMode mode, const nlohmann::json* loadArchive,
                                                        nlohmann::json* saveArchive,
                                                        IInspectorBuilder* inspector) noexcept {
    void* archive = mode == PropertyFieldsMode::Load ? const_cast<nlohmann::json*>(loadArchive) : saveArchive;
    return PropertyFields(mode, archive, inspector);
}

bool PropertyFields::Bool(std::string_view id, std::string_view label, bool& value) {
    return Visit(
        mode_, id, archive_, inspector_,
        [&](const nlohmann::json* stored) {
            return stored != nullptr && stored->is_boolean() && Assign(value, stored->get<bool>());
        },
        [&](nlohmann::json& slot) { slot = value; },
        [&](IInspectorBuilder& builder) { return builder.Bool(MakeField(id, label), value); });
}

bool PropertyFields::Int(std::string_view id, std::string_view label, int& value, int minimum, int maximum) {
    if (minimum > maximum) {
        return false;
    }
    return Visit(
        mode_, id, archive_, inspector_,
        [&](const nlohmann::json* stored) {
            if (stored == nullptr || !stored->is_number_integer()) {
                return false;
            }
            const int64_t next = std::clamp<int64_t>(stored->get<int64_t>(), minimum, maximum);
            return Assign(value, static_cast<int>(next));
        },
        [&](nlohmann::json& slot) { slot = value; },
        [&](IInspectorBuilder& builder) {
            const int previous = value;
            builder.Int(MakeField(id, label), value);
            value = std::clamp(value, minimum, maximum);
            return value != previous;
        });
}

bool PropertyFields::Float(std::string_view id, std::string_view label, float& value) {
    return Visit(
        mode_, id, archive_, inspector_,
        [&](const nlohmann::json* stored) {
            float next = 0.0f;
            return ReadFiniteFloat(stored, next) && Assign(value, next);
        },
        [&](nlohmann::json& slot) { slot = value; },
        [&](IInspectorBuilder& builder) { return builder.Float(MakeField(id, label), value); });
}

bool PropertyFields::Float(std::string_view id, std::string_view label, float& value, float minimum, float maximum,
                           float speed) {
    if (!(minimum <= maximum)) {
        return false;
    }
    return Visit(
        mode_, id, archive_, inspector_,
        [&](const nlohmann::json* stored) {
            float next = 0.0f;
            return ReadFiniteFloat(stored, next) && Assign(value, std::clamp(next, minimum, maximum));
        },
        [&](nlohmann::json& slot) { slot = value; },
        [&](IInspectorBuilder& builder) {
            return builder.FloatRange(MakeField(id, label), value, minimum, maximum, speed);
        });
}

bool PropertyFields::Float3(std::string_view id, std::string_view label, GameProjectFloat3& value) {
    return Visit(
        mode_, id, archive_, inspector_, [&](const nlohmann::json* stored) { return LoadFloat3(stored, value); },
        [&](nlohmann::json& slot) { slot = {value.x, value.y, value.z}; },
        [&](IInspectorBuilder& builder) {
            const std::string base(id);
            const std::string name(label.empty() ? id : label);
            bool changed = builder.Float(MakeField(base + ".x", name + " X"), value.x);
            changed = builder.Float(MakeField(base + ".y", name + " Y"), value.y) || changed;
            changed = builder.Float(MakeField(base + ".z", name + " Z"), value.z) || changed;
            return changed;
        });
}

bool PropertyFields::Color3(std::string_view id, std::string_view label, GameProjectFloat3& value) {
    return Visit(
        mode_, id, archive_, inspector_, [&](const nlohmann::json* stored) { return LoadFloat3(stored, value); },
        [&](nlohmann::json& slot) { slot = {value.x, value.y, value.z}; },
        [&](IInspectorBuilder& builder) { return builder.Color3(MakeField(id, label), value.x, value.y, value.z); });
}

bool PropertyFields::SceneObject(std::string_view id, std::string_view label, uint64_t& sceneObjectId) {
    return Visit(
        mode_, id, archive_, inspector_,
        [&](const nlohmann::json* stored) {
            uint64_t next = 0u;
            return stored != nullptr && SCENE::SERIALIZATION::ReadSceneObjectReference(*stored, next) &&
                   Assign(sceneObjectId, next);
        },
        [&](nlohmann::json& slot) { slot = SCENE::SERIALIZATION::WriteSceneObjectReference(sceneObjectId); },
        [&](IInspectorBuilder& builder) {
            HIKARI::SceneObjectId editable{sceneObjectId};
            const bool changed = builder.SceneObjectIdPicker(MakeField(id, label), editable);
            return Assign(sceneObjectId, editable.value) || changed;
        });
}

bool PropertyFields::Vfx(std::string_view id, std::string_view label, GameProjectVfxAssetReference& reference) {
    return Visit(
        mode_, id, archive_, inspector_,
        [&](const nlohmann::json* stored) { return LoadAssetReference(stored, reference); },
        [&](nlohmann::json& slot) { SaveAssetReference(slot, reference); },
        [&](IInspectorBuilder& builder) {
            return InspectAssetReference(builder, id, label, AssetType::VfxEffect,
                                         "VFX effect asset GUID with a synchronized source path fallback.", reference);
        });
}

bool PropertyFields::Texture(std::string_view id, std::string_view label, GameProjectUiTextureReference& reference) {
    return Visit(
        mode_, id, archive_, inspector_,
        [&](const nlohmann::json* stored) { return LoadAssetReference(stored, reference); },
        [&](nlohmann::json& slot) { SaveAssetReference(slot, reference); },
        [&](IInspectorBuilder& builder) {
            return InspectAssetReference(builder, id, label, AssetType::Texture,
                                         "Texture asset GUID with a synchronized source path fallback.", reference);
        });
}

bool PropertyFields::Choice(std::string_view id, std::string_view label, int& index,
                            std::span<const std::string_view> names) {
    if (!IsValidChoiceTable(names) ||
        (mode_ == PropertyFieldsMode::Save && (index < 0 || static_cast<size_t>(index) >= names.size()))) {
        return false;
    }
    return Visit(
        mode_, id, archive_, inspector_,
        [&](const nlohmann::json* stored) {
            if (stored == nullptr || !stored->is_string()) {
                return false;
            }
            const auto found = std::find(names.begin(), names.end(), stored->get_ref<const std::string&>());
            return found != names.end() && Assign(index, static_cast<int>(found - names.begin()));
        },
        [&](nlohmann::json& slot) { slot = std::string(names[static_cast<size_t>(index)]); },
        [&](IInspectorBuilder& builder) {
            std::vector<AuthoringChoice> choices{};
            choices.reserve(names.size());
            for (const std::string_view name : names) {
                choices.push_back({name, name});
            }
            int selected = index;
            return builder.Choice(MakeField(id, label), selected, choices) && Assign(index, selected);
        });
}

bool PropertyFields::TextField(const GameProjectTextFieldDesc& field, char* value, size_t capacity) {
    if (value == nullptr || capacity == 0u) {
        return false;
    }
    std::string current{};
    if (!ReadStringBuffer(value, capacity, current)) {
        return false;
    }
    return Visit(
        mode_, field.id, archive_, inspector_,
        [&](const nlohmann::json* stored) {
            const std::string* text = StoredText(stored, field.kind);
            if (text == nullptr) {
                return false;
            }
            // 収まらない値は途中で切らず空にする（不完全な ID を参照させない）。
            const std::string next = text->size() < capacity && text->find('\0') == std::string::npos ? *text : "";
            if (next == current) {
                return false;
            }
            return CopyStringToReference(next, value, capacity);
        },
        [&](nlohmann::json& slot) {
            if (field.kind == GameProjectTextFieldKind::Asset) {
                slot = {{std::string(kGameProjectAssetGuidArchiveMarker), current}};
            } else {
                slot = current;
            }
        },
        [&](IInspectorBuilder& builder) {
            std::string editable = current;
            const bool edited = InspectText(builder, field, editable);
            return edited && editable != current && CopyStringToReference(editable, value, capacity);
        });
}

size_t PropertyFields::StoredTextSize(std::string_view id) const noexcept {
    if (mode_ != PropertyFieldsMode::Load) {
        return 0u;
    }
    const nlohmann::json* stored = FindProperty(archive_, id);
    if (stored == nullptr) {
        return 0u;
    }
    if (stored->is_string()) {
        return stored->get_ref<const std::string&>().size();
    }
    const std::string* marker = StoredText(stored, GameProjectTextFieldKind::Asset);
    return marker != nullptr ? marker->size() : 0u;
}

bool PropertyFields::Section(std::string_view label, bool defaultOpen) {
    if (mode_ != PropertyFieldsMode::Inspect) {
        return true;
    }
    auto* builder = static_cast<IInspectorBuilder*>(inspector_);
    return builder != nullptr && builder->Section(label, defaultOpen);
}

void PropertyFields::Note(std::string_view text) {
    if (mode_ == PropertyFieldsMode::Inspect && inspector_ != nullptr) {
        static_cast<IInspectorBuilder*>(inspector_)->Text(text);
    }
}

bool PropertyFields::Button(std::string_view label) {
    return mode_ == PropertyFieldsMode::Inspect && inspector_ != nullptr &&
           static_cast<IInspectorBuilder*>(inspector_)->Button(label);
}

} // namespace HIKARI
