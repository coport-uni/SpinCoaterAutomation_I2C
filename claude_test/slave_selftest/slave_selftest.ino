/**
 * @file slave_selftest.ino
 * @brief Prove the UNO Q can act as an I2C target, using only itself.
 *
 * The emulator of spec stage 3 has to answer as a slave, and the Zephyr
 * core's target support was never exercised on this board. This sketch
 * settles that with two jumper wires and no extra hardware: one I2C
 * controller is opened as a target, a second one drives the bus as a
 * master, and the master then talks to its own board.
 *
 * Controllers, from the variant overlay
 * (i2cs = <&i2c2>, <&i2c4>, <&i2c3>):
 *
 *   Wire  = i2c2, D20 PB11 SDA, D21 PB10 SCL   -- master here
 *   Wire2 = i2c3, A4  PC1  SDA, A5  PC0  SCL   -- target here
 *
 * Wiring, two jumpers:
 *   A4 to D20   (SDA to SDA)
 *   A5 to D21   (SCL to SCL)
 *
 * The keypad board must be unplugged for this test. It answers at 0x21
 * itself, which is the address the target takes, and two devices
 * acknowledging the same address proves nothing. The scan line reports
 * every address found, so a stray 0x22 or 0x60 means the keypad is
 * still attached and the run should be discarded.
 *
 * Output, one line each:
 *   ADDR 0x<nn>                         every address that answered
 *   SCAN <count>
 *   WRITE ack=<0|1>
 *   READ got=0x<nn> want=0x<nn> ok=<0|1>
 *   CB req=<n> recv=<n> last=0x<nn>     target callback counters
 *   RESULT PASS | RESULT FAIL
 */

#include <Arduino_RouterBridge.h>
#include <Wire.h>

/** Serial line speed shared by every sketch in this project. */
#define SERIAL_BAUD 115200

/** Retry delay while the Monitor link is not up yet, ms. */
#define MONITOR_RETRY_MS 500

/** Wire.endTransmission() result meaning the slave acknowledged. */
#define I2C_ACK 0

/**
 * Address the target answers on. It matches the keypad's first
 * expander on purpose, so the emulator is tested at the address it
 * will really have to serve.
 */
#define TARGET_ADDR 0x21

/** Distinctive byte the target hands back, easy to spot in a log. */
#define TARGET_REPLY 0x5A

/** Byte the master writes so the receive callback has something to
 *  report. */
#define MASTER_PROBE 0xA5

/** First and last 7 bit address that may be probed safely. */
#define I2C_ADDR_FIRST 0x08
#define I2C_ADDR_LAST 0x77

/** Delay between probes so a slow target can release the bus, ms. */
#define I2C_PROBE_GAP_MS 2

/** Delay between test rounds, ms. */
#define ROUND_PERIOD_MS 3000

/** Bytes requested from the target in the read phase. */
#define READ_LEN 1

/** Counters touched by the target callbacks. */
static volatile uint32_t request_count = 0;
static volatile uint32_t receive_count = 0;
static volatile uint8_t last_received = 0;

/**
 * @brief Target callback, the master is reading from us.
 */
static void on_request(void) {
    Wire2.write((uint8_t)TARGET_REPLY);
    request_count++;
}

/**
 * @brief Target callback, the master has written to us.
 * @param len Number of bytes received.
 */
static void on_receive(int len) {
    (void)len;

    while (Wire2.available()) {
        last_received = (uint8_t)Wire2.read();
    }
    receive_count++;
}

/**
 * @brief Probe one address with a zero length write.
 * @param addr 7 bit slave address.
 * @return true when something acknowledged.
 */
static bool i2c_probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == I2C_ACK;
}

/**
 * @brief Print an address as 0xNN.
 * @param addr 7 bit slave address.
 */
static void print_addr(uint8_t addr) {
    Monitor.print("ADDR 0x");
    if (addr < 0x10) {
        Monitor.print('0');
    }
    Monitor.println(addr, HEX);
}

/**
 * @brief Sweep the bus and report what answers.
 * @return Number of addresses that acknowledged.
 */
static uint8_t scan_bus(void) {
    uint8_t found = 0;

    for (uint8_t addr = I2C_ADDR_FIRST; addr <= I2C_ADDR_LAST; addr++) {
        if (i2c_probe(addr)) {
            print_addr(addr);
            found++;
        }
        delay(I2C_PROBE_GAP_MS);
    }
    return found;
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    Bridge.begin();

    Monitor.begin(SERIAL_BAUD);
    while (!Monitor) {
        delay(MONITOR_RETRY_MS);
    }

    /* Callbacks are installed before the target is registered, so no
     * transaction can arrive while they are still unset. */
    Wire2.onReceive(on_receive);
    Wire2.onRequest(on_request);
    Wire2.begin((uint8_t)TARGET_ADDR);

    Wire.begin();

    Monitor.println("BOOT slave_selftest");
}

void loop() {
    uint8_t found = 0;
    bool write_ack = false;
    uint8_t got = 0;
    bool read_ok = false;

    found = scan_bus();
    Monitor.print("SCAN ");
    Monitor.println(found);

    Wire.beginTransmission((uint8_t)TARGET_ADDR);
    Wire.write((uint8_t)MASTER_PROBE);
    write_ack = (Wire.endTransmission() == I2C_ACK);

    Monitor.print("WRITE ack=");
    Monitor.println(write_ack ? 1 : 0);

    if (Wire.requestFrom((uint8_t)TARGET_ADDR, (uint8_t)READ_LEN) ==
        READ_LEN) {
        got = (uint8_t)Wire.read();
        read_ok = (got == TARGET_REPLY);
    }

    Monitor.print("READ got=0x");
    Monitor.print(got, HEX);
    Monitor.print(" want=0x");
    Monitor.print(TARGET_REPLY, HEX);
    Monitor.print(" ok=");
    Monitor.println(read_ok ? 1 : 0);

    Monitor.print("CB req=");
    Monitor.print(request_count);
    Monitor.print(" recv=");
    Monitor.print(receive_count);
    Monitor.print(" last=0x");
    Monitor.println(last_received, HEX);

    /* A pass needs the target to be the only thing on the bus, to
     * acknowledge a write, and to hand back the byte it was asked for. */
    Monitor.println((found == 1 && write_ack && read_ok) ? "RESULT PASS"
                                                         : "RESULT FAIL");

    delay(ROUND_PERIOD_MS);
}
