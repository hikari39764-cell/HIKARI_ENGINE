#pragma once

#include <cstdint>
#include <string_view>

namespace HIKARI {

    inline constexpr uint32_t kMaterialAssetSourceVersion = 4u;
    inline constexpr uint32_t kMaterialProgramIrVersion = 2u;

    enum class MaterialAssetKind : uint8_t {
        Material,
        Function,
        Instance,
        Profile,
        Ramp,
    };

    enum class MaterialDomain : uint8_t {
        Surface,
    };

    enum class MaterialTemplateId : uint8_t {
        StandardPbr,
        GenericToon,
        Skin,
        Face,
        Hair,
        Eye,
        Cloth,
        HybridMetal,
        Custom,
    };

    enum class MaterialAlphaMode : uint8_t {
        Opaque,
        Mask,
        Blend,
    };

    enum class MaterialValueType : uint8_t {
        Bool,
        Float1,
        Float2,
        Float3,
        Float4,
        Surface,
        DiffuseResponse,
        SpecularResponse,
        OutlineResponse,
    };

    enum class MaterialResourceType : uint8_t {
        Texture2D,
        TextureCube,
        Ramp1D,
        Ramp2D,
    };

    enum class MaterialResourceSemantic : uint8_t {
        Custom,
        BaseColor,
        Normal,
        MetallicRoughness,
        Occlusion,
        Emissive,
        Specular,
        SpecularColor,
        Mask,
        Ramp,
        MatCap,
        FaceSdf,
        HairShadow,
    };

    enum class MaterialNodeKind : uint8_t {
        Constant,
        Parameter,
        TextureSample,
        Add,
        Multiply,
        Lerp,
        Clamp,
        Saturate,
        OneMinus,
        Power,
        Dot,
        NormalUnpack,
        PbrSurface,
        RampDiffuse,
        MultiRampDiffuse,
        SpecularBand,
        FresnelRim,
        MatCap,
        AnisotropicHair,
        FaceSdf,
        EyeLighting,
        SurfaceOutput,
        FunctionCall,
        SkinLighting,
        FabricSheen,
        HybridLighting,
        Outline,
        HairShadow,
    };

    enum class MaterialFeatureRequest : uint8_t {
        GeometryOutline,
        ScreenOutline,
        VisibilityOverlay,
        FaceHairShadow,
    };

    enum class MaterialRampDimension : uint8_t {
        OneD,
        TwoD,
    };

    enum class MaterialRampInterpolation : uint8_t {
        Constant,
        Linear,
        Smooth,
    };

    std::string_view ToString(MaterialAssetKind value) noexcept;
    std::string_view ToString(MaterialDomain value) noexcept;
    std::string_view ToString(MaterialTemplateId value) noexcept;
    std::string_view ToString(MaterialAlphaMode value) noexcept;
    std::string_view ToString(MaterialValueType value) noexcept;
    std::string_view ToString(MaterialResourceType value) noexcept;
    std::string_view ToString(MaterialResourceSemantic value) noexcept;
    std::string_view ToString(MaterialNodeKind value) noexcept;
    std::string_view ToString(MaterialFeatureRequest value) noexcept;
    std::string_view ToString(MaterialRampDimension value) noexcept;
    std::string_view ToString(MaterialRampInterpolation value) noexcept;

    bool TryParseMaterialAssetKind(std::string_view text, MaterialAssetKind& outValue) noexcept;
    bool TryParseMaterialDomain(std::string_view text, MaterialDomain& outValue) noexcept;
    bool TryParseMaterialTemplateId(std::string_view text, MaterialTemplateId& outValue) noexcept;
    bool TryParseMaterialAlphaMode(std::string_view text, MaterialAlphaMode& outValue) noexcept;
    bool TryParseMaterialValueType(std::string_view text, MaterialValueType& outValue) noexcept;
    bool TryParseMaterialResourceType(std::string_view text, MaterialResourceType& outValue) noexcept;
    bool TryParseMaterialResourceSemantic(std::string_view text, MaterialResourceSemantic& outValue) noexcept;
    bool TryParseMaterialNodeKind(std::string_view text, MaterialNodeKind& outValue) noexcept;
    bool TryParseMaterialFeatureRequest(std::string_view text, MaterialFeatureRequest& outValue) noexcept;
    bool TryParseMaterialRampDimension(std::string_view text, MaterialRampDimension& outValue) noexcept;
    bool TryParseMaterialRampInterpolation(std::string_view text, MaterialRampInterpolation& outValue) noexcept;

    bool IsMaterialNumericType(MaterialValueType type) noexcept;
    uint32_t GetMaterialValueComponentCount(MaterialValueType type) noexcept;

} // namespace HIKARI
