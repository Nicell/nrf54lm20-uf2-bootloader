# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20.0)

# Lily Ultra board definitions and the uf2,matrix-key binding live here.
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(uf2_bootloader LANGUAGES C)

target_sources(app PRIVATE
    src/main.c
    src/vregusb_reset.c
    src/app_image.c
    src/boot_entry.c
    src/uf2_status.c
    src/uf2_writer.c
    src/usbd_setup.c
    src/ghostfat.c
    src/ghostfat_disk.c
    src/board_flash.c
    src/indicator.c
)

target_sources_ifdef(CONFIG_UF2_DISPLAY app PRIVATE
    src/display/lpm_panel.c
    src/display/gfx.c
    src/display/fonts.c
    src/display/ui.c
    src/display/ui_render.c
)

zephyr_linker_sources(SECTIONS src/sections-rom.ld)

target_include_directories(app PRIVATE src)
