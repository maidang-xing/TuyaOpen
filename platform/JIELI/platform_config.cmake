message(STATUS "[JIELI] AC7916A/wl82 platform selected")

# The first milestone is a UART-only bring-up. Keep the TuyaOpen component
# graph out of this image until the corresponding Jieli adapters exist.
set(PLATFORM_SKIP_DEFAULT_COMPONENTS ON)

if(DEFINED ENV{JIELI_SDK_ROOT})
    set(JIELI_SDK_ROOT "$ENV{JIELI_SDK_ROOT}")
else()
    set(JIELI_SDK_ROOT "${PLATFORM_PATH}/../../../AC79_AIoT_SDK")
endif()
set(JIELI_SDK_ROOT "${JIELI_SDK_ROOT}" CACHE PATH "AC79 SDK root")

# The legacy Jieli toolchain ships lto-ar but no llvm-ranlib. A host ranlib is
# sufficient for the archive index and avoids generating a non-existent tool
# path during CMake's compiler identification.
find_program(JIELI_RANLIB NAMES ranlib REQUIRED)
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
    "${TOP_SOURCE_DIR}/src/common/include"
)

list(APPEND PLATFORM_PUBINC
    "${JIELI_SDK_ROOT}/include_lib/driver/device"
    "${JIELI_SDK_ROOT}/include_lib/driver/cpu/wl82"
)

set(PLATFORM_NEED_LIBS "")
