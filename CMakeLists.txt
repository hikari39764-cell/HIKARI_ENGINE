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
    hikari_configure_contract_output("${target_name}" "RuntimeContracts")

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

function(hikari_configure_contract_output target_name category)
    set(hikari_contract_output_directory "${CMAKE_BINARY_DIR}/tests")
    set_target_properties(
        "${target_name}"
        PROPERTIES
            FOLDER "Tests/${category}"
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

endfunction()

if(HIKARI_WITH_EDITOR_BUILD)
    hikari_add_runtime_contract(HIKARI_LightComponentIntegrationContracts
        "Tools/Tests/HIKARI_LightComponentIntegrationContracts.cpp" NO_TOOLING_STUBS)
    target_link_libraries(HIKARI_LightComponentIntegrationContracts PRIVATE HIKARI::Editor)
    add_test(NAME HIKARI_LightComponentIntegrationContracts COMMAND "$<TARGET_FILE:HIKARI_LightComponentIntegrationContracts>")
    set_tests_properties(HIKARI_LightComponentIntegrationContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)
    hikari_add_runtime_contract(HIKARI_RenderReferenceFixture
        "Tools/Tests/HIKARI_RenderReferenceFixture.cpp" NO_TOOLING_STUBS)
    target_link_libraries(HIKARI_RenderReferenceFixture PRIVATE HIKARI::Editor)
    target_sources(HIKARI_RenderReferenceFixture PRIVATE
        "Tools/Tests/RenderReference/HIKARI_RenderValidationProject.cpp"
        "Tools/Tests/RenderReference/HIKARI_RenderValidationScenes.cpp")
    target_compile_definitions(HIKARI_RenderReferenceFixture PRIVATE HIKARI_REFERENCE_PROJECT=1)
    target_include_directories(HIKARI_RenderReferenceFixture PRIVATE "${HIKARI_SOURCE_ROOT}/ThirdParty/DirectXTex/Common")
else()
    hikari_add_runtime_contract(HIKARI_RenderReferenceFixture
        "Tools/Tests/HIKARI_RenderReferenceFixture.cpp")
endif()

hikari_add_runtime_contract(
    HIKARI_AssetDatabaseImportContracts
    "Tools/Tests/HIKARI_AssetDatabaseImportContracts.cpp")
add_test(NAME HIKARI_AssetDatabaseImportContracts COMMAND "$<TARGET_FILE:HIKARI_AssetDatabaseImportContracts>")
set_tests_properties(HIKARI_AssetDatabaseImportContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 120)

hikari_add_runtime_contract(
    HIKARI_TextureUploadContracts
    "Tools/Tests/HIKARI_TextureUploadContracts.cpp")
add_test(NAME HIKARI_TextureUploadContracts COMMAND "$<TARGET_FILE:HIKARI_TextureUploadContracts>")
set_tests_properties(HIKARI_TextureUploadContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 120)

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
    HIKARI_SequencePlaybackReverseSmoke
    "Tools/Tests/HIKARI_SequencePlaybackReverseSmoke.cpp")
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
hikari_add_runtime_contract(
    HIKARI_WorldTextContracts
    "Tools/Tests/HIKARI_WorldTextContracts.cpp")
enable_testing()

add_executable(HIKARI_FrameDiagnosticsContracts EXCLUDE_FROM_ALL
    "Tools/Tests/HIKARI_FrameDiagnosticsContracts.cpp"
    "HIKARI/Core/HIKARI_TimeService.cpp"
    "HIKARI/Diagnostics/HIKARI_BenchmarkRun.cpp")
target_include_directories(HIKARI_FrameDiagnosticsContracts PRIVATE
    "${HIKARI_SOURCE_ROOT}/HIKARI" "${HIKARI_SOURCE_ROOT}/include"
    "${HIKARI_SOURCE_ROOT}/ThirdParty/nlohmann")
