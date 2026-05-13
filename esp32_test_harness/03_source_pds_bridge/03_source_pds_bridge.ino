#include <Arduino.h>

#include "functions_to_print_source_pds_replies.h"
#include "functions_to_send_and_receive_chips_frames.h"
#include "functions_to_translate_serial_text_into_source_pds_commands.h"
#include "source_pds_command_contract.h"

#define USB_LINE_BUFFER_LENGTH 220

static chips_parser_type board_reply_parser;
static char usb_line_buffer[USB_LINE_BUFFER_LENGTH];
static uint16_t usb_line_length = 0u;

static void read_usb_serial_commands(void);
static void read_board_uart_replies(void);
static void handle_one_usb_character(char character);

void setup()
{
    Serial.begin(USB_SERIAL_BAUD);
    Serial2.begin(
        SAMD21_UART_BAUD,
        SERIAL_8N1,
        ESP32_UART_RX_PIN,
        ESP32_UART_TX_PIN);

    reset_chips_parser(&board_reply_parser);
    delay(300);

    Serial.println();
    Serial.println("Source PDS ESP32 bridge ready");
    Serial.println("USB serial: 115200 baud");
    Serial.println("SAMD21 UART: ESP32 RX=GPIO16, TX=GPIO17, 115200 baud");
    print_source_pds_bridge_help(Serial);
}

void loop()
{
    read_usb_serial_commands();
    read_board_uart_replies();
}

static void read_usb_serial_commands(void)
{
    while (Serial.available() > 0)
    {
        handle_one_usb_character((char)Serial.read());
    }
}

static void handle_one_usb_character(char character)
{
    if ((character == '\r') || (character == '\n'))
    {
        if (usb_line_length == 0u)
        {
            return;
        }
        usb_line_buffer[usb_line_length] = '\0';
        execute_text_command_from_usb(usb_line_buffer, Serial, Serial2);
        usb_line_length = 0u;
        return;
    }

    if (usb_line_length < (USB_LINE_BUFFER_LENGTH - 1u))
    {
        usb_line_buffer[usb_line_length++] = character;
    }
}

static void read_board_uart_replies(void)
{
    while (Serial2.available() > 0)
    {
        chips_frame_type reply;
        chips_parse_result_type result =
            parse_one_chips_byte(&board_reply_parser, (uint8_t)Serial2.read(), &reply);

        if (result == CHIPS_PARSE_FRAME_READY)
        {
            print_reply_received_from_source_pds_board(&reply, Serial);
        }
        else if (result == CHIPS_PARSE_CRC_MISMATCH)
        {
            Serial.println("RX error: CHIPS CRC mismatch");
        }
        else if (result == CHIPS_PARSE_FRAME_TOO_LONG)
        {
            Serial.println("RX error: CHIPS frame too long");
        }
    }
}
