# Makefile — satellite firmware build system for SAMD21G17D
# Board: Curiosity Nano DM320119
# Toolchain: arm-none-eabi-gcc (Arm GNU Toolchain 12.2)

CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size

TARGET := satellite_firmware
BUILD  := build/

# ── Add every new .c file here ────────────────────────────────────────────────
SRCS :=
SRCS += src/main.c
SRCS += src/drivers/clock_configure_48mhz_dfll_open_loop.c
SRCS += src/drivers/debug_functions.c
SRCS += startup/startup_samd21g17d.c
SRCS += startup/system_samd21g17d.c
SRCS += syscalls_min.c

OBJS := $(addprefix $(BUILD), $(addsuffix .o, $(basename $(notdir $(SRCS)))))

# Include paths — use -isystem for vendor headers to suppress their warnings
INC := -I src -I src/drivers
VENDOR_INC := -isystem lib/cmsis -isystem lib/samd21-dfp -isystem startup

# ── CPU flags — must match SAMD21G17D exactly ─────────────────────────────────
CPU := -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft

CFLAGS  := $(CPU) $(INC) $(VENDOR_INC)
CFLAGS  += -D__SAMD21G17D__ -DUSE_CMSIS_INIT -DDEBUG_LOGGING_ENABLED
CFLAGS  += -std=c99
CFLAGS  += -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CFLAGS  += -ffunction-sections -fdata-sections -fno-common
CFLAGS  += -g -O0

LDFLAGS := $(CPU)
LDFLAGS += -T samd21g17d_flash.ld
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,-Map=$(BUILD)$(TARGET).map
LDFLAGS += --specs=nano.specs

all: $(BUILD)$(TARGET).bin
	@$(SIZE) $(BUILD)$(TARGET).elf

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)$(TARGET).elf: $(OBJS) | $(BUILD)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD)$(TARGET).bin: $(BUILD)$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)%.o: src/drivers/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)%.o: startup/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

flash: all
	openocd -f openocd.cfg \
		-c "program $(BUILD)$(TARGET).bin verify reset exit 0x00000000"

clean:
	rm -rf $(BUILD)

# Flight build: no logging, optimise for size
flight: CFLAGS := $(filter-out -DDEBUG_LOGGING_ENABLED -O0, $(CFLAGS))
flight: CFLAGS += -Os
flight: all

.PHONY: all flash clean flight
