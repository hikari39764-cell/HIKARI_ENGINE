cmake_minimum_required(VERSION 3.28)

project(HIKARI_Engine LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "HIKARI currently supports Windows only.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "The bootstrap build requires the Visual Studio 2022 MSVC toolchain.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "HIKARI requires an x64 generator.")
endif()

set(HIKARI_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
list(PREPEND CMAKE_MODULE_PATH "${HIKARI_SOURCE_ROOT}/cmake")

include(HikariOptions)
include(HikariCompiler)
include(HikariSources)
include(HikariRuntimeModules)
include(HikariDependencies)
include(HikariRuntimeDeployment)
include(HikariSdk)

unset(HIKARI_GAME_PROJECT_MANIFEST CACHE)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(configuration IN ITEMS Debug EditorRelease Shipping)
    string(TOUPPER "${configuration}" configuration_upper)
    set(
        "CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${configuration_upper}"
        "${CMAKE_BINARY_DIR}/lib")
    set(
        "CMAKE_LIBRARY_OUTPUT_DIRECTORY_${configuration_upper}"
        "${CMAKE_BINARY_DIR}/bin")
    set(
        "CMAKE_RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}"
        "${CMAKE_BINARY_DIR}/bin")
endforeach()

add_subdirectory(ThirdParty)

set(
    HIKARI_RUNTIME_PUBLIC_LINK_LIBRARIES
    HIKARI::Spine
    HIKARI::Meshoptimizer
    HIKARI::Nlohmann
    HIKARI::DirectXTex
    HIKARI::Jolt
    HIKARI::CoACD
    d3d12
    dxgi
    dxguid
    xaudio2)
set(HIKARI_RUNTIME_PRIVATE_LINK_LIBRARIES)
if(HIKARI_WITH_EDITOR_BUILD)
    list(APPEND
        HIKARI_RUNTIME_PRIVATE_LINK_LIBRARIES
        HIKARI::WinPix)
endif()
if(HIKARI_STREAMLINE_ENABLED)
    list(APPEND
        HIKARI_RUNTIME_PRIVATE_LINK_LIBRARIES
        HIKARI::Streamline)
endif()
set(
    HIKARI_RUNTIME_BUILD_DEPENDENCIES
    ${HIKARI_RUNTIME_PUBLIC_LINK_LIBRARIES}
    ${HIKARI_RUNTIME_PRIVATE_LINK_LIBRARIES})
hikari_define_runtime_modules(HIKARI_RUNTIME_MODULE_TARGETS)
target_link_libraries(
    hikari_runtime_foundation
    PRIVATE
        mfplat
        mfreadwrite
        mfuuid)
target_link_libraries(
    hikari_runtime_navigation
    PRIVATE
        HIKARI::RecastNavigation)
if(HIKARI_WITH_EDITOR_BUILD)
    target_link_libraries(
        hikari_runtime_assets
        PRIVATE
            HIKARI::Xatlas)
    target_link_libraries(
        hikari_runtime_foundation
        PRIVATE
            Comctl32)
endif()

add_library(HIKARI_Runtime INTERFACE)
add_library(HIKARI::Runtime ALIAS HIKARI_Runtime)
target_sources(
    HIKARI_Runtime
    INTERFACE
        FILE_SET hikari_game_project_sdk_headers
        TYPE HEADERS
        BASE_DIRS "${HIKARI_SOURCE_ROOT}/include"
        FILES ${HIKARI_GAME_PROJECT_SDK_HEADERS})
target_include_directories(
    HIKARI_Runtime
    INTERFACE
        "${HIKARI_SOURCE_ROOT}"
        "${HIKARI_SOURCE_ROOT}/include"
        "${HIKARI_SOURCE_ROOT}/HIKARI")
target_link_libraries(
    HIKARI_Runtime
    INTERFACE
        ${HIKARI_RUNTIME_MODULE_TARGETS}
        ${HIKARI_RUNTIME_PUBLIC_LINK_LIBRARIES}
        ${HIKARI_RUNTIME_PRIVATE_LINK_LIBRARIES})

add_executable(HIKARI_NavigationRuntimeContracts EXCLUDE_FROM_ALL)
target_sources(
    HIKARI_NavigationRuntimeContracts
    PRIVATE
        "${HIKARI_SOURCE_ROOT}/Tools/Tests/HIKARI_NavigationRuntimeContracts.cpp"
        "${HIKARI_SOURCE_ROOT}/HIKARI/Runtime/Host/HIKARI_RuntimeComponentToolingStubs.cpp")
target_link_libraries(
    HIKARI_NavigationRuntimeContracts
    PRIVATE
        HIKARI::Runtime)
hikari_configure_engine_target(HIKARI_NavigationRuntimeContracts)
hikari_configure_runtime_deployment(
    HIKARI_NavigationRuntimeContracts
    OUTPUT_NAME "HIKARI_NavigationRuntimeContracts"
    NO_INSTALL)

add_executable(HIKARI_AudioRuntimeContracts EXCLUDE_FROM_ALL)
target_sources(
    HIKARI_AudioRuntimeContracts
    PRIVATE
        "${HIKARI_SOURCE_ROOT}/Tools/Tests/HIKARI_AudioRuntimeContracts.cpp")
target_link_libraries(
    HIKARI_AudioRuntimeContracts
    PRIVATE
        HIKARI::Runtime)
hikari_configure_engine_target(HIKARI_AudioRuntimeContracts)
hikari_configure_runtime_deployment(
    HIKARI_AudioRuntimeContracts
    OUTPUT_NAME "HIKARI_AudioRuntimeContracts"
    NO_INSTALL)

add_executable(HIKARI_InputDeviceArbitrationContracts EXCLUDE_FROM_ALL)
target_sources(
    HIKARI_InputDeviceArbitrationContracts
    PRIVATE
        "${HIKARI_SOURCE_ROOT}/Tools/Tests/HIKARI_InputDeviceArbitrationContracts.cpp")
target_link_libraries(
    HIKARI_InputDeviceArbitrationContracts
    PRIVATE
        HIKARI::Runtime)
hikari_configure_engine_target(HIKARI_InputDeviceArbitrationContracts)
hikari_configure_runtime_deployment(
    HIKARI_InputDeviceArbitrationContracts
    OUTPUT_NAME "HIKARI_InputDeviceArbitrationContracts"
    NO_INSTALL)

add_executable(HIKARI_GroundTelegraphContracts EXCLUDE_FROM_ALL)
target_sources(
    HIKARI_GroundTelegraphContracts
    PRIVATE
        "${HIKARI_SOURCE_ROOT}/Tools/Tests/HIKARI_GroundTelegraphContracts.cpp")
target_link_libraries(
    HIKARI_GroundTelegraphContracts
    PRIVATE
        HIKARI::Runtime)
hikari_configure_engine_target(HIKARI_GroundTelegraphContracts)
hikari_configure_runtime_deployment(
    HIKARI_GroundTelegraphContracts
    OUTPUT_NAME "HIKARI_GroundTelegraphContracts"
    NO_INSTALL)

add_executable(HIKARI_ColliderCompositionContracts EXCLUDE_FROM_ALL)
target_sources(
    HIKARI_ColliderCompositionContracts
    PRIVATE
        "${HIKARI_SOURCE_ROOT}/Tools/Tests/HIKARI_ColliderCompositionContracts.cpp")
target_link_libraries(
    HIKARI_ColliderCompositionContracts
    PRIVATE
        HIKARI::Runtime)
hikari_configure_engine_target(HIKARI_ColliderCompositionContracts)
hikari_configure_runtime_deployment(
    HIKARI_ColliderCompositionContracts
    OUTPUT_NAME "HIKARI_ColliderCompositionContracts"
    NO_INSTALL)
set_property(
    SOURCE
        "${HIKARI_SOURCE_ROOT}/HIKARI/Scene/Features/HIKARI_RuntimeFeatureCatalog.cpp"
        "${HIKARI_SOURCE_ROOT}/HIKARI/Runtime/Project/HIKARI_GameProjectActionBridge.cpp"
        "${HIKARI_SOURCE_ROOT}/HIKARI/Runtime/Project/HIKARI_GameProjectNavigationBridge.cpp"
        "${HIKARI_SOURCE_ROOT}/HIKARI/Runtime/Project/HIKARI_GameProjectGameFlowBridge.cpp"
        "${HIKARI_SOURCE_ROOT}/HIKARI/Runtime/Project/HIKARI_GameProjectPresentationBridge.cpp"
        "${HIKARI_SOURCE_ROOT}/HIKARI/Runtime/Project/HIKARI_GameProjectUiBridge.cpp"
        "${HIKARI_SOURCE_ROOT}/HIKARI/Runtime/Project/HIKARI_GameProjectVfxBridge.cpp"
        "${HIKARI_SOURCE_ROOT}/HIKARI/Runtime/Project/HIKARI_GameProjectPropertyBridge.cpp"
        "${HIKARI_SOURCE_ROOT}/HIKARI/Runtime/Project/HIKARI_GameProjectBindingBridge.cpp"
        "${HIKARI_SOURCE_ROOT}/HIKARI/Runtime/Project/HIKARI_GameProjectQueryBridge.cpp"
        "${HIKARI_SOURCE_ROOT}/HIKARI/Runtime/Project/HIKARI_GameProjectRegistration.cpp"
        "${HIKARI_SOURCE_ROOT}/HIKARI/Runtime/Project/HIKARI_GameProjectTransformBridge.cpp"
        "${HIKARI_SOURCE_ROOT}/HIKARI/Runtime/Project/HIKARI_GameProjectWorldBridge.cpp"
    APPEND
    PROPERTY
        COMPILE_DEFINITIONS
            HIKARI_GAME_PROJECT_SDK_EXPORTS=1)

source_group(
    TREE "${HIKARI_SOURCE_ROOT}"
    FILES
        ${HIKARI_RUNTIME_SOURCES})

function(hikari_add_runtime_contract target_name source_path)
    add_executable("${target_name}" EXCLUDE_FROM_ALL)
    target_sources(
        "${target_name}"
        PRIVATE
            "${HIKARI_SOURCE_ROOT}/${source_path}")
    target_link_libraries(
        "${target_name}"
        PRIVATE
            HIKARI::Runtime)
    hikari_configure_engine_target("${target_name}")
    set_target_properties(
        "${target_name}"
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY
                "${CMAKE_BINARY_DIR}/contracts")
endfunction()

hikari_add_runtime_contract(
    HIKARI_GameComponentBindingLifecycleSmoke
    "Tools/Tests/HIKARI_GameComponentBindingLifecycleSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_WorldSdkSmoke
    "Tools/Tests/HIKARI_WorldSdkSmoke.cpp")

if(HIKARI_WITH_EDITOR_BUILD)
    add_library(HIKARI_Editor STATIC)
    add_library(HIKARI::Editor ALIAS HIKARI_Editor)
    target_sources(
        HIKARI_Editor
        PRIVATE
            ${HIKARI_EDITOR_SOURCES})
    target_link_libraries(
        HIKARI_Editor
        PUBLIC
            HIKARI::Runtime
            HIKARI::ImGui
            HIKARI::ImGuizmo
            HIKARI::Assimp
            HIKARI::WinPix
        PRIVATE
            Bcrypt)
    hikari_configure_engine_target(HIKARI_Editor)
    source_group(
        TREE "${HIKARI_SOURCE_ROOT}"
        FILES
            ${HIKARI_EDITOR_SOURCES})

    add_executable(HIKARI_EditorHost WIN32)
    target_sources(
        HIKARI_EditorHost
        PRIVATE
            ${HIKARI_EDITOR_HOST_SOURCES})
    target_link_libraries(
        HIKARI_EditorHost
        PRIVATE
            HIKARI::Editor)
    hikari_configure_engine_target(HIKARI_EditorHost)
    hikari_configure_runtime_deployment(HIKARI_EditorHost)
    set_target_properties(
        HIKARI_EditorHost
        PROPERTIES
            ENABLE_EXPORTS TRUE)
    hikari_configure_game_project_sdk(HIKARI_EditorHost)
    source_group(
        TREE "${HIKARI_SOURCE_ROOT}"
        FILES
            ${HIKARI_EDITOR_HOST_SOURCES})

    add_executable(HIKARI_ProjectLauncher WIN32)
    target_sources(
        HIKARI_ProjectLauncher
        PRIVATE
            ${HIKARI_PROJECT_LAUNCHER_HOST_SOURCES})
    target_link_libraries(
        HIKARI_ProjectLauncher
        PRIVATE
            HIKARI::Editor)
    add_dependencies(
        HIKARI_ProjectLauncher
        HIKARI_EditorHost)
    hikari_configure_engine_target(HIKARI_ProjectLauncher)
    hikari_configure_runtime_deployment(
        HIKARI_ProjectLauncher
        OUTPUT_NAME "HIKARI_ProjectLauncher")
    source_group(
        TREE "${HIKARI_SOURCE_ROOT}"
        FILES
            ${HIKARI_PROJECT_LAUNCHER_HOST_SOURCES})
    set(HIKARI_STARTUP_TARGET HIKARI_ProjectLauncher)
else()
    add_executable(HIKARI_GameHost WIN32)
    target_sources(
        HIKARI_GameHost
        PRIVATE
            ${HIKARI_GAME_HOST_SOURCES})
    target_link_libraries(
        HIKARI_GameHost
        PRIVATE
            HIKARI::Runtime)
    hikari_configure_engine_target(HIKARI_GameHost)
    hikari_configure_runtime_deployment(
        HIKARI_GameHost
        OUTPUT_NAME "${HIKARI_GAME_HOST_OUTPUT_NAME}")
    set_target_properties(
        HIKARI_GameHost
        PROPERTIES
            ENABLE_EXPORTS TRUE)
    hikari_configure_game_project_sdk(HIKARI_GameHost)
    source_group(
        TREE "${HIKARI_SOURCE_ROOT}"
        FILES
            ${HIKARI_GAME_HOST_SOURCES})
    set(HIKARI_STARTUP_TARGET HIKARI_GameHost)
endif()

set_property(
    DIRECTORY
    PROPERTY
        VS_STARTUP_PROJECT "${HIKARI_STARTUP_TARGET}")

find_program(
    HIKARI_POWERSHELL_EXECUTABLE
    NAMES powershell.exe
    REQUIRED)

add_custom_target(
    hikari_cmake_contracts
    COMMAND
        "${HIKARI_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -ExecutionPolicy Bypass
        -File "${HIKARI_SOURCE_ROOT}/Tools/Tests/Test-CMakeBuildContracts.ps1"
    WORKING_DIRECTORY "${HIKARI_SOURCE_ROOT}"
    VERBATIM)

add_custom_target(
    hikari_shaders_check
    COMMAND
        "${HIKARI_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -ExecutionPolicy Bypass
        -File "${HIKARI_SOURCE_ROOT}/Tools/ShaderCheck/Compile-HikariShaders.ps1"
    WORKING_DIRECTORY "${HIKARI_SOURCE_ROOT}"
    VERBATIM)
