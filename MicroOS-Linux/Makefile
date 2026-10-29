CC = /opt/mipsel-buildroot-linux-gnu_sdk-buildroot/bin/mips-mti-linux-gnu-gcc
LVGL_DIR = ./lvgl

CFLAGS = -O2 -Wall -I. -I$(LVGL_DIR) -I$(LVGL_DIR)/../ -D_GNU_SOURCE
CFLAGS += --sysroot=/opt/mipsel-buildroot-linux-gnu_sdk-buildroot/mipsel-buildroot-linux-gnu/sysroot
LDFLAGS = -ldl -lpthread -lpng

CSRCS += main.c input.c draw.c
include $(LVGL_DIR)/lvgl.mk

OBJEXT ?= .o
COBJS = $(CSRCS:.c=$(OBJEXT))
OBJS = $(COBJS)

TOTAL_FILES := $(words $(CSRCS))
COUNTER = 0

all: default

%.o: %.c
	@$(eval COUNTER=$(shell echo $$(($(COUNTER)+1))))
	@printf "[ %3d / %3d ] Compiling: %s\n" $(COUNTER) $(TOTAL_FILES) $<
	@$(CC) $(CFLAGS) -c $< -o $@

default: $(OBJS)
	@echo "--------------------------------------------------"
	@echo "Linking all objects into executable..."
	@$(CC) -o MicroOS $(OBJS) $(LDFLAGS)
	@echo "DONE: lvgl_launcher successfully created!"
	@echo "--------------------------------------------------"

clean:
	@echo "Cleaning up object files and binary..."
	@rm -f $(OBJS) MicroOS
