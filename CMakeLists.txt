# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fire_guard_rt)

zephyr_include_directories(src)

target_sources(app PRIVATE
  src/main.c
  src/app/channels.c
  src/app/config.c
  src/app/threads.c
  src/app/shell_cmds.c
  src/fusion/fusion.c
  src/fusion/fusion_thread.c
  src/sensors/sensor_adc.c
  src/sensors/sensor_dht.c
  src/sensors/sensor_ultrasonic.c
  src/sensors/sensor_rfid.c
  src/drivers/rc522.c
  src/actuators/actuators.c
  src/safety/fault_mgr.c
  src/safety/event_log.c
)

target_sources_ifdef(CONFIG_FG_COMM app PRIVATE
  src/comm/coreiot.c
)

# Wokwi flash image (build/wokwi_flash.bin): app placed at 0x1000
set(FG_WOKWI_BIN ${CMAKE_BINARY_DIR}/wokwi_flash.bin)
add_custom_command(
  OUTPUT ${FG_WOKWI_BIN}
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mk_flash_image.py
          ${ZEPHYR_BINARY_DIR}/zephyr.bin ${FG_WOKWI_BIN}
  DEPENDS zephyr_final ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mk_flash_image.py
  COMMENT "Generating wokwi_flash.bin"
)
add_custom_target(fg_wokwi_image ALL DEPENDS ${FG_WOKWI_BIN})
