/**
 * @file i2c_scan.ino
 * @brief I2C bus scanner for the spincoater keypad board 09-0128-00.
 *
 * Probes every 7 bit address on the primary I2C bus and reports the
 * ones that acknowledge. The keypad board is expected to expose two
 * PCA9555 GPIO expanders in 0x20..0x27 and one PCA9532 LED dimmer in
 * 0x60..0x67.
 *
 * On the UNO Q the sketch talks to the host through the Router Bridge
 * Monitor object, not through the raw Serial UART. The Linux side
 * proxies Monitor to /dev/ttyGS0, which enumerates as a USB CDC port
 * on the host PC.
 *
 * Wiring, stage 1 of docs/spincoater_keypad_spec.md:
 *   UNO Q 3.3V -> X1 VDD, GND -> X1 GND,
 *   D20 SDA -> X1 SDA, D21 SCL -> X1 SCL, D2 -> X1 INT.
 *
 * Output, one line each:
 *   ADDR 0x20
 *   INT <0|1>
 *   SCAN done <count>
 */

#include <Arduino_RouterBridge.h>
#include <Wire.h>

/** Serial line speed shared by every sketch in this project. */
#define SERIAL_BAUD 115200

/** Interrupt line from the keypad board, active low. */
#define PIN_KEYPAD_INT 2

/** First and last 7 bit address that may be probed safely. */
#define I2C_ADDR_FIRST 0x08
#define I2C_ADDR_LAST 0x77

/** Wire.endTransmission() result meaning the slave acknowledged. */
#define I2C_ACK 0

/** Delay between probes so a slow slave can release the bus, ms. */
#define I2C_PROBE_GAP_MS 2

/** Delay between full scan passes, ms. */
#define SCAN_PERIOD_MS 2000

/** Retry delay while the Monitor link is not up yet, ms. */
#define MONITOR_RETRY_MS 500

/**
 * @brief Probe one address with a zero length write.
 * @param addr 7 bit slave address.
 * @return true when the slave acknowledged its address byte.
 */
static bool i2c_probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == I2C_ACK;
}

/**
 * @brief Print an acknowledging address in the format the host parser
 *        expects.
 * @param addr 7 bit slave address.
 */
static void print_addr(uint8_t addr) {
    Monitor.print("ADDR 0x");
    if (addr < 0x10) {
        Monitor.print('0');
    }
    Monitor.println(addr, HEX);
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    Bridge.begin();

    Monitor.begin(SERIAL_BAUD);
    while (!Monitor) {
        delay(MONITOR_RETRY_MS);
    }

    pinMode(PIN_KEYPAD_INT, INPUT_PULLUP);
    Wire.begin();

    Monitor.println("BOOT i2c_scan");
}

void loop() {
    uint8_t found = 0;

    for (uint8_t addr = I2C_ADDR_FIRST; addr <= I2C_ADDR_LAST; addr++) {
        if (i2c_probe(addr)) {
            print_addr(addr);
            found++;
        }
        delay(I2C_PROBE_GAP_MS);
    }

    Monitor.print("INT ");
    Monitor.println(digitalRead(PIN_KEYPAD_INT));

    Monitor.print("SCAN done ");
    Monitor.println(found);

    delay(SCAN_PERIOD_MS);
}
