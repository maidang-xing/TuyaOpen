set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR pi32v2)

if(DEFINED ENV{JIELI_TOOL_DIR})
    set(JIELI_TOOL_DIR "$ENV{JIELI_TOOL_DIR}")
else()
    set(JIELI_TOOL_DIR "${PLATFORM_PATH}/../../jieli-toolchain/pi32v2/bin")
endif()

set(CMAKE_C_COMPILER "${JIELI_TOOL_DIR}/clang")
set(CMAKE_CXX_COMPILER "${JIELI_TOOL_DIR}/clang")
set(CMAKE_AR "${JIELI_TOOL_DIR}/lto-ar")
set(CMAKE_RANLIB "${JIELI_TOOL_DIR}/llvm-ranlib")

if(NOT EXISTS "${CMAKE_C_COMPILER}")
    message(FATAL_ERROR
        "Jieli clang not found: ${CMAKE_C_COMPILER}. "
        "Set JIELI_TOOL_DIR to pi32v2/bin before building.")
endif()

set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)
