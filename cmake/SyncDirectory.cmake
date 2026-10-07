# SOURCE の内容で DESTINATION を鏡写しにする（cmake -P 用）。
# 内容が同じファイルは触らず時刻を保ち、SOURCE に無いファイルは削除する。
if(NOT IS_DIRECTORY "${SOURCE}" OR DESTINATION STREQUAL "")
    message(FATAL_ERROR "SyncDirectory requires an existing SOURCE and a DESTINATION.")
endif()

file(GLOB_RECURSE _source_files LIST_DIRECTORIES false RELATIVE "${SOURCE}" "${SOURCE}/*")
file(GLOB_RECURSE _destination_files LIST_DIRECTORIES false RELATIVE "${DESTINATION}" "${DESTINATION}/*")

foreach(_file IN LISTS _destination_files)
    if(NOT _file IN_LIST _source_files)
        file(REMOVE "${DESTINATION}/${_file}")
    endif()
endforeach()
foreach(_file IN LISTS _source_files)
    get_filename_component(_directory "${DESTINATION}/${_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${_directory}")
    file(COPY_FILE "${SOURCE}/${_file}" "${DESTINATION}/${_file}" ONLY_IF_DIFFERENT)
endforeach()
