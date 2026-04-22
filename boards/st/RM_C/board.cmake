# SPDX-License-Identifier: Apache-2.0

board_runner_args(openocd --cmd-reset-halt "reset halt")
board_runner_args(jlink "--device=STM32F407IG" "--speed=4000")
board_runner_args(pyocd "--target=stm32f407ig")

include(${ZEPHYR_BASE}/boards/common/openocd-stm32.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
