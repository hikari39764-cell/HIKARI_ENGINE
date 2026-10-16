#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/Text/HIKARI_EnumNames.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <string>

#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI::ASSETS::SEMANTICS {
std::filesystem::path MakeSourceMetaPath(const std::filesystem::path& sourceMetaRoot,
                                         const std::filesystem::path& relativeSource) {
    if (sourceMetaRoot.empty() || relativeSource.empty()) {
        return {};
    }

    std::filesystem::path metaPath = sourceMetaRoot / relativeSource;
    metaPath += kSourceMetaSuffix;
    return metaPath.lexically_normal();
}

std::filesystem::path MakeArtifactManifestPath(const std::filesystem::path& libraryRoot, const std::string& guid) {
    if (libraryRoot.empty() || guid.empty()) {
        return {};
    }
    return (libraryRoot / "AssetDatabase" / "Artifacts" / (guid + ".artifact.json")).lexically_normal();
}

std::string MakeAssetFileStem(std::string value, std::string_view fallback) {
    // 英数字・-・_ と UTF-8 の多バイト文字（漢字・かな）は残し、文件名に使えない ASCII の記号と空白は _ にする。
    for (char& character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x80u && !std::isalnum(byte) && character != '-' && character != '_')
            character = '_';
    }
    value.erase(
        std::unique(value.begin(), value.end(), [](char left, char right) { return left == '_' && right == '_'; }),
        value.end());
    const bool named = std::any_of(value.begin(), value.end(), [](char character) { return character != '_'; });
    return named ? value : std::string(fallback);
}

