include_guard(GLOBAL)

include(CMakePackageConfigHelpers)

set(
    HIKARI_GAME_PROJECT_SDK_HEADERS
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Runtime/ComponentTypeId.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/ActionTypes.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/ActionViews.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/AudioTypes.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/AudioView.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/FrameContext.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/GameComponent.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/GameFlowTypes.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/GameFlowViews.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/GameProjectModule.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/GameProjectModuleApi.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/GameProjectSdk.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/GameSystem.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/MassSystem.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/Inspector.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/LightTypes.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/NavigationTypes.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/NavigationView.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/PresentationTypes.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/PresentationViews.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/PropertyArchive.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/RenderDispatch.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/RenderFrame.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/RenderPass.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/RenderShader.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/RuntimeExtension.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/RuntimeFeature.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/RuntimeFeatureRegistration.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/RuntimeRegistration.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/SceneCaptureTypes.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/SceneCaptureView.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/SceneFlowTrigger.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/SequenceTypes.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/SequenceView.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/RuntimeActivityTypes.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/TerrainView.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/UiTypes.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/UiView.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/ValueTypes.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/VfxTypes.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/VfxView.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/WorldMassTypes.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/WorldTypes.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/WorldQuery.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/WorldView.h")

set(
    HIKARI_SDK_GENERATED_DIRECTORY
    "${CMAKE_BINARY_DIR}/generated/HIKARISdk")
file(MAKE_DIRECTORY "${HIKARI_SDK_GENERATED_DIRECTORY}")

if(HIKARI_WITH_EDITOR_BUILD)
    get_filename_component(
        HIKARI_CMAKE_MODULE_DIRECTORY_NAME
        "${CMAKE_ROOT}"
        NAME)
    get_filename_component(
        _hikari_cmake_bin_directory
        "${CMAKE_COMMAND}"
        DIRECTORY)
    get_filename_component(
        HIKARI_CMAKE_DISTRIBUTION_ROOT
        "${_hikari_cmake_bin_directory}"
        DIRECTORY)
    set(
        HIKARI_CMAKE_LICENSE_FILE
        "${HIKARI_CMAKE_DISTRIBUTION_ROOT}/doc/cmake/LICENSE.rst")
    if(NOT EXISTS "${CMAKE_COMMAND}" OR
       NOT IS_DIRECTORY "${CMAKE_ROOT}" OR
       NOT EXISTS "${HIKARI_CMAKE_LICENSE_FILE}")
        message(
            FATAL_ERROR
            "The selected CMake installation cannot be redistributed by "
            "the HIKARI Editor SDK. Expected cmake.exe, CMAKE_ROOT, and "
            "doc/cmake/LICENSE.rst under ${HIKARI_CMAKE_DISTRIBUTION_ROOT}.")
    endif()

    file(SHA256 "${CMAKE_COMMAND}" HIKARI_CMAKE_EXECUTABLE_SHA256)
    set(
        HIKARI_CMAKE_TOOL_MANIFEST
        "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARI_CMakeTool.json")
    file(
        WRITE
        "${HIKARI_CMAKE_TOOL_MANIFEST}"
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"name\": \"CMake\",\n"
        "  \"version\": \"${CMAKE_VERSION}\",\n"
        "  \"executableSha256\": \"${HIKARI_CMAKE_EXECUTABLE_SHA256}\",\n"
        "  \"moduleDirectory\": \"${HIKARI_CMAKE_MODULE_DIRECTORY_NAME}\",\n"
        "  \"license\": \"licenses/CMake-LICENSE.rst\"\n"
        "}\n")
endif()

set(HIKARI_GAME_PROJECT_HOST_FILENAME "HIKARI_Engine.exe")
if(NOT HIKARI_WITH_EDITOR_BUILD)
    set(
        HIKARI_GAME_PROJECT_HOST_FILENAME
        "${HIKARI_GAME_HOST_OUTPUT_NAME}.exe")
endif()

configure_package_config_file(
    "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARIConfig.cmake.in"
    "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfig.cmake"
    INSTALL_DESTINATION "Sdk/lib/cmake/HIKARI"
    NO_SET_AND_CHECK_MACRO)
write_basic_package_version_file(
    "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfigVersion.cmake"
    VERSION 2.17.0
    COMPATIBILITY SameMajorVersion)

function(hikari_configure_game_project_sdk host_target)
    set(_sdk_root "$<TARGET_FILE_DIR:${host_target}>/Sdk")
    if(MSVC)
        target_link_options(
            "${host_target}"
            PRIVATE
                "/WHOLEARCHIVE:$<TARGET_FILE:hikari_runtime_game_project_sdk>")
    endif()
    set(
        _sdk_inputs
        ${HIKARI_GAME_PROJECT_SDK_HEADERS}
        "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARIConfig.cmake.in"
        "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARIGameProject.cmake"
        "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARI_GameProjectModuleAdapter.cpp"
        "${HIKARI_SOURCE_ROOT}/Tools/Build/Build-HikariProject.ps1"
        "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfig.cmake"
        "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfigVersion.cmake")
    if(HIKARI_WITH_EDITOR_BUILD)
        list(
            APPEND
            _sdk_inputs
            "${CMAKE_COMMAND}"
            "${HIKARI_CMAKE_LICENSE_FILE}"
            "${HIKARI_CMAKE_TOOL_MANIFEST}")
    endif()
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

    if(HIKARI_WITH_EDITOR_BUILD)
        add_custom_command(
            TARGET "${host_target}"
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}" -E make_directory
                "${_sdk_root}/Tools/CMake/bin"
                "${_sdk_root}/Tools/CMake/share/${HIKARI_CMAKE_MODULE_DIRECTORY_NAME}"
                "${_sdk_root}/Tools/CMake/licenses"
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_COMMAND}"
                "${_sdk_root}/Tools/CMake/bin/cmake.exe"
            COMMAND
                "${CMAKE_COMMAND}" -E copy_directory_if_different
                "${CMAKE_ROOT}"
                "${_sdk_root}/Tools/CMake/share/${HIKARI_CMAKE_MODULE_DIRECTORY_NAME}"
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${HIKARI_CMAKE_LICENSE_FILE}"
                "${_sdk_root}/Tools/CMake/licenses/CMake-LICENSE.rst"
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${HIKARI_CMAKE_TOOL_MANIFEST}"
                "${_sdk_root}/Tools/CMake/HIKARI_CMakeTool.json"
            VERBATIM)
    endif()

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
    if(HIKARI_WITH_EDITOR_BUILD)
        install(
            PROGRAMS "${CMAKE_COMMAND}"
            DESTINATION "Sdk/Tools/CMake/bin")
        install(
            DIRECTORY "${CMAKE_ROOT}/"
            DESTINATION
                "Sdk/Tools/CMake/share/${HIKARI_CMAKE_MODULE_DIRECTORY_NAME}")
        install(
            FILES "${HIKARI_CMAKE_LICENSE_FILE}"
            DESTINATION "Sdk/Tools/CMake/licenses"
            RENAME "CMake-LICENSE.rst")
        install(
            FILES "${HIKARI_CMAKE_TOOL_MANIFEST}"
            DESTINATION "Sdk/Tools/CMake")
    endif()
endfunction()
