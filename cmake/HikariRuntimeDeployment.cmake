include_guard(GLOBAL)

function(hikari_configure_runtime_deployment target_name)
    set(options NO_INSTALL)
    set(one_value_arguments OUTPUT_NAME WORKING_DIRECTORY)
    cmake_parse_arguments(
        HIKARI_DEPLOY
        "${options}"
        "${one_value_arguments}"
        ""
        ${ARGN})

    if(NOT HIKARI_DEPLOY_OUTPUT_NAME)
        set(HIKARI_DEPLOY_OUTPUT_NAME "HIKARI_Engine")
    endif()
    if(NOT HIKARI_DEPLOY_WORKING_DIRECTORY)
        set(HIKARI_DEPLOY_WORKING_DIRECTORY "${HIKARI_SOURCE_ROOT}")
    endif()

    string(TOUPPER "${HIKARI_PRODUCT}" hikari_product_upper)
    set_target_properties(
        "${target_name}"
        PROPERTIES
            OUTPUT_NAME "${HIKARI_DEPLOY_OUTPUT_NAME}"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            "RUNTIME_OUTPUT_DIRECTORY_${hikari_product_upper}"
                "${CMAKE_BINARY_DIR}/bin"
            PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            "PDB_OUTPUT_DIRECTORY_${hikari_product_upper}"
                "${CMAKE_BINARY_DIR}/bin"
            VS_DEBUGGER_WORKING_DIRECTORY
                "${HIKARI_DEPLOY_WORKING_DIRECTORY}")

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

    add_custom_command(
        TARGET "${target_name}"
        POST_BUILD
        COMMAND
            "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:${target_name}>/EngineContent/Shaders"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_directory_if_different
            "${HIKARI_SOURCE_ROOT}/HIKARI/EngineContent"
            "$<TARGET_FILE_DIR:${target_name}>/EngineContent"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_directory_if_different
            "${HIKARI_SOURCE_ROOT}/HIKARI/Shaders"
            "$<TARGET_FILE_DIR:${target_name}>/EngineContent/Shaders"
        VERBATIM)

    if(HIKARI_PRODUCT STREQUAL "Shipping")
        find_program(
            HIKARI_DEPLOY_POWERSHELL_EXECUTABLE
            NAMES powershell.exe
            REQUIRED)
        add_custom_command(
            TARGET "${target_name}"
            POST_BUILD
            COMMAND
                "${HIKARI_DEPLOY_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File
                    "${HIKARI_SOURCE_ROOT}/Tools/ShaderCheck/Compile-HikariShaders.ps1"
                -DxcPath
                    "${HIKARI_DXC_EXECUTABLE}"
            WORKING_DIRECTORY "${HIKARI_SOURCE_ROOT}"
            VERBATIM)
    endif()

    if(NOT HIKARI_DEPLOY_NO_INSTALL)
        install(
            TARGETS "${target_name}"
            RUNTIME DESTINATION ".")
        if(HIKARI_RUNTIME_DEPENDENCIES)
            install(
                FILES ${HIKARI_RUNTIME_DEPENDENCIES}
                DESTINATION ".")
        endif()
        install(
            DIRECTORY
                "${HIKARI_SOURCE_ROOT}/HIKARI/EngineContent/"
            DESTINATION
                "EngineContent")
        install(
            DIRECTORY
                "${HIKARI_SOURCE_ROOT}/HIKARI/Shaders"
            DESTINATION
                "EngineContent")
        string(CONCAT hikari_shader_install_validation_code
            "execute_process(\n"
            "  COMMAND \"${CMAKE_COMMAND}\"\n"
            "    \"-DHIKARI_SHADER_SOURCE_ROOT=${HIKARI_SOURCE_ROOT}/HIKARI/Shaders\"\n"
            "    \"-DHIKARI_SHADER_COOKED_ROOT=${CMAKE_BINARY_DIR}/bin/EngineContent/Shaders/Cooked\"\n"
            "    -P \"${HIKARI_SOURCE_ROOT}/cmake/ValidateBuiltinShaderInstall.cmake\"\n"
            "  RESULT_VARIABLE _hikari_shader_validation_result)\n"
            "if(NOT _hikari_shader_validation_result EQUAL 0)\n"
            "  message(FATAL_ERROR \"Built-in HSHADERC install validation failed.\")\n"
            "endif()")
        install(CODE "${hikari_shader_install_validation_code}")
        string(CONCAT hikari_shader_install_cleanup_code
            "file(REMOVE_RECURSE\n"
            "  \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/EngineContent/Shaders/Cooked\")")
        install(CODE "${hikari_shader_install_cleanup_code}")
        install(
            DIRECTORY
                "${CMAKE_BINARY_DIR}/bin/EngineContent/Shaders/Cooked/"
            DESTINATION
                "EngineContent/Shaders/Cooked")
    endif()
endfunction()
