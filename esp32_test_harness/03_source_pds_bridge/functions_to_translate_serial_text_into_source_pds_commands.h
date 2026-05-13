#ifndef FUNCTIONS_TO_TRANSLATE_SERIAL_TEXT_INTO_SOURCE_PDS_COMMANDS_H
#define FUNCTIONS_TO_TRANSLATE_SERIAL_TEXT_INTO_SOURCE_PDS_COMMANDS_H

#include <Arduino.h>

void print_source_pds_bridge_help(Stream &output);
void execute_text_command_from_usb(
    char *line,
    Stream &usb_serial,
    Stream &board_uart);

#endif