hikari_configure_engine_target(HIKARI_FrameDiagnosticsContracts)
hikari_configure_contract_output(HIKARI_FrameDiagnosticsContracts "RuntimeContracts")
add_test(NAME HIKARI_FrameDiagnosticsContracts COMMAND "$<TARGET_FILE:HIKARI_FrameDiagnosticsContracts>")
set_tests_properties(HIKARI_FrameDiagnosticsContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

add_executable(HIKARI_GpuProfilingContracts EXCLUDE_FROM_ALL
    "Tools/Tests/HIKARI_GpuProfilingContracts.cpp"
    "HIKARI/Gfx/HIKARI_GpuFrameProfiler.cpp"
    "HIKARI/Gfx/HIKARI_GpuPipelineStatsProfiler.cpp"
    "HIKARI/Gfx/HIKARI_DXCheck.cpp"
    "HIKARI/Core/HIKARI_Logger.cpp"
    "HIKARI/Diagnostics/HIKARI_DebugLogBuffer.cpp")
target_include_directories(HIKARI_GpuProfilingContracts PRIVATE
    "${HIKARI_SOURCE_ROOT}/HIKARI" "${HIKARI_SOURCE_ROOT}/ThirdParty/DirectXTex/Common")
target_link_libraries(HIKARI_GpuProfilingContracts PRIVATE d3d12 dxgi)
hikari_configure_engine_target(HIKARI_GpuProfilingContracts)
hikari_configure_contract_output(HIKARI_GpuProfilingContracts "RuntimeContracts")
add_test(NAME HIKARI_GpuProfilingContracts COMMAND "$<TARGET_FILE:HIKARI_GpuProfilingContracts>")
set_tests_properties(HIKARI_GpuProfilingContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

add_executable(HIKARI_LightmapSurfaceContracts EXCLUDE_FROM_ALL
    "Tools/Tests/HIKARI_LightmapSurfaceContracts.cpp"
    "HIKARI/Assets/Lighting/HIKARI_LightmapSurfaceAnalysis.cpp"
    "HIKARI/Assets/Lighting/HIKARI_LightmapUvGeneration.cpp"
    "HIKARI/Assets/Lighting/HIKARI_LightmapUvGenerationInput.cpp"
    "HIKARI/Core/Math/HIKARI_MathValidation.cpp"
    "HIKARI/Tools/Baking/Backends/Internal/HIKARI_DxrLightmapBakeUtilities.cpp"
    "HIKARI/Render3D/HIKARI_Math3D.cpp")
target_include_directories(HIKARI_LightmapSurfaceContracts PRIVATE "${HIKARI_SOURCE_ROOT}/HIKARI")
target_link_libraries(HIKARI_LightmapSurfaceContracts PRIVATE HIKARI::DirectXTex HIKARI::Nlohmann hikari_vendor_xatlas windowscodecs ole32)
hikari_configure_engine_target(HIKARI_LightmapSurfaceContracts)
hikari_configure_contract_output(HIKARI_LightmapSurfaceContracts "RuntimeContracts")
add_test(NAME HIKARI_LightmapSurfaceContracts COMMAND "$<TARGET_FILE:HIKARI_LightmapSurfaceContracts>")
set_tests_properties(HIKARI_LightmapSurfaceContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

add_executable(HIKARI_VolumetricGpuContracts EXCLUDE_FROM_ALL
    "Tools/Tests/HIKARI_VolumetricGpuContracts.cpp")
target_include_directories(HIKARI_VolumetricGpuContracts PRIVATE "${HIKARI_SOURCE_ROOT}/HIKARI")
target_link_libraries(HIKARI_VolumetricGpuContracts PRIVATE HIKARI::DirectXTex d3d12 dxgi d3dcompiler)
target_compile_definitions(HIKARI_VolumetricGpuContracts PRIVATE HIKARI_CONTRACT_SOURCE_ROOT="${HIKARI_SOURCE_ROOT}")
hikari_configure_engine_target(HIKARI_VolumetricGpuContracts)
hikari_configure_contract_output(HIKARI_VolumetricGpuContracts "RuntimeContracts")
add_test(NAME HIKARI_VolumetricGpuContracts COMMAND "$<TARGET_FILE:HIKARI_VolumetricGpuContracts>")
set_tests_properties(HIKARI_VolumetricGpuContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

hikari_add_runtime_contract(HIKARI_DirectionalShadowContracts "Tools/Tests/HIKARI_DirectionalShadowContracts.cpp")
add_test(NAME HIKARI_DirectionalShadowContracts COMMAND "$<TARGET_FILE:HIKARI_DirectionalShadowContracts>")
set_tests_properties(HIKARI_DirectionalShadowContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

add_executable(HIKARI_DynamicUploadGpuContracts EXCLUDE_FROM_ALL
    "Tools/Tests/HIKARI_DynamicUploadGpuContracts.cpp"
    "HIKARI/Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.cpp"
    "HIKARI/Render3D/HIKARI_Math3D.cpp"
    "HIKARI/Gfx/HIKARI_DynamicUploadBuffer.cpp"
    "HIKARI/Gfx/HIKARI_GpuDeferredReleaseQueue.cpp"
    "HIKARI/Gfx/HIKARI_GfxContext.cpp"
    "HIKARI/Gfx/HIKARI_DXCheck.cpp"
    "HIKARI/Diagnostics/HIKARI_DebugLogBuffer.cpp")
target_include_directories(HIKARI_DynamicUploadGpuContracts PRIVATE
    "${HIKARI_SOURCE_ROOT}/HIKARI" "${HIKARI_SOURCE_ROOT}/ThirdParty/DirectXTex/Common")
target_link_libraries(HIKARI_DynamicUploadGpuContracts PRIVATE d3d12 dxgi d3dcompiler)
hikari_configure_engine_target(HIKARI_DynamicUploadGpuContracts)
hikari_configure_contract_output(HIKARI_DynamicUploadGpuContracts "RuntimeContracts")
add_test(NAME HIKARI_DynamicUploadGpuContracts COMMAND "$<TARGET_FILE:HIKARI_DynamicUploadGpuContracts>")
set_tests_properties(HIKARI_DynamicUploadGpuContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

add_executable(HIKARI_ShadowDepthGpuContracts EXCLUDE_FROM_ALL
    "Tools/Tests/HIKARI_ShadowDepthGpuContracts.cpp"
    "HIKARI/Render3D/Shadow/Pipeline/HIKARI_ShadowDepthPipelines.cpp")
target_include_directories(HIKARI_ShadowDepthGpuContracts PRIVATE
    "${HIKARI_SOURCE_ROOT}/HIKARI" "${HIKARI_SOURCE_ROOT}/ThirdParty/DirectXTex/Common")
target_link_libraries(HIKARI_ShadowDepthGpuContracts PRIVATE d3d12 dxgi d3dcompiler)
hikari_configure_engine_target(HIKARI_ShadowDepthGpuContracts)
hikari_configure_contract_output(HIKARI_ShadowDepthGpuContracts "RuntimeContracts")
add_test(NAME HIKARI_ShadowDepthGpuContracts COMMAND "$<TARGET_FILE:HIKARI_ShadowDepthGpuContracts>")
set_tests_properties(HIKARI_ShadowDepthGpuContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

add_executable(HIKARI_RenderMathContracts EXCLUDE_FROM_ALL "Tools/Tests/HIKARI_RenderMathContracts.cpp")
target_sources(HIKARI_RenderMathContracts PRIVATE "HIKARI/Render3D/Lighting/HIKARI_SkyIrradiance.cpp")
target_include_directories(HIKARI_RenderMathContracts PRIVATE "${HIKARI_SOURCE_ROOT}/HIKARI" "${HIKARI_SOURCE_ROOT}/ThirdParty/DirectXTex/Common")
target_compile_definitions(HIKARI_RenderMathContracts PRIVATE HIKARI_CONTRACT_SOURCE_ROOT="${HIKARI_SOURCE_ROOT}")
target_link_libraries(HIKARI_RenderMathContracts PRIVATE d3d12 dxgi d3dcompiler)
hikari_configure_engine_target(HIKARI_RenderMathContracts)
hikari_configure_contract_output(HIKARI_RenderMathContracts "RuntimeContracts")
add_test(NAME HIKARI_RenderMathContracts COMMAND "$<TARGET_FILE:HIKARI_RenderMathContracts>")
set_tests_properties(HIKARI_RenderMathContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

add_executable(HIKARI_PbrRasterContracts EXCLUDE_FROM_ALL "Tools/Tests/HIKARI_PbrRasterContracts.cpp")
target_include_directories(HIKARI_PbrRasterContracts PRIVATE "${HIKARI_SOURCE_ROOT}/ThirdParty/DirectXTex/Common")
target_compile_definitions(HIKARI_PbrRasterContracts PRIVATE HIKARI_CONTRACT_SOURCE_ROOT="${HIKARI_SOURCE_ROOT}")
target_link_libraries(HIKARI_PbrRasterContracts PRIVATE HIKARI::DirectXTex d3d12 dxgi d3dcompiler)
hikari_configure_engine_target(HIKARI_PbrRasterContracts)
hikari_configure_contract_output(HIKARI_PbrRasterContracts "RuntimeContracts")
add_test(NAME HIKARI_PbrRasterContracts COMMAND "$<TARGET_FILE:HIKARI_PbrRasterContracts>")
set_tests_properties(HIKARI_PbrRasterContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

hikari_add_runtime_contract(HIKARI_MaterialSurfaceContracts "Tools/Tests/HIKARI_MaterialSurfaceContracts.cpp")
target_include_directories(HIKARI_MaterialSurfaceContracts PRIVATE "${HIKARI_SOURCE_ROOT}/ThirdParty/DirectXTex/Common")
target_compile_definitions(HIKARI_MaterialSurfaceContracts PRIVATE HIKARI_CONTRACT_SOURCE_ROOT="${HIKARI_SOURCE_ROOT}")
add_test(NAME HIKARI_MaterialSurfaceContracts COMMAND "$<TARGET_FILE:HIKARI_MaterialSurfaceContracts>")
set_tests_properties(HIKARI_MaterialSurfaceContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 60)

hikari_add_runtime_contract(HIKARI_LightingTransportMaterialContracts "Tools/Tests/HIKARI_LightingTransportMaterialContracts.cpp")
hikari_add_runtime_contract(HIKARI_SkyLightingRuntimeContracts "Tools/Tests/HIKARI_SkyLightingRuntimeContracts.cpp")
hikari_add_runtime_contract(HIKARI_IndirectCommandGpuContracts "Tools/Tests/HIKARI_IndirectCommandGpuContracts.cpp")
add_dependencies(HIKARI_IndirectCommandGpuContracts HIKARI_BuiltinShaderArtifacts)
add_test(NAME HIKARI_IndirectCommandGpuContracts COMMAND "$<TARGET_FILE:HIKARI_IndirectCommandGpuContracts>")
set_tests_properties(HIKARI_IndirectCommandGpuContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

hikari_add_runtime_contract(HIKARI_MeshMaterialUploadGpuContracts "Tools/Tests/HIKARI_MeshMaterialUploadGpuContracts.cpp")
add_test(NAME HIKARI_MeshMaterialUploadGpuContracts COMMAND "$<TARGET_FILE:HIKARI_MeshMaterialUploadGpuContracts>")
set_tests_properties(HIKARI_MeshMaterialUploadGpuContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

hikari_add_runtime_contract(HIKARI_MassGpuSceneUploadContracts "Tools/Tests/HIKARI_MassGpuSceneUploadContracts.cpp")
target_link_libraries(HIKARI_MassGpuSceneUploadContracts PRIVATE d3dcompiler)
add_test(NAME HIKARI_MassGpuSceneUploadContracts COMMAND "$<TARGET_FILE:HIKARI_MassGpuSceneUploadContracts>")
set_tests_properties(HIKARI_MassGpuSceneUploadContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

hikari_add_runtime_contract(HIKARI_ShadowMaterialCoverageGpuContracts "Tools/Tests/HIKARI_ShadowMaterialCoverageGpuContracts.cpp")
add_dependencies(HIKARI_ShadowMaterialCoverageGpuContracts HIKARI_BuiltinShaderArtifacts)
add_test(NAME HIKARI_ShadowMaterialCoverageGpuContracts COMMAND "$<TARGET_FILE:HIKARI_ShadowMaterialCoverageGpuContracts>")
set_tests_properties(HIKARI_ShadowMaterialCoverageGpuContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

hikari_add_runtime_contract(HIKARI_SurfaceFeatureUploadGpuContracts "Tools/Tests/HIKARI_SurfaceFeatureUploadGpuContracts.cpp")
target_link_libraries(HIKARI_SurfaceFeatureUploadGpuContracts PRIVATE d3dcompiler)
add_test(NAME HIKARI_SurfaceFeatureUploadGpuContracts COMMAND "$<TARGET_FILE:HIKARI_SurfaceFeatureUploadGpuContracts>")
set_tests_properties(HIKARI_SurfaceFeatureUploadGpuContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

hikari_add_runtime_contract(HIKARI_ClusterVertexDecodeGpuContracts "Tools/Tests/HIKARI_ClusterVertexDecodeGpuContracts.cpp")
target_link_libraries(HIKARI_ClusterVertexDecodeGpuContracts PRIVATE d3dcompiler)
target_compile_definitions(HIKARI_ClusterVertexDecodeGpuContracts PRIVATE HIKARI_CONTRACT_SOURCE_ROOT="${HIKARI_SOURCE_ROOT}")
add_test(NAME HIKARI_ClusterVertexDecodeGpuContracts COMMAND "$<TARGET_FILE:HIKARI_ClusterVertexDecodeGpuContracts>")
set_tests_properties(HIKARI_ClusterVertexDecodeGpuContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

add_executable(HIKARI_SceneSurfaceFloatPoolContracts EXCLUDE_FROM_ALL "Tools/Tests/HIKARI_SceneSurfaceFloatPoolContracts.cpp")
target_include_directories(HIKARI_SceneSurfaceFloatPoolContracts PRIVATE "${HIKARI_SOURCE_ROOT}/HIKARI")
hikari_configure_engine_target(HIKARI_SceneSurfaceFloatPoolContracts)
hikari_configure_contract_output(HIKARI_SceneSurfaceFloatPoolContracts "RuntimeContracts")
add_test(NAME HIKARI_SceneSurfaceFloatPoolContracts COMMAND "$<TARGET_FILE:HIKARI_SceneSurfaceFloatPoolContracts>")
set_tests_properties(HIKARI_SceneSurfaceFloatPoolContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)
add_test(NAME HIKARI_SkyLightingRuntimeContracts COMMAND "$<TARGET_FILE:HIKARI_SkyLightingRuntimeContracts>")
set_tests_properties(HIKARI_SkyLightingRuntimeContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)
hikari_add_runtime_contract(HIKARI_SunGlareFrameContracts "Tools/Tests/HIKARI_SunGlareFrameContracts.cpp")
add_test(NAME HIKARI_SunGlareFrameContracts COMMAND "$<TARGET_FILE:HIKARI_SunGlareFrameContracts>")
set_tests_properties(HIKARI_SunGlareFrameContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)
add_test(NAME HIKARI_LightingTransportMaterialContracts COMMAND "$<TARGET_FILE:HIKARI_LightingTransportMaterialContracts>")
set_tests_properties(HIKARI_LightingTransportMaterialContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

hikari_add_runtime_contract(HIKARI_TextureComputeGpuContracts "Tools/Tests/HIKARI_TextureComputeGpuContracts.cpp")
target_include_directories(HIKARI_TextureComputeGpuContracts PRIVATE "${HIKARI_SOURCE_ROOT}/ThirdParty/DirectXTex/Common")
target_link_libraries(HIKARI_TextureComputeGpuContracts PRIVATE d3dcompiler)
add_test(NAME HIKARI_TextureComputeGpuContracts COMMAND "$<TARGET_FILE:HIKARI_TextureComputeGpuContracts>")
set_tests_properties(HIKARI_TextureComputeGpuContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

hikari_add_runtime_contract(HIKARI_MaterialSnapshotGpuContracts "Tools/Tests/HIKARI_MaterialSnapshotGpuContracts.cpp")
target_include_directories(HIKARI_MaterialSnapshotGpuContracts PRIVATE "${HIKARI_SOURCE_ROOT}/ThirdParty/DirectXTex/Common")
add_test(NAME HIKARI_MaterialSnapshotGpuContracts COMMAND "$<TARGET_FILE:HIKARI_MaterialSnapshotGpuContracts>")
set_tests_properties(HIKARI_MaterialSnapshotGpuContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 60)

hikari_add_runtime_contract(HIKARI_DxrLightmapGpuContracts "Tools/Tests/HIKARI_DxrLightmapGpuContracts.cpp")
add_test(NAME HIKARI_DxrLightmapGpuContracts COMMAND "$<TARGET_FILE:HIKARI_DxrLightmapGpuContracts>")
set_tests_properties(HIKARI_DxrLightmapGpuContracts PROPERTIES LABELS "RuntimeContracts;HardwareGpu" TIMEOUT 60 SKIP_RETURN_CODE 77)

hikari_add_runtime_contract(HIKARI_SsrReconstructionContracts "Tools/Tests/HIKARI_SsrReconstructionContracts.cpp")
target_include_directories(HIKARI_SsrReconstructionContracts PRIVATE "${HIKARI_SOURCE_ROOT}/ThirdParty/DirectXTex/Common")
target_compile_definitions(HIKARI_SsrReconstructionContracts PRIVATE HIKARI_CONTRACT_SOURCE_ROOT="${HIKARI_SOURCE_ROOT}")
add_test(NAME HIKARI_SsrReconstructionContracts COMMAND "$<TARGET_FILE:HIKARI_SsrReconstructionContracts>")
set_tests_properties(HIKARI_SsrReconstructionContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 60)

hikari_add_runtime_contract(HIKARI_SceneReflectionCaptureContracts "Tools/Tests/HIKARI_SceneReflectionCaptureContracts.cpp")
target_include_directories(HIKARI_SceneReflectionCaptureContracts PRIVATE "${HIKARI_SOURCE_ROOT}/ThirdParty/DirectXTex/Common")
target_compile_definitions(HIKARI_SceneReflectionCaptureContracts PRIVATE HIKARI_CONTRACT_SOURCE_ROOT="${HIKARI_SOURCE_ROOT}")
add_test(NAME HIKARI_SceneReflectionCaptureContracts COMMAND "$<TARGET_FILE:HIKARI_SceneReflectionCaptureContracts>")
set_tests_properties(HIKARI_SceneReflectionCaptureContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 60)

add_executable(HIKARI_IblBakeTool EXCLUDE_FROM_ALL
    "Tools/Baking/HIKARI_IblBakeMain.cpp"
    "HIKARI/Assets/Importers/HIKARI_IblBaker.cpp"
    "HIKARI/Render3D/HIKARI_Math3D.cpp"
    "HIKARI/Core/HIKARI_Logger.cpp"
    "HIKARI/Core/Serialization/Json/HIKARI_JsonFile.cpp"
    "HIKARI/Core/IO/HIKARI_FileReplacementTransaction.cpp")
target_include_directories(HIKARI_IblBakeTool PRIVATE "${HIKARI_SOURCE_ROOT}/HIKARI")
target_link_libraries(HIKARI_IblBakeTool PRIVATE HIKARI::DirectXTex HIKARI::Nlohmann windowscodecs ole32)
hikari_configure_engine_target(HIKARI_IblBakeTool)
set_target_properties(HIKARI_IblBakeTool PROPERTIES FOLDER "Engine/Tools" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
foreach(configuration IN ITEMS Debug EditorRelease Shipping)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(HIKARI_IblBakeTool PROPERTIES "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/tools")
endforeach()

add_executable(HIKARI_IblBakeContracts EXCLUDE_FROM_ALL
    "Tools/Tests/HIKARI_IblBakeContracts.cpp"
    "HIKARI/Assets/Importers/HIKARI_IblBaker.cpp"
    "HIKARI/Render3D/HIKARI_Math3D.cpp"
    "HIKARI/Core/HIKARI_Logger.cpp"
    "HIKARI/Core/Serialization/Json/HIKARI_JsonFile.cpp"
    "HIKARI/Core/IO/HIKARI_FileReplacementTransaction.cpp")
target_include_directories(HIKARI_IblBakeContracts PRIVATE "${HIKARI_SOURCE_ROOT}/HIKARI")
target_link_libraries(HIKARI_IblBakeContracts PRIVATE HIKARI::DirectXTex HIKARI::Nlohmann windowscodecs ole32)
hikari_configure_engine_target(HIKARI_IblBakeContracts)
hikari_configure_contract_output(HIKARI_IblBakeContracts "RuntimeContracts")
add_test(NAME HIKARI_IblBakeContracts COMMAND "$<TARGET_FILE:HIKARI_IblBakeContracts>")
set_tests_properties(HIKARI_IblBakeContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 60)

add_executable(HIKARI_LocalLightSelectionContracts EXCLUDE_FROM_ALL
    "Tools/Tests/HIKARI_LocalLightSelectionContracts.cpp"
    "HIKARI/Scene/Components/Lighting/HIKARI_LightComponent.cpp"
    "HIKARI/Scene/Lighting/HIKARI_LightingAuthoringModel.cpp"
    "HIKARI/Scene/Lighting/HIKARI_LightAuthoringModel.cpp"
    "HIKARI/Scene/HIKARI_SceneDocumentQueries.cpp"
    "HIKARI/Scene/HIKARI_SceneComponentId.cpp"
    "HIKARI/Render3D/Lighting/HIKARI_LocalLightList.cpp"
    "HIKARI/Render3D/Lighting/HIKARI_LocalLightSemantics.cpp"
    "HIKARI/Render3D/Core/HIKARI_Camera3D.cpp"
    "HIKARI/Render3D/HIKARI_Math3D.cpp")
target_include_directories(HIKARI_LocalLightSelectionContracts PRIVATE "${HIKARI_SOURCE_ROOT}/HIKARI")
target_link_libraries(HIKARI_LocalLightSelectionContracts PRIVATE HIKARI::Nlohmann)
hikari_configure_engine_target(HIKARI_LocalLightSelectionContracts)
hikari_configure_contract_output(HIKARI_LocalLightSelectionContracts "RuntimeContracts")
add_test(NAME HIKARI_LocalLightSelectionContracts COMMAND "$<TARGET_FILE:HIKARI_LocalLightSelectionContracts>")
set_tests_properties(HIKARI_LocalLightSelectionContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

add_executable(HIKARI_GeometryFrameContracts EXCLUDE_FROM_ALL
    "Tools/Tests/HIKARI_GeometryFrameContracts.cpp"
    "HIKARI/Assets/Geometry/Cooking/HIKARI_ClusteredGeometrySourcePreparation.cpp"
    "HIKARI/Assets/Geometry/Cooking/HIKARI_ClusteredGeometrySurfacePartitioner.cpp"
    "HIKARI/Assets/Geometry/Cooking/HIKARI_ClusterTriangleGroupBuilder.cpp"
    "HIKARI/Core/Math/HIKARI_NormalMatrix.cpp"
    "HIKARI/Core/Math/HIKARI_MathValidation.cpp"
    "HIKARI/Core/Text/HIKARI_AsciiCase.cpp"
    "HIKARI/Render3D/HIKARI_Math3D.cpp")
target_include_directories(HIKARI_GeometryFrameContracts PRIVATE "${HIKARI_SOURCE_ROOT}/HIKARI")
target_link_libraries(HIKARI_GeometryFrameContracts PRIVATE HIKARI::Nlohmann HIKARI::Meshoptimizer)
hikari_configure_engine_target(HIKARI_GeometryFrameContracts)
hikari_configure_contract_output(HIKARI_GeometryFrameContracts "RuntimeContracts")
add_test(NAME HIKARI_GeometryFrameContracts COMMAND "$<TARGET_FILE:HIKARI_GeometryFrameContracts>")
set_tests_properties(HIKARI_GeometryFrameContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

add_executable(HIKARI_AssetTaskLifecycleContracts EXCLUDE_FROM_ALL
    "Tools/Tests/HIKARI_AssetTaskLifecycleContracts.cpp"
    "HIKARI/Assets/Tasks/HIKARI_AssetTaskService.cpp"
    "HIKARI/Core/Tasks/HIKARI_TaskExecutor.cpp")
target_include_directories(HIKARI_AssetTaskLifecycleContracts PRIVATE "${HIKARI_SOURCE_ROOT}/HIKARI")
hikari_configure_engine_target(HIKARI_AssetTaskLifecycleContracts)
hikari_configure_contract_output(HIKARI_AssetTaskLifecycleContracts "RuntimeContracts")
add_test(NAME HIKARI_AssetTaskLifecycleContracts COMMAND "$<TARGET_FILE:HIKARI_AssetTaskLifecycleContracts>")
set_tests_properties(HIKARI_AssetTaskLifecycleContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

add_executable(HIKARI_AssetImportPublicationContracts EXCLUDE_FROM_ALL
    "Tools/Tests/HIKARI_AssetImportPublicationContracts.cpp"
    "HIKARI/Assets/Tasks/HIKARI_AssetImportPublication.cpp"
    "HIKARI/Core/IO/HIKARI_FileReplacementTransaction.cpp")
target_include_directories(HIKARI_AssetImportPublicationContracts PRIVATE "${HIKARI_SOURCE_ROOT}/HIKARI")
hikari_configure_engine_target(HIKARI_AssetImportPublicationContracts)
hikari_configure_contract_output(HIKARI_AssetImportPublicationContracts "RuntimeContracts")
add_test(NAME HIKARI_AssetImportPublicationContracts COMMAND "$<TARGET_FILE:HIKARI_AssetImportPublicationContracts>")
set_tests_properties(HIKARI_AssetImportPublicationContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)

add_executable(HIKARI_TextureCookContracts EXCLUDE_FROM_ALL
    "Tools/Tests/HIKARI_TextureCookContracts.cpp"
    "HIKARI/Assets/Importers/Policy/HIKARI_TextureImportPolicy.cpp"
    "HIKARI/Assets/Semantics/HIKARI_AssetArtifactSemantics.cpp"
    "HIKARI/Assets/Importers/HIKARI_TextureImportBackend_DirectXTex.cpp"
    "HIKARI/Assets/Formats/HIKARI_HtexFormat.cpp"
    "HIKARI/Assets/Tasks/HIKARI_AssetTaskService.cpp"
    "HIKARI/Core/Tasks/HIKARI_TaskExecutor.cpp"
    "HIKARI/Core/HIKARI_Logger.cpp"
    "HIKARI/Core/Text/HIKARI_AsciiCase.cpp")
target_include_directories(HIKARI_TextureCookContracts PRIVATE "${HIKARI_SOURCE_ROOT}/HIKARI")
target_link_libraries(HIKARI_TextureCookContracts PRIVATE HIKARI::DirectXTex HIKARI::Nlohmann windowscodecs ole32)
hikari_configure_engine_target(HIKARI_TextureCookContracts)
hikari_configure_contract_output(HIKARI_TextureCookContracts "RuntimeContracts")
add_test(NAME HIKARI_TextureCookContracts COMMAND "$<TARGET_FILE:HIKARI_TextureCookContracts>")
set_tests_properties(HIKARI_TextureCookContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 60)

add_executable(HIKARI_HtexLayoutContracts EXCLUDE_FROM_ALL
    "Tools/Tests/HIKARI_HtexLayoutContracts.cpp"
    "HIKARI/Assets/Formats/HIKARI_HtexFormat.cpp")
target_include_directories(HIKARI_HtexLayoutContracts PRIVATE "${HIKARI_SOURCE_ROOT}/HIKARI")
target_link_libraries(HIKARI_HtexLayoutContracts PRIVATE HIKARI::DirectXTex)
hikari_configure_engine_target(HIKARI_HtexLayoutContracts)
hikari_configure_contract_output(HIKARI_HtexLayoutContracts "RuntimeContracts")
add_test(NAME HIKARI_HtexLayoutContracts COMMAND "$<TARGET_FILE:HIKARI_HtexLayoutContracts>" "${HIKARI_SOURCE_ROOT}/HIKARI/EngineContent")
set_tests_properties(HIKARI_HtexLayoutContracts PROPERTIES LABELS "RuntimeContracts" TIMEOUT 30)
foreach(runtime_contract IN ITEMS
    HIKARI_GameComponentBindingLifecycleSmoke
    HIKARI_WorldSdkSmoke
    HIKARI_WorldCommandSchedulerSmoke
    HIKARI_WorldBatchSmoke
    HIKARI_WorldComponentIndexSmoke
    HIKARI_WorldQuerySdkSmoke
    HIKARI_WorldMassStoreSmoke
    HIKARI_RuntimeFeatureTransactionSmoke
    HIKARI_SequencePlaybackReverseSmoke
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
    HIKARI_BuiltinShaderCatalogSmoke
    HIKARI_WorldTextContracts)
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
    hikari_add_runtime_contract(HIKARI_SceneFolderContracts "Tools/Tests/HIKARI_SceneFolderContracts.cpp")
    add_executable(HIKARI_ExportStartupContracts EXCLUDE_FROM_ALL
        "Tools/Tests/HIKARI_ExportStartupContracts.cpp"
        "HIKARI/Editor/Export/HIKARI_NativeRuntimeDeployment.cpp"
        "HIKARI/Editor/Export/HIKARI_GameExportPublication.cpp"
        "HIKARI/Core/Text/HIKARI_AsciiCase.cpp"
        "HIKARI/Core/HIKARI_Logger.cpp")
    target_sources(HIKARI_ExportStartupContracts PRIVATE
        "HIKARI/Platform/HIKARI_StartupSplash.cpp"
        "HIKARI/Platform/HIKARI_Win32Window.cpp"
        $<TARGET_OBJECTS:HIKARI_StartupResources>)
    target_include_directories(HIKARI_ExportStartupContracts PRIVATE "${HIKARI_SOURCE_ROOT}" "${HIKARI_SOURCE_ROOT}/HIKARI")
    target_link_libraries(HIKARI_ExportStartupContracts PRIVATE d3d12 dxgi user32 gdi32 ole32 windowscodecs comctl32 shell32)
    hikari_configure_engine_target(HIKARI_ExportStartupContracts)
    hikari_configure_contract_output(HIKARI_ExportStartupContracts "EditorContracts")
    add_test(NAME HIKARI_ExportStartupContracts COMMAND "$<TARGET_FILE:HIKARI_ExportStartupContracts>")
    set_tests_properties(HIKARI_ExportStartupContracts PROPERTIES LABELS "EditorContracts" TIMEOUT 30)
    target_sources(HIKARI_SceneFolderContracts PRIVATE
        "${HIKARI_SOURCE_ROOT}/HIKARI/Editor/Authoring/HIKARI_SceneFolderAuthoring.cpp"
        "${HIKARI_SOURCE_ROOT}/HIKARI/Editor/Authoring/HIKARI_SceneFolderOperations.cpp"
        "${HIKARI_SOURCE_ROOT}/HIKARI/Editor/Commands/HIKARI_EditorOperationRegistry.cpp"
        "${HIKARI_SOURCE_ROOT}/HIKARI/Editor/History/HIKARI_EditorDocumentHistory.cpp")
    add_test(NAME HIKARI_SceneFolderContracts COMMAND "$<TARGET_FILE:HIKARI_SceneFolderContracts>")
    set_tests_properties(HIKARI_SceneFolderContracts PROPERTIES LABELS "EditorContracts" TIMEOUT 30)
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

    hikari_add_runtime_contract(HIKARI_AssetBrowserContracts "Tools/Tests/HIKARI_AssetBrowserContracts.cpp" NO_TOOLING_STUBS)
    target_link_libraries(HIKARI_AssetBrowserContracts PRIVATE HIKARI::Editor)
    add_test(NAME HIKARI_AssetBrowserContracts COMMAND "$<TARGET_FILE:HIKARI_AssetBrowserContracts>")
    set_tests_properties(HIKARI_AssetBrowserContracts PROPERTIES LABELS "EditorContracts" TIMEOUT 30)

    add_executable(
        HIKARI_GameProjectEditorWorkspaceRegistrySmoke
        EXCLUDE_FROM_ALL
        "Tools/Tests/HIKARI_GameProjectEditorWorkspaceRegistrySmoke.cpp")
    target_link_libraries(
        HIKARI_GameProjectEditorWorkspaceRegistrySmoke
        PRIVATE
            HIKARI::Editor)
    hikari_configure_engine_target(
        HIKARI_GameProjectEditorWorkspaceRegistrySmoke)
    hikari_configure_contract_output(
        HIKARI_GameProjectEditorWorkspaceRegistrySmoke "EditorContracts")
    foreach(runtime_file IN LISTS HIKARI_RUNTIME_DEPENDENCIES)
        add_custom_command(
            TARGET HIKARI_GameProjectEditorWorkspaceRegistrySmoke
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${runtime_file}"
                "$<TARGET_FILE_DIR:HIKARI_GameProjectEditorWorkspaceRegistrySmoke>"
            VERBATIM)
    endforeach()
    add_test(
        NAME HIKARI_GameProjectEditorWorkspaceRegistrySmoke
        COMMAND
            "$<TARGET_FILE:HIKARI_GameProjectEditorWorkspaceRegistrySmoke>")
    set_tests_properties(
        HIKARI_GameProjectEditorWorkspaceRegistrySmoke
        PROPERTIES
            CONFIGURATIONS "Debug;EditorRelease"
            LABELS "EditorContracts"
            TIMEOUT 30)

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

    add_executable(HIKARI_ProjectLauncher WIN32)
    target_sources(HIKARI_ProjectLauncher PRIVATE $<TARGET_OBJECTS:HIKARI_StartupResources>)
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
