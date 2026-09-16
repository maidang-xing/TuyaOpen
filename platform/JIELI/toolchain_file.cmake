set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR pi32v2)

if(DEFINED ENV{JIELI_SDK_ROOT})
    set(JIELI_SDK_ROOT "$ENV{JIELI_SDK_ROOT}")
else()
    set(JIELI_SDK_ROOT "${PLATFORM_PATH}/../../../AC79_AIoT_SDK")
endif()
set(JIELI_SDK_ROOT "${JIELI_SDK_ROOT}" CACHE PATH "AC79 SDK root")

if(DEFINED ENV{JIELI_TOOL_DIR})
    set(JIELI_TOOL_DIR "$ENV{JIELI_TOOL_DIR}")
else()
    set(JIELI_TOOL_DIR "${PLATFORM_PATH}/../../jieli-toolchain/pi32v2/bin")
endif()

set(CMAKE_C_COMPILER "${JIELI_TOOL_DIR}/clang")
set(CMAKE_CXX_COMPILER "${JIELI_TOOL_DIR}/clang")
set(CMAKE_AR "${JIELI_TOOL_DIR}/lto-ar")
set(CMAKE_RANLIB "${PLATFORM_PATH}/jieli_ranlib.sh")

set(JIELI_NEWLIB_INCLUDE "${JIELI_SDK_ROOT}/include_lib/newlib/include")
set(JIELI_CPP_INCLUDE "${JIELI_SDK_ROOT}/include_lib/c++/include")
set(JIELI_C_INCLUDE_FLAGS "-isystem${JIELI_NEWLIB_INCLUDE}")
set(JIELI_CXX_INCLUDE_FLAGS
    "-isystem${JIELI_NEWLIB_INCLUDE} -isystem${JIELI_CPP_INCLUDE}")
set(JIELI_COMMON_DEFINES
    "-DCONFIG_CPU_WL82 -DCONFIG_FREE_RTOS_ENABLE -DCONFIG_THREAD_ENABLE -DBOOL_DEFINE_CONFLICT -DMBEDTLS_TCPIP_LWIP -D_GNU_SOURCE -D_XOPEN_SOURCE=700 -D__ELF__")

set(CMAKE_C_FLAGS
    "-target pi32v2 -integrated-as -mcpu=r3 -mfprev1 -Oz -flto -fno-common -ffunction-sections -fdata-sections -fno-unwind-tables -include stdbool.h -D__GCC_PI32V2__ ${JIELI_COMMON_DEFINES} ${JIELI_C_INCLUDE_FLAGS}")
set(CMAKE_CXX_FLAGS
    "-target pi32v2 -integrated-as -mcpu=r3 -mfprev1 -Oz -flto -fno-common -ffunction-sections -fdata-sections -fno-unwind-tables -fno-exceptions -fno-rtti -std=gnu++14 -D__GCC_PI32V2__ ${JIELI_COMMON_DEFINES} ${JIELI_CXX_INCLUDE_FLAGS}")

if(NOT EXISTS "${CMAKE_C_COMPILER}")
    message(FATAL_ERROR
        "Jieli clang not found: ${CMAKE_C_COMPILER}. "
        "Set JIELI_TOOL_DIR to pi32v2/bin before building.")
endif()

set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)
