include_guard(GLOBAL)

# Runtime module card:
# - Owns: compile ownership for one semantic Runtime source domain.
# - Does not own: public SDK shape, serialized data, or cross-domain behavior.
# - Provides: private STATIC targets composed by HIKARI_Runtime.
# - Consumes: the existing Runtime include and third-party compile contracts.
# - Allowed callers: HIKARI_Runtime only.
# - Internal archives are aggregated by the public Runtime target; these are
#   compile/size ownership boundaries, not independently consumable SDKs.
# - Fallback: HIKARI_Runtime remains the only public link target.
#
# Two kinds of module live here:
# - Moved modules (hikari_add_module): HIKARI/<Dir>/Public/<Dir>/ holds the headers other modules may include,
#   HIKARI/<Dir>/Private/<Dir>/ holds the .cpp files and the module-internal headers. Include paths stay
#   "<Dir>/...". A moved module sees only its own Public and Private, the SDK headers, third-party libraries and
#   the modules it DEPENDS on, so an include of a module it does not depend on fails to compile. The headers
#   shared with HLSL (HIKARI/Shaders/Include) have their own include root, hikari_shader_include (CMakeLists.txt).
# - The groups in HIKARI_RUNTIME_MODULE_SPECS: modules not moved yet. They share the old include roots and link
#   every moved module to see its Public.

set(
    HIKARI_RUNTIME_MODULE_SPECS
    "hikari_runtime_assets|HIKARI_RUNTIME_ASSET_SOURCES"
    "hikari_runtime_asset_pipeline|HIKARI_RUNTIME_ASSET_PIPELINE_SOURCES"
    "hikari_runtime_game_project_sdk|HIKARI_RUNTIME_GAME_PROJECT_SDK_SOURCES"
    "hikari_runtime_navigation|HIKARI_RUNTIME_NAVIGATION_SOURCES"
    "hikari_runtime_animation|HIKARI_RUNTIME_ANIMATION_SOURCES"
    "hikari_runtime_gameplay|HIKARI_RUNTIME_GAMEPLAY_SOURCES"
    "hikari_runtime_scene|HIKARI_RUNTIME_SCENE_SOURCES"
    "hikari_runtime_presentation|HIKARI_RUNTIME_PRESENTATION_SOURCES"
    "hikari_runtime_scene_rendering|HIKARI_RUNTIME_SCENE_RENDERING_SOURCES"
    "hikari_runtime_physics|HIKARI_RUNTIME_PHYSICS_SOURCES"
    "hikari_runtime_render2d|HIKARI_RUNTIME_RENDER2D_SOURCES"
    "hikari_runtime_render3d|HIKARI_RUNTIME_RENDER3D_SOURCES"
    "hikari_runtime_render3d_features|HIKARI_RUNTIME_RENDER3D_FEATURE_SOURCES"
    "hikari_runtime_authoring|HIKARI_RUNTIME_AUTHORING_SOURCES"
    "hikari_runtime_composition|HIKARI_RUNTIME_COMPOSITION_SOURCES")

set(HIKARI_RUNTIME_MODULE_SOURCE_VARIABLES)
foreach(module_spec IN LISTS HIKARI_RUNTIME_MODULE_SPECS)
    string(REPLACE "|" ";" module_parts "${module_spec}")
    list(GET module_parts 1 source_variable)
    set(${source_variable})
    list(APPEND HIKARI_RUNTIME_MODULE_SOURCE_VARIABLES "${source_variable}")
endforeach()

