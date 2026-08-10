#pragma once

#include <string>
#include <string_view>

#include "Assets/Vfx/HIKARI_VfxProgram.h"
#include "Authoring/HIKARI_ComponentAuthoringTypes.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingTypes.h"
#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponent.h"

namespace HIKARI {

    class AssetRegistry;

    namespace VfxPlayerAuthoringInternal {

        BINDING::ValueType ToBindingType(
            VFX::VfxValueType type) noexcept;

        const VFX::VfxProgram* ResolveProgram(
            const AssetRegistry* registry,
            const VfxPlayerSlot& slot) noexcept;

        const VFX::VfxProgramInput* FindProgramInput(
            const VFX::VfxProgram* program,
            std::string_view inputId) noexcept;

        bool IsFiniteVec3(const MATH::Vec3& value) noexcept;

        AuthoringCommandResult Error(
            std::string code,
            std::string message);

        AuthoringCommandResult Ok(
            std::string message,
            nlohmann::json data = nlohmann::json::object());

        VfxPlayerComponent* Cast(IComponent& component) noexcept;
        const VfxPlayerComponent* Cast(
            const IComponent& component) noexcept;

    } // namespace VfxPlayerAuthoringInternal

} // namespace HIKARI
