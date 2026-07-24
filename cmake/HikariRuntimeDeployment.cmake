include_guard(GLOBAL)

function(hikari_configure_runtime_deployment target_name)
    string(TOUPPER "${HIKARI_PRODUCT}" hikari_product_upper)
    set_target_properties(
        "${target_name}"
        PROPERTIES
            OUTPUT_NAME "HIKARI_UpdateTo3D"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            "RUNTIME_OUTPUT_DIRECTORY_${hikari_product_upper}"
                "${CMAKE_BINARY_DIR}/bin"
            PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            "PDB_OUTPUT_DIRECTORY_${hikari_product_upper}"
                "${CMAKE_BINARY_DIR}/bin"
            VS_DEBUGGER_WORKING_DIRECTORY "${HIKARI_SOURCE_ROOT}")

    foreach(runtime_file IN LISTS HIKARI_RUNTIME_DEPENDENCIES)
        add_custom_command(
            TARGET "${target_name}"
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${runtime_file}"
                "$<TARGET_FILE_DIR:${target_name}>"
            VERBATIM)
    endforeach()

    install(
        TARGETS "${target_name}"
        RUNTIME DESTINATION ".")
    if(HIKARI_RUNTIME_DEPENDENCIES)
        install(
            FILES ${HIKARI_RUNTIME_DEPENDENCIES}
            DESTINATION ".")
    endif()
endfunction()
