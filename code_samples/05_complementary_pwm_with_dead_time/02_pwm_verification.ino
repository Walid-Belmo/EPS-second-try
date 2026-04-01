/* =========================================================================
 * ESP32 PWM Verification Test Harness
 * CHESS CubeSat EPS — Phase 5: TCC0 Complementary PWM Verification
 *
 * This sketch runs on the ESP32 and independently measures the PWM
 * signals coming out of the SAMD21's TCC0 peripheral on two pins.
 * It verifies frequency, complementary operation, and duty cycle.
 *
 * Wiring:
 *   SAMD21 PA18 (TCC0 WO[2]) --> ESP32 GPIO 34
 *   SAMD21 PA20 (TCC0 WO[6]) --> ESP32 GPIO 35
 *   SAMD21 GND               --> ESP32 GND
 *
 * Usage:
 *   Open Serial Monitor at 115200 baud.
 *   Type a number and press Enter to run a test:
 *     1 = Frequency on WO[2]  (expect ~300000 Hz)
 *     2 = Frequency on WO[6]  (expect ~300000 Hz)
 *     3 = Complementary check (expect 0 violations)
 *     4 = Duty cycle on WO[2] (shows % HIGH time)
 *     h = Show help menu
 *
 * All tests are non-destructive and can be re-run at any time
 * without reflashing.
 *
 * Technical notes:
 *   - Uses PCNT (Pulse Counter) hardware peripheral for frequency
 *     measurement. PCNT runs at 80 MHz APB clock and counts edges
 *     independently of the CPU. Accurate up to ~40 MHz.
 *   - Uses GPIO_IN1_REG for simultaneous pin reads. GPIO 34 and 35
 *     are both in the 32-39 range, so they appear in the same
 *     32-bit register and are sampled at the exact same instant.
 *   - Does NOT use pulseIn() because it returns integer microseconds,
 *     which gives 40-100% error at 300 kHz (3.33 us period).
 *   - GPIO 34 and 35 are input-only pins on the ESP32. They cannot
 *     accidentally become outputs, which is safer for measurement.
 * =========================================================================
 */

#include "driver/pcnt.h"
#include "soc/gpio_reg.h"

/* ── Pin assignments ─────────────────────────────────────────────────────
 * GPIO 34 and 35 are in the 32-39 range, which means they live in
 * GPIO_IN1_REG (not GPIO_IN_REG which covers 0-31).
 * GPIO 34 = bit 2 of GPIO_IN1_REG  (34 - 32 = 2)
 * GPIO 35 = bit 3 of GPIO_IN1_REG  (35 - 32 = 3)
 * ──────────────────────────────────────────────────────────────────────── */
#define WO2_PIN       GPIO_NUM_34
#define WO6_PIN       GPIO_NUM_35
#define WO2_BIT_MASK  (1u << 2)
#define WO6_BIT_MASK  (1u << 3)

/* PCNT gate time: 100 ms.
 * At 300 kHz, 100 ms gives 30,000 rising edges.
 * The PCNT counter is 16-bit signed (max 32,767), so 30,000 fits
 * without needing overflow handling. */
#define GATE_TIME_MICROSECONDS  100000u

/* Number of samples for complementary and duty cycle tests.
 * 2 million samples at ~40 ns per sample takes about 80 ms. */
#define SAMPLE_COUNT  2000000u

/* ── Forward declarations ────────────────────────────────────────────── */

void print_menu(void);
void test_frequency(gpio_num_t pin, const char *label);
void test_complementary(void);
void test_duty_cycle(void);

/* ── Setup ───────────────────────────────────────────────────────────── */

void setup()
{
    Serial.begin(115200);
    while (!Serial) { delay(10); }

    pinMode(WO2_PIN, INPUT);
    pinMode(WO6_PIN, INPUT);

    Serial.println();
    Serial.println("=== ESP32 PWM Verification Test Harness ===");
    Serial.println("CHESS CubeSat EPS — Phase 5");
    Serial.println();
    Serial.println("Wiring required:");
    Serial.println("  SAMD21 PA18 (WO[2]) --> ESP32 GPIO 34");
    Serial.println("  SAMD21 PA20 (WO[6]) --> ESP32 GPIO 35");
    Serial.println("  SAMD21 GND          --> ESP32 GND");
    Serial.println();
    print_menu();
}

/* ── Main loop: serial command dispatcher ────────────────────────────── */

