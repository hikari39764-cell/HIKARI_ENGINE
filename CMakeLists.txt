cmake_minimum_required(VERSION 3.28)

project(HIKARI_Engine LANGUAGES CXX RC)

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
set(HIKARI_RUNTIME_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(HIKARI_INSTALL_RUNTIME_PREFIX "")
if(HIKARI_WITH_EDITOR_BUILD)
    set(HIKARI_RUNTIME_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Runtime")
    set(HIKARI_INSTALL_RUNTIME_PREFIX "Runtime/")
endif()
include(HikariCompiler)
include(HikariSources)
include(HikariRuntimeModules)
include(HikariDependencies)
include(HikariRuntimeDeployment)
include(HikariSdk)

configure_file(
    "${HIKARI_SOURCE_ROOT}/cmake/Templates/HIKARI_StartupResources.rc.in"
    "${CMAKE_BINARY_DIR}/generated/HIKARI_StartupResources.rc" @ONLY)
set_source_files_properties("${CMAKE_BINARY_DIR}/generated/HIKARI_StartupResources.rc"
    PROPERTIES OBJECT_DEPENDS "${HIKARI_SOURCE_ROOT}/HIKARI/Platform/Resources/HIKARI_StartupLogo.png")
add_library(HIKARI_StartupResources OBJECT "${CMAKE_BINARY_DIR}/generated/HIKARI_StartupResources.rc")

file(
    SHA256
    "${HIKARI_DXC_RUNTIME_ROOT}/dxcompiler.dll"
    HIKARI_DXC_COOK_COMPILER_SHA256)
file(
    SHA256
    "${HIKARI_DXC_RUNTIME_ROOT}/dxil.dll"
    HIKARI_DXC_COOK_VALIDATOR_SHA256)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/Gfx/Internal")
configure_file(
    "${HIKARI_SOURCE_ROOT}/cmake/Templates/HIKARI_DxcCookToolchainConfig.h.in"
    "${CMAKE_BINARY_DIR}/generated/Gfx/Internal/HIKARI_DxcCookToolchainConfig.h"
    @ONLY)

unset(HIKARI_GAME_PROJECT_MANIFEST CACHE)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${HIKARI_RUNTIME_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${HIKARI_RUNTIME_DIRECTORY}")
foreach(configuration IN ITEMS Debug EditorRelease Shipping)
    string(TOUPPER "${configuration}" configuration_upper)
    set(
        "CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${configuration_upper}"
        "${CMAKE_BINARY_DIR}/lib")
    set(
        "CMAKE_LIBRARY_OUTPUT_DIRECTORY_${configuration_upper}"
        "${HIKARI_RUNTIME_DIRECTORY}")
    set(
        "CMAKE_RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}"
        "${HIKARI_RUNTIME_DIRECTORY}")
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
    imm32
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
# DXGI/D3D12 の生成関数は native import より先に Streamline へ解決する。
set(
    HIKARI_RUNTIME_BUILD_DEPENDENCIES
    ${HIKARI_RUNTIME_PRIVATE_LINK_LIBRARIES}
    ${HIKARI_RUNTIME_PUBLIC_LINK_LIBRARIES})
hikari_define_runtime_modules(HIKARI_RUNTIME_MODULE_TARGETS)
target_link_libraries(
    hikari_runtime_foundation
    PRIVATE
        mfuuid
        windowscodecs
        Bcrypt)
target_link_libraries(
    hikari_runtime_navigation
    PRIVATE
        HIKARI::RecastNavigation)
target_link_libraries(
    hikari_runtime_gfx
    PRIVATE
        Version)
target_include_directories(
    hikari_runtime_gfx
    PRIVATE
        "${CMAKE_BINARY_DIR}/generated")
if(HIKARI_WITH_EDITOR_BUILD)
    target_link_libraries(
        hikari_runtime_asset_pipeline
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
        ${HIKARI_RUNTIME_PRIVATE_LINK_LIBRARIES}
        ${HIKARI_RUNTIME_PUBLIC_LINK_LIBRARIES})

add_executable(
    HIKARI_BuiltinShaderCookTool
    EXCLUDE_FROM_ALL
    "Tools/ShaderCook/HIKARI_BuiltinShaderCookMain.cpp")
target_link_libraries(
    HIKARI_BuiltinShaderCookTool
    PRIVATE
        HIKARI::Runtime)
hikari_configure_engine_target(HIKARI_BuiltinShaderCookTool)
set_target_properties(
    HIKARI_BuiltinShaderCookTool
    PROPERTIES
        FOLDER "Engine/Tools"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
foreach(configuration IN ITEMS Debug EditorRelease Shipping)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(
        HIKARI_BuiltinShaderCookTool
        PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}"
                "${CMAKE_BINARY_DIR}/tools")
endforeach()
foreach(runtime_file IN LISTS HIKARI_RUNTIME_DEPENDENCIES)
    add_custom_command(
        TARGET HIKARI_BuiltinShaderCookTool
        POST_BUILD
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${runtime_file}"
            "$<TARGET_FILE_DIR:HIKARI_BuiltinShaderCookTool>"
        VERBATIM)
endforeach()
add_executable(
    HIKARI_EngineContentTextureCookTool
    EXCLUDE_FROM_ALL
    "Tools/EngineContent/HIKARI_EngineContentTextureCookMain.cpp")
target_link_libraries(
    HIKARI_EngineContentTextureCookTool
    PRIVATE
        HIKARI::Runtime)
hikari_configure_engine_target(HIKARI_EngineContentTextureCookTool)
set_target_properties(
    HIKARI_EngineContentTextureCookTool
    PROPERTIES
        FOLDER "Engine/Tools"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
foreach(configuration IN ITEMS Debug EditorRelease Shipping)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(
        HIKARI_EngineContentTextureCookTool
        PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}"
                "${CMAKE_BINARY_DIR}/tools")
endforeach()
foreach(runtime_file IN LISTS HIKARI_RUNTIME_DEPENDENCIES)
    add_custom_command(
        TARGET HIKARI_EngineContentTextureCookTool
        POST_BUILD
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${runtime_file}"
            "$<TARGET_FILE_DIR:HIKARI_EngineContentTextureCookTool>"
        VERBATIM)
endforeach()

set(
    HIKARI_BUILTIN_SHADER_ARTIFACT_ROOT
    "${HIKARI_RUNTIME_DIRECTORY}/EngineContent/Shaders/Cooked")
set(
    HIKARI_BUILTIN_SHADER_CATALOG_PATH
    "${HIKARI_SOURCE_ROOT}/HIKARI/Shaders/BuiltinShaderCatalog.json")
add_custom_target(
    HIKARI_BuiltinShaderArtifacts
    COMMAND
        "$<TARGET_FILE:HIKARI_BuiltinShaderCookTool>"
        --catalog
        "${HIKARI_BUILTIN_SHADER_CATALOG_PATH}"
        --shader-root
        "${HIKARI_SOURCE_ROOT}/HIKARI/Shaders"
        --output-root
        "${HIKARI_BUILTIN_SHADER_ARTIFACT_ROOT}"
    COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different
        "${HIKARI_BUILTIN_SHADER_CATALOG_PATH}"
        "${HIKARI_BUILTIN_SHADER_ARTIFACT_ROOT}/../BuiltinShaderCatalog.json"
    BYPRODUCTS
        "${HIKARI_BUILTIN_SHADER_ARTIFACT_ROOT}/BuiltinShaderArtifacts.json"
        "${HIKARI_BUILTIN_SHADER_ARTIFACT_ROOT}/../BuiltinShaderCatalog.json"
    DEPENDS
        HIKARI_BuiltinShaderCookTool
        "${HIKARI_BUILTIN_SHADER_CATALOG_PATH}"
    WORKING_DIRECTORY "${HIKARI_SOURCE_ROOT}"
    COMMENT "Cooking admitted built-in HSHADERC programs"
    VERBATIM)
set_target_properties(
    HIKARI_BuiltinShaderArtifacts
    PROPERTIES FOLDER "Engine/Tools")

set_property(SOURCE ${HIKARI_GAME_PROJECT_SDK_EXPORT_SOURCES}
    APPEND PROPERTY COMPILE_DEFINITIONS HIKARI_GAME_PROJECT_SDK_EXPORTS=1)
source_group(TREE "${HIKARI_SOURCE_ROOT}" FILES ${HIKARI_RUNTIME_SOURCES})

if(HIKARI_WITH_EDITOR_BUILD)
    add_library(HIKARI_Editor STATIC ${HIKARI_EDITOR_SOURCES})
    add_library(HIKARI::Editor ALIAS HIKARI_Editor)
    target_link_libraries(HIKARI_Editor PUBLIC
        HIKARI::Runtime HIKARI::ImGui HIKARI::ImGuizmo HIKARI::Assimp HIKARI::WinPix
        PRIVATE Bcrypt)
    hikari_configure_engine_target(HIKARI_Editor)
    # 編集 UI はゲームの毎フレーム経路ではないため、全体最適化の中間物と再リンク時間を持たない。
    set_target_properties(HIKARI_Editor PROPERTIES HIKARI_WHOLE_PROGRAM_OPTIMIZATION OFF)
    # エンジン向けに改変した ImGui backend もサードパーティコードなので、エンジンの警告規則を適用しない。
    set(hikari_editor_third_party_sources ${HIKARI_EDITOR_SOURCES})
    list(FILTER hikari_editor_third_party_sources INCLUDE REGEX "/ThirdParty/")
    set_property(SOURCE ${hikari_editor_third_party_sources} APPEND PROPERTY COMPILE_OPTIONS /W0)
    source_group(
        TREE "${HIKARI_SOURCE_ROOT}"
        FILES
            ${HIKARI_EDITOR_SOURCES})

    add_executable(HIKARI_EditorHost WIN32)
    target_sources(HIKARI_EditorHost PRIVATE $<TARGET_OBJECTS:HIKARI_StartupResources>)
    target_sources(
        HIKARI_EditorHost
        PRIVATE
            ${HIKARI_EDITOR_HOST_SOURCES})
    target_link_libraries(
        HIKARI_EditorHost
        PRIVATE
            HIKARI::Editor)
    add_dependencies(
        HIKARI_EditorHost
        HIKARI_BuiltinShaderArtifacts)
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

    add_executable(HIKARI_ProjectLauncher WIN32 ${HIKARI_LAUNCHER_BOOTSTRAP_SOURCES})
    target_sources(HIKARI_ProjectLauncher PRIVATE $<TARGET_OBJECTS:HIKARI_StartupResources>)
    target_link_libraries(HIKARI_ProjectLauncher PRIVATE user32)
    hikari_configure_engine_target(HIKARI_ProjectLauncher)
    add_dependencies(HIKARI_ProjectLauncher HIKARI_EditorHost)
    set_target_properties(HIKARI_ProjectLauncher PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/symbols")
    foreach(configuration IN ITEMS Debug EditorRelease Shipping)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(HIKARI_ProjectLauncher PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/bin"
            "PDB_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/symbols")
    endforeach()
    install(TARGETS HIKARI_ProjectLauncher RUNTIME DESTINATION ".")
    set(HIKARI_STARTUP_TARGET HIKARI_ProjectLauncher)

    # hikari.exe：Claude Code・Codex などの智能体が編集器の本地桥を呼ぶ命令行と MCP。Launcher と同じ場所に置き、
    # Launcher と一緒に作る。連接文件の読み書きは編集器と同じ実装を使う。
    add_executable(HIKARI_AgentCli
        "${HIKARI_SOURCE_ROOT}/Tools/Agent/HIKARI_AgentCliMain.cpp"
        "${HIKARI_SOURCE_ROOT}/Tools/Agent/HIKARI_AgentClient.cpp"
        "${HIKARI_SOURCE_ROOT}/Tools/Agent/HIKARI_AgentHeadless.cpp"
        "${HIKARI_SOURCE_ROOT}/Tools/Agent/HIKARI_AgentMcpServer.cpp"
        "${HIKARI_SOURCE_ROOT}/Tools/Agent/HIKARI_AgentTools.cpp"
        "${HIKARI_SOURCE_ROOT}/HIKARI/Editor/Agent/HIKARI_AgentConnection.cpp")
    target_include_directories(HIKARI_AgentCli PRIVATE "${HIKARI_SOURCE_ROOT}/HIKARI")
    target_link_libraries(HIKARI_AgentCli PRIVATE HIKARI::Nlohmann ws2_32 advapi32)
    hikari_configure_engine_target(HIKARI_AgentCli)
    set_target_properties(HIKARI_AgentCli PROPERTIES
        OUTPUT_NAME "hikari"
        FOLDER "Engine/Tools"
        HIKARI_WHOLE_PROGRAM_OPTIMIZATION OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/symbols")
    foreach(configuration IN ITEMS Debug EditorRelease Shipping)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(HIKARI_AgentCli PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/bin"
            "PDB_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/symbols")
    endforeach()
    install(TARGETS HIKARI_AgentCli RUNTIME DESTINATION ".")
    add_dependencies(HIKARI_ProjectLauncher HIKARI_AgentCli)
else()
    add_executable(HIKARI_GameHost WIN32)
    target_sources(HIKARI_GameHost PRIVATE $<TARGET_OBJECTS:HIKARI_StartupResources>)
    target_sources(
        HIKARI_GameHost
        PRIVATE
            ${HIKARI_GAME_HOST_SOURCES})
    target_link_libraries(
        HIKARI_GameHost
        PRIVATE
            HIKARI::Runtime)
    add_dependencies(
        HIKARI_GameHost
        HIKARI_BuiltinShaderArtifacts)
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
    hikari_shaders_check
    COMMAND
        "${HIKARI_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -ExecutionPolicy Bypass
        -File "${HIKARI_SOURCE_ROOT}/Tools/ShaderCheck/Compile-HikariShaders.ps1"
    WORKING_DIRECTORY "${HIKARI_SOURCE_ROOT}"
    VERBATIM)

# Tests are explicit, excluded from product builds, and grouped by behavior.
include(cmake/HikariTests.cmake)
