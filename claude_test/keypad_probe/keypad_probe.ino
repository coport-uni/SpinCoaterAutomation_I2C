/**
 * @file keypad_probe.ino
 * @brief One off hardware response probe for the keypad board.
 *
 * Polls every register that can carry a key state and reports each bit
 * that changes, so a physical press can be traced to an address, a
 * register and a bit. Also reports every edge on the INT line.
 *
 * Pass 1 mapped 17 of the 18 buttons onto the two PCA9555 expanders.
 * VACUUM produced no change on any of those 32 bits, so this pass adds
 * the PCA9532 input registers: its LED pins can be read back as inputs
 * and the VACUUM key may be wired there.
 *
 * Reading a PCA9532 register only writes the control byte, which moves
 * the register pointer. No LS, PSC or PWM value is modified, so the
 * current LED state is left alone.
 *
 * Output, one line each:
 *   IN   <addr> <reg> 0x<val>
 *   CHG  <addr> <reg> <bit> <val> <ms>
 *   INT  <level> <ms>
 *   ERR  <addr> <reg>
 */

#include <Arduino_RouterBridge.h>
#include <Wire.h>

/** Serial line speed shared by every sketch in this project. */
#define SERIAL_BAUD 115200

/** Interrupt line from the keypad board, active low. */
#define PIN_KEYPAD_INT 2

/** Wire.endTransmission() result meaning the slave acknowledged. */
#define I2C_ACK 0

/** PCA9555 input port registers, read only. */
#define PCA9555_REG_INPUT_0 0x00
#define PCA9555_REG_INPUT_1 0x01

/** PCA9532 pin state registers, read only. */
#define PCA9532_REG_INPUT_0 0x00
#define PCA9532_REG_INPUT_1 0x01

/** Slaves found on the keypad board by firmware/i2c_scan. */
#define KEYPAD_ADDR_EXP_A 0x21
#define KEYPAD_ADDR_EXP_B 0x22
#define KEYPAD_ADDR_LED 0x60

/** Bits in one register. */
#define PORT_BIT_COUNT 8

/** Register poll period, ms. */
#define POLL_PERIOD_MS 50

/** Retry delay while the Monitor link is not up yet, ms. */
#define MONITOR_RETRY_MS 500

/** One register watched for changes. */
typedef struct {
    uint8_t addr;
    uint8_t reg;
} poll_entry_t;

/** Every register that could carry a key state. */
static const poll_entry_t poll_table[] = {
    {KEYPAD_ADDR_EXP_A, PCA9555_REG_INPUT_0},
    {KEYPAD_ADDR_EXP_A, PCA9555_REG_INPUT_1},
    {KEYPAD_ADDR_EXP_B, PCA9555_REG_INPUT_0},
    {KEYPAD_ADDR_EXP_B, PCA9555_REG_INPUT_1},
    {KEYPAD_ADDR_LED, PCA9532_REG_INPUT_0},
    {KEYPAD_ADDR_LED, PCA9532_REG_INPUT_1},
};

/** Number of registers in poll_table. */
#define POLL_ENTRY_COUNT (sizeof(poll_table) / sizeof(poll_table[0]))

/** Last value seen for every polled register. */
static uint8_t last_value[POLL_ENTRY_COUNT];

/** Last level seen on the INT line. */
static int last_int_level;

/**
 * @brief Read one 8 bit register from an I2C slave.
 * @param addr 7 bit slave address.
 * @param reg Register index.
 * @param out Receives the register value on success.
 * @return true when the slave acknowledged and returned a byte.
 */
static bool i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *out) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission() != I2C_ACK) {
        return false;
    }
    if (Wire.requestFrom(addr, (uint8_t)1) != 1) {
        return false;
    }
    *out = Wire.read();
    return true;
}

/**
 * @brief Print a 7 bit address as 0xNN.
 * @param addr 7 bit slave address.
 */
static void print_hex_addr(uint8_t addr) {
    Monitor.print("0x");
    Monitor.print(addr, HEX);
}

/**
 * @brief Report a failed read.
 * @param addr 7 bit slave address.
 * @param reg Register index.
 */
static void report_error(uint8_t addr, uint8_t reg) {
    Monitor.print("ERR ");
    print_hex_addr(addr);
    Monitor.print(' ');
    Monitor.println(reg, HEX);
}

/**
 * @brief Report every bit that differs between two register values.
 * @param addr 7 bit slave address.
 * @param reg Register index.
 * @param old_val Previously seen value.
 * @param new_val Value just read.
 */
static void report_changed_bits(uint8_t addr, uint8_t reg, uint8_t old_val,
                                uint8_t new_val) {
    uint8_t diff = old_val ^ new_val;

    for (uint8_t bit = 0; bit < PORT_BIT_COUNT; bit++) {
        if ((diff & (1 << bit)) == 0) {
            continue;
        }
        Monitor.print("CHG ");
        print_hex_addr(addr);
        Monitor.print(' ');
        Monitor.print(reg, HEX);
        Monitor.print(' ');
        Monitor.print(bit);
        Monitor.print(' ');
        Monitor.print((new_val >> bit) & 1);
        Monitor.print(' ');
        Monitor.println(millis());
    }
}

/**
 * @brief Report the current INT level.
 */
static void report_int_level(void) {
    Monitor.print("INT ");
    Monitor.print(last_int_level);
    Monitor.print(' ');
    Monitor.println(millis());
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

    Monitor.println("BOOT keypad_probe");

    for (uint8_t i = 0; i < POLL_ENTRY_COUNT; i++) {
        uint8_t val = 0xFF;

        if (!i2c_read_reg(poll_table[i].addr, poll_table[i].reg, &val)) {
            report_error(poll_table[i].addr, poll_table[i].reg);
        } else {
            Monitor.print("IN ");
            print_hex_addr(poll_table[i].addr);
            Monitor.print(' ');
            Monitor.print(poll_table[i].reg, HEX);
            Monitor.print(" 0x");
            Monitor.println(val, HEX);
        }
        last_value[i] = val;
    }

    last_int_level = digitalRead(PIN_KEYPAD_INT);
    report_int_level();

    Monitor.println("READY press any key");
}

void loop() {
    int int_level = digitalRead(PIN_KEYPAD_INT);

    if (int_level != last_int_level) {
        last_int_level = int_level;
        report_int_level();
    }

    for (uint8_t i = 0; i < POLL_ENTRY_COUNT; i++) {
        uint8_t val = 0;

        if (!i2c_read_reg(poll_table[i].addr, poll_table[i].reg, &val)) {
            continue;
        }
        if (val != last_value[i]) {
            report_changed_bits(poll_table[i].addr, poll_table[i].reg,
                                last_value[i], val);
            last_value[i] = val;
        }
    }

    delay(POLL_PERIOD_MS);
}
