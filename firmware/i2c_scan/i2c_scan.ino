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

#include <Wire.h>

#ifdef ARDUINO_ARCH_ZEPHYR
#include <Arduino_RouterBridge.h>
/** Stream that reaches the host PC on this board. */
#define HOST Monitor
#else
#define HOST Serial
#endif

/** Serial line speed shared by every sketch in this project. */
#define SERIAL_BAUD 115200

/** Time to wait for a USB CDC host to attach before giving up, ms. */
#define SERIAL_WAIT_MS 3000

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

/** Bus clock used when only weak internal pull-ups are available. */
#define I2C_SLOW_HZ 10000

/** Settle time before sampling the idle bus levels, ms. */
#define BUS_SETTLE_MS 5

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
    HOST.print("ADDR 0x");
    if (addr < 0x10) {
        HOST.print('0');
    }
    HOST.println(addr, HEX);
}

/**
 * @brief Bring up the link to the host PC.
 *
 * The UNO Q reaches the host only through the Router Bridge, and the
 * bridge has to be up before Monitor works. Every other board just
 * opens its USB CDC and carries on if no host is attached.
 */
static void host_begin(void) {
#ifdef ARDUINO_ARCH_ZEPHYR
    Serial.begin(SERIAL_BAUD);
    Bridge.begin();

    Monitor.begin(SERIAL_BAUD);
    while (!Monitor) {
        delay(MONITOR_RETRY_MS);
    }
#else
    unsigned long start_ms = millis();

    Serial.begin(SERIAL_BAUD);
    while (!Serial && (millis() - start_ms) < SERIAL_WAIT_MS) {
    }
#endif
}

/**
 * @brief Report the idle level of the two bus lines.
 *
 * A healthy I2C bus idles high because of its pull-ups. A line reading
 * low before Wire takes the pins over means the bus is unterminated or
 * held down by a stuck device, which is the first thing to check when
 * every transfer is answered with a NACK. The Zephyr core owns its I2C
 * pins, so the check only runs where the pins are plain GPIO first.
 */
static void bus_check(void) {
#ifndef ARDUINO_ARCH_ZEPHYR
    pinMode(SDA, INPUT);
    pinMode(SCL, INPUT);
    delay(BUS_SETTLE_MS);

    HOST.print("BUS sda=");
    HOST.print(digitalRead(SDA));
    HOST.print(" scl=");
    HOST.println(digitalRead(SCL));
#endif
}

void setup() {
    host_begin();

    HOST.println("BOOT i2c_scan");

    bus_check();

    pinMode(PIN_KEYPAD_INT, INPUT_PULLUP);

#ifndef ARDUINO_ARCH_ZEPHYR
    /* Measured on 2026-09-29: the keypad board pulls SDA and INT up but
     * not SCL, because the mainboard supplied that pull-up in the
     * original machine. The Zephyr core biases its I2C pins up and so
     * never saw this; the Renesas core does not. These internal
     * pull-ups are far too weak for I2C rise times and only keep Wire
     * from stalling on a bench setup with no resistor fitted. The real
     * fix is 4.7 kOhm from SCL to the supply. */
    pinMode(SDA, INPUT_PULLUP);
    pinMode(SCL, INPUT_PULLUP);
    delay(BUS_SETTLE_MS);
#endif

    Wire.begin();

#ifndef ARDUINO_ARCH_ZEPHYR
    /* A weak pull-up cannot meet the rise time of a 100 kHz bus, so the
     * clock is dropped until a proper resistor is fitted. */
    Wire.setClock(I2C_SLOW_HZ);
#endif
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

    HOST.print("INT ");
    HOST.println(digitalRead(PIN_KEYPAD_INT));

    HOST.print("SCAN done ");
    HOST.println(found);

    delay(SCAN_PERIOD_MS);
}
