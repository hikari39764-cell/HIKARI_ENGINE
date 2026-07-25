include_guard(GLOBAL)

function(hikari_load_game_project manifest_path)
    if(NOT IS_ABSOLUTE "${manifest_path}")
        get_filename_component(
            manifest_path
            "${manifest_path}"
            ABSOLUTE
            BASE_DIR "${HIKARI_SOURCE_ROOT}")
    endif()
    cmake_path(NORMAL_PATH manifest_path)
    if(NOT EXISTS "${manifest_path}")
        message(
            FATAL_ERROR
            "HIKARI GameProject manifest was not found: ${manifest_path}")
    endif()

    unset(HIKARI_DECLARED_GAME_PROJECT_NAME)
    unset(HIKARI_DECLARED_GAME_PROJECT_SOURCES)

    macro(hikari_declare_game_project)
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
            message(FATAL_ERROR "GameProject requires NAME.")
        endif()
        if(NOT HIKARI_PROJECT_SOURCES)
            message(FATAL_ERROR "GameProject requires at least one source.")
        endif()
        if(HIKARI_DECLARED_GAME_PROJECT_NAME)
            message(
                FATAL_ERROR
                "A GameProject manifest may declare only one module.")
        endif()

        set(HIKARI_DECLARED_GAME_PROJECT_NAME "${HIKARI_PROJECT_NAME}")
        set(HIKARI_DECLARED_GAME_PROJECT_SOURCES)
        foreach(project_source IN LISTS HIKARI_PROJECT_SOURCES)
            if(IS_ABSOLUTE "${project_source}")
                set(resolved_source "${project_source}")
            else()
                set(
                    resolved_source
                    "${CMAKE_CURRENT_LIST_DIR}/${project_source}")
            endif()
            cmake_path(NORMAL_PATH resolved_source)
            if(NOT EXISTS "${resolved_source}")
                message(
                    FATAL_ERROR
                    "GameProject source was not found: ${resolved_source}")
            endif()
            list(
                APPEND
                HIKARI_DECLARED_GAME_PROJECT_SOURCES
                "${resolved_source}")
        endforeach()
    endmacro()

    include("${manifest_path}")
    if(NOT HIKARI_DECLARED_GAME_PROJECT_NAME OR
       NOT HIKARI_DECLARED_GAME_PROJECT_SOURCES)
        message(
            FATAL_ERROR
            "GameProject manifest must call hikari_declare_game_project().")
    endif()

    get_filename_component(
        project_root
        "${manifest_path}"
        DIRECTORY)
    set(
        HIKARI_SELECTED_GAME_PROJECT_NAME
        "${HIKARI_DECLARED_GAME_PROJECT_NAME}"
        PARENT_SCOPE)
    set(
        HIKARI_SELECTED_GAME_PROJECT_ROOT
        "${project_root}"
        PARENT_SCOPE)
    set(
        HIKARI_SELECTED_GAME_PROJECT_SOURCES
        "${HIKARI_DECLARED_GAME_PROJECT_SOURCES}"
        PARENT_SCOPE)
endfunction()
