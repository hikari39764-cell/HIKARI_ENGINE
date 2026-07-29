include_guard(GLOBAL)

set(
    HIKARI_VCPKG_DYNAMIC_ROOT
    "${HIKARI_SOURCE_ROOT}/vcpkg_installed"
    CACHE PATH
    "Existing x64-windows vcpkg install root")
set(
    HIKARI_VCPKG_STATIC_ROOT
    "${HIKARI_SOURCE_ROOT}/vcpkg_installed_static"
    CACHE PATH
    "Existing x64-windows-static vcpkg install root")
set(
    HIKARI_STREAMLINE_SDK_ROOT
    "${HIKARI_SOURCE_ROOT}/ThirdParty/Streamline/sdk"
    CACHE PATH
    "NVIDIA Streamline SDK root")

function(hikari_require_dependency_file file_path description)
    if(NOT EXISTS "${file_path}")
        message(
            FATAL_ERROR
            "Missing ${description}: ${file_path}\n"
            "Restore the pinned HIKARI dependencies before configuring.")
    endif()
endfunction()

function(
    hikari_add_imported_static
    target_name
    debug_library
    release_library)
    hikari_require_dependency_file(
        "${debug_library}"
        "${target_name} Debug library")
    hikari_require_dependency_file(
        "${release_library}"
        "${target_name} release library")
    add_library("${target_name}" STATIC IMPORTED GLOBAL)
    set_target_properties(
        "${target_name}"
        PROPERTIES
            IMPORTED_CONFIGURATIONS "Debug;EditorRelease;Shipping"
            IMPORTED_LOCATION_DEBUG "${debug_library}"
            IMPORTED_LOCATION_EDITORRELEASE "${release_library}"
            IMPORTED_LOCATION_SHIPPING "${release_library}")
endfunction()

set(
    HIKARI_DIRECTXTEX_ROOT
    "${HIKARI_SOURCE_ROOT}/ThirdParty/DirectXTex")
