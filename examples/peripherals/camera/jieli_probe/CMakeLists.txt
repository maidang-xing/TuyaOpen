##
# @file CMakeLists.txt
# @brief Jieli camera probe (no display, no SD card)
#/

if (CONFIG_ENABLE_CAMERA STREQUAL "y")

set(APP_PATH ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(APP_NAME ${APP_PATH} NAME)
aux_source_directory(${APP_PATH}/src APP_SRCS)

add_library(${EXAMPLE_LIB})
target_sources(${EXAMPLE_LIB}
    PRIVATE
        ${APP_SRCS}
    )

else()

message(FATAL_ERROR "jieli_probe cannot work when CONFIG_ENABLE_CAMERA is not set")

endif()
