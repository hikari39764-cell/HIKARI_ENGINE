include_guard(GLOBAL)

function(hikari_add_game_project)
    set(options)
    set(one_value_arguments NAME SOURCE_DIR)
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

    set(_hikari_project_sources ${HIKARI_PROJECT_SOURCES})
    if(HIKARI_PROJECT_SOURCE_DIR)
        get_filename_component(
            _hikari_project_source_root
            "${HIKARI_PROJECT_SOURCE_DIR}"
            ABSOLUTE
            BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if(NOT IS_DIRECTORY "${_hikari_project_source_root}")
            message(
                FATAL_ERROR
                "HIKARI GameProject SOURCE_DIR does not exist: "
                "${_hikari_project_source_root}")
        endif()
        file(
            REAL_PATH
            "${_hikari_project_source_root}"
            _hikari_project_source_root)
        get_filename_component(
            _hikari_project_manifest_root
            "${CMAKE_CURRENT_SOURCE_DIR}"
            ABSOLUTE)
        file(
            REAL_PATH
            "${_hikari_project_manifest_root}"
            _hikari_project_manifest_root)
        cmake_path(
            IS_PREFIX
            _hikari_project_manifest_root
            "${_hikari_project_source_root}"
            NORMALIZE
            _hikari_source_is_project_relative)
        if(NOT _hikari_source_is_project_relative OR
           _hikari_project_source_root STREQUAL
                _hikari_project_manifest_root)
            message(
                FATAL_ERROR
                "HIKARI GameProject SOURCE_DIR must identify a "
                "dedicated directory inside the project manifest root.")
        endif()
        file(
            GLOB_RECURSE
            _hikari_discovered_project_sources
            CONFIGURE_DEPENDS
            LIST_DIRECTORIES false
            "${_hikari_project_source_root}/*.c"
            "${_hikari_project_source_root}/*.cc"
            "${_hikari_project_source_root}/*.cpp"
            "${_hikari_project_source_root}/*.cxx"
            "${_hikari_project_source_root}/*.h"
            "${_hikari_project_source_root}/*.hh"
            "${_hikari_project_source_root}/*.hpp"
            "${_hikari_project_source_root}/*.hxx"
            "${_hikari_project_source_root}/*.inl")
        list(
            APPEND
            _hikari_project_sources
            ${_hikari_discovered_project_sources})
    endif()
    if(NOT _hikari_project_sources)
        message(
            FATAL_ERROR
            "hikari_add_game_project requires SOURCE_DIR or SOURCES.")
    endif()
    list(REMOVE_DUPLICATES _hikari_project_sources)

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
            ${_hikari_project_sources}
            "${_hikari_adapter}")
    target_link_libraries(
        HIKARI_GameProjectModule
        PRIVATE
            HIKARI::GameProjectSdk)
    if(HIKARI_PROJECT_SOURCE_DIR)
        target_include_directories(
            HIKARI_GameProjectModule
            PRIVATE
                "${_hikari_project_source_root}")
    endif()
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
        FILES ${_hikari_project_sources})
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
