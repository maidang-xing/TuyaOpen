##
# @file CMakeLists.txt
# @brief 
#/

if (CONFIG_ENABLE_CAMERA STREQUAL "y")
# MODULE_PATH
set(MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})

# MODULE_NAME
get_filename_component(MODULE_NAME ${MODULE_PATH} NAME)

# LIB_SRCS
file(GLOB_RECURSE camera_srcs "${MODULE_PATH}/tdl_camera/src/*.c")

if (CONFIG_ENABLE_DVP STREQUAL "y")
    file(GLOB_RECURSE tdd_camera_dvp_srcs "${MODULE_PATH}/tdd_camera/src/dvp/*.c")
    # Two DVP cores live in that directory and both implement the device
    # registration; keep the one the platform's TKL can drive.
    if (CONFIG_ENABLE_DVP_PULL STREQUAL "y")
        # JIELI keeps its own core and its own GC2145 driver
        list(FILTER tdd_camera_dvp_srcs EXCLUDE REGEX "tdd_camera_dvp[.]c$")
        list(FILTER tdd_camera_dvp_srcs EXCLUDE REGEX "tdd_camera_dvp_gc2145[.]c$")
    else()
        list(FILTER tdd_camera_dvp_srcs EXCLUDE REGEX "tdd_camera_dvp_pull[.]c$")
        list(FILTER tdd_camera_dvp_srcs EXCLUDE REGEX "tdd_camera_dvp_gc2145_v1[.]c$")
    endif()
endif()

set(LIB_SRCS ${camera_srcs} ${tdd_camera_dvp_srcs})

# LIB_PUBLIC_INC
set(LIB_PUBLIC_INC 
    ${MODULE_PATH}/tdl_camera/include
    ${MODULE_PATH}/tdd_camera/include
    )

########################################
# Target Configure
########################################
add_library(${MODULE_NAME})

target_sources(${MODULE_NAME}
    PRIVATE
        ${LIB_SRCS}
    )

target_include_directories(${MODULE_NAME}
    PRIVATE
        ${LIB_PRIVATE_INC}

    PUBLIC
        ${LIB_PUBLIC_INC}
    )


########################################
# Layer Configure
########################################
list(APPEND COMPONENT_LIBS ${MODULE_NAME})
set(COMPONENT_LIBS "${COMPONENT_LIBS}" PARENT_SCOPE)
list(APPEND COMPONENT_PUBINC ${LIB_PUBLIC_INC})
set(COMPONENT_PUBINC "${COMPONENT_PUBINC}" PARENT_SCOPE)

endif()