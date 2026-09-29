/**
 * @file dual_target.ino
 * @brief Try to answer two I2C addresses from one controller.
 *
 * Only two of the UNO Q's three I2C controllers reach the ordinary
 * headers, but the emulator has to present three addresses: 0x21 and
 * 0x22 for the button expanders and 0x60 for the LED dimmer. If one
 * controller can hold two target addresses, two controllers are enough
 * and the high density connector stays unused.
 *
 * The STM32 I2C peripheral has two own-address registers, OA1 and OA2,
 * and the Zephyr driver mirrors that: struct i2c_stm32_data carries
 * both target_cfg and target2_cfg, the second guarded by
 * CONFIG_I2C_STM32_V2, which the generated autoconf.h sets to 1 here.
 * This sketch checks whether that translates into two addresses on the
 * wire.
 *
 * The Arduino Wire wrapper keeps a single i2c_target_config per
 * instance, so the second address cannot go through it. It is
 * registered by calling Zephyr's i2c_target_register() directly on the
 * same device, once the Arduino API has brought the controller up.
 *
 * TARGETS_ON_WIRE picks which controller carries the pair and which one
 * drives the bus. Both roles have to be run: a pass on one controller
 * says nothing about the other, and the emulator wants a pair on each.
 * A controller cannot test itself, because a master does not
 * acknowledge its own target address, so this is as far as two
 * controllers can go. Proving three or four addresses at once needs an
 * external master.
 *
 * Wiring, unchanged from claude_test/slave_selftest:
 *   A4 to D20   (SDA to SDA)
 *   A5 to D21   (SCL to SCL)
 *
 * The keypad must stay unplugged; it answers at 0x21 and 0x22 itself.
 *
 * Output, one line each:
 *   BOOT dual_target targets=<bus> master=<bus>
 *   REGISTER2 rc=<n>                   0 means the driver accepted it
 *   ADDR 0x<nn>                        every address that answered
 *   SCAN <count>
 *   READ1 got=0x<nn> want=0x<nn> ok=<0|1>
 *   READ2 got=0x<nn> want=0x<nn> ok=<0|1>
 *   CB1 req=<n> recv=<n>
 *   CB2 req=<n> recv=<n> last=0x<nn>
 *   RESULT PASS | RESULT FAIL
 */

#include <Arduino_RouterBridge.h>
#include <Wire.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>

/**
 * Which controller carries the two targets. The other one drives the
 * bus. Both roles are run, because a pass on one controller says
 * nothing about the other, and the emulator needs two targets on each.
 *
 *   0 -- targets on Wire2 = i2c3, master is Wire  = i2c2
 *   1 -- targets on Wire  = i2c2, master is Wire2 = i2c3
 */
#define TARGETS_ON_WIRE 1

#if TARGETS_ON_WIRE
#define TARGET_BUS Wire
#define MASTER_BUS Wire2
#define TARGET_NODE i2c2
#define ROLE_NAME "targets=i2c2 master=i2c3"
#else
#define TARGET_BUS Wire2
#define MASTER_BUS Wire
#define TARGET_NODE i2c3
#define ROLE_NAME "targets=i2c3 master=i2c2"
#endif

/** Serial line speed shared by every sketch in this project. */
#define SERIAL_BAUD 115200

/** Retry delay while the Monitor link is not up yet, ms. */
#define MONITOR_RETRY_MS 500

/** MASTER_BUS.endTransmission() result meaning the target acknowledged. */
#define I2C_ACK 0

/** First target, registered through the Arduino Wire API. */
#define TARGET1_ADDR 0x21
#define TARGET1_REPLY 0x5A

/** Second target, registered straight against the Zephyr driver. */
#define TARGET2_ADDR 0x22
#define TARGET2_REPLY 0xB7

/** Byte the master writes so the receive path has something to show. */
#define MASTER_PROBE 0xA5

/** First and last 7 bit address that may be probed safely. */
#define I2C_ADDR_FIRST 0x08
#define I2C_ADDR_LAST 0x77

/** Delay between probes so a slow target can release the bus, ms. */
#define I2C_PROBE_GAP_MS 2

/** Delay between test rounds, ms. */
#define ROUND_PERIOD_MS 3000

/** Bytes requested from a target in the read phase. */
#define READ_LEN 1

/** Addresses that must answer for the test to pass. */
#define EXPECTED_ADDR_COUNT 2

/** The controller behind Wire2, needed to register a second target. */
static const struct device *const i2c_target_dev =
    DEVICE_DT_GET(DT_NODELABEL(TARGET_NODE));

/** Counters for the Arduino-registered target. */
static volatile uint32_t cb1_request = 0;
static volatile uint32_t cb1_receive = 0;

/** Counters for the directly registered target. */
static volatile uint32_t cb2_request = 0;
static volatile uint32_t cb2_receive = 0;
static volatile uint8_t cb2_last = 0;

/** Registration state for the second target. */
static struct i2c_target_callbacks target2_callbacks;
static struct i2c_target_config target2_config;
static int register2_rc = -1;

/**
 * @brief Arduino target callback, the master is reading from us.
 */
static void on_request(void) {
    TARGET_BUS.write((uint8_t)TARGET1_REPLY);
    cb1_request++;
}

/**
 * @brief Arduino target callback, the master has written to us.
 * @param len Number of bytes received.
 */
static void on_receive(int len) {
    (void)len;

    while (TARGET_BUS.available()) {
        (void)TARGET_BUS.read();
    }
    cb1_receive++;
}

