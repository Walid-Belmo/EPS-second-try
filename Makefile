# Makefile — satellite firmware build system
# Toolchain: arm-none-eabi-gcc (Arm GNU Toolchain 12.2)
#
# This Makefile supports two physical boards. Pick one EXPLICITLY every time
# (there is intentionally no default — silent defaults caused confusion):
#
#   make BOARD=devboard         -> Curiosity Nano DM320119 (SAMD21G17D, 48-pin)
#   make BOARD=devboard flash   -> ditto, then flash
#   make BOARD=devboard-pds     -> Source PDS firmware skeleton on devboard
#   make BOARD=mainboard        -> EPS PCU testing board V4.1
#                                  (SAMD21J17D-MUT, 64-pin QFN)
#   make BOARD=mainboard flash  -> ditto, then flash
#   make BOARD=mainboard-pds    -> Source PDS firmware skeleton on mainboard
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
  else ifeq ($(BOARD),devboard-pds)
    # Source Project Sylvester on the Curiosity Nano dev board.
    #
    # This uses the same cleaned src-pds logic as the real-board build, but it
    # selects the dev-board chip files and the dev-board PA10/PA11 UART driver.
    # PWM is intentionally disabled here so flashing a desk setup cannot drive
    # accidental buck-converter outputs.
    CHIP_NAME_DEFINE := -D__SAMD21G17D__
    STARTUP_SRCS     := startup/startup_samd21g17d.c startup/system_samd21g17d.c
    LINKER_SCRIPT    := samd21g17d_flash.ld
    APP_SRCS         := src-pds/app/main.c \
                        src-pds/board_hardware_startup/functions_to_initialize_board_hardware_before_main_loop_runs.c \
                        src-pds/communication_with_esp32/functions_to_read_chips_commands_received_from_esp32.c \
                        src-pds/communication_with_esp32/chips_reply_sending/functions_to_send_chips_replies_to_esp32.c \
                        src-pds/communication_with_esp32/command_execution/functions_to_execute_board_commands_received_from_esp32.c \
                        src-pds/communication_with_esp32/command_execution/payload_parsing/functions_to_parse_payloads_inside_board_commands.c \
                        src-pds/command_controlled_ram_values/functions_to_store_values_changed_by_esp32_commands.c \
                        src-pds/externally_controlled_board_behaviors/functions_to_apply_manually_requested_pwm_to_buck_converter.c \
                        src-pds/externally_controlled_board_behaviors/functions_to_keep_board_outputs_off_when_requested.c \
                        src-pds/externally_controlled_board_behaviors/functions_to_run_mppt_algorithm_with_simulated_solar_panel_curve.c \
                        src-pds/externally_controlled_board_behaviors/functions_to_run_power_state_machine_with_injected_sensor_values.c \
                        src-pds/status_reporting_to_esp32/functions_to_build_status_replies_sent_to_esp32.c \
                        src-pds/status_reporting_to_esp32/functions_to_stream_status_replies_to_esp32.c \
                        src-pds/board_outputs/functions_to_apply_allowed_pwm_to_board_hardware.c \
                        src-pds/board_outputs/functions_to_block_outputs_when_faults_are_injected.c \
                        src-pds/board_outputs/functions_to_store_requested_pwm_output_before_safety_checks.c \
                        src-pds/shared_helpers/functions_to_read_and_write_little_endian_values.c \
                        src/drivers/clock_configure_48mhz_dfll_open_loop.c \
                        src/drivers/uart_obc_sercom0_pa10_pa11_on_devboard.c \
                        src/drivers/millisecond_tick_timer_using_arm_systick.c \
                        src/drivers/pwm_buck_converter_disabled_stub.c \
                        src/drivers/chips_protocol_encode_decode_frames_with_crc16_kermit.c \
                        src/assertion_handler.c \
                        src/mppt_algorithm.c \
                        src/eps_state_machine.c
    EXTRA_DEFINES    :=
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
  else ifeq ($(BOARD),mainboard-pds)
    # Source Project Sylvester: cleaned firmware architecture for the same
    # CHESS EPS PCU testing board V4.1 hardware.
    CHIP_NAME_DEFINE := -D__SAMD21J17D__
    STARTUP_SRCS     := startup/startup_samd21j17d.c startup/system_samd21j17d.c
    LINKER_SCRIPT    := samd21j17d_flash.ld
    APP_SRCS         := src-pds/app/main.c \
                        src-pds/board_hardware_startup/functions_to_initialize_board_hardware_before_main_loop_runs.c \
                        src-pds/communication_with_esp32/functions_to_read_chips_commands_received_from_esp32.c \
                        src-pds/communication_with_esp32/chips_reply_sending/functions_to_send_chips_replies_to_esp32.c \
                        src-pds/communication_with_esp32/command_execution/functions_to_execute_board_commands_received_from_esp32.c \
                        src-pds/communication_with_esp32/command_execution/payload_parsing/functions_to_parse_payloads_inside_board_commands.c \
                        src-pds/command_controlled_ram_values/functions_to_store_values_changed_by_esp32_commands.c \
                        src-pds/externally_controlled_board_behaviors/functions_to_apply_manually_requested_pwm_to_buck_converter.c \
                        src-pds/externally_controlled_board_behaviors/functions_to_keep_board_outputs_off_when_requested.c \
                        src-pds/externally_controlled_board_behaviors/functions_to_run_mppt_algorithm_with_simulated_solar_panel_curve.c \
                        src-pds/externally_controlled_board_behaviors/functions_to_run_power_state_machine_with_injected_sensor_values.c \
                        src-pds/status_reporting_to_esp32/functions_to_build_status_replies_sent_to_esp32.c \
                        src-pds/status_reporting_to_esp32/functions_to_stream_status_replies_to_esp32.c \
                        src-pds/board_outputs/functions_to_apply_allowed_pwm_to_board_hardware.c \
                        src-pds/board_outputs/functions_to_block_outputs_when_faults_are_injected.c \
                        src-pds/board_outputs/functions_to_store_requested_pwm_output_before_safety_checks.c \
                        src-pds/shared_helpers/functions_to_read_and_write_little_endian_values.c \
                        src/drivers/clock_configure_48mhz_dfll_open_loop.c \
                        src/drivers/uart_obc_sercom0_pa10_pa11_on_mainboard.c \
                        src/drivers/mainboard_adc_reader.c \
                        src/drivers/millisecond_tick_timer_using_arm_systick.c \
                        src/drivers/pwm_buck_converter_tcc0_pa12_pa13_on_mainboard.c \
                        src/drivers/chips_protocol_encode_decode_frames_with_crc16_kermit.c \
                        src/assertion_handler.c \
                        src/mppt_algorithm.c \
                        src/eps_state_machine.c
    EXTRA_DEFINES    :=
  else
    $(error BOARD must be 'devboard', 'devboard-pds', 'mainboard', or 'mainboard-pds' (got '$(BOARD)'))
  endif
endif

# ── Source list ──────────────────────────────────────────────────────────────

SRCS := $(APP_SRCS) $(STARTUP_SRCS) syscalls_min.c

OBJS := $(addprefix $(BUILD), $(patsubst %.c,%.o,$(SRCS)))

# Include paths — use -isystem for vendor headers to suppress their warnings
INC := -I src -I src/drivers -I src-pds
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
	if not exist "$(BUILD)" mkdir "$(BUILD)"

$(BUILD)$(TARGET).elf: $(OBJS) | $(BUILD)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD)$(TARGET).bin: $(BUILD)$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)%.o: src/%.c | $(BUILD)
	if not exist "$(dir $@)" mkdir "$(dir $@)"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)%.o: src-pds/%.c | $(BUILD)
	if not exist "$(dir $@)" mkdir "$(dir $@)"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)%.o: src/drivers/%.c | $(BUILD)
	if not exist "$(dir $@)" mkdir "$(dir $@)"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)%.o: startup/%.c | $(BUILD)
	if not exist "$(dir $@)" mkdir "$(dir $@)"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)%.o: %.c | $(BUILD)
	if not exist "$(dir $@)" mkdir "$(dir $@)"
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
