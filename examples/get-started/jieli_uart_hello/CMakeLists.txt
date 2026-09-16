set(APP_PATH ${CMAKE_CURRENT_LIST_DIR})
set(APP_SRCS ${APP_PATH}/src/example_jieli_uart_hello.c)

add_library(${EXAMPLE_LIB})
target_sources(${EXAMPLE_LIB} PRIVATE ${APP_SRCS})
