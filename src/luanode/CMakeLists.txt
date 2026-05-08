##
# @file CMakeLists.txt
# @brief Lua node component for TuyaOpen
#/

if (CONFIG_ENABLE_LUANODE STREQUAL "y")

set(MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})
get_filename_component(MODULE_NAME ${MODULE_PATH} NAME)

set(LUA_DIR ${MODULE_PATH}/lua)

set(LUA_CORE_SRCS
    ${LUA_DIR}/lapi.c
    ${LUA_DIR}/lcode.c
    ${LUA_DIR}/lctype.c
    ${LUA_DIR}/ldebug.c
    ${LUA_DIR}/ldo.c
    ${LUA_DIR}/ldump.c
    ${LUA_DIR}/lfunc.c
    ${LUA_DIR}/lgc.c
    ${LUA_DIR}/llex.c
    ${LUA_DIR}/lmem.c
    ${LUA_DIR}/lobject.c
    ${LUA_DIR}/lopcodes.c
    ${LUA_DIR}/lparser.c
    ${LUA_DIR}/lstate.c
    ${LUA_DIR}/lstring.c
    ${LUA_DIR}/ltable.c
    ${LUA_DIR}/ltm.c
    ${LUA_DIR}/lundump.c
    ${LUA_DIR}/lvm.c
    ${LUA_DIR}/lzio.c
)

set(LUA_LIB_SRCS
    ${LUA_DIR}/lauxlib.c
    ${LUA_DIR}/lbaselib.c
    ${LUA_DIR}/lcorolib.c
    ${LUA_DIR}/ldblib.c
    ${LUA_DIR}/liolib.c
    ${LUA_DIR}/lmathlib.c
    ${LUA_DIR}/loadlib.c
    ${LUA_DIR}/loslib.c
    ${LUA_DIR}/lstrlib.c
    ${LUA_DIR}/ltablib.c
    ${LUA_DIR}/lutf8lib.c
    ${LUA_DIR}/linit.c
)

set(LIB_SRCS ${LUA_CORE_SRCS} ${LUA_LIB_SRCS})
aux_source_directory(${MODULE_PATH}/src LUANODE_SRCS)
aux_source_directory(${MODULE_PATH}/port LUANODE_PORT_SRCS)
list(APPEND LIB_SRCS ${LUANODE_SRCS} ${LUANODE_PORT_SRCS})

set(LIB_PUBLIC_INC ${MODULE_PATH}/include)
set(LIB_PRIVATE_INC ${MODULE_PATH}/port ${MODULE_PATH}/lua)

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

target_compile_definitions(${MODULE_NAME}
    PRIVATE
        LUA_COMPAT_5_4
        LUANODE_ENABLE=1
    )

list(APPEND COMPONENT_LIBS ${MODULE_NAME})
set(COMPONENT_LIBS "${COMPONENT_LIBS}" PARENT_SCOPE)
list(APPEND COMPONENT_PUBINC ${LIB_PUBLIC_INC})
set(COMPONENT_PUBINC "${COMPONENT_PUBINC}" PARENT_SCOPE)

endif()
