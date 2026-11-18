# Host tests for the UF2 writer. Usage: make -C tests/host
SRC := ../../bootloader/src
CFLAGS := -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
	-Istubs -I$(SRC) -DCONFIG_UF2_FAMILY_ID=0x4ED75C3F \
	-include stdlib.h

test_uf2_writer: test_uf2_writer.c $(SRC)/uf2_writer.c
	$(CC) $(CFLAGS) -o $@ $^

ui_preview: ui_preview.c $(SRC)/display/ui_render.c $(SRC)/display/gfx.c $(SRC)/display/fonts.c
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -I$(SRC) -I$(SRC)/display -Istubs -o $@ $^

.PHONY: preview
preview: ui_preview
	./ui_preview

.PHONY: run
run: test_uf2_writer
	./test_uf2_writer

.DEFAULT_GOAL := run
