##
# @file CMakeLists.txt
# @brief Display module build configuration
#/

set(APP_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR})

set(APP_MODULE_SRCS)

aux_source_directory(${APP_MODULE_PATH} DISPLAY_ROOT_SRCS)
aux_source_directory(${APP_MODULE_PATH}/screens SCREEN_SRCS)
aux_source_directory(${APP_MODULE_PATH}/widgets WIDGET_SRCS)
aux_source_directory(${APP_MODULE_PATH}/persona PERSONA_SRCS)
aux_source_directory(${APP_MODULE_PATH}/fonts FONTS_SRCS)

if(DISABLE_BUDDY_CJK_FONT)
    list(FILTER FONTS_SRCS EXCLUDE REGEX ".*ui_font_puhui_.*\\.c$")
endif()

list(APPEND APP_MODULE_SRCS
    ${DISPLAY_ROOT_SRCS}
    ${SCREEN_SRCS}
    ${WIDGET_SRCS}
    ${PERSONA_SRCS}
    ${FONTS_SRCS}
)

set(APP_MODULE_INC
    ${APP_MODULE_PATH}
    ${APP_MODULE_PATH}/screens
    ${APP_MODULE_PATH}/widgets
    ${APP_MODULE_PATH}/persona
)

target_sources(${EXAMPLE_LIB}
    PRIVATE
        ${APP_MODULE_SRCS}
)

target_include_directories(${EXAMPLE_LIB}
    PRIVATE
        ${APP_MODULE_INC}
)
