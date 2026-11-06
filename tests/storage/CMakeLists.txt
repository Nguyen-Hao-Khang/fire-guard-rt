# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fg_storage_test)

set(FG_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
zephyr_include_directories(${FG_SRC})
target_sources(app PRIVATE
  src/test_storage.c
  ${FG_SRC}/app/channels.c
  ${FG_SRC}/app/config.c
  ${FG_SRC}/app/storage.c
  ${FG_SRC}/safety/event_log.c
)
