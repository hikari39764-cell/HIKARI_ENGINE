if(NOT DEFINED HIKARI_SHADER_SOURCE_ROOT OR
   NOT DEFINED HIKARI_SHADER_COOKED_ROOT)
    message(FATAL_ERROR "Built-in Shader install validation is unconfigured.")
endif()

set(
    hikari_shader_catalog
    "${HIKARI_SHADER_SOURCE_ROOT}/BuiltinShaderCatalog.json")
set(
    hikari_shader_index
    "${HIKARI_SHADER_COOKED_ROOT}/BuiltinShaderArtifacts.json")
if(NOT EXISTS "${hikari_shader_catalog}" OR
   NOT EXISTS "${hikari_shader_index}")
    message(FATAL_ERROR "Built-in Shader catalog/index is missing.")
endif()
if("${hikari_shader_catalog}" IS_NEWER_THAN "${hikari_shader_index}")
    message(FATAL_ERROR "Built-in HSHADERC index is stale against its catalog.")
endif()
file(
    GLOB_RECURSE
    hikari_shader_sources
    LIST_DIRECTORIES FALSE
    "${HIKARI_SHADER_SOURCE_ROOT}/*.hlsl"
    "${HIKARI_SHADER_SOURCE_ROOT}/*.hlsli"
    "${HIKARI_SHADER_SOURCE_ROOT}/*.h")
foreach(hikari_shader_source IN LISTS hikari_shader_sources)
    if("${hikari_shader_source}" IS_NEWER_THAN "${hikari_shader_index}")
        message(
            FATAL_ERROR
            "Built-in HSHADERC index is stale against ${hikari_shader_source}.")
    endif()
endforeach()

file(READ "${hikari_shader_index}" hikari_shader_index_json)
string(
    JSON
    hikari_shader_schema
    GET
    "${hikari_shader_index_json}"
    schema)
string(
    JSON
    hikari_shader_version
    GET
    "${hikari_shader_index_json}"
    version)
string(
    JSON
    hikari_shader_program_count
    LENGTH
    "${hikari_shader_index_json}"
    programs)
if(NOT hikari_shader_schema STREQUAL "HIKARI.BuiltinShaderArtifacts" OR
   NOT hikari_shader_version EQUAL 1 OR
   hikari_shader_program_count EQUAL 0)
    message(FATAL_ERROR "Built-in HSHADERC index schema is invalid.")
endif()
math(EXPR hikari_shader_program_last "${hikari_shader_program_count} - 1")
foreach(hikari_shader_index RANGE 0 ${hikari_shader_program_last})
    string(
        JSON
        hikari_shader_relative
        GET
        "${hikari_shader_index_json}"
        programs ${hikari_shader_index} artifact)
    string(
        JSON
        hikari_shader_expected_hash
        GET
        "${hikari_shader_index_json}"
        programs ${hikari_shader_index} artifactHash)
    if(IS_ABSOLUTE "${hikari_shader_relative}" OR
       hikari_shader_relative MATCHES "(^|/)\\.\\.(/|$)" OR
       NOT hikari_shader_relative MATCHES "\\.hshaderc$")
        message(FATAL_ERROR "Built-in HSHADERC index path is unsafe.")
    endif()
    set(
        hikari_shader_artifact
        "${HIKARI_SHADER_COOKED_ROOT}/${hikari_shader_relative}")
    if(NOT EXISTS "${hikari_shader_artifact}")
        message(FATAL_ERROR "Indexed built-in HSHADERC artifact is missing.")
    endif()
    file(SHA256 "${hikari_shader_artifact}" hikari_shader_actual_hash)
    string(TOLOWER "${hikari_shader_actual_hash}" hikari_shader_actual_hash)
    string(TOLOWER "${hikari_shader_expected_hash}" hikari_shader_expected_hash)
    if(NOT hikari_shader_actual_hash STREQUAL hikari_shader_expected_hash)
        message(FATAL_ERROR "Indexed built-in HSHADERC hash differs.")
    endif()
endforeach()
