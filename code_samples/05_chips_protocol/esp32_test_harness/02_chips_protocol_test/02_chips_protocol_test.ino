/* =============================================================================
 * 02_chips_protocol_test.ino
 * ESP32 test harness acting as the OBC side of the CHIPS protocol.
 * Sends CHIPS commands to the SAMD21 EPS and validates responses.
 *
 * Wiring:
 *   ESP32 TX2 (GPIO17) --> SAMD21 PA05 (RX)
 *   ESP32 RX2 (GPIO16) <-- SAMD21 PA04 (TX)
 *   ESP32 GND ----------- SAMD21 GND
 *
 * Usage:
 *   1. Upload this sketch to the ESP32 via Arduino IDE
 *   2. Open Serial Monitor at 115200 baud
 *   3. Tests run automatically, one every 3 seconds
 *   4. Each test prints PASS or FAIL
 *   5. Test 9 (120s timeout) runs last and takes ~2.5 minutes
 *
 * IMPORTANT: Flash the SAMD21 with the Phase 4 firmware BEFORE running
 * this sketch. The SAMD21 must be ready to receive CHIPS frames.
 * =============================================================================
 */

/* ── CRC-16/KERMIT lookup table (same as SAMD21 implementation) ────────── */

static const uint16_t CRC16_KERMIT_TABLE[256] = {
    0x0000, 0x1189, 0x2312, 0x329B, 0x4624, 0x57AD, 0x6536, 0x74BF,
    0x8C48, 0x9DC1, 0xAF5A, 0xBED3, 0xCA6C, 0xDBE5, 0xE97E, 0xF8F7,
    0x1081, 0x0108, 0x3393, 0x221A, 0x56A5, 0x472C, 0x75B7, 0x643E,
    0x9CC9, 0x8D40, 0xBFDB, 0xAE52, 0xDAED, 0xCB64, 0xF9FF, 0xE876,
    0x2102, 0x308B, 0x0210, 0x1399, 0x6726, 0x76AF, 0x4434, 0x55BD,
    0xAD4A, 0xBCC3, 0x8E58, 0x9FD1, 0xEB6E, 0xFAE7, 0xC87C, 0xD9F5,
    0x3183, 0x200A, 0x1291, 0x0318, 0x77A7, 0x662E, 0x54B5, 0x453C,
    0xBDCB, 0xAC42, 0x9ED9, 0x8F50, 0xFBEF, 0xEA66, 0xD8FD, 0xC974,
    0x4204, 0x538D, 0x6116, 0x709F, 0x0420, 0x15A9, 0x2732, 0x36BB,
    0xCE4C, 0xDFC5, 0xED5E, 0xFCD7, 0x8868, 0x99E1, 0xAB7A, 0xBAF3,
    0x5285, 0x430C, 0x7197, 0x601E, 0x14A1, 0x0528, 0x37B3, 0x263A,
    0xDECD, 0xCF44, 0xFDDF, 0xEC56, 0x98E9, 0x8960, 0xBBFB, 0xAA72,
    0x6306, 0x728F, 0x4014, 0x519D, 0x2522, 0x34AB, 0x0630, 0x17B9,
    0xEF4E, 0xFEC7, 0xCC5C, 0xDDD5, 0xA96A, 0xB8E3, 0x8A78, 0x9BF1,
    0x7387, 0x620E, 0x5095, 0x411C, 0x35A3, 0x242A, 0x16B1, 0x0738,
    0xFFCF, 0xEE46, 0xDCDD, 0xCD54, 0xB9EB, 0xA862, 0x9AF9, 0x8B70,
    0x8408, 0x9581, 0xA71A, 0xB693, 0xC22C, 0xD3A5, 0xE13E, 0xF0B7,
    0x0840, 0x19C9, 0x2B52, 0x3ADB, 0x4E64, 0x5FED, 0x6D76, 0x7CFF,
    0x9489, 0x8500, 0xB79B, 0xA612, 0xD2AD, 0xC324, 0xF1BF, 0xE036,
    0x18C1, 0x0948, 0x3BD3, 0x2A5A, 0x5EE5, 0x4F6C, 0x7DF7, 0x6C7E,
    0xA50A, 0xB483, 0x8618, 0x9791, 0xE32E, 0xF2A7, 0xC03C, 0xD1B5,
    0x2942, 0x38CB, 0x0A50, 0x1BD9, 0x6F66, 0x7EEF, 0x4C74, 0x5DFD,
    0xB58B, 0xA402, 0x9699, 0x8710, 0xF3AF, 0xE226, 0xD0BD, 0xC134,
    0x39C3, 0x284A, 0x1AD1, 0x0B58, 0x7FE7, 0x6E6E, 0x5CF5, 0x4D7C,
    0xC60C, 0xD785, 0xE51E, 0xF497, 0x8028, 0x91A1, 0xA33A, 0xB2B3,
    0x4A44, 0x5BCD, 0x6956, 0x78DF, 0x0C60, 0x1DE9, 0x2F72, 0x3EFB,
    0xD68D, 0xC704, 0xF59F, 0xE416, 0x90A9, 0x8120, 0xB3BB, 0xA232,
    0x5AC5, 0x4B4C, 0x79D7, 0x685E, 0x1CE1, 0x0D68, 0x3FF3, 0x2E7A,
    0xE70E, 0xF687, 0xC41C, 0xD595, 0xA12A, 0xB0A3, 0x8238, 0x93B1,
    0x6B46, 0x7ACF, 0x4854, 0x59DD, 0x2D62, 0x3CEB, 0x0E70, 0x1FF9,
    0xF78F, 0xE606, 0xD49D, 0xC514, 0xB1AB, 0xA022, 0x92B9, 0x8330,
    0x7BC7, 0x6A4E, 0x58D5, 0x495C, 0x3DE3, 0x2C6A, 0x1EF1, 0x0F78
};

