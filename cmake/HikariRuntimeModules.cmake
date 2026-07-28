include_guard(GLOBAL)

# Runtime module card:
# - Owns: compile ownership for one semantic Runtime source domain.
# - Does not own: public SDK shape, serialized data, or cross-domain behavior.
# - Provides: private STATIC targets composed by HIKARI_Runtime.
# - Consumes: the existing Runtime include and third-party compile contracts.
# - Allowed callers: HIKARI_Runtime only.
# - Replacement seam: each OBJECT target can become a linked internal library
#   after its source dependencies are made one-way.
# - Fallback: HIKARI_Runtime remains the only public link target.

set(HIKARI_RUNTIME_FOUNDATION_SOURCES)
set(HIKARI_RUNTIME_ASSET_SOURCES)
set(HIKARI_RUNTIME_PROJECT_SOURCES)
set(HIKARI_RUNTIME_SCENE_SOURCES)
set(HIKARI_RUNTIME_PHYSICS_SOURCES)
set(HIKARI_RUNTIME_GFX_SOURCES)
set(HIKARI_RUNTIME_RENDER3D_SOURCES)
set(HIKARI_RUNTIME_AUTHORING_SOURCES)
set(HIKARI_RUNTIME_COMPOSITION_SOURCES)

foreach(runtime_source IN LISTS HIKARI_RUNTIME_SOURCES)
    file(
        RELATIVE_PATH
        runtime_relative_path
        "${HIKARI_SOURCE_ROOT}"
        "${runtime_source}")
    string(REPLACE "\\" "/" runtime_relative_path "${runtime_relative_path}")

    if(
        runtime_relative_path MATCHES
        "^HIKARI/(Core|Diagnostics|Platform|Input|Audio)/" OR
        runtime_relative_path STREQUAL "External/WICTextureLoader.cpp" OR
        runtime_relative_path STREQUAL "Matrix3x3.cpp" OR
        runtime_relative_path STREQUAL "SpineDefaultExtensionShim.cpp")
        list(APPEND HIKARI_RUNTIME_FOUNDATION_SOURCES "${runtime_source}")
    elseif(runtime_relative_path MATCHES "^HIKARI/Assets/")
        list(APPEND HIKARI_RUNTIME_ASSET_SOURCES "${runtime_source}")
    elseif(runtime_relative_path MATCHES "^HIKARI/Project/")
        list(APPEND HIKARI_RUNTIME_PROJECT_SOURCES "${runtime_source}")
    elseif(runtime_relative_path MATCHES "^HIKARI/(Scene|Animation|Gameplay)/")
        list(APPEND HIKARI_RUNTIME_SCENE_SOURCES "${runtime_source}")
    elseif(runtime_relative_path MATCHES "^HIKARI/Physics/")
        list(APPEND HIKARI_RUNTIME_PHYSICS_SOURCES "${runtime_source}")
    elseif(runtime_relative_path MATCHES "^HIKARI/(Gfx|Render2D|Vfx)/")
        list(APPEND HIKARI_RUNTIME_GFX_SOURCES "${runtime_source}")
    elseif(runtime_relative_path MATCHES "^HIKARI/Render3D/")
        list(APPEND HIKARI_RUNTIME_RENDER3D_SOURCES "${runtime_source}")
    elseif(runtime_relative_path MATCHES "^HIKARI/Tools/")
        list(APPEND HIKARI_RUNTIME_AUTHORING_SOURCES "${runtime_source}")
    elseif(runtime_relative_path MATCHES "^HIKARI/Runtime/")
        list(APPEND HIKARI_RUNTIME_COMPOSITION_SOURCES "${runtime_source}")
    else()
        message(
            FATAL_ERROR
            "Runtime source has no internal module owner: "
            "${runtime_relative_path}")
    endif()
endforeach()

