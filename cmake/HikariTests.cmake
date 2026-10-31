# Small, opt-in behavior suites. Product targets never depend on these targets.
enable_testing()

function(hikari_add_contract name group owner)
    set(target "HIKARI_${name}")
    add_executable(${target} EXCLUDE_FROM_ALL "${HIKARI_SOURCE_ROOT}/Tools/Tests/${target}.cpp")
    set_target_properties(${target} PROPERTIES HIKARI_KEEP_ASSERTIONS ON)
    if(owner STREQUAL "Editor")
        target_link_libraries(${target} PRIVATE HIKARI::Editor)
    else()
        target_link_libraries(${target} PRIVATE HIKARI::Runtime)
        target_sources(${target} PRIVATE
            "${HIKARI_SOURCE_ROOT}/HIKARI/Runtime/Host/HIKARI_RuntimeComponentToolingStubs.cpp")
    endif()
    hikari_configure_engine_target(${target})
    target_include_directories(${target} PRIVATE
        "${HIKARI_SOURCE_ROOT}" "${HIKARI_SOURCE_ROOT}/ThirdParty/DirectXTex/Common")
    target_compile_definitions(${target} PRIVATE
        HIKARI_CONTRACT_SOURCE_ROOT="${HIKARI_SOURCE_ROOT}"
        HIKARI_CONTRACT_ENGINE_CONTENT_ROOT="${HIKARI_RUNTIME_DIRECTORY}/EngineContent"
        # テストの一時ファイルは起動時の作業ディレクトリに依存させず、ビルドツリー内へ出す。
        HIKARI_CONTRACT_OUTPUT_ROOT="${CMAKE_BINARY_DIR}/tests")
    set_target_properties(${target} PROPERTIES
        FOLDER "Tests/${group}" EXCLUDE_FROM_DEFAULT_BUILD TRUE)
    foreach(configuration IN ITEMS Debug EditorRelease Shipping)
        string(TOUPPER "${configuration}" config)
        foreach(kind IN ITEMS ARCHIVE RUNTIME PDB COMPILE_PDB)
            set_target_properties(${target} PROPERTIES
                "${kind}_OUTPUT_DIRECTORY_${config}" "${CMAKE_BINARY_DIR}/tests")
        endforeach()
    endforeach()
    foreach(runtime_file IN LISTS HIKARI_RUNTIME_DEPENDENCIES)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${runtime_file}" "$<TARGET_FILE_DIR:${target}>" VERBATIM)
    endforeach()
    if(NOT group STREQUAL "fixture")
        add_test(NAME ${target} COMMAND "$<TARGET_FILE:${target}>" ${ARGN})
        set_tests_properties(${target} PROPERTIES LABELS "${group}" TIMEOUT 60
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    endif()
    if(group STREQUAL "assets" OR group STREQUAL "vfx")
        set_tests_properties(${target} PROPERTIES TIMEOUT 120)
    endif()
    if(group STREQUAL "gpu")
        target_link_libraries(${target} PRIVATE d3dcompiler)
        add_dependencies(${target} HIKARI_BuiltinShaderArtifacts)
        set_tests_properties(${target} PROPERTIES SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
    endif()
endfunction()

if(HIKARI_WITH_EDITOR_BUILD)
    foreach(name IN ITEMS AssetDatabaseImportContracts AssetImportPublicationContracts)
        hikari_add_contract(${name} assets Editor)
    endforeach()
    target_sources(HIKARI_AssetDatabaseImportContracts PRIVATE
        "${HIKARI_SOURCE_ROOT}/Tools/Tests/HIKARI_ProjectPostContracts.cpp")
    foreach(name IN ITEMS EditorControlContracts MaterialWorkspaceEditingContracts)
        hikari_add_contract(${name} editor Editor)
    endforeach()
    target_sources(HIKARI_EditorControlContracts PRIVATE
        "${HIKARI_SOURCE_ROOT}/Tools/Tests/HIKARI_SequenceCameraEditingContracts.cpp"
        "${HIKARI_SOURCE_ROOT}/Tools/Tests/HIKARI_CurveEditingContracts.cpp"
        "${HIKARI_SOURCE_ROOT}/Tools/Tests/HIKARI_ComponentInspectorContracts.cpp")
    hikari_add_contract(AgentContracts editor Editor "${CMAKE_BINARY_DIR}/tests/agent-contracts")
    # 本地桥の実際の往復は hikari.exe と同じ客户端で確かめる。
    target_sources(HIKARI_AgentContracts PRIVATE "${HIKARI_SOURCE_ROOT}/Tools/Agent/HIKARI_AgentClient.cpp")
    foreach(name IN ITEMS UiAuthoringContracts UiAnimationContracts DialogueContracts)
        hikari_add_contract(${name} ui Editor)
    endforeach()
    hikari_add_contract(GameplayCameraContracts camera Editor)
    target_sources(HIKARI_GameplayCameraContracts PRIVATE
        "${HIKARI_SOURCE_ROOT}/Tools/Tests/HIKARI_KinematicMotionContracts.cpp")
    hikari_add_contract(TerrainSurfaceContracts terrain Editor)
    foreach(name IN ITEMS NativeVfxSmoke VfxPresetSmoke VfxSpecialOutputSmoke)
        hikari_add_contract(${name} vfx Editor)
    endforeach()
    hikari_add_contract(UiRenderingContracts gpu Editor)

    # On-demand scene generator for visual/performance investigation, not a test.
    hikari_add_contract(RenderReferenceFixture fixture Editor)
    target_sources(HIKARI_RenderReferenceFixture PRIVATE
        "${HIKARI_SOURCE_ROOT}/Tools/Tests/RenderReference/HIKARI_RenderValidationProject.cpp"
        "${HIKARI_SOURCE_ROOT}/Tools/Tests/RenderReference/HIKARI_RenderValidationScenes.cpp")
    target_compile_definitions(HIKARI_RenderReferenceFixture PRIVATE HIKARI_REFERENCE_PROJECT=1)
endif()

hikari_add_contract(ShaderArtifactFormatSmoke assets Runtime)
foreach(name IN ITEMS WorldSdkSmoke WorldQuerySdkSmoke ActionRuntimeContracts
        RuntimeFeatureTransactionSmoke InputDeviceArbitrationContracts EnvironmentContracts)
    hikari_add_contract(${name} runtime Runtime)
endforeach()
target_sources(HIKARI_ActionRuntimeContracts PRIVATE
    "${HIKARI_SOURCE_ROOT}/Tools/Tests/HIKARI_AudioPlaybackContracts.cpp"
    "${HIKARI_SOURCE_ROOT}/Tools/Tests/HIKARI_AfterimageContracts.cpp")
foreach(name IN ITEMS RenderGraphSmoke RenderGraphCapacitySmoke TemporalFrameStateSmoke)
    hikari_add_contract(${name} render Runtime)
endforeach()
foreach(name IN ITEMS RenderMathContracts DynamicUploadGpuContracts ClusterVertexDecodeGpuContracts
        ParticleCompactionGpuContracts VfxStyleGpuContracts MaterialSurfaceContracts)
    hikari_add_contract(${name} gpu Runtime)
endforeach()

target_sources(HIKARI_VfxStyleGpuContracts PRIVATE
    "${HIKARI_SOURCE_ROOT}/Tools/Tests/HIKARI_SceneCaptureStyleContracts.cpp"
    "${HIKARI_SOURCE_ROOT}/Tools/Tests/HIKARI_PreservedVfxContracts.cpp")

# Public SDK compilation must not inherit private engine includes.
add_library(HIKARI_GameProjectActionSdkContracts OBJECT EXCLUDE_FROM_ALL
    "${HIKARI_SOURCE_ROOT}/Tools/Tests/HIKARI_GameProjectActionSdkContracts.cpp")
target_include_directories(HIKARI_GameProjectActionSdkContracts PRIVATE "${HIKARI_SOURCE_ROOT}/include")
target_compile_features(HIKARI_GameProjectActionSdkContracts PRIVATE cxx_std_20)
target_compile_options(HIKARI_GameProjectActionSdkContracts PRIVATE /utf-8)
set_target_properties(HIKARI_GameProjectActionSdkContracts PROPERTIES FOLDER "Tests/sdk")