void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();

        /* Flush remaining characters (carriage return, line feed) */
        delay(10);
        while (Serial.available() > 0) { (void)Serial.read(); }

        switch (command)
        {
            case '1':
                test_frequency(WO2_PIN, "WO[2] on PA18 (GPIO 34)");
                break;
            case '2':
                test_frequency(WO6_PIN, "WO[6] on PA20 (GPIO 35)");
                break;
            case '3':
                test_complementary();
                break;
            case '4':
                test_duty_cycle();
                break;
            case 'h':
            case 'H':
            case '?':
                print_menu();
                break;
            default:
                break;
        }
    }
}

/* ── Help menu ───────────────────────────────────────────────────────── */

void print_menu(void)
{
    Serial.println("--- Test Menu ---");
    Serial.println("  1 = Frequency on WO[2]  (expect ~300000 Hz)");
    Serial.println("  2 = Frequency on WO[6]  (expect ~300000 Hz)");
    Serial.println("  3 = Complementary check  (expect 0 violations)");
    Serial.println("  4 = Duty cycle on WO[2]  (shows % HIGH time)");
    Serial.println("  h = Show this menu");
    Serial.println("-----------------");
}

/* ── Test 1/2: Frequency measurement using hardware pulse counter ──── */

void test_frequency(gpio_num_t pin, const char *label)
{
    Serial.print("Measuring frequency on ");
    Serial.print(label);
    Serial.println(" ...");

    /* Configure the PCNT (Pulse Counter) hardware peripheral.
     *
     * PCNT is a dedicated counting circuit built into the ESP32 silicon.
     * It counts electrical edges (voltage transitions) on a pin with no
     * CPU involvement. At 80 MHz APB clock, it detects edges as narrow
     * as 12.5 ns. Our 300 kHz signal has edges spaced ~1.67 us apart,
     * which is trivial for PCNT to count. */
    pcnt_config_t config = {};
    config.pulse_gpio_num = pin;
    config.ctrl_gpio_num  = PCNT_PIN_NOT_USED;
    config.channel        = PCNT_CHANNEL_0;
    config.unit           = PCNT_UNIT_0;
    config.pos_mode       = PCNT_COUNT_INC;   /* count on rising edge */
    config.neg_mode       = PCNT_COUNT_DIS;   /* ignore falling edge */
    config.lctrl_mode     = PCNT_MODE_KEEP;
    config.hctrl_mode     = PCNT_MODE_KEEP;
    config.counter_h_lim  = 32767;
    config.counter_l_lim  = -32768;

    pcnt_unit_config(&config);

    /* Glitch filter: reject pulses shorter than 50 APB cycles (625 ns).
     * Our 300 kHz signal has a half-period of ~1670 ns — well above the
     * filter threshold. This rejects electrical noise from jumper wires.
     *
     * WARNING: Setting this above ~130 would filter out 300 kHz signals!
     * At 130 APB cycles: minimum pulse = 130 × 12.5 ns = 1625 ns,
     * which is dangerously close to our 1670 ns half-period. */
    pcnt_set_filter_value(PCNT_UNIT_0, 50);
    pcnt_filter_enable(PCNT_UNIT_0);

    /* Reset counter to zero, then count for exactly 100 ms.
     * We use a busy-wait on micros() for accurate gate timing.
     * PCNT counts in hardware regardless of what the CPU does. */
    pcnt_counter_pause(PCNT_UNIT_0);
    pcnt_counter_clear(PCNT_UNIT_0);

    pcnt_counter_resume(PCNT_UNIT_0);
    uint32_t start_us = micros();
    while ((micros() - start_us) < GATE_TIME_MICROSECONDS)
    {
        /* Busy-wait. PCNT hardware counts independently. */
    }
    pcnt_counter_pause(PCNT_UNIT_0);
    uint32_t elapsed_us = micros() - start_us;

    int16_t count = 0;
    pcnt_get_counter_value(PCNT_UNIT_0, &count);

    /* Scale to Hz: edges / elapsed_seconds */
    float frequency = (float)count / (float)elapsed_us * 1000000.0f;

    Serial.print("  Edges counted in 100ms: ");
    Serial.println(count);
    Serial.print("  Actual gate time: ");
    Serial.print(elapsed_us);
    Serial.println(" us");
    Serial.print("  Calculated frequency: ");
    Serial.print(frequency, 0);
    Serial.println(" Hz");

    if (count == 0)
    {
        Serial.println("  ** FAIL: No edges detected. Check wiring! **");
    }
    else if (frequency > 285000.0f && frequency < 315000.0f)
    {
        Serial.println("  PASS: Frequency within 5% of 300 kHz");
    }
    else
    {
        Serial.println("  ** FAIL: Frequency outside expected range **");
    }
    Serial.println();
}

