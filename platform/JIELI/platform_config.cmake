message(STATUS "[JIELI] wl82 platform selected (board AC7916A)")

if(CONFIG_JIELI_MINIMAL_HELLO STREQUAL "y")
    set(PLATFORM_SKIP_DEFAULT_COMPONENTS ON)
else()
    set(PLATFORM_SKIP_DEFAULT_COMPONENTS OFF)
endif()

if(DEFINED ENV{JIELI_SDK_ROOT})
    set(JIELI_SDK_ROOT "$ENV{JIELI_SDK_ROOT}")
else()
    set(JIELI_SDK_ROOT "${PLATFORM_PATH}/../../../AC79_AIoT_SDK")
endif()
set(JIELI_SDK_ROOT "${JIELI_SDK_ROOT}" CACHE PATH "AC79 SDK root")

# The legacy Jieli toolchain's lto-ar must create the archive index. A host
# ranlib cannot index pi32v2 bitcode archives used by the full TuyaOpen image.
set(JIELI_RANLIB "${PLATFORM_PATH}/jieli_ranlib.sh")
set(CMAKE_RANLIB "${JIELI_RANLIB}" CACHE FILEPATH "Jieli archive indexer" FORCE)
set(CMAKE_C_COMPILER_RANLIB "${JIELI_RANLIB}" CACHE FILEPATH "Jieli C archive indexer" FORCE)
set(CMAKE_CXX_COMPILER_RANLIB "${JIELI_RANLIB}" CACHE FILEPATH "Jieli C++ archive indexer" FORCE)

set(JIELI_ADAPTER_PATH "${PLATFORM_PATH}/tuyaos/tuyaos_adapter")
list(APPEND PLATFORM_PUBINC
    "${JIELI_ADAPTER_PATH}/include"
    "${TOP_SOURCE_DIR}/tools/porting/adapter/system"
    "${TOP_SOURCE_DIR}/tools/porting/adapter/uart"
    "${TOP_SOURCE_DIR}/tools/porting/adapter/init/include"
    "${TOP_SOURCE_DIR}/tools/porting/adapter/utilities/include"
    "${TOP_SOURCE_DIR}/tools/porting/adapter/security"
    "${TOP_SOURCE_DIR}/tools/porting/adapter/flash"
    "${TOP_SOURCE_DIR}/tools/porting/adapter/network"
    "${TOP_SOURCE_DIR}/tools/porting/adapter/wifi"
    "${TOP_SOURCE_DIR}/tools/porting/adapter/bluetooth"
    "${TOP_SOURCE_DIR}/tools/porting/adapter/timer"
    "${TOP_SOURCE_DIR}/src/common/include"
)

list(APPEND PLATFORM_PUBINC
    "${JIELI_SDK_ROOT}/include_lib/driver/device"
    "${JIELI_SDK_ROOT}/include_lib/driver/cpu/wl82"
    "${JIELI_SDK_ROOT}/include_lib/net/lwip_2_2_0"
    "${JIELI_SDK_ROOT}/include_lib/net/lwip_2_2_0/lwip/src/include"
    "${JIELI_SDK_ROOT}/include_lib/net/lwip_2_2_0/lwip/src/include/compat"
    "${JIELI_SDK_ROOT}/include_lib/net/lwip_2_2_0/lwip/port"
    "${JIELI_SDK_ROOT}/include_lib"
    "${JIELI_SDK_ROOT}/include_lib/btstack"
    "${JIELI_SDK_ROOT}/include_lib/btstack/le"
    "${JIELI_SDK_ROOT}/include_lib/btctrler"
    "${JIELI_SDK_ROOT}/include_lib/btctrler/port/wl82"
    "${JIELI_SDK_ROOT}/include_lib/net"
    "${JIELI_SDK_ROOT}/include_lib/system"
    "${JIELI_SDK_ROOT}/include_lib/system/generic"
    "${JIELI_SDK_ROOT}/include_lib/utils"
    "${JIELI_SDK_ROOT}/include_lib/utils/syscfg"
    "${JIELI_SDK_ROOT}/include_lib/utils/event"
)

set(PLATFORM_NEED_LIBS "")
