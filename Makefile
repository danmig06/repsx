NAME := repsx-core
CC := clang

INCLUDE_DIR := include
SRC_DIR := src
OUT_DIR := .
FILES := cpu.c gte.c memory.c bios.c system.c log.c util.c opcodes.c memctl.c gpu.c dma.c input/pad.c input/backupunit.c gpucmds.c irq.c exe.c spu.c mdec.c sio.c sched.c cdrom/queue.c cdrom/cdrom.c cdrom/cmd.c cdrom/disc.c timer.c
SRCS := $(FILES:%=$(SRC_DIR)/%)
CFLAGS := -shared -fPIC -I$(INCLUDE_DIR) -Ofast -g3 -DLOG_USE_COLOR -Wall -Wextra -Wno-initializer-overrides -Wno-unused-parameter -Wno-unused-function

$(NAME): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(OUT_DIR)/lib$(NAME).so

clean:
	rm lib$(NAME).so
