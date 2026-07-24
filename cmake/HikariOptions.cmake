include_guard(GLOBAL)

set(
    HIKARI_PRODUCT
    "Debug"
    CACHE STRING
    "HIKARI compatibility product: Debug, EditorRelease, or Shipping")
set_property(
    CACHE HIKARI_PRODUCT
    PROPERTY STRINGS
        Debug
        EditorRelease
        Shipping)

set(
    HIKARI_SUPPORTED_PRODUCTS
    Debug
    EditorRelease
    Shipping)
if(NOT HIKARI_PRODUCT IN_LIST HIKARI_SUPPORTED_PRODUCTS)
    message(
        FATAL_ERROR
        "Unsupported HIKARI_PRODUCT='${HIKARI_PRODUCT}'. "
        "Use Debug, EditorRelease, or Shipping.")
endif()

if(NOT CMAKE_CONFIGURATION_TYPES)
    message(
        FATAL_ERROR
        "HIKARI bootstrap expects the Visual Studio multi-config generator.")
endif()
set(
    CMAKE_CONFIGURATION_TYPES
    "${HIKARI_PRODUCT}"
    CACHE STRING
    "Configurations exposed by the selected HIKARI preset"
    FORCE)

if(HIKARI_PRODUCT STREQUAL "Shipping")
    set(HIKARI_WITH_EDITOR_BUILD FALSE)
else()
    set(HIKARI_WITH_EDITOR_BUILD TRUE)
endif()

set(
    HIKARI_STREAMLINE_MODE
    "AUTO"
    CACHE STRING
    "NVIDIA Streamline integration: AUTO, ON, or OFF")
set_property(
    CACHE HIKARI_STREAMLINE_MODE
    PROPERTY STRINGS
        AUTO
        ON
        OFF)

message(STATUS "HIKARI product: ${HIKARI_PRODUCT}")
message(STATUS "HIKARI editor sources: ${HIKARI_WITH_EDITOR_BUILD}")