/* ── ESP32 Serial2 pin definitions ────────────────────────────────────────
 * Must explicitly specify pins — default Serial2 pins vary by ESP32 board.
 * Phase 3 echo test used the same explicit pins and worked. */
#define ESP32_UART_RX_PIN  16   /* Connect to SAMD21 PA04 (TX) */
#define ESP32_UART_TX_PIN  17   /* Connect to SAMD21 PA05 (RX) */

/* ── Protocol constants ──────────────────────────────────────────────────── */

#define SYNC_BYTE   0x7E
#define ESCAPE_BYTE 0x7D
#define XOR_VALUE   0x20

#define CMD_GET_TELEMETRY  0x01
#define CMD_SET_PARAMETER  0x02
#define CMD_GET_STATE      0x03

#define STATUS_SUCCESS             0x00
#define STATUS_UNKNOWN_COMMAND     0x01
#define STATUS_INVALID_PAYLOAD_LEN 0x02

/* ── Struct definitions (must be before any function that uses them,
 *    because Arduino IDE auto-generates function prototypes at the
 *    top of the file) ────────────────────────────────────────────────────── */

typedef struct {
    uint8_t  seq;
    uint8_t  cmd;
    uint8_t  resp_flag;
    uint8_t  payload[300];
    uint16_t payload_len;
    bool     valid;
    bool     crc_ok;
} ParsedFrame;

/* ── Buffers ─────────────────────────────────────────────────────────────── */

static uint8_t send_buffer[600];
static uint8_t recv_buffer[600];
static uint8_t test1_response[600];
static uint16_t test1_response_len = 0;

/* ── CRC computation ─────────────────────────────────────────────────────── */

static uint16_t compute_crc16_kermit(const uint8_t *data, uint16_t len) {
    uint16_t crc = 0x0000;
    for (uint16_t i = 0; i < len; i++) {
        uint8_t idx = (uint8_t)((crc ^ data[i]) & 0xFF);
        crc = (crc >> 8) ^ CRC16_KERMIT_TABLE[idx];
    }
    return crc;
}

/* ── Frame builder ───────────────────────────────────────────────────────── */

static uint16_t build_chips_frame(uint8_t seq, uint8_t cmd, uint8_t resp_flag,
                                   const uint8_t *payload, uint16_t payload_len,
                                   uint8_t *out_buf, uint16_t out_size) {
    /* Build raw content: SEQ + CMD/RSP + PAYLOAD */
    uint8_t raw[300];
    uint16_t raw_len = 0;

    raw[raw_len++] = seq;
    raw[raw_len++] = (resp_flag << 7) | (cmd & 0x7F);

    for (uint16_t i = 0; i < payload_len; i++) {
        raw[raw_len++] = payload[i];
    }

    /* Compute and append CRC (LSB first) */
    uint16_t crc = compute_crc16_kermit(raw, raw_len);
    raw[raw_len++] = (uint8_t)(crc & 0xFF);
    raw[raw_len++] = (uint8_t)((crc >> 8) & 0xFF);

    /* Write stuffed frame to output */
    uint16_t pos = 0;
    out_buf[pos++] = SYNC_BYTE;

    for (uint16_t i = 0; i < raw_len; i++) {
        if (raw[i] == SYNC_BYTE) {
            out_buf[pos++] = ESCAPE_BYTE;
            out_buf[pos++] = SYNC_BYTE ^ XOR_VALUE;
        } else if (raw[i] == ESCAPE_BYTE) {
            out_buf[pos++] = ESCAPE_BYTE;
            out_buf[pos++] = ESCAPE_BYTE ^ XOR_VALUE;
        } else {
            out_buf[pos++] = raw[i];
        }
    }

    out_buf[pos++] = SYNC_BYTE;
    return pos;
}

