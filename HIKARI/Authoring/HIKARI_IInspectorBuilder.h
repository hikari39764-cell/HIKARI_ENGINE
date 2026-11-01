#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "Authoring/HIKARI_ComponentAuthoringTypes.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Core/Binding/HIKARI_BindingValue.h"

namespace HIKARI {

namespace INPUT {
enum class InputActionValueType;
}

enum class AssetType;
class AssetDatabase;
class AssetRegistry;
class IModelMaterialSlotProvider;
class WorldServiceRegistry;
struct ProceduralMeshSettings;
struct RuntimeObjectHandle;
struct SceneDocument;
struct SceneObjectId;

struct InspectorContext {
    AssetRegistry* assetRegistry = nullptr;
    AssetDatabase* assetDatabase = nullptr;
    const SceneDocument* sceneDocument = nullptr;
    const WorldServiceRegistry* worldServices = nullptr;
    const RuntimeObjectHandle* runtimeObject = nullptr;
    IModelMaterialSlotProvider* modelMaterialSlotProvider = nullptr;
    // プロシージャルオブジェクトの検査時、隣接する ProceduralMeshComponent の設定。
    const ProceduralMeshSettings* proceduralMeshSettings = nullptr;
};

class IInspectorBuilder {
public:
    virtual ~IInspectorBuilder() = default;

    virtual void SetContext(const InspectorContext& context) = 0;
    virtual const InspectorContext& GetContext() const noexcept = 0;

    virtual void Text(std::string_view text) = 0;
    virtual bool Section(std::string_view label, bool defaultOpen = false) {
        (void)defaultOpen;
        Text(label);
        return true;
    }
    virtual bool Button(std::string_view label) = 0;
    // 値を持つ項目は必ず安定 ID を持つ（表示名だけの項目は作れない）。ID はできるだけコンポーネント JSON のキー名に
    // 合わせ、component.describe / component.property.set（外部智能体を含む）はこの ID で読み書きする。
    virtual bool Bool(const AuthoringFieldDescriptor& field, bool& value) = 0;
    virtual bool Int(const AuthoringFieldDescriptor& field, int& value) = 0;
    virtual bool Float(const AuthoringFieldDescriptor& field, float& value) = 0;
    virtual bool FloatRange(const AuthoringFieldDescriptor& field, float& value, float minimum, float maximum,
                            float speed = 0.1f) = 0;
    virtual bool TypedValue(const AuthoringFieldDescriptor& field, BINDING::Value& value, bool hasRange, float minimum,
                            float maximum, float step) = 0;
    virtual bool Color3(const AuthoringFieldDescriptor& field, float& red, float& green, float& blue) = 0;
    virtual bool Vec2(const AuthoringFieldDescriptor& field, float& x, float& y) = 0;
    virtual bool Vec3(const AuthoringFieldDescriptor& field, float& x, float& y, float& z) = 0;
    virtual bool String(const AuthoringFieldDescriptor& field, std::string& value) = 0;
    // 複数行テキストも同じ String プロパティとして操作・スキーマに公開する。
    virtual bool MultilineString(const AuthoringFieldDescriptor& field, std::string& value) {
        return String(field, value);
    }
    virtual bool InputActionIdPicker(const AuthoringFieldDescriptor& field, INPUT::InputActionValueType expectedType,
                                     std::string& value) = 0;
    virtual bool RuntimeVariableIdPicker(const AuthoringFieldDescriptor& field, BINDING::ValueType expectedType,
                                         std::string& value) {
        return RuntimeVariableIdPicker(field, std::span<const BINDING::ValueType>(&expectedType, 1u), value);
    }
    virtual bool RuntimeVariableIdPicker(const AuthoringFieldDescriptor& field,
                                         std::span<const BINDING::ValueType> allowedTypes, std::string& value) = 0;
    virtual bool RuntimeEventIdPicker(const AuthoringFieldDescriptor& field, bool hasPayload,
                                      BINDING::ValueType payloadType, std::string& value) = 0;
    virtual bool RuntimeEventIdPicker(const AuthoringFieldDescriptor& field, AuthoringEventPayloadPolicy policy,
                                      BINDING::ValueType payloadType, std::string& value) = 0;
    virtual bool Choice(const AuthoringFieldDescriptor& field, int& selectedIndex,
                        std::span<const AuthoringChoice> choices) = 0;
    virtual bool AssetIdPicker(const AuthoringFieldDescriptor& field, AssetType assetType, std::string& value) = 0;
    virtual bool SceneIdPicker(const AuthoringFieldDescriptor& field, std::string& value) = 0;
    virtual bool SceneObjectIdPicker(const AuthoringFieldDescriptor& field, SceneObjectId& value) = 0;
};

// 列挙名表（別名なし）の値を選ぶ。選択肢 ID は表の名前（JSON に書く名前と同じ）、表示は labels があればそれ。
template <class E, std::size_t N>
bool EnumChoice(IInspectorBuilder& builder, const AuthoringFieldDescriptor& field, E& value,
                const TEXT::EnumName<E> (&names)[N], const std::array<std::string_view, N>* labels = nullptr) {
    std::array<AuthoringChoice, N> choices{};
    int selected = 0;
    for (std::size_t index = 0; index < N; ++index) {
        choices[index] = {names[index].name, labels != nullptr ? (*labels)[index] : names[index].name};
        if (names[index].value == value)
            selected = static_cast<int>(index);
    }
    if (!builder.Choice(field, selected, choices) || selected < 0 || static_cast<std::size_t>(selected) >= N)
        return false;
    value = names[static_cast<std::size_t>(selected)].value;
    return true;
}

} // namespace HIKARI
