##
# @file CMakeLists.txt
# @brief sanoTTS serial application
#/

set(APP_PATH ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(APP_NAME ${APP_PATH} NAME)

file(GLOB APP_SRCS "${APP_PATH}/src/*.c")
file(GLOB SANOTTS_SRCS "${APP_PATH}/third_party/sanotts/src/*.c")
file(GLOB ESPEAK_SRCS "${APP_PATH}/third_party/espeak-ng/libespeak-ng/*.c")
file(GLOB UCD_SRCS "${APP_PATH}/third_party/espeak-ng/ucd-tools/*.c")
set(MINIZ_SRCS "${APP_PATH}/third_party/miniz/miniz.c")

set(ALL_SRCS
    ${APP_SRCS}
    ${SANOTTS_SRCS}
    ${ESPEAK_SRCS}
    ${UCD_SRCS}
    ${MINIZ_SRCS}
)

set(APP_INC
    ${APP_PATH}/src
    ${APP_PATH}/third_party/miniz
    ${APP_PATH}/third_party/sanotts/include
    ${APP_PATH}/third_party/sanotts/models/en_us_e12nano
    ${APP_PATH}/third_party/espeak-ng
    ${APP_PATH}/third_party/espeak-ng/include
    ${APP_PATH}/third_party/espeak-ng/include/compat
    ${APP_PATH}/third_party/espeak-ng/libespeak-ng
    ${APP_PATH}/third_party/espeak-ng/ucd-tools/include
)

add_library(${EXAMPLE_LIB})

target_sources(${EXAMPLE_LIB}
    PRIVATE
        ${ALL_SRCS}
)

target_include_directories(${EXAMPLE_LIB}
    PUBLIC
        ${APP_INC}
)

target_compile_definitions(${EXAMPLE_LIB}
    PRIVATE
        HAVE_CONFIG_H
        SNT_NO_FULL_EN_FIXTURE
        MINIZ_NO_TIME
        MINIZ_NO_ARCHIVE_APIS
        MINIZ_NO_ARCHIVE_WRITING_APIS
)

target_compile_options(${EXAMPLE_LIB}
    PRIVATE
        -Wno-error
        -Wno-unused-parameter
        -Wno-sign-compare
        -Wno-unused-variable
)
