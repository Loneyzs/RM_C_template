cmake_minimum_required(VERSION 3.20.0)

# 将本仓库根目录加入 BOARD_ROOT / DTS_ROOT，
# 使 Zephyr 构建系统能够发现 boards/st/RM_C/* 以及 dts/bindings/* 下的本地绑定。
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND DTS_ROOT   ${CMAKE_CURRENT_SOURCE_DIR})

# 查找 Zephyr 包（必须在 project() 之前）
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# 定义项目名称
project(RM_C_Template)

# 添加源文件
set(APP_SOURCES
    src/main.c
    src/imu_9axis.c
)

target_sources(app PRIVATE ${APP_SOURCES})

# 添加包含目录
target_include_directories(app PRIVATE
    include
)

# 添加编译选项
target_compile_options(app PRIVATE
    -Wall
    -Wextra
)

target_compile_definitions(app PRIVATE
    APP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    APP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
)

# 条件编译：如果启用了调试模式
if(CONFIG_DEBUG)
    target_compile_definitions(app PRIVATE DEBUG_MODE=1)
endif()