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
# 公開ヘッダーを唯一のバージョン定義とする。
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
    COMPATIBILITY SameMajorVersion)

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
        "${HIKARI_SDK_GENERATED_DIRECTORY}/HIKARIConfigVersion.cmake")
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
            "${_sdk_root}/Tools"
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
