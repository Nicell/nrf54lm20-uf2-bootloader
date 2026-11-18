# Host tests for the UF2 writer. Usage: make -C tests/host
SRC := ../../bootloader/src
CFLAGS := -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
	-Istubs -I$(SRC) -DCONFIG_UF2_FAMILY_ID=0x4ED75C3F -DCONFIG_UF2_REBOOT_DELAY_MS=250 \
	-include stdlib.h

test_uf2_writer: test_uf2_writer.c $(SRC)/uf2_writer.c
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: run
run: test_uf2_writer
	./test_uf2_writer

.DEFAULT_GOAL := run
