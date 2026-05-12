# Makefile — satellite firmware build system
# Toolchain: arm-none-eabi-gcc (Arm GNU Toolchain 12.2)
#
# This Makefile supports two physical boards. Pick one EXPLICITLY every time
# (there is intentionally no default — silent defaults caused confusion):
#
#   make BOARD=devboard         -> Curiosity Nano DM320119 (SAMD21G17D, 48-pin)
#   make BOARD=devboard flash   -> ditto, then flash
#   make BOARD=mainboard        -> EPS PCU testing board V4.1
#                                  (SAMD21J17D-MUT, 64-pin QFN)
#   make BOARD=mainboard flash  -> ditto, then flash
#   make clean                  -> wipes build/ regardless of BOARD
#
# IMPORTANT: ALWAYS run `make clean` when switching the BOARD variable.
# Object files are written to a single build/ directory and reusing them
# across different chip variants would silently produce a broken binary.
#
# For the file-by-file map of what compiles into which build, see:
#   docs/build_targets_and_file_map.md

CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size

TARGET := satellite_firmware
BUILD  := build/
OPENOCD_CONFIG ?= openocd.cfg

# ── Board selector — BOARD must be set explicitly on the command line ───────
# (`make clean` is the one exception — it does not need BOARD because it just
# wipes the build/ directory regardless of which board was last built.)

ifeq ($(MAKECMDGOALS),clean)
  # `make clean` does not need any BOARD-specific configuration; skip the
  # whole selector and just let the clean rule run.
else
  ifndef BOARD
    $(error BOARD is not set. Run `make BOARD=devboard ...` or `make BOARD=mainboard ...`. \
There is no default. See docs/build_targets_and_file_map.md for the file map.)
  endif

  ifeq ($(BOARD),devboard)
    # Microchip Curiosity Nano DM320119: SAMD21G17D + on-board nEDBG.
    CHIP_NAME_DEFINE := -D__SAMD21G17D__
    STARTUP_SRCS     := startup/startup_samd21g17d.c startup/system_samd21g17d.c
    LINKER_SCRIPT    := samd21g17d_flash.ld
    APP_SRCS         := src/main.c \
                        src/drivers/clock_configure_48mhz_dfll_open_loop.c \
                        src/drivers/debug_functions.c \
                        src/drivers/uart_obc_sercom0_pa10_pa11_on_devboard.c \
                        src/drivers/millisecond_tick_timer_using_arm_systick.c \
                        src/drivers/pwm_buck_converter_complementary_on_devboard.c \
                        src/drivers/chips_protocol_encode_decode_frames_with_crc16_kermit.c \
                        src/assertion_handler.c \
                        src/mppt_algorithm.c \
                        src/eps_state_machine.c \
                        src/eps_demo_chips_command_dispatch.c
    EXTRA_DEFINES    := -DDEBUG_LOGGING_ENABLED
  else ifeq ($(BOARD),mainboard)
    # CHESS EPS PCU testing board V4.1: SAMD21J17D-MUT (64-pin QFN).
    CHIP_NAME_DEFINE := -D__SAMD21J17D__
    STARTUP_SRCS     := startup/startup_samd21j17d.c startup/system_samd21j17d.c
    LINKER_SCRIPT    := samd21j17d_flash.ld
    APP_SRCS         := src/main_mainboard_chips_injection_demo.c \
                        src/drivers/clock_configure_48mhz_dfll_open_loop.c \
                        src/drivers/led_status_pb22_active_high_on_mainboard.c \
                        src/drivers/uart_obc_sercom0_pa10_pa11_on_mainboard.c \
                        src/drivers/mainboard_adc_reader.c \
                        src/drivers/millisecond_tick_timer_using_arm_systick.c \
                        src/drivers/pwm_buck_converter_tcc0_pa12_pa13_on_mainboard.c \
                        src/drivers/chips_protocol_encode_decode_frames_with_crc16_kermit.c \
                        src/assertion_handler.c \
                        src/mppt_algorithm.c \
                        src/eps_state_machine.c \
                        src/eps_demo_chips_command_dispatch.c
    EXTRA_DEFINES    :=
  else
    $(error BOARD must be 'devboard' or 'mainboard' (got '$(BOARD)'))
  endif
endif

# ── Source list ──────────────────────────────────────────────────────────────

SRCS := $(APP_SRCS) $(STARTUP_SRCS) syscalls_min.c

OBJS := $(addprefix $(BUILD), $(addsuffix .o, $(basename $(notdir $(SRCS)))))

# Include paths — use -isystem for vendor headers to suppress their warnings
INC := -I src -I src/drivers
VENDOR_INC := -isystem lib/cmsis -isystem lib/samd21-dfp -isystem startup

# ── CPU flags — same Cortex-M0+ silicon on both boards ───────────────────────
CPU := -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft

CFLAGS  := $(CPU) $(INC) $(VENDOR_INC)
CFLAGS  += $(CHIP_NAME_DEFINE) -DUSE_CMSIS_INIT $(EXTRA_DEFINES)
CFLAGS  += -std=c99
CFLAGS  += -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CFLAGS  += -ffunction-sections -fdata-sections -fno-common
CFLAGS  += -g -O0

LDFLAGS := $(CPU)
LDFLAGS += -T $(LINKER_SCRIPT)
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
	openocd -f $(OPENOCD_CONFIG) \
		-c "program $(BUILD)$(TARGET).bin verify reset exit 0x00000000"

clean:
	rm -rf $(BUILD)

# Flight build: drop debug logging, optimise for size. Currently only changes
# the devboard build (mainboard has no DEBUG_LOGGING_ENABLED to filter).
flight: CFLAGS := $(filter-out -DDEBUG_LOGGING_ENABLED -O0, $(CFLAGS))
flight: CFLAGS += -Os
flight: all

.PHONY: all flash clean flight
