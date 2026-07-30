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
