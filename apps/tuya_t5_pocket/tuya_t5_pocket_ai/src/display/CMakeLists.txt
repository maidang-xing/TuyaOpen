##
# @file CMakeLists.txt
# @brief 
#/
set(APP_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR})

set(APP_MODULE_SRCS)

aux_source_directory(${APP_MODULE_PATH}/ducky DUCKY_SRCS)
aux_source_directory(${APP_MODULE_PATH}/icons ICONS_SRCS)
aux_source_directory(${APP_MODULE_PATH}/logo LOGO_SRCS)
aux_source_directory(${APP_MODULE_PATH}/ui UI_SRCS)
aux_source_directory(${APP_MODULE_PATH}/fonts FONTS_SRCS)
aux_source_directory(${APP_MODULE_PATH}/ui/buddy_ui BUDDY_UI_SRCS)

# Big CJK bitmap fonts (~3MB each) noticeably slow down compilation but are
# required for buddy UI to render Chinese tool/session/project names that
# come from .claude logs. Keep them ON by default; set
# DISABLE_BUDDY_CJK_FONT=y in app_default.config / using.config to drop them
# (e.g. for fast iteration on non-CJK builds).
if(DISABLE_BUDDY_CJK_FONT)
    list(FILTER FONTS_SRCS EXCLUDE REGEX ".*ui_font_puhui_.*\\.c$")
endif()


list(APPEND APP_MODULE_SRCS
    ${DUCKY_SRCS}
    ${ICONS_SRCS}
    ${LOGO_SRCS}
    ${UI_SRCS}
    ${FONTS_SRCS}
    ${BUDDY_UI_SRCS}
)

set(APP_MODULE_INC
    ${APP_MODULE_PATH}
    ${APP_MODULE_PATH}/ui
    ${APP_MODULE_PATH}/ui/buddy_ui
)

########################################
# Target Configure
########################################
target_sources(${EXAMPLE_LIB}
    PRIVATE
        ${APP_MODULE_SRCS}
    )

target_include_directories(${EXAMPLE_LIB}
    PRIVATE
        ${APP_MODULE_INC}
    )