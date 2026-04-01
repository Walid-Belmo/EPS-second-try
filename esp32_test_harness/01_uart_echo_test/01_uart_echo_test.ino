/*
 * 01_uart_echo_test.ino
 * ESP32 test harness for SAMD21 OBC UART communication (Phase 3).
 *
 * This sketch sends "PING" to the SAMD21 every 2 seconds and prints
 * any response received. The user can also type messages in the Arduino
 * Serial Monitor to send arbitrary strings to the SAMD21.
 *
 * Wiring (3 wires):
 *   ESP32 GPIO17 (TX2) -----> SAMD21 PA05 (RX, pin 6 on left header)
 *   ESP32 GPIO16 (RX2) <----- SAMD21 PA04 (TX, pin 5 on left header)
 *   ESP32 GND -------------- SAMD21 GND
 *
 * Both boards are 3.3V — no level shifting needed.
 *
 * If your ESP32 board uses different pins for Serial2, change the
 * ESP32_UART_RX_PIN and ESP32_UART_TX_PIN values below.
 *
 * Usage:
 *   1. Open Arduino IDE, select your ESP32 board
 *   2. Upload this sketch
 *   3. Open Serial Monitor at 115200 baud
 *   4. You should see "PING sent" every 2 seconds
 *   5. When the SAMD21 echoes back, you will see "Received: PING"
 *   6. Type any message in the Serial Monitor input box and press Enter
 *      to send it to the SAMD21
 */

/* Pin definitions for Serial2 on ESP32 DevKit v1 (ESP32-WROOM-32).
 * Change these if your board has different default Serial2 pins. */
#define ESP32_UART_RX_PIN  16   /* Connect to SAMD21 PA04 (TX) */
#define ESP32_UART_TX_PIN  17   /* Connect to SAMD21 PA05 (RX) */

/* How often to send a PING message, in milliseconds. */
#define PING_INTERVAL_MS   2000

static unsigned long last_ping_time_ms = 0;

void setup()
{
    /* Serial: USB connection to PC (Arduino Serial Monitor). */
    Serial.begin(115200);
    while (!Serial)
    {
        /* Wait for USB serial to be ready (some boards need this). */
    }

    /* Serial2: hardware UART connected to the SAMD21. */
    Serial2.begin(115200, SERIAL_8N1, ESP32_UART_RX_PIN, ESP32_UART_TX_PIN);

    Serial.println("=== ESP32 UART Echo Test ===");
    Serial.println("Connected to SAMD21 on Serial2 (115200 baud)");
    Serial.print("  RX pin (from SAMD21 TX): GPIO");
    Serial.println(ESP32_UART_RX_PIN);
    Serial.print("  TX pin (to SAMD21 RX):   GPIO");
    Serial.println(ESP32_UART_TX_PIN);
    Serial.println("Sending PING every 2 seconds...");
    Serial.println("Type a message and press Enter to send it to SAMD21.");
    Serial.println("---");
}

void loop()
{
    /* ── Send PING every 2 seconds ─────────────────────────────────── */
    unsigned long now_ms = millis();
    if ((now_ms - last_ping_time_ms) >= PING_INTERVAL_MS)
    {
        Serial2.println("PING");
        Serial.println("[TX] PING");
        last_ping_time_ms = now_ms;
    }

    /* ── Print anything received from SAMD21 ───────────────────────── */
    while (Serial2.available() > 0)
    {
        String received_line = Serial2.readStringUntil('\n');
        received_line.trim();
        if (received_line.length() > 0)
        {
            Serial.print("[RX] Received: ");
            Serial.println(received_line);
        }
    }

    /* ── Forward user input from Serial Monitor to SAMD21 ──────────── */
    while (Serial.available() > 0)
    {
        String user_input = Serial.readStringUntil('\n');
        user_input.trim();
        if (user_input.length() > 0)
        {
            Serial2.println(user_input);
            Serial.print("[TX] ");
            Serial.println(user_input);
        }
    }
}
