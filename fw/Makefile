# MX29F1615 Programmer Rev 1 is STM32F407-Discovery
# MX29F1615 Programmer Rev 2 is STM32F103
# MX29F1615 Programmer Rev 3+ is STM32F107

BOARD_REV ?= 3


SRCS   := main.c clock.c gpio.c printf.c timer.c uart.c usb.c version.c \
	  led.c irq.c mem_access.c readline.c cmdline.c cmds.c pcmds.c \
	  prom_access.c mx29f1615.c utils.c crc32.c adc.c button.c

OBJDIR := objs
OBJS   := $(SRCS:%.c=$(OBJDIR)/%.o)

# libopencm3 is a git submodule in the current directory
OPENCM3_DIR := libopencm3
OPENCM3_HEADER := $(OPENCM3_DIR)/include/libopencm3/stm32/rcc.h

# Our output name
BINARY = $(OBJDIR)/fw

# Linker script for our MCU
LDSCRIPT = stm32f1.ld  # Also works for STM32F4


ifeq ($(BOARD_REV), 1)
$(info Building for STM32F4)
LIBNAME		:= opencm3_stm32f4
ARCH_FLAGS	:= -mthumb -mcpu=cortex-m4 -mhard-float
DEFS		+= -DSTM32F4 -DSTM32F407xx  # Rev 1
else ifeq ($(BOARD_REV), 2)
$(info Building for STM32F103)
LIBNAME		:= opencm3_stm32f1
ARCH_FLAGS	:= -mthumb -mcpu=cortex-m3 -msoft-float
DEFS		+= -DSTM32F1 -DSTM32F103xE  # Rev 2
else
$(info Building for STM32F107)
LIBNAME		:= opencm3_stm32f1
ARCH_FLAGS	:= -mthumb -mcpu=cortex-m3 -msoft-float
DEFS		+= -DSTM32F1 -DSTM32F107xC  # Rev 3+
endif
DEFS		+= -DEMBEDDED_CMD

OPENCM3_LIB := $(OPENCM3_DIR)/lib/lib$(LIBNAME).a

# Where the Black Magic Probe is attached
BMP_PORT = /dev/ttyACM0

DFU_UTIL=dfu-util
ST_BUILD_DIR=stutils
ST_TOOLS_PATH=$(ST_BUILD_DIR)/build/Release/bin

NOW  := $(shell date)
DATE := $(shell date -d '$(NOW)' '+%Y-%m-%d')
TIME := $(shell date -d '$(NOW)' '+%H:%M:%S')

## Boilerplate

# Compiler configuration
PREFIX		?= arm-none-eabi
CC		:= $(PREFIX)-gcc
CXX		:= $(PREFIX)-g++
LD		:= $(PREFIX)-gcc
AR		:= $(PREFIX)-ar
AS		:= $(PREFIX)-as
SIZE		:= $(PREFIX)-size
OBJCOPY		:= $(PREFIX)-objcopy
OBJDUMP		:= $(PREFIX)-objdump
GDB		:= $(PREFIX)-gdb
STFLASH		= $(shell which st-flash)
OPT		:= -Os
DEBUG		:= -ggdb3
CSTD		?= -std=gnu99

# C flags
TGT_CFLAGS	+= $(OPT) $(CSTD) $(DEBUG)
TGT_CFLAGS	+= $(ARCH_FLAGS)
TGT_CFLAGS	+= -Wextra -Wshadow -Wimplicit-function-declaration
TGT_CFLAGS	+= -Wredundant-decls -Wmissing-prototypes -Wstrict-prototypes
TGT_CFLAGS	+= -fno-common -ffunction-sections -fdata-sections
TGT_CFLAGS	+= -DBUILD_DATE=\"$(DATE)\" -DBUILD_TIME=\"$(TIME)\"

# C++ flags
TGT_CXXFLAGS	+= $(OPT) $(CXXSTD) $(DEBUG)
TGT_CXXFLAGS	+= $(ARCH_FLAGS)
TGT_CXXFLAGS	+= -Wextra -Wshadow -Wredundant-decls -Weffc++
TGT_CXXFLAGS	+= -fno-common -ffunction-sections -fdata-sections
TGT_CXXFLAGS	+= -std=c++11

# C & C++ preprocessor common flags
TGT_CPPFLAGS	+= -MD
TGT_CPPFLAGS	+= -Wall -Wundef
TGT_CPPFLAGS	+= $(DEFS)
TGT_CPPFLAGS	+= -Wno-unused-parameter

# Linker flags
TGT_LDFLAGS		+= --static -nostartfiles
TGT_LDFLAGS		+= -T$(LDSCRIPT)
TGT_LDFLAGS		+= $(ARCH_FLAGS) $(DEBUG)
TGT_LDFLAGS		+= -Wl,-Map=$(OBJDIR)/$*.map -Wl,--cref
TGT_LDFLAGS		+= -Wl,--gc-sections
ifeq ($(V),99)
TGT_LDFLAGS		+= -Wl,--print-gc-sections
endif

