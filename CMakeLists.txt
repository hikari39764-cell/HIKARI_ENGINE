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
include(HikariGameProject)
include(HikariDependencies)
include(HikariRuntimeDeployment)

set(
    HIKARI_GAME_PROJECT_MANIFEST
    "${HIKARI_SOURCE_ROOT}/GameProject/CMakeLists.txt"
    CACHE FILEPATH
    "CMake manifest for the GameProject module linked into the Host")
hikari_load_game_project("${HIKARI_GAME_PROJECT_MANIFEST}")
message(
    STATUS
    "HIKARI GameProject: ${HIKARI_SELECTED_GAME_PROJECT_NAME} "
    "(${HIKARI_SELECTED_GAME_PROJECT_ROOT})")

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

add_library(HIKARI_Runtime STATIC)
add_library(HIKARI::Runtime ALIAS HIKARI_Runtime)
target_sources(
    HIKARI_Runtime
    PRIVATE
        ${HIKARI_RUNTIME_SOURCES})
target_include_directories(
    HIKARI_Runtime
    PUBLIC
        "${HIKARI_SOURCE_ROOT}"
        "${HIKARI_SOURCE_ROOT}/include"
        "${HIKARI_SOURCE_ROOT}/HIKARI")
target_link_libraries(
    HIKARI_Runtime
    PUBLIC
        HIKARI::Spine
        HIKARI::Meshoptimizer
        HIKARI::Nlohmann
        HIKARI::DirectXTex
        HIKARI::Effekseer
        HIKARI::EffekseerRendererDX12
        HIKARI::LLGI
        HIKARI::Assimp
        HIKARI::Jolt
        HIKARI::CoACD
        dxguid
        xaudio2)
if(HIKARI_WITH_EDITOR_BUILD)
    target_link_libraries(
        HIKARI_Runtime
        PRIVATE
            HIKARI::WinPix)
endif()
if(HIKARI_STREAMLINE_ENABLED)
    target_link_libraries(
        HIKARI_Runtime
        PRIVATE
            HIKARI::Streamline)
endif()
hikari_configure_engine_target(HIKARI_Runtime)

source_group(
    TREE "${HIKARI_SOURCE_ROOT}"
    FILES
        ${HIKARI_RUNTIME_SOURCES})

add_library(HIKARI_GameProject STATIC)
add_library(HIKARI::GameProject ALIAS HIKARI_GameProject)
target_sources(
    HIKARI_GameProject
    PRIVATE
        ${HIKARI_SELECTED_GAME_PROJECT_SOURCES})
target_link_libraries(
    HIKARI_GameProject
    PUBLIC
        HIKARI::Runtime)
hikari_configure_engine_target(HIKARI_GameProject)
source_group(
    TREE "${HIKARI_SELECTED_GAME_PROJECT_ROOT}"
    FILES
        ${HIKARI_SELECTED_GAME_PROJECT_SOURCES})

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
            HIKARI::WinPix)
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
            HIKARI::Editor
            HIKARI::GameProject)
    hikari_configure_engine_target(HIKARI_EditorHost)
    hikari_configure_runtime_deployment(HIKARI_EditorHost)
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
    target_compile_definitions(
        HIKARI_ProjectLauncher
        PRIVATE
            HIKARI_ENGINE_SOURCE_ROOT="${HIKARI_SOURCE_ROOT}")
    hikari_configure_engine_target(HIKARI_ProjectLauncher)
    hikari_configure_runtime_deployment(
        HIKARI_ProjectLauncher
        NO_INSTALL
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
            HIKARI::Runtime
            HIKARI::GameProject)
    hikari_configure_engine_target(HIKARI_GameHost)
    hikari_configure_runtime_deployment(HIKARI_GameHost)
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
