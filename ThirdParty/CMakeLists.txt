add_library(hikari_vendor_spine STATIC)
target_sources(
    hikari_vendor_spine
    PRIVATE
        ${HIKARI_VENDOR_SPINE_SOURCES})
target_include_directories(
    hikari_vendor_spine
    SYSTEM PUBLIC
        "${HIKARI_SOURCE_ROOT}/ThirdParty/spine/include")
hikari_configure_vendor_target(hikari_vendor_spine)
add_library(HIKARI::Spine ALIAS hikari_vendor_spine)

add_library(hikari_vendor_meshoptimizer STATIC)
target_sources(
    hikari_vendor_meshoptimizer
    PRIVATE
        ${HIKARI_VENDOR_MESHOPTIMIZER_SOURCES})
target_include_directories(
    hikari_vendor_meshoptimizer
    SYSTEM PUBLIC
        "${HIKARI_SOURCE_ROOT}/ThirdParty/meshoptimizer/src")
hikari_configure_vendor_target(hikari_vendor_meshoptimizer)
add_library(HIKARI::Meshoptimizer ALIAS hikari_vendor_meshoptimizer)

if(HIKARI_WITH_EDITOR_BUILD)
    add_library(hikari_vendor_xatlas STATIC)
    target_sources(
        hikari_vendor_xatlas
        PRIVATE
            ${HIKARI_VENDOR_XATLAS_SOURCES})
    target_include_directories(
        hikari_vendor_xatlas
        SYSTEM PUBLIC
            "${HIKARI_SOURCE_ROOT}/ThirdParty/xatlas/source")
    hikari_configure_vendor_target(hikari_vendor_xatlas)
    add_library(HIKARI::Xatlas ALIAS hikari_vendor_xatlas)
endif()

add_library(hikari_vendor_nlohmann INTERFACE)
target_include_directories(
    hikari_vendor_nlohmann
    SYSTEM INTERFACE
        "${HIKARI_SOURCE_ROOT}/ThirdParty/nlohmann")
add_library(HIKARI::Nlohmann ALIAS hikari_vendor_nlohmann)

if(HIKARI_WITH_EDITOR_BUILD)
    add_library(hikari_vendor_imgui STATIC)
    target_sources(
        hikari_vendor_imgui
        PRIVATE
            ${HIKARI_VENDOR_IMGUI_SOURCES})
    target_include_directories(
        hikari_vendor_imgui
        SYSTEM PUBLIC
            "${HIKARI_SOURCE_ROOT}/ThirdParty/imgui")
    hikari_configure_vendor_target(hikari_vendor_imgui)
    add_library(HIKARI::ImGui ALIAS hikari_vendor_imgui)

    add_library(hikari_vendor_imguizmo STATIC)
    target_sources(
        hikari_vendor_imguizmo
        PRIVATE
            ${HIKARI_VENDOR_IMGUIZMO_SOURCES})
    target_include_directories(
        hikari_vendor_imguizmo
        SYSTEM PUBLIC
            "${HIKARI_SOURCE_ROOT}/ThirdParty/ImGuizmo")
    target_link_libraries(
        hikari_vendor_imguizmo
        PUBLIC
            hikari_vendor_imgui)
    hikari_configure_vendor_target(hikari_vendor_imguizmo)
    add_library(HIKARI::ImGuizmo ALIAS hikari_vendor_imguizmo)
endif()

# 地形データ核（Modules/TerrainKit）専用。FastNoise2 1.1.1 と固定版の FastSIMD を、ネットワークも try_compile も使わずに組む。
# x64 MSVC 前提で、dispatch の feature set は SSE2 / SSE41 / AVX2 / AVX512（relaxed）に固定する。
set(hikari_fastnoise2_root "${HIKARI_SOURCE_ROOT}/ThirdParty/FastNoise2")
set(hikari_fastsimd_root "${hikari_fastnoise2_root}/FastSIMD")
set(hikari_fastnoise2_generated "${CMAKE_CURRENT_BINARY_DIR}/fastsimd/FastSIMD_FastNoise")
set(simd_library_name FastSIMD_FastNoise)
set(FastSIMD_SOURCE_DIR "${hikari_fastsimd_root}")
set(simd_inl_full "${hikari_fastnoise2_root}/src/FastNoise/FastSIMD_Build.inl")
set(feature_arch_detect "1")
set(feature_set_list "")
set(hikari_fastnoise2_dispatch_sources)
foreach(feature_set IN ITEMS SSE2 SSE41 AVX2 AVX512)
    set(hikari_feature_source "${hikari_fastnoise2_generated}/FastSIMD_FastNoise_${feature_set}.cpp")
    configure_file("${hikari_fastsimd_root}/dispatch/cmake/feature_set_source.cpp.in" "${hikari_feature_source}")
    string(APPEND feature_set_list ",FastSIMD::FeatureSet::${feature_set}\n")
    list(APPEND hikari_fastnoise2_dispatch_sources "${hikari_feature_source}")
    if(feature_set STREQUAL "AVX2")
        set_source_files_properties("${hikari_feature_source}" PROPERTIES COMPILE_OPTIONS /arch:AVX2)
    elseif(feature_set STREQUAL "AVX512")
        set_source_files_properties("${hikari_feature_source}" PROPERTIES COMPILE_OPTIONS /arch:AVX512)
    endif()