/**
 * @brief Zephyr target callback, a write transaction has started.
 * @param config Target being addressed.
 * @return 0 to acknowledge.
 */
static int target2_write_requested(struct i2c_target_config *config) {
    (void)config;
    return 0;
}

/**
 * @brief Zephyr target callback, a read transaction has started.
 * @param config Target being addressed.
 * @param val Receives the first byte to transmit.
 * @return 0 to acknowledge.
 */
static int target2_read_requested(struct i2c_target_config *config,
                                  uint8_t *val) {
    (void)config;
    *val = (uint8_t)TARGET2_REPLY;
    cb2_request++;
    return 0;
}

/**
 * @brief Zephyr target callback, a byte arrived from the master.
 * @param config Target being addressed.
 * @param val Byte received.
 * @return 0 to acknowledge.
 */
static int target2_write_received(struct i2c_target_config *config,
                                  uint8_t val) {
    (void)config;
    cb2_last = val;
    cb2_receive++;
    return 0;
}

/**
 * @brief Zephyr target callback, the master wants another byte.
 * @param config Target being addressed.
 * @param val Receives the next byte to transmit.
 * @return 0 to continue.
 */
static int target2_read_processed(struct i2c_target_config *config,
                                  uint8_t *val) {
    (void)config;
    *val = (uint8_t)TARGET2_REPLY;
    return 0;
}

/**
 * @brief Zephyr target callback, the transaction ended.
 * @param config Target being addressed.
 * @return 0 always.
 */
static int target2_stop(struct i2c_target_config *config) {
    (void)config;
    return 0;
}

/**
 * @brief Probe one address with a zero length write.
 * @param addr 7 bit target address.
 * @return true when something acknowledged.
 */
static bool i2c_probe(uint8_t addr) {
    MASTER_BUS.beginTransmission(addr);
    return MASTER_BUS.endTransmission() == I2C_ACK;
}

/**
 * @brief Print an address as 0xNN.
 * @param addr 7 bit target address.
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

/**
 * @brief Write one probe byte, then read one byte back.
 * @param tag Label printed in front of the reading.
 * @param addr Target address.
 * @param want Byte the target is expected to return.
 * @return true when the returned byte matched.
 */
static bool exercise_target(const char *tag, uint8_t addr, uint8_t want) {
    uint8_t got = 0;
    bool ok = false;

    MASTER_BUS.beginTransmission(addr);
    MASTER_BUS.write((uint8_t)MASTER_PROBE);
    MASTER_BUS.endTransmission();

    if (MASTER_BUS.requestFrom(addr, (uint8_t)READ_LEN) == READ_LEN) {
        got = (uint8_t)MASTER_BUS.read();
        ok = (got == want);
    }

    Monitor.print(tag);
    Monitor.print(" got=0x");
    Monitor.print(got, HEX);
    Monitor.print(" want=0x");
    Monitor.print(want, HEX);
    Monitor.print(" ok=");
    Monitor.println(ok ? 1 : 0);
    return ok;
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    Bridge.begin();

    Monitor.begin(SERIAL_BAUD);
    while (!Monitor) {
        delay(MONITOR_RETRY_MS);
    }

    TARGET_BUS.onReceive(on_receive);
    TARGET_BUS.onRequest(on_request);
    TARGET_BUS.begin((uint8_t)TARGET1_ADDR);

    /* Fields are assigned rather than brace-initialised, because the
     * callback struct grows extra members when
     * CONFIG_I2C_TARGET_BUFFER_MODE is set, as it is in this build.
     * Leaving those NULL keeps the driver on the byte-at-a-time path,
     * which is what the Arduino wrapper itself uses. */
    target2_callbacks.write_requested = target2_write_requested;
    target2_callbacks.read_requested = target2_read_requested;
    target2_callbacks.write_received = target2_write_received;
    target2_callbacks.read_processed = target2_read_processed;
    target2_callbacks.stop = target2_stop;

    target2_config.address = (uint16_t)TARGET2_ADDR;
    target2_config.callbacks = &target2_callbacks;

    register2_rc = i2c_target_register(i2c_target_dev, &target2_config);

    Monitor.print("REGISTER2 rc=");
    Monitor.println(register2_rc);

    MASTER_BUS.begin();

    Monitor.print("BOOT dual_target ");
    Monitor.println(ROLE_NAME);
}

void loop() {
    uint8_t found = 0;
    bool read1_ok = false;
    bool read2_ok = false;

    found = scan_bus();
    Monitor.print("SCAN ");
    Monitor.println(found);

    read1_ok = exercise_target("READ1", TARGET1_ADDR, TARGET1_REPLY);
    read2_ok = exercise_target("READ2", TARGET2_ADDR, TARGET2_REPLY);

    Monitor.print("CB1 req=");
    Monitor.print(cb1_request);
    Monitor.print(" recv=");
    Monitor.println(cb1_receive);

    Monitor.print("CB2 req=");
    Monitor.print(cb2_request);
    Monitor.print(" recv=");
    Monitor.print(cb2_receive);
    Monitor.print(" last=0x");
    Monitor.println(cb2_last, HEX);

    /* A pass needs both addresses on the bus, both serving their own
     * distinct byte, and nothing else answering. */
    Monitor.println((register2_rc == 0 && found == EXPECTED_ADDR_COUNT &&
                     read1_ok && read2_ok)
                        ? "RESULT PASS"
                        : "RESULT FAIL");

    delay(ROUND_PERIOD_MS);
}
