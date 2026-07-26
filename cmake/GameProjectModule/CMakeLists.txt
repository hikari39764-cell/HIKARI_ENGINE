cmake_minimum_required(VERSION 3.28)

project(HIKARI_GameProjectModule LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(
        FATAL_ERROR
        "HIKARI GameProject modules require x64 Visual Studio 2022.")
endif()
foreach(required_variable
        IN ITEMS
            HIKARI_ENGINE_ROOT
            HIKARI_ENGINE_BUILD_ROOT
            HIKARI_GAME_PROJECT_MANIFEST
            HIKARI_GAME_PROJECT_OUTPUT_ROOT
            HIKARI_PRODUCT)
    if(NOT DEFINED "${required_variable}" OR
       "${${required_variable}}" STREQUAL "")
        message(
            FATAL_ERROR
            "${required_variable} is required.")
    endif()
endforeach()

cmake_path(
    ABSOLUTE_PATH
    HIKARI_ENGINE_ROOT
    NORMALIZE
    OUTPUT_VARIABLE HIKARI_SOURCE_ROOT)
cmake_path(
    ABSOLUTE_PATH
    HIKARI_ENGINE_BUILD_ROOT
    NORMALIZE
    OUTPUT_VARIABLE HIKARI_ENGINE_BUILD_ROOT)
cmake_path(
    ABSOLUTE_PATH
    HIKARI_GAME_PROJECT_MANIFEST
    NORMALIZE
    OUTPUT_VARIABLE HIKARI_GAME_PROJECT_MANIFEST)
cmake_path(
    ABSOLUTE_PATH
    HIKARI_GAME_PROJECT_OUTPUT_ROOT
    NORMALIZE
    OUTPUT_VARIABLE HIKARI_GAME_PROJECT_OUTPUT_ROOT)

list(PREPEND CMAKE_MODULE_PATH "${HIKARI_SOURCE_ROOT}/cmake")
include(HikariCompiler)
include(HikariGameProject)

hikari_load_game_project("${HIKARI_GAME_PROJECT_MANIFEST}")
message(
    STATUS
    "HIKARI standalone GameProject module: "
    "${HIKARI_SELECTED_GAME_PROJECT_NAME}")

set(
    HIKARI_MODULE_SDK_INCLUDE_DIRECTORIES
    "${HIKARI_SOURCE_ROOT}"
    "${HIKARI_SOURCE_ROOT}/HIKARI")
foreach(sdk_include_candidate
        IN ITEMS
            "${HIKARI_SOURCE_ROOT}/ThirdParty/nlohmann"
            "${HIKARI_SOURCE_ROOT}/ThirdParty/spine/include"
            "${HIKARI_SOURCE_ROOT}/ThirdParty/meshoptimizer/src"
            "${HIKARI_SOURCE_ROOT}/ThirdParty/DirectXTex/Common"
            "${HIKARI_SOURCE_ROOT}/ThirdParty/Effekseer/include"
            "${HIKARI_SOURCE_ROOT}/ThirdParty/Effekseer/include/Effekseer"
            "${HIKARI_SOURCE_ROOT}/ThirdParty/Effekseer/include/EffekseerRendererDX12"
            "${HIKARI_SOURCE_ROOT}/ThirdParty/Effekseer/include/LLGI"
            "${HIKARI_SOURCE_ROOT}/ThirdParty/CoACD/include"
            "${HIKARI_SOURCE_ROOT}/ThirdParty/Streamline/sdk/include"
            "${HIKARI_SOURCE_ROOT}/ThirdParty/WinPixEventRuntime/Include/WinPixEventRuntime"
            "${HIKARI_SOURCE_ROOT}/vcpkg_installed/x64-windows/include")
    if(IS_DIRECTORY "${sdk_include_candidate}")
        list(
            APPEND
            HIKARI_MODULE_SDK_INCLUDE_DIRECTORIES
            "${sdk_include_candidate}")
    endif()
endforeach()

add_library(HIKARI_HostSdk SHARED IMPORTED GLOBAL)
set_target_properties(
    HIKARI_HostSdk
    PROPERTIES
        IMPORTED_IMPLIB
            "${HIKARI_ENGINE_BUILD_ROOT}/lib/HIKARI_UpdateTo3D.lib"
        IMPORTED_LOCATION
            "${HIKARI_ENGINE_BUILD_ROOT}/bin/HIKARI_UpdateTo3D.exe"
        INTERFACE_INCLUDE_DIRECTORIES
            "${HIKARI_MODULE_SDK_INCLUDE_DIRECTORIES}")

add_library(HIKARI_GameProjectModule MODULE)
target_sources(
    HIKARI_GameProjectModule
    PRIVATE
        ${HIKARI_SELECTED_GAME_PROJECT_SOURCES}
        "${HIKARI_SOURCE_ROOT}/cmake/GameProjectModule/HIKARI_GameProjectModuleAdapter.cpp")
target_link_libraries(
    HIKARI_GameProjectModule
    PRIVATE
        HIKARI_HostSdk)
target_compile_definitions(
    HIKARI_GameProjectModule
    PRIVATE
        HIKARI_GAME_PROJECT_SDK_IMPORTS=1)
hikari_configure_engine_target(HIKARI_GameProjectModule)
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
    TREE "${HIKARI_SELECTED_GAME_PROJECT_ROOT}"
    FILES
        ${HIKARI_SELECTED_GAME_PROJECT_SOURCES})
source_group(
    "HIKARI Module Adapter"
    FILES
        "${HIKARI_SOURCE_ROOT}/cmake/GameProjectModule/HIKARI_GameProjectModuleAdapter.cpp")

install(
    TARGETS HIKARI_GameProjectModule
    RUNTIME DESTINATION "."
    LIBRARY DESTINATION ".")
