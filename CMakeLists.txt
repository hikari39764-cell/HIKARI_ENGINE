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
        mfuuid
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
file(
    GLOB_RECURSE
    HIKARI_BUILTIN_SHADER_COOK_INPUTS
    CONFIGURE_DEPENDS
    "${HIKARI_SOURCE_ROOT}/HIKARI/Shaders/*.hlsl"
    "${HIKARI_SOURCE_ROOT}/HIKARI/Shaders/*.hlsli"
    "${HIKARI_SOURCE_ROOT}/HIKARI/Shaders/*.h")
set(
    HIKARI_BUILTIN_SHADER_ARTIFACT_ROOT
    "${CMAKE_BINARY_DIR}/bin/EngineContent/Shaders/Cooked")
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
    BYPRODUCTS
        "${HIKARI_BUILTIN_SHADER_ARTIFACT_ROOT}/BuiltinShaderArtifacts.json"
    DEPENDS
        HIKARI_BuiltinShaderCookTool
        "${HIKARI_BUILTIN_SHADER_CATALOG_PATH}"
        ${HIKARI_BUILTIN_SHADER_COOK_INPUTS}
    WORKING_DIRECTORY "${HIKARI_SOURCE_ROOT}"
    COMMENT "Cooking admitted built-in HSHADERC programs"
    VERBATIM)
set_target_properties(
    HIKARI_BuiltinShaderArtifacts
    PROPERTIES FOLDER "Engine/Tools")

function(hikari_add_runtime_contract target_name source_path)
    set(options NO_TOOLING_STUBS)
    cmake_parse_arguments(
        HIKARI_CONTRACT
        "${options}"
        ""
        ""
        ${ARGN})

    set(
        hikari_contract_sources
        "${HIKARI_SOURCE_ROOT}/${source_path}")
    if(NOT HIKARI_CONTRACT_NO_TOOLING_STUBS)
        list(APPEND
            hikari_contract_sources
            "${HIKARI_SOURCE_ROOT}/HIKARI/Runtime/Host/HIKARI_RuntimeComponentToolingStubs.cpp")
    endif()

    add_executable("${target_name}" EXCLUDE_FROM_ALL)
    target_sources(
        "${target_name}"
        PRIVATE
            ${hikari_contract_sources})
    target_link_libraries(
        "${target_name}"
        PRIVATE
            HIKARI::Runtime)
    hikari_configure_engine_target("${target_name}")

    set(hikari_contract_output_directory "${CMAKE_BINARY_DIR}/tests")
    set_target_properties(
        "${target_name}"
        PROPERTIES
            FOLDER "Tests/RuntimeContracts"
            EXCLUDE_FROM_DEFAULT_BUILD TRUE
            ARCHIVE_OUTPUT_DIRECTORY "${hikari_contract_output_directory}"
            RUNTIME_OUTPUT_DIRECTORY "${hikari_contract_output_directory}"
            PDB_OUTPUT_DIRECTORY "${hikari_contract_output_directory}"
            COMPILE_PDB_OUTPUT_DIRECTORY
                "${hikari_contract_output_directory}"
            VS_DEBUGGER_WORKING_DIRECTORY "${HIKARI_SOURCE_ROOT}")
    foreach(configuration IN ITEMS Debug EditorRelease Shipping)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(
            "${target_name}"
            PROPERTIES
                "EXCLUDE_FROM_DEFAULT_BUILD_${configuration_upper}" TRUE
                "ARCHIVE_OUTPUT_DIRECTORY_${configuration_upper}"
                    "${hikari_contract_output_directory}"
                "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}"
                    "${hikari_contract_output_directory}"
                "PDB_OUTPUT_DIRECTORY_${configuration_upper}"
                    "${hikari_contract_output_directory}"
                "COMPILE_PDB_OUTPUT_DIRECTORY_${configuration_upper}"
                    "${hikari_contract_output_directory}")
    endforeach()

    foreach(runtime_file IN LISTS HIKARI_RUNTIME_DEPENDENCIES)
        add_custom_command(
            TARGET "${target_name}"
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${runtime_file}"
                "$<TARGET_FILE_DIR:${target_name}>"
            VERBATIM)
    endforeach()
endfunction()

hikari_add_runtime_contract(
    HIKARI_NavigationRuntimeContracts
    "Tools/Tests/HIKARI_NavigationRuntimeContracts.cpp")
hikari_add_runtime_contract(
    HIKARI_AudioRuntimeContracts
    "Tools/Tests/HIKARI_AudioRuntimeContracts.cpp"
    NO_TOOLING_STUBS)
hikari_add_runtime_contract(
    HIKARI_InputDeviceArbitrationContracts
    "Tools/Tests/HIKARI_InputDeviceArbitrationContracts.cpp"
    NO_TOOLING_STUBS)
hikari_add_runtime_contract(
    HIKARI_GroundTelegraphContracts
    "Tools/Tests/HIKARI_GroundTelegraphContracts.cpp"
    NO_TOOLING_STUBS)
