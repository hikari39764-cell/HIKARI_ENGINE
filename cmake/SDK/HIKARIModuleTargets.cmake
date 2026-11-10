include_guard(GLOBAL)

# GameProject（HIKARIGameProject.cmake）と外部モジュール（HIKARIModule.cmake）の構築が共有する部分：
# ソースの解決と、モジュール DLL の目標の設定。

set(_HIKARI_SOURCE_PATTERNS "*.c" "*.cc" "*.cpp" "*.cxx" "*.h" "*.hh" "*.hpp" "*.hxx" "*.inl")

# 明示の一覧（ARGN）と専用フォルダ（source_dir）からソースを集める。フォルダは CMakeLists のあるフォルダの
# 内側にある専用のフォルダに限る。out_root はフォルダの実路径（指定が無ければ空）。
function(_hikari_collect_sources out_sources out_root label source_dir)
    set(_sources "")
    foreach(_source IN LISTS ARGN)
        if(_source MATCHES "^\\$<")
            list(APPEND _sources "${_source}")
        else()
            cmake_path(
                ABSOLUTE_PATH _source
                BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                NORMALIZE
                OUTPUT_VARIABLE _absolute)
            list(APPEND _sources "${_absolute}")
        endif()
    endforeach()

    set(_root "")
    if(source_dir)
        get_filename_component(_root "${source_dir}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if(NOT IS_DIRECTORY "${_root}")
            message(FATAL_ERROR "HIKARI ${label} does not exist: ${_root}")
        endif()
        file(REAL_PATH "${_root}" _root)
        file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" _manifest_root)
        cmake_path(IS_PREFIX _manifest_root "${_root}" NORMALIZE _inside_manifest_root)
        if(NOT _inside_manifest_root OR _root STREQUAL _manifest_root)
            message(
                FATAL_ERROR
                "HIKARI ${label} must identify a dedicated directory inside the folder of its CMakeLists.txt.")
        endif()
        set(_globs "")
        foreach(_pattern IN LISTS _HIKARI_SOURCE_PATTERNS)
            list(APPEND _globs "${_root}/${_pattern}")
        endforeach()
        file(GLOB_RECURSE _discovered CONFIGURE_DEPENDS LIST_DIRECTORIES false ${_globs})
        list(APPEND _sources ${_discovered})
    endif()
    if(_sources)
        list(REMOVE_DUPLICATES _sources)
    endif()
    set(${out_sources} "${_sources}" PARENT_SCOPE)
    set(${out_root} "${_root}" PARENT_SCOPE)
endfunction()

# 呼び出し側の <prefix>_SOURCE_DIR / _SOURCES / _EDITOR_SOURCE_DIR / _EDITOR_SOURCES を読み、実行と編集器の
# ソースへ分ける。編集器のファイルは実行側の広い glob に入っていても編集器だけに属する。
# 結果は <prefix>_RUNTIME_SOURCES / _RUNTIME_ROOT / _EDITOR_FILES / _EDITOR_ROOT。
function(_hikari_resolve_module_sources prefix label)
    _hikari_collect_sources(
        _runtime_sources _runtime_root "${label} SOURCE_DIR" "${${prefix}_SOURCE_DIR}" ${${prefix}_SOURCES})
    if(NOT _runtime_sources)
        message(FATAL_ERROR "HIKARI ${label} requires SOURCE_DIR or SOURCES.")
    endif()
    _hikari_collect_sources(
        _editor_sources _editor_root "${label} EDITOR_SOURCE_DIR" "${${prefix}_EDITOR_SOURCE_DIR}"
        ${${prefix}_EDITOR_SOURCES})

    if(_runtime_root AND _editor_root)
        cmake_path(IS_PREFIX _editor_root "${_runtime_root}" NORMALIZE _runtime_is_below_editor)
        if(_runtime_is_below_editor OR _runtime_root STREQUAL _editor_root)
            message(
                FATAL_ERROR
                "HIKARI ${label} SOURCE_DIR and EDITOR_SOURCE_DIR must not overlap with EDITOR_SOURCE_DIR "
                "containing SOURCE_DIR. SOURCE_DIR may contain EDITOR_SOURCE_DIR; Editor files are excluded "
                "from Runtime sources.")
        endif()
    endif()
    if(_editor_sources)
        list(REMOVE_ITEM _runtime_sources ${_editor_sources})
    endif()
    if(NOT _runtime_sources)
        message(
            FATAL_ERROR
            "HIKARI ${label} Runtime sources cannot be empty after excluding EDITOR_SOURCE_DIR and "
            "EDITOR_SOURCES.")
    endif()

    set(${prefix}_RUNTIME_SOURCES "${_runtime_sources}" PARENT_SCOPE)
    set(${prefix}_RUNTIME_ROOT "${_runtime_root}" PARENT_SCOPE)
    set(${prefix}_EDITOR_FILES "${_editor_sources}" PARENT_SCOPE)
    set(${prefix}_EDITOR_ROOT "${_editor_root}" PARENT_SCOPE)
endfunction()

# 今の構築木が Shipping 以外の構成を持つか（編集器の部分を作れるか）。
function(_hikari_editor_configuration_available out_available)
    set(_available TRUE)
    if(CMAKE_CONFIGURATION_TYPES)
        set(_available FALSE)
        foreach(_configuration IN LISTS CMAKE_CONFIGURATION_TYPES)
            if(NOT _configuration STREQUAL "Shipping")
                set(_available TRUE)
            endif()
        endforeach()
    elseif(CMAKE_BUILD_TYPE STREQUAL "Shipping")
        set(_available FALSE)
    endif()
    set(${out_available} "${_available}" PARENT_SCOPE)
endfunction()

# SDK に同梱の入口アダプター（Sdk/src）の路径。
function(_hikari_sdk_adapter out_path filename)
    get_filename_component(_sdk_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../.." ABSOLUTE)
    set(_path "${_sdk_root}/src/${filename}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "HIKARI SDK module adapter is missing: ${_path}")
    endif()
    set(${out_path} "${_path}" PARENT_SCOPE)
endfunction()

# 実行側の SDK へ繋ぐ。Shipping の SDK では host が今のプロセスなので、host の import を遅延読み込みにする。
function(_hikari_link_runtime_sdk target)
    target_link_libraries("${target}" PRIVATE HIKARI::GameProjectSdk)
    if(MSVC AND NOT HIKARI_GAME_PROJECT_EDITOR_AVAILABLE)
        get_target_property(_host_path HIKARI::GameProjectHost IMPORTED_LOCATION)
        get_filename_component(_host_filename "${_host_path}" NAME)
        target_compile_definitions(
            "${target}" PRIVATE "HIKARI_GAME_PROJECT_HOST_IMPORT_NAME=\"${_host_filename}\"")
        target_link_options("${target}" PRIVATE "/DELAYLOAD:${_host_filename}")
        target_link_libraries("${target}" PRIVATE delayimp)
    endif()
endfunction()

# モジュール DLL の共通の編訳設定（言語規格、定義、CRT、警告）。
function(_hikari_configure_module_target target)
    target_compile_features("${target}" PRIVATE cxx_std_20)
    target_compile_definitions(
        "${target}"
        PRIVATE
            UNICODE
            _UNICODE
            _WINDOWS
            _CRT_SECURE_NO_WARNINGS
            _CRT_NONSTDC_NO_DEPRECATE
            $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
            $<$<CONFIG:Shipping>:HIKARI_SHIPPING>)
    set_property(
        TARGET "${target}"
        PROPERTY
            MSVC_RUNTIME_LIBRARY
            "$<$<CONFIG:Debug>:MultiThreadedDebugDLL>$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:MultiThreaded>")
    if(MSVC)
        target_compile_options(
            "${target}"
            PRIVATE
                /permissive-
                /sdl
                /MP
                "$<$<CONFIG:Debug>:/W3>"
                "$<$<CONFIG:Debug>:/WX>"
                "$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:/W4>"
                "$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:/O2>")
    endif()
endfunction()

# DLL の名前と、構成ごとの出力先。
function(_hikari_set_module_output target output_name archive_directory debug_directory editor_release_directory
         shipping_directory)
    set_target_properties(
        "${target}"
        PROPERTIES
            PREFIX ""
            OUTPUT_NAME "${output_name}"
            RUNTIME_OUTPUT_DIRECTORY "${editor_release_directory}"
            LIBRARY_OUTPUT_DIRECTORY "${editor_release_directory}"
            ARCHIVE_OUTPUT_DIRECTORY "${archive_directory}"
            PDB_OUTPUT_DIRECTORY "${editor_release_directory}")
    foreach(_entry IN ITEMS "DEBUG|${debug_directory}" "EDITORRELEASE|${editor_release_directory}"
                            "SHIPPING|${shipping_directory}")
        string(REPLACE "|" ";" _entry "${_entry}")
        list(GET _entry 0 _configuration)
        list(GET _entry 1 _directory)
        set_target_properties(
            "${target}"
            PROPERTIES
                "RUNTIME_OUTPUT_DIRECTORY_${_configuration}" "${_directory}"
                "LIBRARY_OUTPUT_DIRECTORY_${_configuration}" "${_directory}"
                "ARCHIVE_OUTPUT_DIRECTORY_${_configuration}" "${archive_directory}"
                "PDB_OUTPUT_DIRECTORY_${_configuration}" "${_directory}")
    endforeach()
endfunction()
