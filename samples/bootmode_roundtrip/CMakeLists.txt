# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20.0)

# Pick up the Lily Ultra boards from the bootloader directory.
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../bootloader)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../bootloader)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bootmode_roundtrip LANGUAGES C)
target_sources(app PRIVATE src/main.c)

# Zephyr 4.4's CONFIG_BUILD_OUTPUT_UF2 passes an empty base address when the
# board uses zephyr,mapped-partition (CONFIG_FLASH_LOAD_OFFSET is never
# defined). Convert from the .hex instead, which carries absolute addresses.
set(UF2_FAMILY_ID 0x4ED75C3F)
add_custom_target(uf2 ALL
  COMMAND ${PYTHON_EXECUTABLE} ${ZEPHYR_BASE}/scripts/build/uf2conv.py
          -c -f ${UF2_FAMILY_ID} -o ${CMAKE_BINARY_DIR}/zephyr/zephyr.uf2
          ${CMAKE_BINARY_DIR}/zephyr/zephyr.hex
  BYPRODUCTS ${CMAKE_BINARY_DIR}/zephyr/zephyr.uf2
  COMMENT "Generating zephyr.uf2 from zephyr.hex"
)
add_dependencies(uf2 zephyr_final)