set(
    HIKARI_RUNTIME_MODULE_SOURCE_VARIABLES
    HIKARI_RUNTIME_FOUNDATION_SOURCES
    HIKARI_RUNTIME_ASSET_SOURCES
    HIKARI_RUNTIME_PROJECT_SOURCES
    HIKARI_RUNTIME_SCENE_SOURCES
    HIKARI_RUNTIME_PHYSICS_SOURCES
    HIKARI_RUNTIME_GFX_SOURCES
    HIKARI_RUNTIME_RENDER3D_SOURCES
    HIKARI_RUNTIME_AUTHORING_SOURCES
    HIKARI_RUNTIME_COMPOSITION_SOURCES)

set(hikari_owned_runtime_sources)
foreach(source_variable IN LISTS HIKARI_RUNTIME_MODULE_SOURCE_VARIABLES)
    if(NOT ${source_variable})
        message(
            FATAL_ERROR
            "Runtime module source set is empty: ${source_variable}")
    endif()
    list(APPEND hikari_owned_runtime_sources ${${source_variable}})
endforeach()
list(LENGTH HIKARI_RUNTIME_SOURCES hikari_runtime_source_count)
list(LENGTH hikari_owned_runtime_sources hikari_owned_runtime_source_count)
set(hikari_unique_runtime_sources ${hikari_owned_runtime_sources})
list(REMOVE_DUPLICATES hikari_unique_runtime_sources)
list(LENGTH hikari_unique_runtime_sources hikari_unique_runtime_source_count)
if(
    NOT hikari_owned_runtime_source_count EQUAL hikari_runtime_source_count OR
    NOT hikari_unique_runtime_source_count EQUAL hikari_runtime_source_count)
    message(
        FATAL_ERROR
        "Internal Runtime module ownership must cover every Runtime source "
        "exactly once.")
endif()

function(
    hikari_add_runtime_module
    target_name
    source_variable)
    add_library("${target_name}" STATIC)
    target_sources(
        "${target_name}"
        PRIVATE
            ${${source_variable}})
    target_include_directories(
        "${target_name}"
        PRIVATE
            "${HIKARI_SOURCE_ROOT}"
            "${HIKARI_SOURCE_ROOT}/include"
            "${HIKARI_SOURCE_ROOT}/HIKARI")
    target_link_libraries(
        "${target_name}"
        PRIVATE
            ${HIKARI_RUNTIME_BUILD_DEPENDENCIES})
    hikari_configure_engine_target("${target_name}")
    set_target_properties(
        "${target_name}"
        PROPERTIES
            FOLDER "Engine/Runtime/Internal")
    source_group(
        TREE "${HIKARI_SOURCE_ROOT}"
        FILES
            ${${source_variable}})
endfunction()

function(hikari_define_runtime_modules output_variable)
    set(
        runtime_module_specs
        "hikari_runtime_foundation|HIKARI_RUNTIME_FOUNDATION_SOURCES"
        "hikari_runtime_assets|HIKARI_RUNTIME_ASSET_SOURCES"
        "hikari_runtime_project|HIKARI_RUNTIME_PROJECT_SOURCES"
        "hikari_runtime_scene|HIKARI_RUNTIME_SCENE_SOURCES"
        "hikari_runtime_physics|HIKARI_RUNTIME_PHYSICS_SOURCES"
        "hikari_runtime_gfx|HIKARI_RUNTIME_GFX_SOURCES"
        "hikari_runtime_render3d|HIKARI_RUNTIME_RENDER3D_SOURCES"
        "hikari_runtime_authoring|HIKARI_RUNTIME_AUTHORING_SOURCES"
        "hikari_runtime_composition|HIKARI_RUNTIME_COMPOSITION_SOURCES")

    set(runtime_targets)
    foreach(module_spec IN LISTS runtime_module_specs)
        string(REPLACE "|" ";" module_parts "${module_spec}")
        list(GET module_parts 0 module_target)
        list(GET module_parts 1 module_sources)
        hikari_add_runtime_module(
            "${module_target}"
            "${module_sources}")
        list(APPEND runtime_targets "${module_target}")
    endforeach()
    set("${output_variable}" "${runtime_targets}" PARENT_SCOPE)
endfunction()
