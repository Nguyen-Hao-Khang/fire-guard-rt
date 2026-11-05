# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fg_fusion_test)

set(FG_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
zephyr_include_directories(${FG_SRC})
target_sources(app PRIVATE src/test_fusion.c ${FG_SRC}/fusion/fusion.c)
