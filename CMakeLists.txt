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

target_sources_ifdef(CONFIG_FG_PERSIST app PRIVATE
  src/app/storage.c
)

target_sources_ifdef(CONFIG_FG_RT_STATS app PRIVATE
  src/safety/rt_stats.c
)

target_sources_ifdef(CONFIG_FG_TRACE app PRIVATE
  src/safety/rt_trace.c
)

target_sources_ifdef(CONFIG_FG_BENCH app PRIVATE
  src/app/bench.c
)

target_sources_ifdef(CONFIG_FG_DIAG_STATS app PRIVATE
  src/app/diag.c
)

target_sources_ifdef(CONFIG_FG_COMM app PRIVATE
  src/comm/coreiot.c
)

# Wokwi loads build/flasher_args.json (same format as ESP-IDF). Zephyr on
# ESP32 uses simple boot: the application image itself sits at 0x1000 and
# is started by the ROM loader, so no second-stage bootloader is listed.
file(WRITE ${CMAKE_BINARY_DIR}/flasher_args.json
"{
  \"write_flash_args\": [\"--flash_mode\", \"dio\", \"--flash_size\", \"4MB\", \"--flash_freq\", \"40m\"],
  \"flash_settings\": {\"flash_mode\": \"dio\", \"flash_size\": \"4MB\", \"flash_freq\": \"40m\"},
  \"flash_files\": {\"0x1000\": \"zephyr/zephyr.bin\"},
  \"extra_esptool_args\": {\"after\": \"hard_reset\", \"before\": \"default_reset\", \"stub\": true, \"chip\": \"esp32\"}
}
")
