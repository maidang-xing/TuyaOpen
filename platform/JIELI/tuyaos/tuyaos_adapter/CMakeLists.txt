set(JIELI_ADAPTER_NAME jieli_tuya_adapter)
set(JIELI_ADAPTER_SRC
    ${CMAKE_CURRENT_LIST_DIR}/src/tkl_output.c
    ${CMAKE_CURRENT_LIST_DIR}/src/tkl_system.c
    ${CMAKE_CURRENT_LIST_DIR}/src/tkl_uart.c
)

add_library(${JIELI_ADAPTER_NAME} STATIC ${JIELI_ADAPTER_SRC})
target_include_directories(${JIELI_ADAPTER_NAME}
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${TOP_SOURCE_DIR}/tools/porting/adapter/system
        ${TOP_SOURCE_DIR}/tools/porting/adapter/uart
        ${TOP_SOURCE_DIR}/tools/porting/adapter/init/include
        ${TOP_SOURCE_DIR}/tools/porting/adapter/utilities/include
        ${TOP_SOURCE_DIR}/src/common/include
        ${JIELI_SDK_ROOT}/include_lib
        ${JIELI_SDK_ROOT}/include_lib/driver
        ${JIELI_SDK_ROOT}/include_lib/driver/device
        ${JIELI_SDK_ROOT}/include_lib/driver/cpu/wl82
        ${JIELI_SDK_ROOT}/include_lib/system
)
