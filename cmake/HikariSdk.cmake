include_guard(GLOBAL)

include(CMakePackageConfigHelpers)

# 公開ヘッダーは include/HIKARI 以下のすべて。手書きの一覧は追加漏れで外部ビルドを壊すため持たない。
file(GLOB HIKARI_GAME_PROJECT_SDK_HEADERS CONFIGURE_DEPENDS
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/*.h"
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Runtime/*.h")

# モジュールへ公開する引擎の共用の核（HIKARI::CoreShared）の一覧。
include("${HIKARI_SOURCE_ROOT}/cmake/SDK/CoreSharedFiles.cmake")
set(HIKARI_SDK_CMAKE_MODULES
    "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARIGameProject.cmake"
    "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARIModule.cmake"
    "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARIModuleTargets.cmake"
    "${HIKARI_SOURCE_ROOT}/cmake/SyncDirectory.cmake")
# HIKARIConfig.cmake が INTERFACE_SOURCES に使う路径（SDK の根は取り込み時に決まる）。
set(HIKARI_SDK_CORE_SHARED_SOURCE_PATHS "")
foreach(_source IN LISTS HIKARI_SDK_CORE_SHARED_SOURCES)
    list(APPEND HIKARI_SDK_CORE_SHARED_SOURCE_PATHS "\${_HIKARI_SDK_ROOT}/shared/src/${_source}")
endforeach()

set(
    HIKARI_SDK_GENERATED_DIRECTORY
    "${CMAKE_BINARY_DIR}/generated/HIKARISdk")
file(MAKE_DIRECTORY "${HIKARI_SDK_GENERATED_DIRECTORY}")

if(HIKARI_WITH_EDITOR_BUILD)
    get_filename_component(
        HIKARI_CMAKE_MODULE_DIRECTORY_NAME
        "${CMAKE_ROOT}"
        NAME)
    get_filename_component(
        _hikari_cmake_bin_directory
        "${CMAKE_COMMAND}"
        DIRECTORY)
    get_filename_component(
        HIKARI_CMAKE_DISTRIBUTION_ROOT
        "${_hikari_cmake_bin_directory}"
        DIRECTORY)
    set(
        HIKARI_CMAKE_LICENSE_FILE
        "${HIKARI_CMAKE_DISTRIBUTION_ROOT}/doc/cmake/LICENSE.rst")
    if(NOT EXISTS "${CMAKE_COMMAND}" OR
       NOT IS_DIRECTORY "${CMAKE_ROOT}" OR
       NOT EXISTS "${HIKARI_CMAKE_LICENSE_FILE}")
        message(
            FATAL_ERROR
            "The selected CMake installation cannot be redistributed by "
            "the HIKARI Editor SDK. Expected cmake.exe, CMAKE_ROOT, and "
            "doc/cmake/LICENSE.rst under ${HIKARI_CMAKE_DISTRIBUTION_ROOT}.")
    endif()

    file(SHA256 "${CMAKE_COMMAND}" HIKARI_CMAKE_EXECUTABLE_SHA256)
    set(
        HIKARI_CMAKE_TOOL_MANIFEST
        "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARI_CMakeTool.json")
    file(
        WRITE
        "${HIKARI_CMAKE_TOOL_MANIFEST}"
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"name\": \"CMake\",\n"
        "  \"version\": \"${CMAKE_VERSION}\",\n"
        "  \"executableSha256\": \"${HIKARI_CMAKE_EXECUTABLE_SHA256}\",\n"
        "  \"moduleDirectory\": \"${HIKARI_CMAKE_MODULE_DIRECTORY_NAME}\",\n"
        "  \"license\": \"licenses/CMake-LICENSE.rst\"\n"
        "}\n")
endif()

set(HIKARI_GAME_PROJECT_HOST_FILENAME "HIKARI_Engine.exe")
if(NOT HIKARI_WITH_EDITOR_BUILD)
    set(
        HIKARI_GAME_PROJECT_HOST_FILENAME
        "${HIKARI_GAME_HOST_OUTPUT_NAME}.exe")
endif()

set(_hikari_host_binding "namedHost")
if(NOT HIKARI_WITH_EDITOR_BUILD)
    set(_hikari_host_binding "currentProcess")
endif()
file(WRITE "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARI_GameHost.json"
    "{\n  \"schemaVersion\": 1,\n  \"executable\": \"${HIKARI_GAME_PROJECT_HOST_FILENAME}\",\n  \"sdkBinding\": \"${_hikari_host_binding}\"\n}\n")

configure_package_config_file(
    "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARIConfig.cmake.in"
    "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfig.cmake"
    INSTALL_DESTINATION "Sdk/lib/cmake/HIKARI"
    NO_SET_AND_CHECK_MACRO)
# 公開ヘッダーを唯一のバージョン定義とする。版は配布物の名前で、互換の判定には使わない（界面の指紋で決める）：
# 古い版を書いた項目もそのまま通し、今より新しい版を求める項目だけを断る。
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/GameProjectSdk.h")
file(READ "${HIKARI_SOURCE_ROOT}/include/HIKARI/Project/GameProjectSdk.h" _hikari_sdk_header)
foreach(_part Major Minor Patch)
    string(REGEX MATCH "kVersion${_part} = ([0-9]+)u" _matched "${_hikari_sdk_header}")
    if(NOT _matched)
        message(FATAL_ERROR "Missing public SDK version: ${_part}")
    endif()
    set(_hikari_sdk_${_part} "${CMAKE_MATCH_1}")
endforeach()
write_basic_package_version_file(
    "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfigVersion.cmake"
    VERSION "${_hikari_sdk_Major}.${_hikari_sdk_Minor}.${_hikari_sdk_Patch}"
    COMPATIBILITY AnyNewerVersion)

# SDK の界面の指紋（HIKARI/Project/Public/Project/HIKARI_SdkInterface.h）。SDK が配る編訳対象（公開ヘッダー、
# 共用の核、入口アダプター）を道具（Tools/SdkInterface）で指紋の表にする：SDK に置く JSON と、host に埋め込む C++。
# 同じ道具を SDK に入れ、モジュールの構築が「DLL が読んだ SDK のファイル」を記録するのに使う。
# hikari_project を定義した後に呼ぶ。
function(hikari_configure_sdk_interface)
    file(GLOB_RECURSE _headers CONFIGURE_DEPENDS LIST_DIRECTORIES false
        RELATIVE "${HIKARI_SOURCE_ROOT}/include" "${HIKARI_SOURCE_ROOT}/include/HIKARI/*")
    # 「SDK の中の相対路径|元のファイル」。SDK への写し（hikari_configure_game_project_sdk）と同じ対応。
    set(_entries "")
    foreach(_file IN LISTS _headers)
        list(APPEND _entries "include/${_file}|${HIKARI_SOURCE_ROOT}/include/${_file}")
    endforeach()
    foreach(_file IN LISTS HIKARI_SDK_CORE_SHARED_HEADERS)
        list(APPEND _entries "shared/include/${_file}|${HIKARI_SOURCE_ROOT}/HIKARI/Core/Public/${_file}")
    endforeach()
    foreach(_file IN LISTS HIKARI_SDK_CORE_SHARED_SOURCES)
        list(APPEND _entries "shared/src/${_file}|${HIKARI_SOURCE_ROOT}/HIKARI/Core/Private/${_file}")
    endforeach()
    list(APPEND _entries
        "third_party/nlohmann/json.hpp|${HIKARI_SOURCE_ROOT}/ThirdParty/nlohmann/json.hpp"
        "src/HIKARI_GameProjectModuleAdapter.cpp|${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARI_GameProjectModuleAdapter.cpp")
    if(HIKARI_WITH_EDITOR_BUILD)
        list(APPEND _entries
            "src/HIKARI_GameProjectEditorModuleAdapter.cpp|${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARI_GameProjectEditorModuleAdapter.cpp")
    endif()
    set(_inputs "")
    foreach(_entry IN LISTS _entries)
        string(REGEX REPLACE "^[^|]*\\|" "" _source "${_entry}")
        list(APPEND _inputs "${_source}")
    endforeach()
    list(JOIN _entries "\n" _list_text)
    set(_list "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARI_SdkInterfaceFiles.txt")
    set(_existing "")
    if(EXISTS "${_list}")
        file(READ "${_list}" _existing)
    endif()
    if(NOT _existing STREQUAL "${_list_text}\n")
        file(WRITE "${_list}" "${_list_text}\n")
    endif()

    add_executable(HIKARI_SdkInterfaceTool "${HIKARI_SOURCE_ROOT}/Tools/SdkInterface/HIKARI_SdkInterfaceMain.cpp")
    target_link_libraries(HIKARI_SdkInterfaceTool PRIVATE HIKARI::Nlohmann Bcrypt user32)
    hikari_configure_engine_target(HIKARI_SdkInterfaceTool)
    set_target_properties(HIKARI_SdkInterfaceTool PROPERTIES
        OUTPUT_NAME "HIKARI_SdkInterface"
        FOLDER "Engine/Tools"
        HIKARI_WHOLE_PROGRAM_OPTIMIZATION OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
    foreach(configuration IN ITEMS Debug EditorRelease Shipping)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(HIKARI_SdkInterfaceTool PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/tools")
    endforeach()

    # 道具は中身が同じ出力を書き直さないので、注釈だけの変更では表を使う側が編訳し直されない。
    # 構築の依存は、毎回書く印のファイルで持つ。
    set(_json "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARI_SdkInterface.json")
    set(_table "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARI_SdkInterfaceTable.cpp")
    set(_stamp "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARI_SdkInterface.stamp")
    add_custom_command(
        OUTPUT "${_stamp}"
        BYPRODUCTS "${_json}" "${_table}"
        COMMAND HIKARI_SdkInterfaceTool table "${_list}" "${_json}" "${_table}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
        DEPENDS HIKARI_SdkInterfaceTool "${_list}" ${_inputs}
        COMMENT "HIKARI SDK interface fingerprints"
        VERBATIM)
    target_sources(hikari_project PRIVATE "${_table}" "${_stamp}")
    set_source_files_properties("${_stamp}" PROPERTIES HEADER_FILE_ONLY TRUE)
    set(HIKARI_SDK_INTERFACE_JSON "${_json}" PARENT_SCOPE)
endfunction()

function(hikari_configure_game_project_sdk host_target)
    set(_sdk_root "$<TARGET_FILE_DIR:${host_target}>/Sdk")
    if(MSVC)
        target_link_options(
            "${host_target}"
            PRIVATE
                "/WHOLEARCHIVE:$<TARGET_FILE:hikari_runtime_game_project_sdk>")
    endif()
    list(TRANSFORM HIKARI_SDK_CORE_SHARED_HEADERS PREPEND "${HIKARI_SOURCE_ROOT}/HIKARI/Core/Public/"
         OUTPUT_VARIABLE _core_shared_headers)
    list(TRANSFORM HIKARI_SDK_CORE_SHARED_SOURCES PREPEND "${HIKARI_SOURCE_ROOT}/HIKARI/Core/Private/"
         OUTPUT_VARIABLE _core_shared_sources)
    set(
        _sdk_inputs
        ${HIKARI_GAME_PROJECT_SDK_HEADERS}
        ${_core_shared_headers}
        ${_core_shared_sources}
        ${HIKARI_SDK_CMAKE_MODULES}
        "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARIConfig.cmake.in"
        "${HIKARI_SOURCE_ROOT}/cmake/SDK/CoreSharedFiles.cmake"
        "${HIKARI_SOURCE_ROOT}/cmake/SDK/StageCoreShared.cmake"
        "${HIKARI_SOURCE_ROOT}/ThirdParty/nlohmann/json.hpp"
        "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARI_GameProjectModuleAdapter.cpp"
        "${HIKARI_SOURCE_ROOT}/Tools/Build/Build-HikariProject.ps1"
        "${HIKARI_SOURCE_ROOT}/Tools/Build/HikariProjectPublication.ps1"
        "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfig.cmake"
        "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARI_GameHost.json"
        "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfigVersion.cmake"
        "${HIKARI_SDK_INTERFACE_JSON}"
        "$<TARGET_FILE:HIKARI_SdkInterfaceTool>")
    add_dependencies("${host_target}" HIKARI_SdkInterfaceTool)
    if(HIKARI_WITH_EDITOR_BUILD)
        list(
            APPEND
            _sdk_inputs
            "${CMAKE_COMMAND}"
            "${HIKARI_CMAKE_LICENSE_FILE}"
            "${HIKARI_CMAKE_TOOL_MANIFEST}"
            "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARI_GameProjectEditorModuleAdapter.cpp")
    endif()
    set_property(
        TARGET "${host_target}"
        APPEND
        PROPERTY LINK_DEPENDS ${_sdk_inputs})

    add_custom_command(
        TARGET "${host_target}"
        POST_BUILD
        COMMAND
            "${CMAKE_COMMAND}" -E make_directory
            "${_sdk_root}/include"
            "${_sdk_root}/lib/cmake/HIKARI"
            "${_sdk_root}/src"
            "${_sdk_root}/Tools"
            "${_sdk_root}/third_party/nlohmann"
        COMMAND
            "${CMAKE_COMMAND}"
            "-DSOURCE=${HIKARI_SOURCE_ROOT}/include/HIKARI"
            "-DDESTINATION=${_sdk_root}/include/HIKARI"
            -P "${HIKARI_SOURCE_ROOT}/cmake/SyncDirectory.cmake"
        COMMAND
            "${CMAKE_COMMAND}"
            "-DCORE_ROOT=${HIKARI_SOURCE_ROOT}/HIKARI/Core"
            "-DDESTINATION=${_sdk_root}/shared"
            -P "${HIKARI_SOURCE_ROOT}/cmake/SDK/StageCoreShared.cmake"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${HIKARI_SOURCE_ROOT}/ThirdParty/nlohmann/json.hpp"
            "${_sdk_root}/third_party/nlohmann/json.hpp"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_LINKER_FILE:${host_target}>"
            "${_sdk_root}/lib/HIKARI_Engine.lib"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfig.cmake"
            "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfigVersion.cmake"
            "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARI_GameHost.json"
            ${HIKARI_SDK_CMAKE_MODULES}
            "${_sdk_root}/lib/cmake/HIKARI"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARI_GameProjectModuleAdapter.cpp"
            "${_sdk_root}/src/HIKARI_GameProjectModuleAdapter.cpp"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${HIKARI_SOURCE_ROOT}/Tools/Build/Build-HikariProject.ps1"
            "${HIKARI_SOURCE_ROOT}/Tools/Build/HikariProjectPublication.ps1"
            "$<TARGET_FILE:HIKARI_SdkInterfaceTool>"
            "${_sdk_root}/Tools"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${HIKARI_SDK_INTERFACE_JSON}"
            "${_sdk_root}/HIKARI_SdkInterface.json"
        VERBATIM)

    if(HIKARI_WITH_EDITOR_BUILD)
        add_custom_command(
            TARGET "${host_target}"
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}" -E make_directory
                "${_sdk_root}/Tools/CMake/bin"
                "${_sdk_root}/Tools/CMake/share/${HIKARI_CMAKE_MODULE_DIRECTORY_NAME}"
                "${_sdk_root}/Tools/CMake/licenses"
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${HIKARI_SOURCE_ROOT}/Tools/Build/Invoke-HikariProjectCodeWorker.ps1"
                "${_sdk_root}/Tools/Invoke-HikariProjectCodeWorker.ps1"
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_COMMAND}"
                "${_sdk_root}/Tools/CMake/bin/cmake.exe"
            COMMAND
                "${CMAKE_COMMAND}" -E copy_directory_if_different
                "${CMAKE_ROOT}"
                "${_sdk_root}/Tools/CMake/share/${HIKARI_CMAKE_MODULE_DIRECTORY_NAME}"
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${HIKARI_CMAKE_LICENSE_FILE}"
                "${_sdk_root}/Tools/CMake/licenses/CMake-LICENSE.rst"
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${HIKARI_CMAKE_TOOL_MANIFEST}"
                "${_sdk_root}/Tools/CMake/HIKARI_CMakeTool.json"
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARI_GameProjectEditorModuleAdapter.cpp"
                "${_sdk_root}/src/HIKARI_GameProjectEditorModuleAdapter.cpp"
            VERBATIM)
    endif()

    install(
        DIRECTORY "${HIKARI_SOURCE_ROOT}/include/HIKARI"
        DESTINATION "${HIKARI_INSTALL_RUNTIME_PREFIX}Sdk/include")
    # The upstream single header carries its MIT license. HIKARI::CoreShared reads JSON with it in every
    # configuration; Editor project tools use the same bundled parser.
    install(FILES "${HIKARI_SOURCE_ROOT}/ThirdParty/nlohmann/json.hpp"
        DESTINATION "${HIKARI_INSTALL_RUNTIME_PREFIX}Sdk/third_party/nlohmann")
    foreach(_header IN LISTS HIKARI_SDK_CORE_SHARED_HEADERS)
        get_filename_component(_directory "${_header}" DIRECTORY)
        install(FILES "${HIKARI_SOURCE_ROOT}/HIKARI/Core/Public/${_header}"
            DESTINATION "${HIKARI_INSTALL_RUNTIME_PREFIX}Sdk/shared/include/${_directory}")
    endforeach()
    foreach(_source IN LISTS HIKARI_SDK_CORE_SHARED_SOURCES)
        get_filename_component(_directory "${_source}" DIRECTORY)
        install(FILES "${HIKARI_SOURCE_ROOT}/HIKARI/Core/Private/${_source}"
            DESTINATION "${HIKARI_INSTALL_RUNTIME_PREFIX}Sdk/shared/src/${_directory}")
    endforeach()
    if(HIKARI_WITH_EDITOR_BUILD)
        install(FILES "${HIKARI_SOURCE_ROOT}/Tools/Build/Invoke-HikariProjectCodeWorker.ps1"
            DESTINATION "${HIKARI_INSTALL_RUNTIME_PREFIX}Sdk/Tools")
    endif()
    install(
        FILES
            "$<TARGET_LINKER_FILE:${host_target}>"
        DESTINATION "${HIKARI_INSTALL_RUNTIME_PREFIX}Sdk/lib"
        RENAME "HIKARI_Engine.lib")
    install(
        FILES
            "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfig.cmake"
            "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfigVersion.cmake"
            "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARI_GameHost.json"
            ${HIKARI_SDK_CMAKE_MODULES}
        DESTINATION "${HIKARI_INSTALL_RUNTIME_PREFIX}Sdk/lib/cmake/HIKARI")
    install(
        FILES
            "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARI_GameProjectModuleAdapter.cpp"
        DESTINATION "${HIKARI_INSTALL_RUNTIME_PREFIX}Sdk/src")
    if(HIKARI_WITH_EDITOR_BUILD)
        install(
            FILES
                "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARI_GameProjectEditorModuleAdapter.cpp"
            DESTINATION "${HIKARI_INSTALL_RUNTIME_PREFIX}Sdk/src")
    endif()
    install(
        FILES
            "${HIKARI_SOURCE_ROOT}/Tools/Build/Build-HikariProject.ps1"
            "${HIKARI_SOURCE_ROOT}/Tools/Build/HikariProjectPublication.ps1"
        DESTINATION "${HIKARI_INSTALL_RUNTIME_PREFIX}Sdk/Tools")
    install(
        PROGRAMS "$<TARGET_FILE:HIKARI_SdkInterfaceTool>"
        DESTINATION "${HIKARI_INSTALL_RUNTIME_PREFIX}Sdk/Tools")
    install(
        FILES "${HIKARI_SDK_INTERFACE_JSON}"
        DESTINATION "${HIKARI_INSTALL_RUNTIME_PREFIX}Sdk")
    if(HIKARI_WITH_EDITOR_BUILD)
        install(
            PROGRAMS "${CMAKE_COMMAND}"
            DESTINATION "${HIKARI_INSTALL_RUNTIME_PREFIX}Sdk/Tools/CMake/bin")
        install(
            DIRECTORY "${CMAKE_ROOT}/"
            DESTINATION
                "${HIKARI_INSTALL_RUNTIME_PREFIX}Sdk/Tools/CMake/share/${HIKARI_CMAKE_MODULE_DIRECTORY_NAME}")
        install(
            FILES "${HIKARI_CMAKE_LICENSE_FILE}"
            DESTINATION "${HIKARI_INSTALL_RUNTIME_PREFIX}Sdk/Tools/CMake/licenses"
            RENAME "CMake-LICENSE.rst")
        install(
            FILES "${HIKARI_CMAKE_TOOL_MANIFEST}"
            DESTINATION "${HIKARI_INSTALL_RUNTIME_PREFIX}Sdk/Tools/CMake")
        install(
            DIRECTORY "${HIKARI_SOURCE_ROOT}/out/install/game-shipping/"
            DESTINATION "Runtime/Shipping"
            PATTERN "*.pdb" EXCLUDE
            PATTERN "HIKARI_GameProject.dll" EXCLUDE)
    endif()
endfunction()
