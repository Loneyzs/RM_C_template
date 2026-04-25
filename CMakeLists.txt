cmake_minimum_required(VERSION 3.20.0)

# Add this repository root as a custom board/devicetree root.
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(RM_C_Template)

set(APP_SOURCES
    src/main.c
    src/drivers/imu/imu_board_init.c
    src/drivers/imu/imu_9axis.c
    src/board_package/unit_tests/uart_test.c
    src/board_package/unit_tests/pwm_led_test.c
    src/board_package/unit_tests/can_loopback_test.c
    src/board_package/unit_tests/bmi088_test.c
    src/board_package/unit_tests/ist8310_test.c
    src/board_package/unit_tests/imu_temperature_test.c
    src/board_package/unit_tests/imu_justfloat_test.c
)

target_sources(app PRIVATE ${APP_SOURCES})

target_include_directories(app PRIVATE include)

target_compile_options(app PRIVATE
    -Wall
    -Wextra
)

if(CONFIG_DEBUG)
    target_compile_definitions(app PRIVATE DEBUG_MODE=1)
endif()