# Used libraries
DEFS		+= -I$(OPENCM3_DIR)/include
LDFLAGS		+= -L$(OPENCM3_DIR)/lib
LDLIBS		+= -l$(LIBNAME)
LDLIBS		+= -Wl,--start-group -lc -lgcc -lnosys -Wl,--end-group

.SUFFIXES: .elf .bin .hex .srec .list .map .images
.SECONDEXPANSION:
.SECONDARY:

all: bin size
size: $(BINARY).size
elf: $(BINARY).elf
bin: $(BINARY).bin
hex: $(BINARY).hex
srec: $(BINARY).srec
list: $(BINARY).list
flashbmp: $(BINARY).flashbmp

GENERATED_BINARIES=$(BINARY).elf $(BINARY).bin $(BINARY).hex $(BINARY).srec $(BINARY).list $(BINARY).map

$(OBJDIR)/%.bin: $(OBJDIR)/%.elf
	$(OBJCOPY) -Obinary $< $@

$(OBJDIR)/%.hex: $(OBJDIR)/%.elf
	$(OBJCOPY) -Oihex $< $@

$(OBJDIR)/%.srec: $(OBJDIR)/%.elf
	$(OBJCOPY) -Osrec $< $@

$(OBJDIR)/%.list: $(OBJDIR)/%.elf
	$(OBJDUMP) -S $< > $@

$(OBJDIR)/%.map: $(OBJDIR)/%.elf

$(OBJDIR)/%.elf: $(OBJS) $(LDSCRIPT) | $(OPENCM3_LIB)
	$(LD) $(TGT_LDFLAGS) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

$(OBJDIR)/%.o: %.c
	$(CC) $(TGT_CFLAGS) $(CFLAGS) $(TGT_CPPFLAGS) $(CPPFLAGS) -o $@ -c $(*).c

$(OBJDIR)/%.o: %.cxx
	$(CXX) $(TGT_CXXFLAGS) $(CXXFLAGS) $(TGT_CPPFLAGS) $(CPPFLAGS) -o $@ -c $(*).cxx

$(OBJDIR)/%.o: %.cpp
	$(CXX) $(TGT_CXXFLAGS) $(CXXFLAGS) $(TGT_CPPFLAGS) $(CPPFLAGS) -o $@ -c $(*).cpp

$(OBJDIR)/%.size: $(OBJDIR)/%.elf
	@echo "Output code size:"
	@$(SIZE) -A -d $< | egrep 'text|data|bss' | awk ' \
    function human(x) { \
        if (x<1000) {return x} else {x/=1024} \
        s="kMGTEPZY"; \
        while (x>=1000 && length(s)>1) \
            {x/=1024; s=substr(s,2)} \
        return int(x+0.5) substr(s,1,1) \
    } \
	{printf("%10s %8s\n", $$1, human($$2))} \
'
$(OBJDIR)/version.o: $(filter-out $(OBJDIR)/version.o, $(OBJS))

$(OBJS): Makefile | $(OBJDIR) $(OPENCM3_DIR) $(OPENCM3_HEADER)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OPENCM3_DIR) $(OPENCM3_HEADER):
	@echo Cloning $@
	git submodule update
	make -C $(OPENCM3_DIR) -j8

$(OPENCM3_LIB): | $(OPENCM3_DIR)

#	@echo Building $@
#	make -C $(OPENCM3_DIR) -j8

%.flashbmp: %.elf
	$(GDB) --batch \
		-ex 'target extended-remote $(BMP_PORT)' \
		-x bmp_flash.scr \
		$(OBJDIR)/$*.elf

clean:
	$(RM) $(GENERATED_BINARIES) generated.* $(OBJS) $(OBJS:%.o=%.d)

# Dependencies
-include $(OBJS:.o=.d)

# Write STM32 flash using DFU mode
just-dfu:
	$(DFU_UTIL) --device 0483:df11 --alt 0 --download $(BINARY).bin --dfuse-address 0x08000000:leave

# Write STM32 flash using ST-Link
just-flash: $(ST_TOOLS_PATH)/st-flash | $(BINARY).bin
	$(ST_TOOLS_PATH)/st-flash --reset write $(BINARY).bin 0x08000000

# Erase STM32 flash using ST-Link
just-erase: $(ST_TOOLS_PATH)/st-flash
	$(ST_TOOLS_PATH)/st-flash --debug --flash=0x100000 erase

dfu: all just-dfu
flash: all just-flash

# Connect to remote STM32 via ST-Link (follow with "make gdb")
stlink:
	$(ST_TOOLS_PATH)/st-util

# Get ST-TOOLS
$(ST_BUILD_DIR) get-stutils:
	git clone https://github.com/texane/stlink.git stutils

# Build ST-TOOLS
$(ST_TOOLS_PATH)/st-flash build-stutils: | $(ST_BUILD_DIR)
	make -C $(ST_BUILD_DIR)

gdb:
	gdb -q -x .gdbinit $(BINARY).elf

.PHONY: images clean get-stutils build_stutils stlink dfu flash just-dfu just-flash clean size elf bin hex srec list