namespace {

constexpr std::array<AssetImporterSemantics, static_cast<size_t>(AssetImporterKind::Count)> kImporterSemantics{{
    {
        AssetImporterKind::SkyCubemap,
        AssetType::Sky,
        "SkyCubemapImporter",
        3u,
        ".dds under an Assets/Skies or Assets/Sky directory",
    },
    {
        AssetImporterKind::Texture,
        AssetType::Texture,
        "TextureImporter",
        8u,
        ".png, .jpg, .jpeg, .tga, .bmp, .dds, .hdr",
    },
    {AssetImporterKind::Sprite, AssetType::Sprite, "SpriteAssetImporter", 1u, ".hsprite"},
    {AssetImporterKind::SpriteMaterial, AssetType::SpriteMaterial, "SpriteMaterialImporter", 1u, ".hspritematerial"},
    {
        AssetImporterKind::Model,
        AssetType::Model,
        "ModelImporter",
        43u,
        ".gltf, .obj, .fbx",
    },
    {
        AssetImporterKind::Scene,
        AssetType::Scene,
        "SceneAssetImporter",
        2u,
        ".hscene, .scene.json, or JSON under Assets/Scenes",
    },
    {AssetImporterKind::Prefab, AssetType::Prefab, "PrefabAssetImporter", 1u, ".hprefab"},
    {
        AssetImporterKind::Sequence,
        AssetType::Sequence,
        "SequenceAssetImporter",
        3u,
        ".hsequence",
    },
    {
        AssetImporterKind::AnimationStateMachine,
        AssetType::AnimationStateMachine,
        "AnimationStateMachineAssetImporter",
        2u,
        ".hanimsm",
    },
    {
        AssetImporterKind::Dialogue,
        AssetType::Dialogue,
        "DialogueAssetImporter",
        4u,
        ".hdialogue",
    },
    {
        AssetImporterKind::Material,
        AssetType::Material,
        "MaterialImporter",
        8u,
        ".material.json",
    },
    {
        AssetImporterKind::ModelMaterialBinding,
        AssetType::ModelMaterialBinding,
        "ModelMaterialBindingImporter",
        1u,
        ".hmodelmaterial",
    },
    {
        AssetImporterKind::VfxEffect,
        AssetType::VfxEffect,
        "VfxAssetImporter",
        // 現在の描画形状へ適用する絶対値入力を含む HVFXC v17。
        27u,
        ".hvfx",
    },
    {
        AssetImporterKind::Shader,
        AssetType::Shader,
        "ShaderImporter",
        1u,
        ".hshader under Assets/Shaders",
    },
    {AssetImporterKind::GameplayCamera, AssetType::GameplayCamera, "GameplayCameraAssetImporter", 1u, ".hcamera"},
    {AssetImporterKind::Font, AssetType::Font, "FontAssetImporter", 2u, ".ttf, .otf, .ttc"},
    {AssetImporterKind::TextStyle, AssetType::TextStyle, "TextStyleAssetImporter", 1u, ".htextstyle"},
    {AssetImporterKind::UiAnimation, AssetType::UiAnimation, "UiAnimationAssetImporter", 1u, ".huianim"},
    {AssetImporterKind::PostEffect, AssetType::PostEffect, "PostEffectAssetImporter", 2u, ".hposteffect"},
    {AssetImporterKind::PostProfile, AssetType::PostProfile, "PostProfileAssetImporter", 2u, ".hpostprofile"},
    {AssetImporterKind::Environment, AssetType::Environment, "EnvironmentAssetImporter", 1u, ".henvironment"},
    {AssetImporterKind::Weather, AssetType::Weather, "WeatherAssetImporter", 1u, ".hweather"},
    {AssetImporterKind::RenderStyle, AssetType::RenderStyle, "RenderStyleAssetImporter", 1u, ".hrenderstyle"},
    {AssetImporterKind::ColorLut, AssetType::ColorLut, "ColorLutAssetImporter", 1u, ".cube"},
    {AssetImporterKind::LightProfile, AssetType::LightProfile, "LightProfileAssetImporter", 1u, ".ies"},
}};

static_assert(
    [] {
        for (size_t i = 0; i < kImporterSemantics.size(); ++i)
            if (static_cast<size_t>(kImporterSemantics[i].kind) != i)
                return false;
        return true;
    }(),
    "Importer semantics must match AssetImporterKind ordering");

bool EndsWith(std::string_view text, std::string_view suffix) noexcept {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

bool IsSkyDirectoryPath(const std::filesystem::path& path) {
    for (const std::filesystem::path& part : path) {
        const std::string name = TEXT::ToLowerAsciiCopy(TEXT::PathToUtf8(part));
        if (name == "skies" || name == "sky") {
            return true;
        }
    }
    return false;
}

bool IsTextureExtension(std::string_view extension) noexcept {
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" ||
           extension == ".bmp" || extension == ".dds" || extension == ".hdr";
}

bool IsModelExtension(std::string_view extension) noexcept {
    return extension == ".gltf" || extension == ".obj" || extension == ".fbx";
}

bool IsScenePath(const std::string& lowerPath, const std::string& lowerFilename, std::string_view extension) noexcept {
    return extension == ".hscene" || EndsWith(lowerFilename, ".scene.json") ||
           (extension == ".json" && lowerPath.find("assets/scenes/") != std::string::npos);
}

bool IsShaderSourcePath(const std::string& lowerPath) noexcept { return lowerPath.rfind("assets/shaders/", 0u) == 0u; }

} // namespace

constexpr TEXT::EnumName<AssetType> kAssetTypeNames[] = {
    {AssetType::Model, "Model"},
    {AssetType::Scene, "Scene"},
    {AssetType::Prefab, "Prefab"},
    {AssetType::Sky, "Sky"},
    {AssetType::Texture, "Texture"},
    {AssetType::Sprite, "Sprite"},
    {AssetType::SpriteMaterial, "SpriteMaterial"},
    {AssetType::Material, "Material"},
    {AssetType::Animation, "Animation"},
    {AssetType::Particle, "Particle"},
    {AssetType::VfxEffect, "VfxEffect"},
    {AssetType::Font, "Font"},
    {AssetType::TextStyle, "TextStyle"},
    {AssetType::UiAnimation, "UiAnimation"},
    {AssetType::PostEffect, "PostEffect"},
    {AssetType::PostProfile, "PostProfile"},
    {AssetType::Environment, "Environment"},
    {AssetType::Weather, "Weather"},
    {AssetType::RenderStyle, "RenderStyle"},
    {AssetType::ColorLut, "ColorLut"},
    {AssetType::LightProfile, "LightProfile"},
    {AssetType::GameplayCamera, "GameplayCamera"},
    {AssetType::Sequence, "Sequence"},
    {AssetType::AnimationStateMachine, "AnimationStateMachine"},
    {AssetType::Dialogue, "Dialogue"},
    {AssetType::ModelMaterialBinding, "ModelMaterialBinding"},
    {AssetType::Shader, "Shader"},
    {AssetType::Unknown, "Unknown"},
};
std::string_view ToString(AssetType type) noexcept { return TEXT::EnumToName(kAssetTypeNames, type, "Unknown"); }

AssetType ParseAssetType(std::string_view text) noexcept {
    return TEXT::ParseEnum(kAssetTypeNames, text, AssetType::Unknown);
}

const AssetImporterSemantics& GetAssetImporterSemantics(AssetImporterKind kind) noexcept {
    const size_t index = static_cast<size_t>(kind);
    if (index < kImporterSemantics.size()) {
        return kImporterSemantics[index];
    }
    return kImporterSemantics[static_cast<size_t>(AssetImporterKind::Texture)];
}

const AssetImporterSemantics* FindDefaultAssetImporterSemantics(AssetType assetType) noexcept {
    for (const AssetImporterSemantics& semantics : kImporterSemantics) {
        if (semantics.assetType == assetType) {
            return &semantics;
        }
    }
    return nullptr;
}

const AssetImporterSemantics* ResolveAssetSourceSemantics(const std::filesystem::path& sourcePath) {
    const std::string extension = TEXT::ToLowerAsciiCopy(TEXT::PathToUtf8(sourcePath.extension()));
    const std::string filename = TEXT::ToLowerAsciiCopy(TEXT::PathToUtf8(sourcePath.filename()));
    const std::string genericPath = TEXT::ToLowerAsciiCopy(TEXT::PathToUtf8(sourcePath));

    AssetImporterKind kind = AssetImporterKind::Count;
    if (extension == ".dds" && IsSkyDirectoryPath(sourcePath)) {
        kind = AssetImporterKind::SkyCubemap;
    } else if (IsTextureExtension(extension)) {
        kind = AssetImporterKind::Texture;
    } else if (extension == ".hsprite") {
        kind = AssetImporterKind::Sprite;
    } else if (extension == ".hspritematerial") {
        kind = AssetImporterKind::SpriteMaterial;
    } else if (IsModelExtension(extension)) {
        kind = AssetImporterKind::Model;
    } else if (IsScenePath(genericPath, filename, extension)) {
        kind = AssetImporterKind::Scene;
    } else if (extension == ".hprefab") {
        kind = AssetImporterKind::Prefab;
    } else if (extension == ".ttf" || extension == ".otf" || extension == ".ttc") {
        kind = AssetImporterKind::Font;
    } else if (extension == ".huianim") {
        kind = AssetImporterKind::UiAnimation;
    } else if (extension == ".htextstyle") {
        kind = AssetImporterKind::TextStyle;
    } else if (extension == ".hcamera") {
        kind = AssetImporterKind::GameplayCamera;
    } else if (extension == ".hposteffect") {
        kind = AssetImporterKind::PostEffect;
    } else if (extension == ".hpostprofile") {
        kind = AssetImporterKind::PostProfile;
    } else if (extension == ".henvironment") {
        kind = AssetImporterKind::Environment;
    } else if (extension == ".hweather") {
        kind = AssetImporterKind::Weather;
    } else if (extension == ".hrenderstyle") {
        kind = AssetImporterKind::RenderStyle;
    } else if (extension == ".cube") {
        kind = AssetImporterKind::ColorLut;
    } else if (extension == ".ies") {
        kind = AssetImporterKind::LightProfile;
    } else if (extension == ".hsequence") {
        kind = AssetImporterKind::Sequence;
    } else if (extension == ".hanimsm") {
        kind = AssetImporterKind::AnimationStateMachine;
    } else if (extension == ".hdialogue") {
        kind = AssetImporterKind::Dialogue;
    } else if (EndsWith(genericPath, ".material.json")) {
        kind = AssetImporterKind::Material;
    } else if (extension == ".hmodelmaterial") {
        kind = AssetImporterKind::ModelMaterialBinding;
    } else if (extension == ".hvfx") {
        kind = AssetImporterKind::VfxEffect;
    } else if (extension == ".hshader" && IsShaderSourcePath(genericPath)) {
        kind = AssetImporterKind::Shader;
    }

    return kind == AssetImporterKind::Count ? nullptr : &GetAssetImporterSemantics(kind);
}

AssetType ClassifyAssetTypeFromPath(const std::filesystem::path& path) {
    if (const AssetImporterSemantics* semantics = ResolveAssetSourceSemantics(path)) {
        return semantics->assetType;
    }

    const std::string extension = TEXT::ToLowerAsciiCopy(TEXT::PathToUtf8(path.extension()));
    return extension == ".hmat" ? AssetType::Material : AssetType::Unknown;
}

bool IsAssetSourceForImporter(const std::filesystem::path& sourcePath, AssetImporterKind kind) {
    const AssetImporterSemantics* semantics = ResolveAssetSourceSemantics(sourcePath);
    return semantics != nullptr && semantics->kind == kind;
}

AssetMeta MakeBaseAssetMeta(const std::filesystem::path& sourcePath, const AssetGuid& guid,
                            AssetImporterKind importerKind) {
    const AssetImporterSemantics& semantics = GetAssetImporterSemantics(importerKind);

    AssetMeta meta{};
    meta.metaVersion = 1;
    meta.guid = guid;
    meta.type = semantics.assetType;
    meta.importerId = semantics.importerId;
    meta.importerVersion = semantics.importerVersion;
    meta.sourcePath = TEXT::PathToUtf8(sourcePath);
    meta.displayName = TEXT::PathToUtf8(sourcePath.stem());
    return meta;
}

bool IsAssetCompanionSource(const std::filesystem::path& path) {
    const std::string extension = TEXT::ToLowerAsciiCopy(TEXT::PathToUtf8(path.extension()));
    return extension == ".bin" || extension == ".mtl" || extension == ".hlsl" || extension == ".hlsli" ||
           extension == ".h";
}

bool IsSourceOnlyAssetDependencyRole(std::string_view role) noexcept {
    return role == "SourceBuffer" || role == "SourceCompanion" || role == "ShaderSource" || role == "ShaderInclude" ||
           role == "SpriteSourcePage";
}

} // namespace HIKARI::ASSETS::SEMANTICS