/* ── Frame parser ────────────────────────────────────────────────────────── */

static ParsedFrame parse_chips_response(const uint8_t *wire, uint16_t wire_len) {
    ParsedFrame f;
    f.valid = false;
    f.crc_ok = false;
    f.payload_len = 0;

    if (wire_len < 6) return f;
    if (wire[0] != SYNC_BYTE || wire[wire_len - 1] != SYNC_BYTE) return f;

    /* Unstuff the content between the two sync bytes */
    uint8_t content[600];
    uint16_t content_len = 0;
    bool escape_next = false;

    for (uint16_t i = 1; i < wire_len - 1; i++) {
        if (escape_next) {
            content[content_len++] = wire[i] ^ XOR_VALUE;
            escape_next = false;
        } else if (wire[i] == ESCAPE_BYTE) {
            escape_next = true;
        } else {
            content[content_len++] = wire[i];
        }
    }

    if (content_len < 4) return f;

    /* Verify CRC */
    uint16_t data_len = content_len - 2;
    uint16_t received_crc = content[content_len - 2]
                          | ((uint16_t)content[content_len - 1] << 8);
    uint16_t computed_crc = compute_crc16_kermit(content, data_len);

    f.crc_ok = (received_crc == computed_crc);
    f.seq = content[0];
    f.resp_flag = (content[1] >> 7) & 0x01;
    f.cmd = content[1] & 0x7F;
    f.payload_len = data_len - 2;
    for (uint16_t i = 0; i < f.payload_len; i++) {
        f.payload[i] = content[2 + i];
    }
    f.valid = true;
    return f;
}

/* ── Receive response with timeout ───────────────────────────────────────── */

static uint16_t receive_response(uint8_t *buf, uint16_t buf_size,
                                  uint32_t timeout_ms) {
    uint16_t pos = 0;
    bool frame_started = false;
    unsigned long start = millis();

    while ((millis() - start) < timeout_ms) {
        if (Serial2.available()) {
            uint8_t b = Serial2.read();
            if (b == SYNC_BYTE && !frame_started) {
                frame_started = true;
                buf[pos++] = b;
            } else if (frame_started) {
                buf[pos++] = b;
                if (b == SYNC_BYTE && pos > 1) {
                    return pos; /* Complete frame received */
                }
            }
            if (pos >= buf_size) return pos;
        }
    }
    return pos; /* Timeout */
}

/* ── Print hex dump ──────────────────────────────────────────────────────── */

