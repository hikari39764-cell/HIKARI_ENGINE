include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/HIKARIModuleTargets.cmake")

# ゲーム項目自身のコード。実行の DLL（HIKARI_GameProject）と、編集器のソースがあれば編集器の DLL
# （HIKARI_GameProjectEditor）を <HIKARI_GAME_PROJECT_OUTPUT_ROOT>/bin へ出す。一つの項目に一つだけ。
# 項目をまたいで使う機能は hikari_add_module（HIKARIModule.cmake）で挿件包にする。
function(hikari_add_game_project)
    set(options)
    set(one_value_arguments NAME SOURCE_DIR EDITOR_SOURCE_DIR)
    set(multi_value_arguments SOURCES EDITOR_SOURCES)
    cmake_parse_arguments(
        HIKARI_PROJECT
        "${options}"
        "${one_value_arguments}"
        "${multi_value_arguments}"
        ${ARGN})

    if(NOT HIKARI_PROJECT_NAME)
        message(FATAL_ERROR "hikari_add_game_project requires NAME.")
    endif()
    if(NOT DEFINED HIKARI_GAME_PROJECT_OUTPUT_ROOT OR
       HIKARI_GAME_PROJECT_OUTPUT_ROOT STREQUAL "")
        message(
            FATAL_ERROR
            "HIKARI_GAME_PROJECT_OUTPUT_ROOT is required.")
    endif()
    if(TARGET HIKARI_GameProjectModule)
        message(
            FATAL_ERROR
            "Only one HIKARI GameProject module may be declared.")
    endif()

    # Editor のソースは構成に関わらず解決する：Shipping の構成でも、実行側の広い glob から外す必要がある。
    _hikari_resolve_module_sources(HIKARI_PROJECT "GameProject")
    _hikari_editor_configuration_available(_hikari_editor_configuration_available)
    _hikari_sdk_adapter(_hikari_adapter "HIKARI_GameProjectModuleAdapter.cpp")

    set(_hikari_binary_directory "${HIKARI_GAME_PROJECT_OUTPUT_ROOT}/bin")
    set(_hikari_archive_directory "${HIKARI_GAME_PROJECT_OUTPUT_ROOT}/lib")

    add_library(HIKARI_GameProjectModule MODULE)
    target_sources(
        HIKARI_GameProjectModule
        PRIVATE
            ${HIKARI_PROJECT_RUNTIME_SOURCES}
            "${_hikari_adapter}")
    _hikari_link_runtime_sdk(HIKARI_GameProjectModule)
    if(HIKARI_PROJECT_RUNTIME_ROOT)
        target_include_directories(
            HIKARI_GameProjectModule
            PRIVATE
                "${HIKARI_PROJECT_RUNTIME_ROOT}")
    endif()
    hikari_configure_module_target(HIKARI_GameProjectModule)
    _hikari_set_module_output(
        HIKARI_GameProjectModule "HIKARI_GameProject" "${_hikari_archive_directory}"
        "${_hikari_binary_directory}" "${_hikari_binary_directory}" "${_hikari_binary_directory}")

    source_group(
        TREE "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES ${HIKARI_PROJECT_RUNTIME_SOURCES})
    source_group(
        "HIKARI SDK"
        FILES "${_hikari_adapter}")

    set(_hikari_editor_companion_enabled FALSE)
    if(HIKARI_GAME_PROJECT_EDITOR_AVAILABLE AND
       _hikari_editor_configuration_available AND
       HIKARI_PROJECT_EDITOR_FILES)
        _hikari_sdk_adapter(_hikari_editor_adapter "HIKARI_GameProjectEditorModuleAdapter.cpp")

        add_library(HIKARI_GameProjectEditorModule MODULE)
        target_sources(
            HIKARI_GameProjectEditorModule
            PRIVATE
                ${HIKARI_PROJECT_EDITOR_FILES}
                "${_hikari_editor_adapter}")
        target_link_libraries(
            HIKARI_GameProjectEditorModule
            PRIVATE
                HIKARI::GameProjectEditorSdk)
        if(HIKARI_PROJECT_EDITOR_ROOT)
            target_include_directories(
                HIKARI_GameProjectEditorModule
                PRIVATE
                    "${HIKARI_PROJECT_EDITOR_ROOT}")
        endif()
        hikari_configure_module_target(HIKARI_GameProjectEditorModule)
        _hikari_set_module_output(
            HIKARI_GameProjectEditorModule "HIKARI_GameProjectEditor" "${_hikari_archive_directory}"
            "${_hikari_binary_directory}" "${_hikari_binary_directory}" "${_hikari_binary_directory}")
        set_target_properties(
            HIKARI_GameProjectEditorModule
            PROPERTIES
                EXCLUDE_FROM_DEFAULT_BUILD_SHIPPING TRUE)
        source_group(
            TREE "${CMAKE_CURRENT_SOURCE_DIR}"
            FILES ${HIKARI_PROJECT_EDITOR_FILES})
        source_group(
            "HIKARI Editor SDK"
            FILES "${_hikari_editor_adapter}")
        install(
            TARGETS HIKARI_GameProjectEditorModule
            CONFIGURATIONS Debug EditorRelease
            RUNTIME DESTINATION "."
            LIBRARY DESTINATION ".")
        set(_hikari_editor_companion_enabled TRUE)
    endif()

    file(
        WRITE
        "${CMAKE_BINARY_DIR}/HIKARI_GameProjectEditorCompanion.txt"
        "${_hikari_editor_companion_enabled}\n")

    install(
        TARGETS HIKARI_GameProjectModule
        RUNTIME DESTINATION "."
        LIBRARY DESTINATION ".")

    message(
        STATUS
        "HIKARI GameProject: ${HIKARI_PROJECT_NAME}")
    message(
        STATUS
        "HIKARI GameProject Editor companion: "
        "${_hikari_editor_companion_enabled}")
endfunction()