set(BUILD_TOOLS OFF CACHE BOOL "Disable DirectXTex tools" FORCE)
set(BUILD_SAMPLE OFF CACHE BOOL "Disable DirectXTex sample" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build DirectXTex statically" FORCE)
set(BUILD_DX11 ON CACHE BOOL "Keep existing DirectXTex DX11 support" FORCE)
set(BUILD_DX12 ON CACHE BOOL "Keep existing DirectXTex DX12 support" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Disable third-party tests" FORCE)
add_subdirectory(
    "${HIKARI_DIRECTXTEX_ROOT}"
    "${CMAKE_BINARY_DIR}/ThirdParty/DirectXTex"
    EXCLUDE_FROM_ALL)
if(NOT TARGET DirectXTex)
    message(FATAL_ERROR "The vendored DirectXTex CMake target was not created.")
endif()
get_target_property(
    hikari_directxtex_compile_options
    DirectXTex
    COMPILE_OPTIONS)
if(hikari_directxtex_compile_options)
    list(FILTER
        hikari_directxtex_compile_options
        EXCLUDE
        REGEX "^/Wall$")
    set_property(
        TARGET DirectXTex
        PROPERTY
            COMPILE_OPTIONS
            "${hikari_directxtex_compile_options}")
endif()
hikari_configure_vendor_target(DirectXTex)
target_include_directories(
    DirectXTex
    SYSTEM INTERFACE
        "$<BUILD_INTERFACE:${HIKARI_DIRECTXTEX_ROOT}/Common>")
add_library(HIKARI::DirectXTex ALIAS DirectXTex)

set(
    HIKARI_EFFEKSEER_ROOT
    "${HIKARI_SOURCE_ROOT}/ThirdParty/Effekseer")
hikari_add_imported_static(
    HIKARI::Effekseer
    "${HIKARI_EFFEKSEER_ROOT}/Lib/Debug/Effekseerd.lib"
    "${HIKARI_EFFEKSEER_ROOT}/Lib/Release/Effekseer.lib")
hikari_add_imported_static(
    HIKARI::EffekseerRendererDX12
    "${HIKARI_EFFEKSEER_ROOT}/Lib/Debug/EffekseerRendererDX12d.lib"
    "${HIKARI_EFFEKSEER_ROOT}/Lib/Release/EffekseerRendererDX12.lib")
hikari_add_imported_static(
    HIKARI::LLGI
    "${HIKARI_EFFEKSEER_ROOT}/Lib/Debug/LLGId.lib"
    "${HIKARI_EFFEKSEER_ROOT}/Lib/Release/LLGI.lib")
foreach(
    effekseer_target
    IN ITEMS
        HIKARI::Effekseer
        HIKARI::EffekseerRendererDX12
        HIKARI::LLGI)
    target_include_directories(
        "${effekseer_target}"
        SYSTEM INTERFACE
            "${HIKARI_EFFEKSEER_ROOT}/include"
            "${HIKARI_EFFEKSEER_ROOT}/include/Effekseer"
            "${HIKARI_EFFEKSEER_ROOT}/include/EffekseerRendererDX12"
            "${HIKARI_EFFEKSEER_ROOT}/include/LLGI")
endforeach()

set(
    HIKARI_VCPKG_INCLUDE_DIR
    "${HIKARI_VCPKG_DYNAMIC_ROOT}/x64-windows/include")
hikari_require_dependency_file(
    "${HIKARI_VCPKG_INCLUDE_DIR}/Jolt/Jolt.h"
    "Jolt headers")

if(HIKARI_WITH_EDITOR_BUILD)
    hikari_require_dependency_file(
        "${HIKARI_VCPKG_INCLUDE_DIR}/assimp/Importer.hpp"
        "Assimp headers")
    set(
        HIKARI_ASSIMP_DEBUG_IMPLIB
        "${HIKARI_VCPKG_DYNAMIC_ROOT}/x64-windows/debug/lib/assimp-vc143-mtd.lib")
    set(
        HIKARI_ASSIMP_DEBUG_DLL
        "${HIKARI_VCPKG_DYNAMIC_ROOT}/x64-windows/debug/bin/assimp-vc143-mtd.dll")
    set(
        HIKARI_ASSIMP_RELEASE_IMPLIB
        "${HIKARI_VCPKG_DYNAMIC_ROOT}/x64-windows/lib/assimp-vc143-mt.lib")
    set(
        HIKARI_ASSIMP_RELEASE_DLL
        "${HIKARI_VCPKG_DYNAMIC_ROOT}/x64-windows/bin/assimp-vc143-mt.dll")
    foreach(
        assimp_file
        IN ITEMS
            "${HIKARI_ASSIMP_DEBUG_IMPLIB}"
            "${HIKARI_ASSIMP_DEBUG_DLL}"
            "${HIKARI_ASSIMP_RELEASE_IMPLIB}"
            "${HIKARI_ASSIMP_RELEASE_DLL}")
        hikari_require_dependency_file(
            "${assimp_file}"
            "Assimp artifact")
    endforeach()

    add_library(HIKARI::Assimp SHARED IMPORTED GLOBAL)
    set_target_properties(
        HIKARI::Assimp
        PROPERTIES
            IMPORTED_CONFIGURATIONS "Debug;EditorRelease;Shipping"
            IMPORTED_IMPLIB_DEBUG "${HIKARI_ASSIMP_DEBUG_IMPLIB}"
            IMPORTED_LOCATION_DEBUG "${HIKARI_ASSIMP_DEBUG_DLL}"
            IMPORTED_IMPLIB_EDITORRELEASE "${HIKARI_ASSIMP_RELEASE_IMPLIB}"
            IMPORTED_LOCATION_EDITORRELEASE "${HIKARI_ASSIMP_RELEASE_DLL}"
            IMPORTED_IMPLIB_SHIPPING "${HIKARI_ASSIMP_RELEASE_IMPLIB}"
            IMPORTED_LOCATION_SHIPPING "${HIKARI_ASSIMP_RELEASE_DLL}")
    target_include_directories(
        HIKARI::Assimp
        SYSTEM INTERFACE
            "${HIKARI_VCPKG_INCLUDE_DIR}")
endif()

hikari_add_imported_static(
    HIKARI::Jolt
    "${HIKARI_VCPKG_DYNAMIC_ROOT}/x64-windows/debug/lib/Jolt.lib"
    "${HIKARI_VCPKG_STATIC_ROOT}/x64-windows-static/lib/Jolt.lib")
target_include_directories(
    HIKARI::Jolt
    SYSTEM INTERFACE
        "${HIKARI_VCPKG_INCLUDE_DIR}")

set(
    HIKARI_COACD_ROOT
    "${HIKARI_SOURCE_ROOT}/ThirdParty/CoACD")
hikari_require_dependency_file(
    "${HIKARI_COACD_ROOT}/include/coacd_c_api.h"
    "CoACD C API header")
add_library(HIKARI::CoACD INTERFACE IMPORTED GLOBAL)
target_include_directories(
    HIKARI::CoACD
    SYSTEM INTERFACE
        "${HIKARI_COACD_ROOT}/include")

set(HIKARI_RUNTIME_DEPENDENCIES)
if(HIKARI_WITH_EDITOR_BUILD)
    if(HIKARI_PRODUCT STREQUAL "Debug")
        set(
            hikari_vcpkg_runtime_root
            "${HIKARI_VCPKG_DYNAMIC_ROOT}/x64-windows/debug/bin")
        set(
            hikari_vcpkg_runtime_names
            assimp-vc143-mtd.dll
            kubazip.dll
            minizipd.dll
            poly2tri.dll
            pugixml.dll
            zd.dll)
    else()
        set(
            hikari_vcpkg_runtime_root
            "${HIKARI_VCPKG_DYNAMIC_ROOT}/x64-windows/bin")
        set(
            hikari_vcpkg_runtime_names
            assimp-vc143-mt.dll
            kubazip.dll
            minizip.dll
            poly2tri.dll
            pugixml.dll
            z.dll)
    endif()
    foreach(runtime_name IN LISTS hikari_vcpkg_runtime_names)
        set(
            runtime_file
            "${hikari_vcpkg_runtime_root}/${runtime_name}")
        hikari_require_dependency_file(
            "${runtime_file}"
            "vcpkg runtime dependency")
        list(APPEND HIKARI_RUNTIME_DEPENDENCIES "${runtime_file}")
    endforeach()
endif()

find_program(
    HIKARI_DXC_EXECUTABLE
    NAMES dxc.exe
    HINTS
        "C:/Program Files (x86)/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/${CMAKE_SYSTEM_VERSION}/x64")
if(NOT HIKARI_DXC_EXECUTABLE)
    message(
        FATAL_ERROR
        "HIKARI requires the Windows SDK DXC runtime for reproducible "
        "build/install/package deployment.")
endif()
get_filename_component(
    HIKARI_DXC_RUNTIME_ROOT
    "${HIKARI_DXC_EXECUTABLE}"
    DIRECTORY)
foreach(runtime_name IN ITEMS dxcompiler.dll dxil.dll)
    set(
        runtime_file
        "${HIKARI_DXC_RUNTIME_ROOT}/${runtime_name}")
    hikari_require_dependency_file(
        "${runtime_file}"
        "Windows SDK DXC runtime dependency")
    list(APPEND
        HIKARI_RUNTIME_DEPENDENCIES
        "${runtime_file}")
endforeach()

if(HIKARI_WITH_EDITOR_BUILD)
    set(
        HIKARI_COACD_RUNTIME
        "${HIKARI_COACD_ROOT}/bin/win-x64/lib_coacd.dll")
    hikari_require_dependency_file(
        "${HIKARI_COACD_RUNTIME}"
        "CoACD editor runtime")
    list(APPEND
        HIKARI_RUNTIME_DEPENDENCIES
        "${HIKARI_COACD_RUNTIME}")

    set(
        HIKARI_WINPIX_ROOT
        "${HIKARI_SOURCE_ROOT}/ThirdParty/WinPixEventRuntime")
    hikari_require_dependency_file(
        "${HIKARI_WINPIX_ROOT}/Include/WinPixEventRuntime/pix3.h"
        "WinPix headers")
    hikari_require_dependency_file(
        "${HIKARI_WINPIX_ROOT}/bin/x64/WinPixEventRuntime.lib"
        "WinPix import library")
    hikari_require_dependency_file(
        "${HIKARI_WINPIX_ROOT}/bin/x64/WinPixEventRuntime.dll"
        "WinPix runtime")
    add_library(HIKARI::WinPix SHARED IMPORTED GLOBAL)
    set_target_properties(
        HIKARI::WinPix
        PROPERTIES
            IMPORTED_CONFIGURATIONS "Debug;EditorRelease"
            IMPORTED_IMPLIB_DEBUG
                "${HIKARI_WINPIX_ROOT}/bin/x64/WinPixEventRuntime.lib"
            IMPORTED_LOCATION_DEBUG
                "${HIKARI_WINPIX_ROOT}/bin/x64/WinPixEventRuntime.dll"
            IMPORTED_IMPLIB_EDITORRELEASE
                "${HIKARI_WINPIX_ROOT}/bin/x64/WinPixEventRuntime.lib"
            IMPORTED_LOCATION_EDITORRELEASE
                "${HIKARI_WINPIX_ROOT}/bin/x64/WinPixEventRuntime.dll")
    target_include_directories(
        HIKARI::WinPix
        SYSTEM INTERFACE
            "${HIKARI_WINPIX_ROOT}/Include/WinPixEventRuntime")
    list(APPEND
        HIKARI_RUNTIME_DEPENDENCIES
        "${HIKARI_WINPIX_ROOT}/bin/x64/WinPixEventRuntime.dll")
endif()

set(
    _hikari_streamline_modes
    AUTO
    ON
    OFF)
string(TOUPPER "${HIKARI_STREAMLINE_MODE}" hikari_streamline_mode_upper)
if(NOT hikari_streamline_mode_upper IN_LIST _hikari_streamline_modes)
    message(
        FATAL_ERROR
        "HIKARI_STREAMLINE_MODE must be AUTO, ON, or OFF.")
endif()

set(
    hikari_streamline_header
    "${HIKARI_STREAMLINE_SDK_ROOT}/include/sl.h")
set(
    hikari_streamline_implib
    "${HIKARI_STREAMLINE_SDK_ROOT}/lib/x64/sl.interposer.lib")
set(
    hikari_streamline_available
    FALSE)
if(
    EXISTS "${hikari_streamline_header}" AND
    EXISTS "${hikari_streamline_implib}")
    set(hikari_streamline_available TRUE)
endif()
if(
    hikari_streamline_mode_upper STREQUAL "ON" AND
    NOT hikari_streamline_available)
    message(
        FATAL_ERROR
        "Streamline was requested but its SDK is incomplete: "
        "${HIKARI_STREAMLINE_SDK_ROOT}")
endif()

set(HIKARI_STREAMLINE_ENABLED FALSE)
if(
    hikari_streamline_available AND
    NOT hikari_streamline_mode_upper STREQUAL "OFF")
    set(
        hikari_streamline_interposer
        "${HIKARI_STREAMLINE_SDK_ROOT}/bin/x64/sl.interposer.dll")
    hikari_require_dependency_file(
        "${hikari_streamline_interposer}"
        "Streamline interposer")
    add_library(HIKARI::Streamline SHARED IMPORTED GLOBAL)
    set_target_properties(
        HIKARI::Streamline
        PROPERTIES
            IMPORTED_CONFIGURATIONS "Debug;EditorRelease;Shipping"
            IMPORTED_IMPLIB_DEBUG "${hikari_streamline_implib}"
            IMPORTED_LOCATION_DEBUG "${hikari_streamline_interposer}"
            IMPORTED_IMPLIB_EDITORRELEASE "${hikari_streamline_implib}"
            IMPORTED_LOCATION_EDITORRELEASE "${hikari_streamline_interposer}"
            IMPORTED_IMPLIB_SHIPPING "${hikari_streamline_implib}"
            IMPORTED_LOCATION_SHIPPING "${hikari_streamline_interposer}"
            INTERFACE_COMPILE_DEFINITIONS "HIKARI_WITH_STREAMLINE=1")
    target_include_directories(
        HIKARI::Streamline
        SYSTEM INTERFACE
            "${HIKARI_STREAMLINE_SDK_ROOT}/include")

    set(
        hikari_streamline_required_runtime
        sl.interposer.dll
        sl.common.dll
        sl.dlss.dll
        nvngx_dlss.dll
        nvngx_dlss.license.txt)
    foreach(runtime_name IN LISTS hikari_streamline_required_runtime)
        set(
            runtime_file
            "${HIKARI_STREAMLINE_SDK_ROOT}/bin/x64/${runtime_name}")
        hikari_require_dependency_file(
            "${runtime_file}"
            "Streamline runtime")
        list(APPEND HIKARI_RUNTIME_DEPENDENCIES "${runtime_file}")
    endforeach()

    set(
        hikari_streamline_optional_runtime
        sl.dlss_g.dll
        sl.reflex.dll
        sl.pcl.dll
        nvngx_dlssg.dll
        reflex.license.txt)
    foreach(runtime_name IN LISTS hikari_streamline_optional_runtime)
        set(
            runtime_file
            "${HIKARI_STREAMLINE_SDK_ROOT}/bin/x64/${runtime_name}")
        if(EXISTS "${runtime_file}")
            list(APPEND HIKARI_RUNTIME_DEPENDENCIES "${runtime_file}")
        endif()
    endforeach()
    set(HIKARI_STREAMLINE_ENABLED TRUE)
endif()

message(
    STATUS
    "HIKARI Streamline enabled: ${HIKARI_STREAMLINE_ENABLED} "
    "(mode ${hikari_streamline_mode_upper})")
