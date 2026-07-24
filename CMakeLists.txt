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
include(HikariDependencies)
include(HikariRuntimeDeployment)

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

add_executable(HIKARI_UpdateTo3D WIN32)
target_sources(
    HIKARI_UpdateTo3D
    PRIVATE
        ${HIKARI_COMPAT_SOURCES})
if(HIKARI_WITH_EDITOR_BUILD)
    target_sources(
        HIKARI_UpdateTo3D
        PRIVATE
            ${HIKARI_EDITOR_SOURCES})
endif()

target_include_directories(
    HIKARI_UpdateTo3D
    PRIVATE
        "${HIKARI_SOURCE_ROOT}"
        "${HIKARI_SOURCE_ROOT}/include"
        "${HIKARI_SOURCE_ROOT}/HIKARI")

target_link_libraries(
    HIKARI_UpdateTo3D
    PRIVATE
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
        HIKARI_UpdateTo3D
        PRIVATE
            HIKARI::ImGui
            HIKARI::ImGuizmo
            HIKARI::WinPix)
endif()
if(HIKARI_STREAMLINE_ENABLED)
    target_link_libraries(
        HIKARI_UpdateTo3D
        PRIVATE
            HIKARI::Streamline)
endif()

hikari_configure_engine_target(HIKARI_UpdateTo3D)
hikari_configure_runtime_deployment(HIKARI_UpdateTo3D)

source_group(
    TREE "${HIKARI_SOURCE_ROOT}"
    FILES
        ${HIKARI_COMPAT_SOURCES}
        ${HIKARI_EDITOR_SOURCES})

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT HIKARI_UpdateTo3D)

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
