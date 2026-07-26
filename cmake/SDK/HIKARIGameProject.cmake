include_guard(GLOBAL)

function(hikari_add_game_project)
    set(options)
    set(one_value_arguments NAME)
    set(multi_value_arguments SOURCES)
    cmake_parse_arguments(
        HIKARI_PROJECT
        "${options}"
        "${one_value_arguments}"
        "${multi_value_arguments}"
        ${ARGN})

    if(NOT HIKARI_PROJECT_NAME)
        message(FATAL_ERROR "hikari_add_game_project requires NAME.")
    endif()
    if(NOT HIKARI_PROJECT_SOURCES)
        message(FATAL_ERROR "hikari_add_game_project requires SOURCES.")
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

    get_filename_component(
        _hikari_sdk_root
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../.."
        ABSOLUTE)
    set(
        _hikari_adapter
        "${_hikari_sdk_root}/src/HIKARI_GameProjectModuleAdapter.cpp")
    if(NOT EXISTS "${_hikari_adapter}")
        message(
            FATAL_ERROR
            "HIKARI SDK module adapter is missing: ${_hikari_adapter}")
    endif()

    add_library(HIKARI_GameProjectModule MODULE)
    target_sources(
        HIKARI_GameProjectModule
        PRIVATE
            ${HIKARI_PROJECT_SOURCES}
            "${_hikari_adapter}")
    target_link_libraries(
        HIKARI_GameProjectModule
        PRIVATE
            HIKARI::GameProjectSdk)
    target_compile_features(
        HIKARI_GameProjectModule
        PRIVATE
            cxx_std_20)
    target_compile_definitions(
        HIKARI_GameProjectModule
        PRIVATE
            UNICODE
            _UNICODE
            _WINDOWS
            _CRT_SECURE_NO_WARNINGS
            _CRT_NONSTDC_NO_DEPRECATE
            $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
            $<$<CONFIG:Shipping>:HIKARI_SHIPPING>)
    set_property(
        TARGET HIKARI_GameProjectModule
        PROPERTY
            MSVC_RUNTIME_LIBRARY
            "$<$<CONFIG:Debug>:MultiThreadedDebugDLL>$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:MultiThreaded>")
    if(MSVC)
        target_compile_options(
            HIKARI_GameProjectModule
            PRIVATE
                /utf-8
                /permissive-
                /sdl
                /MP
                "$<$<CONFIG:Debug>:/W3>"
                "$<$<CONFIG:Debug>:/WX>"
                "$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:/W4>"
                "$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:/O2>")
    endif()

    set_target_properties(
        HIKARI_GameProjectModule
        PROPERTIES
            PREFIX ""
            OUTPUT_NAME "HIKARI_GameProject"
            RUNTIME_OUTPUT_DIRECTORY
                "${HIKARI_GAME_PROJECT_OUTPUT_ROOT}/bin"
            LIBRARY_OUTPUT_DIRECTORY
                "${HIKARI_GAME_PROJECT_OUTPUT_ROOT}/bin"
            ARCHIVE_OUTPUT_DIRECTORY
                "${HIKARI_GAME_PROJECT_OUTPUT_ROOT}/lib"
            PDB_OUTPUT_DIRECTORY
                "${HIKARI_GAME_PROJECT_OUTPUT_ROOT}/bin")
    foreach(configuration IN ITEMS Debug EditorRelease Shipping)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(
            HIKARI_GameProjectModule
            PROPERTIES
                "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}"
                    "${HIKARI_GAME_PROJECT_OUTPUT_ROOT}/bin"
                "LIBRARY_OUTPUT_DIRECTORY_${configuration_upper}"
                    "${HIKARI_GAME_PROJECT_OUTPUT_ROOT}/bin"
                "ARCHIVE_OUTPUT_DIRECTORY_${configuration_upper}"
                    "${HIKARI_GAME_PROJECT_OUTPUT_ROOT}/lib"
                "PDB_OUTPUT_DIRECTORY_${configuration_upper}"
                    "${HIKARI_GAME_PROJECT_OUTPUT_ROOT}/bin")
    endforeach()

    source_group(
        TREE "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES ${HIKARI_PROJECT_SOURCES})
    source_group(
        "HIKARI SDK"
        FILES "${_hikari_adapter}")

    install(
        TARGETS HIKARI_GameProjectModule
        RUNTIME DESTINATION "."
        LIBRARY DESTINATION ".")

    message(
        STATUS
        "HIKARI GameProject: ${HIKARI_PROJECT_NAME}")
endfunction()