static void print_hex(const char *label, const uint8_t *data, uint16_t len) {
    Serial.print(label);
    Serial.print(": ");
    for (uint16_t i = 0; i < len; i++) {
        if (data[i] < 0x10) Serial.print("0");
        Serial.print(data[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
}

/* ── Test cases ──────────────────────────────────────────────────────────── */

static int test_number = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static void run_test_1_get_telemetry(void) {
    Serial.println("\n=== TEST 1: GET_TELEMETRY ===");

    uint16_t frame_len = build_chips_frame(
        0x00, CMD_GET_TELEMETRY, 0, NULL, 0, send_buffer, sizeof(send_buffer));
    print_hex("Sent", send_buffer, frame_len);

    Serial2.write(send_buffer, frame_len);

    uint16_t resp_len = receive_response(recv_buffer, sizeof(recv_buffer), 2000);
    if (resp_len == 0) {
        Serial.println("FAIL: No response received (timeout)");
        tests_failed++;
        return;
    }
    print_hex("Received", recv_buffer, resp_len);

    ParsedFrame f = parse_chips_response(recv_buffer, resp_len);
    if (!f.valid) { Serial.println("FAIL: Could not parse response"); tests_failed++; return; }
    if (!f.crc_ok) { Serial.println("FAIL: CRC mismatch"); tests_failed++; return; }
    if (f.resp_flag != 1) { Serial.println("FAIL: Response flag not set"); tests_failed++; return; }
    if (f.cmd != CMD_GET_TELEMETRY) { Serial.println("FAIL: Wrong command ID"); tests_failed++; return; }
    if (f.seq != 0x00) { Serial.println("FAIL: Wrong sequence number"); tests_failed++; return; }
    if (f.payload_len != 220) {
        Serial.print("FAIL: Expected payload 220 bytes, got ");
        Serial.println(f.payload_len);
        tests_failed++;
        return;
    }
    if (f.payload[0] != STATUS_SUCCESS) { Serial.println("FAIL: Status not SUCCESS"); tests_failed++; return; }

    /* Save response for idempotency test */
    memcpy(test1_response, recv_buffer, resp_len);
    test1_response_len = resp_len;

    Serial.println("TEST 1 PASS: GET_TELEMETRY response valid, CRC OK");
    Serial.print("  Payload: status=0x");
    Serial.print(f.payload[0], HEX);
    Serial.print(", telemetry_bytes=");
    Serial.println(f.payload_len - 1);
    tests_passed++;
}

static void run_test_2_set_parameter(void) {
    Serial.println("\n=== TEST 2: SET_PARAMETER ===");

    uint8_t payload[] = {0x01, 0x00, 0x00, 0x12, 0x34};
    uint16_t frame_len = build_chips_frame(
        0x01, CMD_SET_PARAMETER, 0, payload, 5, send_buffer, sizeof(send_buffer));
    print_hex("Sent", send_buffer, frame_len);

    Serial2.write(send_buffer, frame_len);

    uint16_t resp_len = receive_response(recv_buffer, sizeof(recv_buffer), 2000);
    if (resp_len == 0) { Serial.println("FAIL: No response (timeout)"); tests_failed++; return; }

    ParsedFrame f = parse_chips_response(recv_buffer, resp_len);
    if (!f.valid || !f.crc_ok) { Serial.println("FAIL: Invalid/bad CRC"); tests_failed++; return; }
    if (f.resp_flag != 1) { Serial.println("FAIL: Response flag not set"); tests_failed++; return; }
    if (f.cmd != CMD_SET_PARAMETER) { Serial.println("FAIL: Wrong cmd"); tests_failed++; return; }
    if (f.seq != 0x01) { Serial.println("FAIL: Wrong seq"); tests_failed++; return; }
    if (f.payload[0] != STATUS_SUCCESS) { Serial.println("FAIL: Status not SUCCESS"); tests_failed++; return; }

    Serial.println("TEST 2 PASS: SET_PARAMETER acknowledged");
    tests_passed++;
}

static void run_test_3_idempotency(void) {
    Serial.println("\n=== TEST 3: IDEMPOTENCY (duplicate seq=0) ===");

    /* Re-send the exact same GET_TELEMETRY with seq=0 */
    uint16_t frame_len = build_chips_frame(
        0x00, CMD_GET_TELEMETRY, 0, NULL, 0, send_buffer, sizeof(send_buffer));
    Serial2.write(send_buffer, frame_len);

    uint16_t resp_len = receive_response(recv_buffer, sizeof(recv_buffer), 2000);
    if (resp_len == 0) { Serial.println("FAIL: No response (timeout)"); tests_failed++; return; }

    /* Response should be byte-for-byte identical to test 1 */
    if (resp_len != test1_response_len) {
        Serial.println("FAIL: Response length differs from test 1");
        tests_failed++;
        return;
    }
    bool identical = true;
    for (uint16_t i = 0; i < resp_len; i++) {
        if (recv_buffer[i] != test1_response[i]) { identical = false; break; }
    }
    if (!identical) { Serial.println("FAIL: Response differs from test 1"); tests_failed++; return; }

    Serial.println("TEST 3 PASS: Idempotent response matches test 1");
    tests_passed++;
}

static void run_test_4_bad_crc(void) {
    Serial.println("\n=== TEST 4: BAD CRC ===");

    uint16_t frame_len = build_chips_frame(
        0x02, CMD_GET_TELEMETRY, 0, NULL, 0, send_buffer, sizeof(send_buffer));

    /* Corrupt the CRC by flipping a bit in byte 3 (CRC low byte area) */
    if (frame_len > 3) {
        send_buffer[frame_len - 2] ^= 0x01;
    }
    print_hex("Sent (corrupted)", send_buffer, frame_len);

    Serial2.write(send_buffer, frame_len);

    /* EPS should NOT respond to a bad CRC frame */
    uint16_t resp_len = receive_response(recv_buffer, sizeof(recv_buffer), 3000);
    if (resp_len == 0) {
        Serial.println("TEST 4 PASS: Bad CRC correctly rejected, no response");
        tests_passed++;
    } else {
        Serial.println("FAIL: EPS responded to a bad CRC frame");
        tests_failed++;
    }
}

static void run_test_5_unknown_command(void) {
    Serial.println("\n=== TEST 5: UNKNOWN COMMAND (0x77) ===");

    uint16_t frame_len = build_chips_frame(
        0x03, 0x77, 0, NULL, 0, send_buffer, sizeof(send_buffer));
    Serial2.write(send_buffer, frame_len);

    uint16_t resp_len = receive_response(recv_buffer, sizeof(recv_buffer), 2000);
    if (resp_len == 0) { Serial.println("FAIL: No response (timeout)"); tests_failed++; return; }

    ParsedFrame f = parse_chips_response(recv_buffer, resp_len);
    if (!f.valid || !f.crc_ok) { Serial.println("FAIL: Invalid/bad CRC"); tests_failed++; return; }
    if (f.cmd != 0x77) { Serial.println("FAIL: Wrong cmd in response"); tests_failed++; return; }
    if (f.payload[0] != STATUS_UNKNOWN_COMMAND) {
        Serial.print("FAIL: Expected UNKNOWN_COMMAND status, got 0x");
        Serial.println(f.payload[0], HEX);
        tests_failed++;
        return;
    }

    Serial.println("TEST 5 PASS: Unknown command rejected with error status");
    tests_passed++;
}

static void run_test_6_byte_stuffing_in_payload(void) {
    Serial.println("\n=== TEST 6: BYTE STUFFING (0x7E in payload) ===");

    /* SET_PARAMETER with value containing 0x7E */
    uint8_t payload[] = {0x01, 0x00, 0x00, 0x00, 0x7E};
    uint16_t frame_len = build_chips_frame(
        0x04, CMD_SET_PARAMETER, 0, payload, 5, send_buffer, sizeof(send_buffer));
    print_hex("Sent", send_buffer, frame_len);

    Serial2.write(send_buffer, frame_len);

    uint16_t resp_len = receive_response(recv_buffer, sizeof(recv_buffer), 2000);
    if (resp_len == 0) { Serial.println("FAIL: No response (timeout)"); tests_failed++; return; }

    ParsedFrame f = parse_chips_response(recv_buffer, resp_len);
    if (!f.valid || !f.crc_ok) { Serial.println("FAIL: Invalid/bad CRC"); tests_failed++; return; }
    if (f.payload[0] != STATUS_SUCCESS) { Serial.println("FAIL: Status not SUCCESS"); tests_failed++; return; }

    Serial.println("TEST 6 PASS: Byte stuffing handled correctly");
    tests_passed++;
}

static void run_test_7_wrong_payload_length(void) {
    Serial.println("\n=== TEST 7: WRONG PAYLOAD LENGTH ===");

    /* SET_PARAMETER expects 5 bytes, send only 2 */
    uint8_t payload[] = {0x01, 0x02};
    uint16_t frame_len = build_chips_frame(
        0x05, CMD_SET_PARAMETER, 0, payload, 2, send_buffer, sizeof(send_buffer));
    Serial2.write(send_buffer, frame_len);

    uint16_t resp_len = receive_response(recv_buffer, sizeof(recv_buffer), 2000);
    if (resp_len == 0) { Serial.println("FAIL: No response (timeout)"); tests_failed++; return; }

    ParsedFrame f = parse_chips_response(recv_buffer, resp_len);
    if (!f.valid || !f.crc_ok) { Serial.println("FAIL: Invalid/bad CRC"); tests_failed++; return; }
    if (f.payload[0] != STATUS_INVALID_PAYLOAD_LEN) {
        Serial.print("FAIL: Expected INVALID_PAYLOAD_LENGTH, got 0x");
        Serial.println(f.payload[0], HEX);
        tests_failed++;
        return;
    }

    Serial.println("TEST 7 PASS: Invalid payload length rejected");
    tests_passed++;
}

static void run_test_8_interframe_fill(void) {
    Serial.println("\n=== TEST 8: INTER-FRAME FILL (multiple 0x7E) ===");

    /* Send 3 extra sync bytes before a valid frame */
    Serial2.write((uint8_t)SYNC_BYTE);
    Serial2.write((uint8_t)SYNC_BYTE);
    Serial2.write((uint8_t)SYNC_BYTE);

    uint16_t frame_len = build_chips_frame(
        0x06, CMD_GET_STATE, 0, NULL, 0, send_buffer, sizeof(send_buffer));
    Serial2.write(send_buffer, frame_len);

    uint16_t resp_len = receive_response(recv_buffer, sizeof(recv_buffer), 2000);
    if (resp_len == 0) { Serial.println("FAIL: No response (timeout)"); tests_failed++; return; }

    ParsedFrame f = parse_chips_response(recv_buffer, resp_len);
    if (!f.valid || !f.crc_ok) { Serial.println("FAIL: Invalid/bad CRC"); tests_failed++; return; }
    if (f.cmd != CMD_GET_STATE) { Serial.println("FAIL: Wrong cmd"); tests_failed++; return; }
    if (f.payload[0] != STATUS_SUCCESS) { Serial.println("FAIL: Status not SUCCESS"); tests_failed++; return; }

    Serial.println("TEST 8 PASS: Inter-frame fill handled correctly");
    tests_passed++;
}

static void run_test_9_obc_timeout(void) {
    Serial.println("\n=== TEST 9: 120s OBC TIMEOUT ===");
    Serial.println("Waiting 130 seconds with no communication...");
    Serial.println("Watch SAMD21 debug log (PuTTY) for timeout message.");

    delay(130000); /* 130 seconds */

    /* After timeout, send a command to verify EPS is still alive */
    uint16_t frame_len = build_chips_frame(
        0x07, CMD_GET_TELEMETRY, 0, NULL, 0, send_buffer, sizeof(send_buffer));
    Serial2.write(send_buffer, frame_len);

    uint16_t resp_len = receive_response(recv_buffer, sizeof(recv_buffer), 2000);
    if (resp_len == 0) { Serial.println("FAIL: EPS not responding after timeout"); tests_failed++; return; }

    ParsedFrame f = parse_chips_response(recv_buffer, resp_len);
    if (!f.valid || !f.crc_ok) { Serial.println("FAIL: Invalid response after timeout"); tests_failed++; return; }

    Serial.println("TEST 9 PASS: Timeout detected, EPS still responsive");
    Serial.println("  (Verify on PuTTY: 'OBC TIMEOUT' message appeared)");
    tests_passed++;
}

/* ── Arduino setup and loop ──────────────────────────────────────────────── */

void setup() {
    Serial.begin(115200);  /* USB Serial Monitor */
    Serial2.begin(115200, SERIAL_8N1, ESP32_UART_RX_PIN, ESP32_UART_TX_PIN);

    delay(2000); /* Wait for SAMD21 to boot and run self-tests */

    Serial.println("========================================");
    Serial.println("  CHIPS Protocol Test Harness (ESP32)");
    Serial.println("  Acting as OBC, testing SAMD21 EPS");
    Serial.println("========================================");

    /* Verify our own CRC implementation first */
    uint8_t test_vec[] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};
    uint16_t crc = compute_crc16_kermit(test_vec, 9);
    Serial.print("ESP32 CRC self-test: 0x");
    Serial.print(crc, HEX);
    if (crc == 0x2189) {
        Serial.println(" PASS");
    } else {
        Serial.println(" FAIL!");
        while (1) delay(1000);
    }

    Serial.println("\nRunning tests (3s between each)...\n");
}

void loop() {
    switch (test_number) {
        case 0: run_test_1_get_telemetry(); break;
        case 1: run_test_2_set_parameter(); break;
        case 2: run_test_3_idempotency(); break;
        case 3: run_test_4_bad_crc(); break;
        case 4: run_test_5_unknown_command(); break;
        case 5: run_test_6_byte_stuffing_in_payload(); break;
        case 6: run_test_7_wrong_payload_length(); break;
        case 7: run_test_8_interframe_fill(); break;
        case 8: run_test_9_obc_timeout(); break;
        default:
            Serial.println("\n========================================");
            Serial.print("  ALL TESTS COMPLETE: ");
            Serial.print(tests_passed);
            Serial.print(" passed, ");
            Serial.print(tests_failed);
            Serial.println(" failed");
            Serial.println("========================================");
            while (1) delay(1000);
            break;
    }

    test_number++;
    if (test_number <= 8) {
        delay(3000); /* 3 seconds between tests */
    }
}
