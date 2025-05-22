##
# @file CMakeLists.txt
# @brief 
#/

# MODULE_PATH
set(MODULE_PATH ${CMAKE_CURRENT_LIST_DIR})

# MODULE_NAME
get_filename_component(MODULE_NAME ${MODULE_PATH} NAME)

# LIB_SRCS
aux_source_directory(${MODULE_PATH} LIB_SRCS)

# LIB_PUBLIC_INC
set(LIB_PUBLIC_INC ${MODULE_PATH})

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
        # 保持原有其他路径配置...
        ${MODULE_PATH}/../../../platform/ESP32/esp-idf/components
        ${MODULE_PATH}/../../../platform/ESP32/esp-idf/components/esp_common/include
        ${MODULE_PATH}/../../../platform/ESP32/esp-idf/components/hal/include
        ${MODULE_PATH}/../../../platform/ESP32/esp-idf/components/esp_driver_i2c/include
        ${MODULE_PATH}/../../../platform/ESP32/esp-idf/components/esp_driver_i2c/include/driver
        ${MODULE_PATH}/../../../platform/ESP32/esp-idf/components/esp_driver_i2s/include
        ${MODULE_PATH}/../../../platform/ESP32/esp-idf/components/esp_driver_i2s/include/driver
        ${MODULE_PATH}/../../../platform/ESP32/esp-idf/components/esp_driver_gpio/include/driver
        ${MODULE_PATH}/../../../platform/ESP32/esp-idf/components/hal/include
        ${MODULE_PATH}/../../../platform/ESP32/tuya_open_sdk/managed_components/espressif__esp-dsp/modules/common/include_sim
        ${MODULE_PATH}/../../../platform/ESP32/esp-idf/components/soc/esp32s3/include/soc
    )


########################################
# Layer Configure
########################################
list(APPEND COMPONENT_LIBS ${MODULE_NAME})
set(COMPONENT_LIBS "${COMPONENT_LIBS}" PARENT_SCOPE)
list(APPEND COMPONENT_PUBINC ${LIB_PUBLIC_INC})
set(COMPONENT_PUBINC "${COMPONENT_PUBINC}" PARENT_SCOPE)