# A moved module is found by its directory layout; its key is the directory name in lower case.
set(HIKARI_MODULE_KEYS)
foreach(runtime_source IN LISTS HIKARI_RUNTIME_SOURCES)
    file(
        RELATIVE_PATH
        runtime_relative_path
        "${HIKARI_SOURCE_ROOT}"
        "${runtime_source}")
    string(REPLACE "\\" "/" runtime_relative_path "${runtime_relative_path}")

    if(runtime_relative_path MATCHES "^HIKARI/([A-Za-z0-9]+)/(Public|Private)/")
        set(module_directory "${CMAKE_MATCH_1}")
        string(TOLOWER "${module_directory}" module_key)
        set(HIKARI_MODULE_DIRECTORY_${module_key} "${module_directory}")
        list(APPEND HIKARI_MODULE_KEYS "${module_key}")
        list(APPEND HIKARI_MODULE_SOURCES_${module_key} "${runtime_source}")
    elseif(runtime_source IN_LIST HIKARI_GAME_PROJECT_SDK_EXPORT_SOURCES)
        list(
            APPEND
            HIKARI_RUNTIME_GAME_PROJECT_SDK_SOURCES
            "${runtime_source}")
    elseif(runtime_relative_path MATCHES "^HIKARI/Assets/(Importers|Cooking|Geometry|Lighting)/")
        # Source conversion, validation and packaging; records/readers remain asset-owned.
        list(APPEND HIKARI_RUNTIME_ASSET_PIPELINE_SOURCES "${runtime_source}")
    elseif(runtime_relative_path MATCHES "^HIKARI/Assets/")
        list(APPEND HIKARI_RUNTIME_ASSET_SOURCES "${runtime_source}")
    elseif(runtime_relative_path MATCHES "^HIKARI/Scene/Navigation/")
        list(APPEND HIKARI_RUNTIME_NAVIGATION_SOURCES "${runtime_source}")
    elseif(runtime_relative_path MATCHES "^HIKARI/Animation/")
        list(APPEND HIKARI_RUNTIME_ANIMATION_SOURCES "${runtime_source}")
    elseif(runtime_relative_path MATCHES "^HIKARI/Gameplay/")
        list(APPEND HIKARI_RUNTIME_GAMEPLAY_SOURCES "${runtime_source}")
    elseif(runtime_relative_path MATCHES "^HIKARI/Scene/Components/Rendering/")
        list(APPEND HIKARI_RUNTIME_SCENE_RENDERING_SOURCES "${runtime_source}")
    elseif(runtime_relative_path MATCHES "^HIKARI/Scene/(Ui|Sequencer|Camera)/")
        # UI projection and camera/sequence presentation; Scene owns world/ECS lifetime.
        list(APPEND HIKARI_RUNTIME_PRESENTATION_SOURCES "${runtime_source}")
    elseif(runtime_relative_path MATCHES "^HIKARI/Scene/")
        list(APPEND HIKARI_RUNTIME_SCENE_SOURCES "${runtime_source}")
    elseif(runtime_relative_path MATCHES "^HIKARI/Physics/")
        list(APPEND HIKARI_RUNTIME_PHYSICS_SOURCES "${runtime_source}")
    elseif(runtime_relative_path MATCHES "^HIKARI/Render2D/")
        list(APPEND HIKARI_RUNTIME_RENDER2D_SOURCES "${runtime_source}")
    elseif(runtime_relative_path MATCHES
        "^HIKARI/Render3D/Surface/HIKARI_(SceneSurfaceSet|RenderFrameSurfaceRegistry)\\.cpp$")
        # The surface set and its cross-stage registry are core semantic
        # handoffs. Concrete Surface render targets remain feature-owned.
        list(APPEND HIKARI_RUNTIME_RENDER3D_SOURCES "${runtime_source}")
    elseif(
        runtime_relative_path MATCHES
        "^HIKARI/(Vfx|Render3D/(Color|Features|Lighting|Reflection|ScreenSpace|Shadow|Surface|Terrain|Ui|Vfx|Water))/")
        list(APPEND HIKARI_RUNTIME_RENDER3D_FEATURE_SOURCES "${runtime_source}")
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
list(REMOVE_DUPLICATES HIKARI_MODULE_KEYS)
foreach(module_key IN LISTS HIKARI_MODULE_KEYS)
    list(APPEND HIKARI_RUNTIME_MODULE_SOURCE_VARIABLES "HIKARI_MODULE_SOURCES_${module_key}")
