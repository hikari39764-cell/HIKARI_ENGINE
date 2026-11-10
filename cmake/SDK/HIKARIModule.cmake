include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/HIKARIModuleTargets.cmake")

# 項目をまたいで使う外部モジュールを構築し、そのまま配れる挿件包のフォルダを作る：
#
#   <HIKARI_MODULE_PACKAGE_ROOT>/<id>/
#       <id>.hmodule            構築が書く清単（元の清単 + SDK の版 + DLL の路径）
#       <library>.dll           EditorRelease の実行 DLL
#       <library>Editor.dll     編集器の DLL（SINGLE_LIBRARY なら実行 DLL が兼ねる）
#       Shipping/<library>.dll  Shipping の実行 DLL（編集器の分を含まない）
#       Debug/                  Debug の DLL（清単からは指さない）
#       <内容のフォルダ>        元の清単の contentDirectory と PACKAGE_DIRECTORIES の写し
#
# MANIFEST は元の清単：id、name、version、dependencies、assetTypes、contentDirectory だけを書く。
# DLL の路径（runtimeLibrary など）は構築が決めるので、元の清単には書かない。
# HIKARI_MODULE_PACKAGE_ROOT の既定は <CMAKE_SOURCE_DIR>/Library/Plugins。
#
# SINGLE_LIBRARY：編集器の部分も同じ DLL に入れる（編集器の画面が実行側の状態を直接読むモジュール向け）。
# Shipping の DLL に編集器の分を入れないため、Shipping は Shipping だけの構築木で編訳する。
function(hikari_add_module)
    set(options SINGLE_LIBRARY)
    set(one_value_arguments MANIFEST SOURCE_DIR EDITOR_SOURCE_DIR LIBRARY_NAME)
    set(multi_value_arguments SOURCES EDITOR_SOURCES PACKAGE_DIRECTORIES)
    cmake_parse_arguments(
        HIKARI_MODULE
        "${options}"
        "${one_value_arguments}"
        "${multi_value_arguments}"
        ${ARGN})

    if(NOT HIKARI_MODULE_MANIFEST)
        message(FATAL_ERROR "hikari_add_module requires MANIFEST.")
    endif()
    cmake_path(
        ABSOLUTE_PATH HIKARI_MODULE_MANIFEST
        BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        NORMALIZE
        OUTPUT_VARIABLE _manifest_path)
    if(NOT EXISTS "${_manifest_path}")
        message(FATAL_ERROR "HIKARI Module MANIFEST does not exist: ${_manifest_path}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_manifest_path}")
    file(READ "${_manifest_path}" _manifest)
    string(JSON _id ERROR_VARIABLE _error GET "${_manifest}" id)
    if(_error OR _id STREQUAL "")
        message(FATAL_ERROR "HIKARI Module MANIFEST has no id: ${_manifest_path}")
    endif()
    foreach(_built IN ITEMS sdkVersion runtimeLibrary shippingRuntimeLibrary editorLibrary)
        string(JSON _value ERROR_VARIABLE _error GET "${_manifest}" "${_built}")
        if(NOT _error)
            message(
                FATAL_ERROR
                "HIKARI Module ${_id}: remove ${_built} from ${_manifest_path}. The build writes the SDK version "
                "and the library paths into the packaged manifest.")
        endif()
    endforeach()
    if(NOT HIKARI_VERSION)
        message(FATAL_ERROR "HIKARI Module ${_id}: call find_package(HIKARI CONFIG REQUIRED) before hikari_add_module.")
    endif()

    set(_library "${HIKARI_MODULE_LIBRARY_NAME}")
    if(NOT _library)
        string(REPLACE "." "_" _library "${_id}")
    endif()
    if(TARGET "${_library}")
        message(FATAL_ERROR "HIKARI Module ${_id}: target ${_library} is already declared.")
    endif()

    _hikari_resolve_module_sources(HIKARI_MODULE "Module ${_id}")
    _hikari_editor_configuration_available(_editor_configuration_available)
    set(_editor_enabled FALSE)
    if(HIKARI_GAME_PROJECT_EDITOR_AVAILABLE AND _editor_configuration_available AND HIKARI_MODULE_EDITOR_FILES)
        set(_editor_enabled TRUE)
    endif()
    if(HIKARI_MODULE_SINGLE_LIBRARY AND _editor_enabled AND "Shipping" IN_LIST CMAKE_CONFIGURATION_TYPES)
        message(
            FATAL_ERROR
            "HIKARI Module ${_id}: a SINGLE_LIBRARY module must build Shipping from a Shipping-only build tree, "
            "so that the Shipping library carries no Editor code.")
    endif()

    if(NOT HIKARI_MODULE_PACKAGE_ROOT)
        set(HIKARI_MODULE_PACKAGE_ROOT "${CMAKE_SOURCE_DIR}/Library/Plugins")
    endif()
    set(_package "${HIKARI_MODULE_PACKAGE_ROOT}/${_id}")
    set(_archive_directory "${CMAKE_BINARY_DIR}/lib")

    _hikari_sdk_adapter(_adapter "HIKARI_GameProjectModuleAdapter.cpp")
    add_library("${_library}" MODULE)
    target_sources("${_library}" PRIVATE ${HIKARI_MODULE_RUNTIME_SOURCES} "${_adapter}")
    _hikari_link_runtime_sdk("${_library}")
    if(HIKARI_MODULE_RUNTIME_ROOT)
        target_include_directories("${_library}" PRIVATE "${HIKARI_MODULE_RUNTIME_ROOT}")
    endif()
    _hikari_configure_module_target("${_library}")
    _hikari_set_module_output(
        "${_library}" "${_library}" "${_archive_directory}" "${_package}/Debug" "${_package}" "${_package}/Shipping")
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${HIKARI_MODULE_RUNTIME_SOURCES})
    source_group("HIKARI SDK" FILES "${_adapter}")

    # 清単の editorLibrary は、今の構築木が編集器を作れるかに関わらず同じにする
    # （EditorRelease と Shipping の構築木が同じ清単を書く）。
    set(_editor_library_file "")
    if(HIKARI_MODULE_EDITOR_FILES)
        if(HIKARI_MODULE_SINGLE_LIBRARY)
            set(_editor_target "${_library}")
            set(_editor_library_file "${_library}.dll")
        else()
            set(_editor_target "${_library}Editor")
            set(_editor_library_file "${_library}Editor.dll")
        endif()
    endif()
    if(_editor_enabled)
        _hikari_sdk_adapter(_editor_adapter "HIKARI_GameProjectEditorModuleAdapter.cpp")
        if(NOT HIKARI_MODULE_SINGLE_LIBRARY)
            add_library("${_editor_target}" MODULE)
            _hikari_configure_module_target("${_editor_target}")
            _hikari_set_module_output(
                "${_editor_target}" "${_editor_target}" "${_archive_directory}" "${_package}/Debug" "${_package}"
                "${_package}/Shipping")
            set_target_properties("${_editor_target}" PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD_SHIPPING TRUE)
        endif()
        target_sources("${_editor_target}" PRIVATE ${HIKARI_MODULE_EDITOR_FILES} "${_editor_adapter}")
        target_link_libraries("${_editor_target}" PRIVATE HIKARI::GameProjectEditorSdk)
        if(HIKARI_MODULE_EDITOR_ROOT)
            target_include_directories("${_editor_target}" PRIVATE "${HIKARI_MODULE_EDITOR_ROOT}")
        endif()
        source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${HIKARI_MODULE_EDITOR_FILES})
        source_group("HIKARI Editor SDK" FILES "${_editor_adapter}")
    endif()

    # 挿件包の清単。内容が変わった時だけ書く。
    # sdkVersion：引擎は major と minor が同じ SDK で作った DLL だけを読み込む。
    set(_packaged "${_manifest}")
    string(JSON _packaged SET "${_packaged}" sdkVersion "\"${HIKARI_VERSION}\"")
    string(JSON _packaged SET "${_packaged}" runtimeLibrary "\"${_library}.dll\"")
    string(JSON _packaged SET "${_packaged}" shippingRuntimeLibrary "\"Shipping/${_library}.dll\"")
    if(_editor_library_file)
        string(JSON _packaged SET "${_packaged}" editorLibrary "\"${_editor_library_file}\"")
    endif()
    set(_packaged_path "${_package}/${_id}.hmodule")
    set(_existing "")
    if(EXISTS "${_packaged_path}")
        file(READ "${_packaged_path}" _existing)
    endif()
    if(NOT _existing STREQUAL "${_packaged}\n")
        file(WRITE "${_packaged_path}" "${_packaged}\n")
    endif()

    # 内容のフォルダは毎回の構築で鏡写しにする（コードが変わらなくても内容の変更が包に入る）。
    set(_package_directories ${HIKARI_MODULE_PACKAGE_DIRECTORIES})
    string(JSON _content_directory ERROR_VARIABLE _error GET "${_manifest}" contentDirectory)
    if(NOT _error AND NOT _content_directory STREQUAL "")
        list(APPEND _package_directories "${_content_directory}")
    endif()
    if(_package_directories)
        list(REMOVE_DUPLICATES _package_directories)
        set(_sync_commands "")
        foreach(_directory IN LISTS _package_directories)
            if(IS_ABSOLUTE "${_directory}" OR NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_directory}")
                message(
                    FATAL_ERROR
                    "HIKARI Module ${_id}: package directory must be a folder next to CMakeLists.txt: "
                    "${_directory}")
            endif()
            list(
                APPEND _sync_commands
                COMMAND "${CMAKE_COMMAND}" "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/${_directory}"
                        "-DDESTINATION=${_package}/${_directory}" -P
                        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/SyncDirectory.cmake")
        endforeach()
        add_custom_target("${_library}Content" ALL ${_sync_commands} VERBATIM)
    endif()

    message(STATUS "HIKARI Module: ${_id} -> ${_package}")
endfunction()
