include_guard(GLOBAL)

function(hikari_set_msvc_runtime target_name)
    set_property(
        TARGET "${target_name}"
        PROPERTY
            MSVC_RUNTIME_LIBRARY
            "$<$<CONFIG:Debug>:MultiThreadedDebugDLL>$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:MultiThreaded>")
endfunction()

function(hikari_configure_vendor_target target_name)
    target_compile_features("${target_name}" PUBLIC cxx_std_20)
    hikari_set_msvc_runtime("${target_name}")
    target_compile_definitions(
        "${target_name}"
        PRIVATE
            $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
    if(MSVC)
        target_compile_options(
            "${target_name}"
            PRIVATE
                /utf-8
                /W0
                "$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:/O2>"
                "$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:/Oi>"
                "$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:/Gy>"
                "$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:/GL>")
    endif()
endfunction()

function(hikari_configure_engine_target target_name)
    target_compile_features("${target_name}" PRIVATE cxx_std_20)
    hikari_set_msvc_runtime("${target_name}")

    get_target_property(hikari_target_type "${target_name}" TYPE)
    if(WIN32 AND MSVC AND hikari_target_type STREQUAL "EXECUTABLE")
        target_sources(
            "${target_name}"
            PRIVATE
                "${HIKARI_SOURCE_ROOT}/HIKARI/Platform/HIKARI_WindowsApplication.manifest")
    endif()

    target_compile_definitions(
        "${target_name}"
        PRIVATE
            $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
            UNICODE
            _UNICODE
            _WINDOWS
            NOMINMAX
            _CRT_SECURE_NO_WARNINGS
            _CRT_NONSTDC_NO_DEPRECATE
            $<$<CONFIG:Shipping>:HIKARI_SHIPPING>
            HIKARI_ENABLE_ASSIMP_IMPORTER
            JPH_FLOATING_POINT_EXCEPTIONS_ENABLED
            JPH_USE_CPU_COMPUTE
            JPH_OBJECT_STREAM)

    if(HIKARI_WITH_EDITOR_BUILD)
        target_compile_definitions(
            "${target_name}"
            PRIVATE
                HIKARI_ENABLE_PIX
                HIKARI_ENABLE_IMGUI
                HIKARI_WITH_EDITOR)
    endif()

    if(MSVC)
        target_compile_options(
            "${target_name}"
            PRIVATE
                /utf-8
                /permissive-
                /sdl
                /MP
                # 全構成で警告をエラーにする。C4324 は alignas による意図したパディングの通知なので除外する。
                /WX
                /wd4324
                "$<$<CONFIG:Debug>:/W3>"
                "$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:/W4>"
                "$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:/O2>"
                "$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:/Oi>"
                "$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:/Gy>"
                "$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:/GL>")
        target_link_options(
            "${target_name}"
            PRIVATE
                "$<$<CONFIG:Debug>:/WX>"
                "$<$<CONFIG:Debug>:/DEBUG:FULL>"
                "$<$<CONFIG:EditorRelease>:/DEBUG:FULL>"
                "$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:/INCREMENTAL:NO>"
                "$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:/OPT:REF>"
                "$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:/OPT:ICF>"
                "$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:/LTCG>")
    endif()
endfunction()
