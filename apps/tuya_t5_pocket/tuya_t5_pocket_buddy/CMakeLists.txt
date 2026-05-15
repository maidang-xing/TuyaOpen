##
# @file CMakeLists.txt
# @brief Build configuration for Claude Buddy (WebSocket refactor)
#/

set(APP_PATH ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(APP_NAME ${APP_PATH} NAME)

if (CONFIG_LVGL_PC_SIMULATOR STREQUAL "y")
########################################
# Simulator mode: display only
########################################
add_library(${EXAMPLE_LIB})

target_compile_options(${EXAMPLE_LIB}
    PRIVATE
        "-DLV_LVGL_H_INCLUDE_SIMPLE"
)

target_include_directories(${EXAMPLE_LIB}
    PRIVATE
        ${APP_PATH}/include
        ${APP_PATH}/src/transport
)

target_sources(${EXAMPLE_LIB}
    PRIVATE
        ${APP_PATH}/src/transport/buddy_sim_stubs.c
)

add_subdirectory(${APP_PATH}/src/display)

else()
########################################
# Embedded mode: full build
########################################

set(APP_SRCS)
aux_source_directory(${APP_PATH}/src APP_SRCS)
aux_source_directory(${APP_PATH}/src/input INPUT_SRCS)
aux_source_directory(${APP_PATH}/src/transport TRANSPORT_SRCS)
list(FILTER TRANSPORT_SRCS EXCLUDE REGEX "buddy_sim_stubs")
aux_source_directory(${APP_PATH}/src/protocol PROTOCOL_SRCS)
aux_source_directory(${APP_PATH}/src/media MEDIA_SRCS)

list(APPEND APP_SRCS ${INPUT_SRCS} ${TRANSPORT_SRCS} ${PROTOCOL_SRCS} ${MEDIA_SRCS})

set(APP_INC
    ${APP_PATH}/include
    ${APP_PATH}/include/media
    ${APP_PATH}/src/transport
    ${APP_PATH}/src/protocol
)

add_library(${EXAMPLE_LIB})

target_sources(${EXAMPLE_LIB}
    PRIVATE
        ${APP_SRCS}
)

target_include_directories(${EXAMPLE_LIB}
    PRIVATE
        ${APP_INC}
)

target_compile_options(${EXAMPLE_LIB}
    PRIVATE
        "-DLV_LVGL_H_INCLUDE_SIMPLE"
)

add_subdirectory(${APP_PATH}/src/display)
add_subdirectory(${APP_PATH}/../../tuya.ai/ai_components)
target_include_directories(${EXAMPLE_LIB} PRIVATE ${APP_PATH}/../../tuya.ai/ai_components)

endif()
