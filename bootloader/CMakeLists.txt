# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(uf2_bootloader LANGUAGES C)

target_sources(app PRIVATE
    src/main.c
    src/usbd_setup.c
    src/ghostfat.c
    src/ghostfat_disk.c
    src/board_flash.c
    src/dbl_tap.c
    src/boot_jump.c
    src/indicator.c
)

target_include_directories(app PRIVATE src)
