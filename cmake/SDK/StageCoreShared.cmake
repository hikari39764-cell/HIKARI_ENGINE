# 引擎の共用の核（HIKARI::CoreShared、一覧は CoreSharedFiles.cmake）を SDK へ写す（cmake -P 用）。
#   CORE_ROOT    HIKARI/Core
#   DESTINATION  Sdk/shared（include/ と src/ を作り、一覧に無い古いファイルは消す）
# 外部モジュールは SDK だけで編訳するので、写すファイルが一覧に無い Core のヘッダーを include していたら止める。
if(NOT IS_DIRECTORY "${CORE_ROOT}" OR "${DESTINATION}" STREQUAL "")
    message(FATAL_ERROR "StageCoreShared requires CORE_ROOT and DESTINATION.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CoreSharedFiles.cmake")
set(_staged "")

function(_stage_group source_directory destination_directory)
    foreach(_file IN LISTS ARGN)
        set(_from "${CORE_ROOT}/${source_directory}/${_file}")
        if(NOT EXISTS "${_from}")
            message(FATAL_ERROR "SDK shared core file is missing: ${_from}")
        endif()
        file(STRINGS "${_from}" _include_lines REGEX "^#include \"Core/")
        foreach(_line IN LISTS _include_lines)
            string(REGEX REPLACE "^#include \"([^\"]+)\".*$" "\\1" _included "${_line}")
            if(NOT _included IN_LIST HIKARI_SDK_CORE_SHARED_HEADERS)
                message(
                    FATAL_ERROR
                    "${source_directory}/${_file} includes ${_included}, which the SDK does not ship. External "
                    "modules build against the SDK alone: add it to cmake/SDK/CoreSharedFiles.cmake if it needs "
                    "no engine library, or remove the include.")
            endif()
        endforeach()
        set(_to "${DESTINATION}/${destination_directory}/${_file}")
        get_filename_component(_to_directory "${_to}" DIRECTORY)
        file(MAKE_DIRECTORY "${_to_directory}")
        file(COPY_FILE "${_from}" "${_to}" ONLY_IF_DIFFERENT)
        list(APPEND _staged "${destination_directory}/${_file}")
    endforeach()
    set(_staged "${_staged}" PARENT_SCOPE)
endfunction()

_stage_group("Public" "include" ${HIKARI_SDK_CORE_SHARED_HEADERS})
_stage_group("Private" "src" ${HIKARI_SDK_CORE_SHARED_SOURCES})

file(GLOB_RECURSE _existing LIST_DIRECTORIES false RELATIVE "${DESTINATION}" "${DESTINATION}/*")
foreach(_file IN LISTS _existing)
    if(NOT _file IN_LIST _staged)
        file(REMOVE "${DESTINATION}/${_file}")
    endif()
endforeach()