/* ── Test 3: Complementary verification ──────────────────────────────── */

void test_complementary(void)
{
    Serial.println("Complementary check: sampling both pins 2,000,000 times ...");

    uint32_t violations    = 0;
    uint32_t both_low      = 0;
    uint32_t wo2_high_only = 0;
    uint32_t wo6_high_only = 0;

    for (uint32_t i = 0; i < SAMPLE_COUNT; i++)
    {
        /* GPIO_IN1_REG captures GPIO 32-39 in a single 32-bit bus read.
         * GPIO 34 (WO[2]) and GPIO 35 (WO[6]) are bits 2 and 3 of the
         * SAME register, read in the SAME bus transaction. They represent
         * the exact same instant in time — no possibility of reading one
         * pin before the other transitions. */
        uint32_t snapshot = REG_READ(GPIO_IN1_REG);
        uint32_t wo2 = snapshot & WO2_BIT_MASK;
        uint32_t wo6 = snapshot & WO6_BIT_MASK;

        if      (wo2 && wo6)   { violations++; }
        else if (!wo2 && !wo6) { both_low++; }
        else if (wo2)          { wo2_high_only++; }
        else                   { wo6_high_only++; }
    }

    Serial.print("  Total samples:        ");
    Serial.println(SAMPLE_COUNT);
    Serial.print("  WO[2] HIGH only:      ");
    Serial.print(wo2_high_only);
    Serial.print("  (");
    Serial.print((float)wo2_high_only / SAMPLE_COUNT * 100.0f, 1);
    Serial.println("%)");
    Serial.print("  WO[6] HIGH only:      ");
    Serial.print(wo6_high_only);
    Serial.print("  (");
    Serial.print((float)wo6_high_only / SAMPLE_COUNT * 100.0f, 1);
    Serial.println("%)");
    Serial.print("  Both LOW (dead time):  ");
    Serial.print(both_low);
    Serial.print("  (");
    Serial.print((float)both_low / SAMPLE_COUNT * 100.0f, 1);
    Serial.println("%)");
    Serial.print("  VIOLATIONS (both HIGH): ");
    Serial.println(violations);

    if (violations == 0 && wo2_high_only > 0 && wo6_high_only > 0)
    {
        Serial.println("  PASS: Complementary with dead time confirmed");
    }
    else if (violations > 0)
    {
        Serial.println("  ** FAIL: BOTH HIGH DETECTED — SHOOT-THROUGH RISK **");
    }
    else
    {
        Serial.println("  ** FAIL: No signal transitions. Check wiring! **");
    }
    Serial.println();
}

/* ── Test 4: Duty cycle measurement ──────────────────────────────────── */

void test_duty_cycle(void)
{
    Serial.println("Duty cycle on WO[2] (GPIO 34): sampling 2,000,000 times ...");

    /* Sample the pin state 2 million times in a tight loop.
     * Each sample takes ~30-50 ns (depending on cache and loop overhead).
     * The ratio of HIGH samples to total samples converges to the true
     * duty cycle because the sampling is asynchronous to the 300 kHz
     * PWM — there is no aliasing. Over 2M samples, we cover ~10,000+
     * complete PWM periods, giving ~0.5% statistical accuracy. */

    uint32_t high_count = 0;

    for (uint32_t i = 0; i < SAMPLE_COUNT; i++)
    {
        if (REG_READ(GPIO_IN1_REG) & WO2_BIT_MASK)
        {
            high_count++;
        }
    }

    float duty = (float)high_count / (float)SAMPLE_COUNT * 100.0f;

    Serial.print("  HIGH samples: ");
    Serial.println(high_count);
    Serial.print("  Total samples: ");
    Serial.println(SAMPLE_COUNT);
    Serial.print("  Measured duty cycle: ");
    Serial.print(duty, 1);
    Serial.println(" %");
    Serial.println("  (Accuracy: approximately +/- 2%)");
    Serial.println();
}
