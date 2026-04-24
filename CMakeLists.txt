cmake_minimum_required(VERSION 3.20.0)

# 将本仓库根目录加入 BOARD_ROOT / DTS_ROOT
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(RM_C_Template)

set(APP_SOURCES
    src/main.c
    src/imu_board_init.c
    src/imu_9axis.c
    src/minimal_tests/minimal_test_serial.c
    src/minimal_tests/test_can_loopback.c
    src/minimal_tests/test_pwm_led.c
    src/minimal_tests/test_imu_uart.c
    src/minimal_tests/uart_minimal_test.c
    src/minimal_tests/bmi088_minimal_test.c
    src/minimal_tests/ist8310_minimal_test.c
)

target_sources(app PRIVATE ${APP_SOURCES})

target_include_directories(app PRIVATE
    include
)

target_compile_options(app PRIVATE
    -Wall
    -Wextra
)

target_compile_definitions(app PRIVATE
    APP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    APP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
)

if(CONFIG_DEBUG)
    target_compile_definitions(app PRIVATE DEBUG_MODE=1)
endif()