endforeach()
configure_file(
    "${hikari_fastsimd_root}/dispatch/cmake/simd_lib_config.h.in"
    "${hikari_fastnoise2_generated}/include/FastSIMD/FastSIMD_FastNoise_config.h")
# dispatch 単位は全体最適化（/GL）を使わない別の目標にする：AVX のコードが SSE2 経路へインライン展開されないように（上流と同じ）。
add_library(hikari_vendor_fastnoise2_dispatch OBJECT ${hikari_fastnoise2_dispatch_sources})
target_include_directories(
    hikari_vendor_fastnoise2_dispatch
    PRIVATE
        "${hikari_fastnoise2_root}/include"
        "${hikari_fastsimd_root}/include"
        "${hikari_fastnoise2_generated}/include")
target_compile_definitions(
    hikari_vendor_fastnoise2_dispatch
    PRIVATE
        FASTNOISE_STATIC_LIB
        FASTSIMD_STATIC_LIB
        FASTSIMD_IS_RELAXED=1
        FASTSIMD_EXPORT
        FASTSIMD_LIBRARY_NAME=FastSIMD_FastNoise
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
target_compile_features(hikari_vendor_fastnoise2_dispatch PRIVATE cxx_std_20)
hikari_set_msvc_runtime(hikari_vendor_fastnoise2_dispatch)
target_compile_options(
    hikari_vendor_fastnoise2_dispatch
    PRIVATE
        /utf-8 /W0 /GS- /fp:fast /permissive- /d2vzeroupper-
        "$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:/O2>"
        "$<$<OR:$<CONFIG:EditorRelease>,$<CONFIG:Shipping>>:/Oi>")
add_library(hikari_vendor_fastnoise2 STATIC)
target_sources(
    hikari_vendor_fastnoise2
    PRIVATE
        "${hikari_fastsimd_root}/src/FastSIMD.cpp"
        "${hikari_fastnoise2_root}/src/FastNoise/FastNoise_C.cpp"
        "${hikari_fastnoise2_root}/src/FastNoise/Metadata.cpp"
        "${hikari_fastnoise2_root}/src/FastNoise/SmartNode.cpp"
        $<TARGET_OBJECTS:hikari_vendor_fastnoise2_dispatch>)
target_include_directories(
    hikari_vendor_fastnoise2
    SYSTEM PUBLIC
        "${hikari_fastnoise2_root}/include"
        "${hikari_fastsimd_root}/include"
        "${hikari_fastnoise2_generated}/include")
target_compile_definitions(
    hikari_vendor_fastnoise2
    PUBLIC
        FASTNOISE_STATIC_LIB
        FASTSIMD_STATIC_LIB
        FASTSIMD_IS_RELAXED=1
    PRIVATE
        FASTNOISE_EXPORT
        FASTSIMD_EXPORT
        FASTSIMD_LIBRARY_NAME=FastSIMD_FastNoise
        FASTNOISE2_VERSION="1.1.1")
hikari_configure_vendor_target(hikari_vendor_fastnoise2)
add_library(HIKARI::FastNoise2 ALIAS hikari_vendor_fastnoise2)

# tinyexr（v1 の単一ヘッダー API）と同梱の miniz。EXR の読み書きと、地形セルファイルの deflate / CRC32 に使う。
# miniz.c は C でしか組めない（C の仮定義を使う）ので、この目録だけ C を有効にする。
enable_language(C)
add_library(hikari_vendor_tinyexr STATIC)
target_sources(
    hikari_vendor_tinyexr
    PRIVATE
        "${HIKARI_SOURCE_ROOT}/ThirdParty/tinyexr/HIKARI_tinyexr_impl.cc"
        "${HIKARI_SOURCE_ROOT}/ThirdParty/tinyexr/miniz/miniz.c")
target_include_directories(
    hikari_vendor_tinyexr
    SYSTEM PUBLIC
        "${HIKARI_SOURCE_ROOT}/ThirdParty/tinyexr"
        "${HIKARI_SOURCE_ROOT}/ThirdParty/tinyexr/miniz")
hikari_configure_vendor_target(hikari_vendor_tinyexr)
add_library(HIKARI::TinyExr ALIAS hikari_vendor_tinyexr)