endforeach()

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

# A module name is the directory name under HIKARI/ in lower case; underscores are ignored, so
# render_contracts is HIKARI/RenderContracts.
function(hikari_module_key name output_variable)
    string(REPLACE "_" "" module_key "${name}")
    string(TOLOWER "${module_key}" module_key)
    set("${output_variable}" "${module_key}" PARENT_SCOPE)
endfunction()

# hikari_add_module(<name> [DEPENDS <lower module>...])
# PUBLIC include directories are the module's Public and the SDK headers (the stable contract below every
# module); Private is a PRIVATE include directory. DEPENDS modules and their Public pass down through
# target_link_libraries. Third-party and system libraries are linked by the caller.
function(hikari_add_module name)
    cmake_parse_arguments(PARSE_ARGV 1 module "" "" "DEPENDS")
    hikari_module_key("${name}" module_key)
    if(NOT HIKARI_MODULE_SOURCES_${module_key})
        message(
            FATAL_ERROR
            "Module '${name}' has no sources under HIKARI/<Dir>/Public or HIKARI/<Dir>/Private.")
    endif()
    set(target_name "hikari_${module_key}")
    set(module_directory "${HIKARI_SOURCE_ROOT}/HIKARI/${HIKARI_MODULE_DIRECTORY_${module_key}}")
    add_library("${target_name}" STATIC ${HIKARI_MODULE_SOURCES_${module_key}})
    target_include_directories(
        "${target_name}"
        PUBLIC
            "${module_directory}/Public"
            "${HIKARI_SOURCE_ROOT}/include"
        PRIVATE
            "${module_directory}/Private")
    foreach(dependency IN LISTS module_DEPENDS)
        hikari_module_key("${dependency}" dependency_key)
        # Only an already defined module can be a dependency, so the module graph cannot contain a cycle.
        if(NOT TARGET "hikari_${dependency_key}")
            message(
                FATAL_ERROR
                "Module '${name}' depends on '${dependency}', which is not defined before it.")
        endif()
        target_link_libraries("${target_name}" PUBLIC "hikari_${dependency_key}")
    endforeach()
    hikari_configure_engine_target("${target_name}")
    set_target_properties(
        "${target_name}"
        PROPERTIES
            FOLDER "Engine/Runtime/Internal")
    source_group(
        TREE "${HIKARI_SOURCE_ROOT}"
        FILES
            ${HIKARI_MODULE_SOURCES_${module_key}})
    set_property(GLOBAL APPEND PROPERTY HIKARI_MODULE_TARGETS "${target_name}")
endfunction()

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
    get_property(moved_modules GLOBAL PROPERTY HIKARI_MODULE_TARGETS)
    target_link_libraries(
        "${target_name}"
        PRIVATE
            ${moved_modules}
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

    # Moved modules, lowest first: a DEPENDS module must be listed before the module that uses it.
    hikari_add_module(core)
    hikari_add_module(diagnostics DEPENDS core)
    hikari_add_module(platform DEPENDS core)
    hikari_add_module(input DEPENDS core platform)
    hikari_add_module(audio DEPENDS core)
    hikari_add_module(project DEPENDS core)
    hikari_add_module(gfx DEPENDS core diagnostics project)
    hikari_add_module(render_contracts DEPENDS core gfx)
    hikari_add_module(render_core DEPENDS core diagnostics gfx render_contracts)
    foreach(module_key IN LISTS HIKARI_MODULE_KEYS)
        if(NOT TARGET "hikari_${module_key}")
            message(
                FATAL_ERROR
                "HIKARI/${HIKARI_MODULE_DIRECTORY_${module_key}} is laid out as Public/Private "
                "but is not declared with hikari_add_module.")
        endif()
    endforeach()

    get_property(runtime_targets GLOBAL PROPERTY HIKARI_MODULE_TARGETS)
    foreach(module_spec IN LISTS HIKARI_RUNTIME_MODULE_SPECS)
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
