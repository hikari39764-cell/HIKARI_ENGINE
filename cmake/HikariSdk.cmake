include_guard(GLOBAL)

include(CMakePackageConfigHelpers)

set(
    HIKARI_GAME_PROJECT_SDK_HEADERS
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/FrameContext.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/GameComponent.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/GameProjectModule.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/GameProjectModuleApi.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/GameProjectSdk.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/GameSystem.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/Inspector.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/PropertyArchive.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/RuntimeExtension.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/RuntimeFeature.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/RuntimeFeatureRegistration.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/RuntimeRegistration.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/ValueTypes.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/WorldView.h")

set(
    HIKARI_SDK_GENERATED_DIRECTORY
    "${CMAKE_BINARY_DIR}/generated/HIKARISdk")
file(MAKE_DIRECTORY "${HIKARI_SDK_GENERATED_DIRECTORY}")

configure_package_config_file(
    "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARIConfig.cmake.in"
    "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfig.cmake"
    INSTALL_DESTINATION "Sdk/lib/cmake/HIKARI"
    NO_SET_AND_CHECK_MACRO)
write_basic_package_version_file(
    "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfigVersion.cmake"
    VERSION 1.2.0
    COMPATIBILITY SameMajorVersion)

function(hikari_configure_game_project_sdk host_target)
    set(_sdk_root "$<TARGET_FILE_DIR:${host_target}>/Sdk")
    set(
        _sdk_inputs
        ${HIKARI_GAME_PROJECT_SDK_HEADERS}
        "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARIConfig.cmake.in"
        "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARIGameProject.cmake"
        "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARI_GameProjectModuleAdapter.cpp"
        "${HIKARI_SOURCE_ROOT}/Tools/Build/Build-HikariProject.ps1"
        "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfig.cmake"
        "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfigVersion.cmake")
    set_property(
        TARGET "${host_target}"
        APPEND
        PROPERTY LINK_DEPENDS ${_sdk_inputs})

    add_custom_command(
        TARGET "${host_target}"
        POST_BUILD
        COMMAND
            "${CMAKE_COMMAND}" -E make_directory
            "${_sdk_root}/include"
            "${_sdk_root}/lib/cmake/HIKARI"
            "${_sdk_root}/src"
            "${_sdk_root}/Tools"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_directory_if_different
            "${HIKARI_SOURCE_ROOT}/include/HIKARI"
            "${_sdk_root}/include/HIKARI"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_LINKER_FILE:${host_target}>"
            "${_sdk_root}/lib/HIKARI_Engine.lib"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfig.cmake"
            "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfigVersion.cmake"
            "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARIGameProject.cmake"
            "${_sdk_root}/lib/cmake/HIKARI"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARI_GameProjectModuleAdapter.cpp"
            "${_sdk_root}/src/HIKARI_GameProjectModuleAdapter.cpp"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${HIKARI_SOURCE_ROOT}/Tools/Build/Build-HikariProject.ps1"
            "${_sdk_root}/Tools/Build-HikariProject.ps1"
        VERBATIM)

    install(
        DIRECTORY "${HIKARI_SOURCE_ROOT}/include/HIKARI"
        DESTINATION "Sdk/include")
    install(
        FILES
            "$<TARGET_LINKER_FILE:${host_target}>"
        DESTINATION "Sdk/lib"
        RENAME "HIKARI_Engine.lib")
    install(
        FILES
            "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfig.cmake"
            "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfigVersion.cmake"
            "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARIGameProject.cmake"
        DESTINATION "Sdk/lib/cmake/HIKARI")
    install(
        FILES
            "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARI_GameProjectModuleAdapter.cpp"
        DESTINATION "Sdk/src")
    install(
        FILES
            "${HIKARI_SOURCE_ROOT}/Tools/Build/Build-HikariProject.ps1"
        DESTINATION "Sdk/Tools")
endfunction()