hikari_add_runtime_contract(
    HIKARI_ColliderCompositionContracts
    "Tools/Tests/HIKARI_ColliderCompositionContracts.cpp"
    NO_TOOLING_STUBS)
set_property(
    SOURCE
        ${HIKARI_GAME_PROJECT_SDK_EXPORT_SOURCES}
    APPEND
    PROPERTY
        COMPILE_DEFINITIONS
            HIKARI_GAME_PROJECT_SDK_EXPORTS=1)

source_group(
    TREE "${HIKARI_SOURCE_ROOT}"
    FILES
        ${HIKARI_RUNTIME_SOURCES})

hikari_add_runtime_contract(
    HIKARI_GameComponentBindingLifecycleSmoke
    "Tools/Tests/HIKARI_GameComponentBindingLifecycleSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_WorldSdkSmoke
    "Tools/Tests/HIKARI_WorldSdkSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_WorldCommandSchedulerSmoke
    "Tools/Tests/HIKARI_WorldCommandSchedulerSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_WorldBatchSmoke
    "Tools/Tests/HIKARI_WorldBatchSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_WorldComponentIndexSmoke
    "Tools/Tests/HIKARI_WorldComponentIndexSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_WorldQuerySdkSmoke
    "Tools/Tests/HIKARI_WorldQuerySdkSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_WorldMassStoreSmoke
    "Tools/Tests/HIKARI_WorldMassStoreSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_RuntimeFeatureTransactionSmoke
    "Tools/Tests/HIKARI_RuntimeFeatureTransactionSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_RenderGraphSmoke
    "Tools/Tests/HIKARI_RenderGraphSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_RenderGraphCapacitySmoke
    "Tools/Tests/HIKARI_RenderGraphCapacitySmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_SceneSurfaceSemanticSmoke
    "Tools/Tests/HIKARI_SceneSurfaceSemanticSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_RenderFrameSurfaceRegistrySmoke
    "Tools/Tests/HIKARI_RenderFrameSurfaceRegistrySmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_RenderFrameGraphPlanSmoke
    "Tools/Tests/HIKARI_RenderFrameGraphPlanSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_TemporalFrameStateSmoke
    "Tools/Tests/HIKARI_TemporalFrameStateSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_ShaderSourceManifestSmoke
    "Tools/Tests/HIKARI_ShaderSourceManifestSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_ShaderArtifactFormatSmoke
    "Tools/Tests/HIKARI_ShaderArtifactFormatSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_ShaderCookArgumentsSmoke
    "Tools/Tests/HIKARI_ShaderCookArgumentsSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_ShaderImporterContractSmoke
    "Tools/Tests/HIKARI_ShaderImporterContractSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_ShaderIncludePlanSmoke
    "Tools/Tests/HIKARI_ShaderIncludePlanSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_ShaderCookProducerSmoke
    "Tools/Tests/HIKARI_ShaderCookProducerSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_ShaderAssetResolverSmoke
    "Tools/Tests/HIKARI_ShaderAssetResolverSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_ShaderAssetRegistryAdmissionSmoke
    "Tools/Tests/HIKARI_ShaderAssetRegistryAdmissionSmoke.cpp")
hikari_add_runtime_contract(
    HIKARI_BuiltinShaderCatalogSmoke
    "Tools/Tests/HIKARI_BuiltinShaderCatalogSmoke.cpp")
enable_testing()
foreach(runtime_contract IN ITEMS
    HIKARI_GameComponentBindingLifecycleSmoke
    HIKARI_WorldSdkSmoke
    HIKARI_WorldCommandSchedulerSmoke
    HIKARI_WorldBatchSmoke
    HIKARI_WorldComponentIndexSmoke
    HIKARI_WorldQuerySdkSmoke
    HIKARI_WorldMassStoreSmoke
    HIKARI_RuntimeFeatureTransactionSmoke
    HIKARI_RenderGraphSmoke
    HIKARI_RenderGraphCapacitySmoke
    HIKARI_SceneSurfaceSemanticSmoke
    HIKARI_RenderFrameSurfaceRegistrySmoke
    HIKARI_RenderFrameGraphPlanSmoke
    HIKARI_TemporalFrameStateSmoke
    HIKARI_ShaderSourceManifestSmoke
    HIKARI_ShaderArtifactFormatSmoke
    HIKARI_ShaderCookArgumentsSmoke
    HIKARI_ShaderImporterContractSmoke
    HIKARI_ShaderIncludePlanSmoke
    HIKARI_ShaderCookProducerSmoke
    HIKARI_ShaderAssetResolverSmoke
    HIKARI_ShaderAssetRegistryAdmissionSmoke
    HIKARI_BuiltinShaderCatalogSmoke)
    add_test(
        NAME "${runtime_contract}"
        COMMAND "$<TARGET_FILE:${runtime_contract}>")
    set_tests_properties(
        "${runtime_contract}"
        PROPERTIES
            LABELS "RuntimeContracts"
            TIMEOUT 30)
endforeach()

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
