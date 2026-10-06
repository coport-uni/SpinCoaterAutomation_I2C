/**
 * @file pca9555_emu.ino
 * @brief Stand in for the Laurell keypad board on the spin coater's
 *        I2C bus and inject arrow keys on command.
 *
 * The mainboard is the only master on this bus. It polls the keypad
 * board's two PCA9555 expanders for key state and writes the PCA9532
 * dimmer to light the keys that are currently pressable. With the
 * keypad unplugged, this sketch answers all three addresses:
 *
 *   0x21  PCA9555, 16 keys          i2c2 own-address 1  (D20, D21)
 *   0x22  PCA9555, 2 keys + 2 LEDs  i2c2 own-address 2  (D20, D21)
 *   0x60  PCA9532, 16 key LEDs      i2c3 own-address 1  (A4, A5)
 *
 * Two controllers are enough because the STM32 I2C peripheral has two
 * own-address registers; claude_test/dual_target proved both halves of
 * that on this board. The jumpers A4 to D20 and A5 to D21 put both
 * controllers on the one physical bus, which reaches the mainboard
 * through a PCA9306 translator: VREF1 on the 3.3 V side, VREF2 on the
 * 5 V side, EN high, and a pull-up on each side of each line. The
 * PCA9306 is a pass gate, not a driver, so without those pull-ups
 * neither side ever reaches a high level.
 *
 * Every address is served by raw Zephyr i2c_target_callbacks rather
 * than by the Arduino Wire wrapper. Wire reports a write only at STOP,
 * so for the register-write-then-repeated-start-read sequence that a
 * PCA9555 read uses, its onRequest would fire before the register
 * pointer had arrived. The Zephyr callbacks are byte-by-byte and see
 * the pointer in time. Wire.begin() is still called on both buses: it
 * only re-applies the pinctrl state, it does not claim the bus.
 *
 * Key injection is deliberately limited. Only the up and down arrows
 * can be pressed, and the firmware rejects every other key name rather
 * than relying on the host to be careful. With the keypad unplugged
 * there is no physical STOP key, so the mains switch is the stop.
 *
 * Host protocol on Monitor, one command per line:
 *   PRESS UP [ms]      Hold the up arrow, default 100 ms
 *   PRESS DOWN [ms]    Hold the down arrow
 *   RELEASE            Release early
 *   STATE              Print the register files and the INT level
 *   LOG ON | LOG OFF   Log every read, not just the ones that changed
 *   HELP               List the commands
 *
 * Output lines:
 *   BOOT pca9555_emu
 *   REG addr=0x<nn> rc=<n>              one per registered address
 *   READY
 *   RX 0x<nn> reg=0x<nn> data=0x<nn> ms=<n>    mainboard wrote
 *   TX 0x<nn> reg=0x<nn> data=0x<nn> ms=<n>    mainboard read
 *   LEDS <16 chars> ms=<n>              . off  * on  0 pwm0  1 pwm1
 *   POLL ms=<n> rx21=<n> tx21=<n> ... drop=<n>
 *   KEY <name> <down|up> ms=<n>
 *   STATE ...
 *   OK | ERR raw=[<line>]
 */

#include <Arduino_RouterBridge.h>
#include <Wire.h>

#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

/** Serial line speed shared by every sketch in this project. */
#define SERIAL_BAUD 115200

/** Retry delay while the Monitor link is not up yet, ms. */
#define MONITOR_RETRY_MS 500

/** The three addresses the keypad board presents to the mainboard. */
#define ADDR_BTN_A 0x21
#define ADDR_BTN_B 0x22
#define ADDR_LED 0x60

/** Index of each address in the per-device arrays. */
#define DEV_BTN_A 0
#define DEV_BTN_B 1
#define DEV_LED 2
#define DEV_COUNT 3

/** X1 pin 5, the expanders' shared open-drain interrupt line. */
#define INT_PIN 2

/**
 * Whether INT is physically wired through a translator.
 *
 * A PCA9306 carries two channels, SDA and SCL, and has none left for
 * INT. Wiring INT straight across would put the mainboard's 5 V pull-up
 * on D2 whenever the line is released, which the UNO Q's pin does not
 * tolerate, so the default is to leave it unconnected and let the
 * mainboard poll. Bench run 1 shows from the POLL counters whether it
 * polls on its own; only if it does not does INT need a third channel,
 * and this may then be set to 1.
 */
#define INT_WIRED 0

/* PCA9555 register map. The pointer toggles within a pair, so the
 * eight registers behave as four two-register banks. */
#define PCA9555_INPUT_0 0x00
#define PCA9555_INPUT_1 0x01
#define PCA9555_OUTPUT_0 0x02
#define PCA9555_POLARITY_0 0x04
#define PCA9555_CONFIG_0 0x06
#define PCA9555_REG_MASK 0x07
#define PCA9555_PORT_COUNT 2

/** Power-on reset values. Every pin is an input, nothing inverted. */
#define PCA9555_POR_OUTPUT 0xFF
#define PCA9555_POR_POLARITY 0x00
#define PCA9555_POR_CONFIG 0xFF

/** Key state with nothing pressed. The keys are active low. */
#define KEYS_RELEASED 0xFF

/* PCA9532 register map. Bit 4 of the command byte asks for
 * auto-increment, and the pointer then wraps at the last register. */
#define PCA9532_INPUT_0 0x00
#define PCA9532_INPUT_1 0x01
#define PCA9532_PSC0 0x02
#define PCA9532_PWM0 0x03
#define PCA9532_PSC1 0x04
#define PCA9532_PWM1 0x05
#define PCA9532_LS0 0x06
#define PCA9532_REG_COUNT 10
#define PCA9532_REG_MASK 0x0F
#define PCA9532_AUTO_INCREMENT 0x10

/** Power-on reset values from the PCA9532 datasheet. */
#define PCA9532_POR_PSC 0xFF
#define PCA9532_POR_PWM 0x80
#define PCA9532_POR_LS 0x00

/** LED selector field, two bits per channel. */
#define LS_BITS_PER_LED 2
#define LS_LEDS_PER_REG 4
#define LS_FIELD_MASK 0x03
#define LS_OFF 0
#define LS_ON 1
#define LS_PWM0 2
#define LS_PWM1 3

/** Number of LED channels the PCA9532 drives. */
#define LED_CHANNEL_COUNT 16

/* The only two keys this firmware is allowed to press, from
 * docs/button_map.json. Both live on expander 0x21, port 1. */
#define KEY_UP_PORT 1
#define KEY_UP_BIT 7
#define KEY_DOWN_PORT 1
#define KEY_DOWN_BIT 0

/** Hold time for an injected key press, ms. */
#define PRESS_HOLD_DEFAULT_MS 100
#define PRESS_HOLD_MIN_MS 20
#define PRESS_HOLD_MAX_MS 1000

/** Nothing is being pressed. */
#define PRESS_NONE 0xFF

/** Transaction log ring. The size must stay a power of two. */
#define LOG_RING_SIZE 256
#define LOG_RING_MASK (LOG_RING_SIZE - 1)

/** Event kinds in the ring. */
#define EV_READ 0
#define EV_WRITE 1

/* The first reads after boot are logged in full so the mainboard's
 * polling order is recorded once; after that only changes are kept,
 * which keeps the link usable during a long run. */
#define LOG_READ_BURST 300

/** Period of the traffic summary line, ms. */
#define POLL_REPORT_MS 1000

/** Longest host command line accepted, including the terminator. */
#define LINE_BUF_SIZE 64

/** Radix used for the decimal arguments of a host command. */
#define DECIMAL_BASE 10

/**
 * @brief One emulated PCA9555.
 *
 * The fields are volatile because the I2C callbacks run in interrupt
 * context while loop() presses keys and prints the log.
 */
typedef struct {
    volatile uint8_t keys[PCA9555_PORT_COUNT];
    volatile uint8_t output[PCA9555_PORT_COUNT];
    volatile uint8_t polarity[PCA9555_PORT_COUNT];
    volatile uint8_t config[PCA9555_PORT_COUNT];
    volatile uint8_t pointer;
    volatile bool expect_pointer;
    volatile bool int_pending;
} pca9555_state_t;

/**
 * @brief The emulated PCA9532 LED dimmer.
 */
typedef struct {
    volatile uint8_t reg[PCA9532_REG_COUNT];
    volatile uint8_t pointer;
    volatile bool auto_increment;
    volatile bool expect_pointer;
    volatile bool ls_dirty;
} pca9532_state_t;

/**
 * @brief One logged byte of bus traffic.
 */
typedef struct {
    uint32_t ms;
    uint8_t kind;
    uint8_t addr;
    uint8_t reg;
    uint8_t data;
} log_event_t;

/** The two button expanders, 0x21 first. */
static pca9555_state_t btn[2];

/** The LED dimmer at 0x60. */
static pca9532_state_t led;

/** Single producer in the callbacks, single consumer in loop(). */
static volatile log_event_t log_ring[LOG_RING_SIZE];
static volatile uint16_t log_head = 0;
static volatile uint16_t log_tail = 0;
static volatile uint32_t log_dropped = 0;
static volatile uint32_t log_reads_seen = 0;
static volatile bool log_all_reads = false;

/** Last byte handed back per device and register, for change detection. */
static volatile uint8_t tx_last[DEV_COUNT][PCA9532_REG_COUNT];

/** Traffic counters, reported once a second. */
static volatile uint32_t rx_count[DEV_COUNT];
static volatile uint32_t tx_count[DEV_COUNT];
static uint32_t rx_reported[DEV_COUNT];
static uint32_t tx_reported[DEV_COUNT];
static uint32_t poll_report_at = 0;

/** The key press currently being held, or PRESS_NONE. */
static uint8_t press_port = PRESS_NONE;
static uint8_t press_bit = 0;
static uint32_t press_end_ms = 0;
static const char *press_name = "none";

/** Host command line assembly. */
static char line_buf[LINE_BUF_SIZE];
static uint8_t line_len = 0;

/** Registration state, one entry per address. */
static struct i2c_target_callbacks target_cb[DEV_COUNT];
static struct i2c_target_config target_cfg[DEV_COUNT];
static int target_rc[DEV_COUNT];

/** The two controllers that carry the three addresses. */
static const struct device *const btn_bus =
    DEVICE_DT_GET(DT_NODELABEL(i2c2));
static const struct device *const led_bus =
    DEVICE_DT_GET(DT_NODELABEL(i2c3));

/**
 * @brief Map a 7 bit address to its index in the per-device arrays.
 * @param addr Address taken from the Zephyr target config.
 * @return DEV_BTN_A, DEV_BTN_B, DEV_LED, or DEV_COUNT if unknown.
 */
static uint8_t dev_index(uint16_t addr) {
    if (addr == ADDR_BTN_A) {
        return DEV_BTN_A;
    }
    if (addr == ADDR_BTN_B) {
        return DEV_BTN_B;
    }
    if (addr == ADDR_LED) {
        return DEV_LED;
    }
    return DEV_COUNT;
}

/**
 * @brief Append one event to the log ring.
 *
 * Called from interrupt context. A full ring drops the event and bumps
 * a counter rather than blocking the bus.
 *
 * The two button expanders sit on i2c2 and the dimmer on i2c3, so this
 * has two independent interrupt sources as producers, not one. If their
 * priorities differ, one can preempt the other between the head test
 * and the head store and both writers then claim the same slot, which
 * would quietly corrupt the stage 2 log. Interrupts are locked across
 * the update to make the claim atomic; the body is a handful of stores
 * and never waits on the bus.
 *
 * @param kind EV_READ or EV_WRITE.
 * @param addr Address that was addressed.
 * @param reg Register the byte belonged to.
 * @param data The byte itself.
 */
static void log_push(uint8_t kind, uint8_t addr, uint8_t reg,
                     uint8_t data) {
    unsigned int key = irq_lock();
    uint16_t next = (uint16_t)((log_head + 1) & LOG_RING_MASK);

    if (next == log_tail) {
        log_dropped++;
        irq_unlock(key);
        return;
    }
    log_ring[log_head].ms = millis();
    log_ring[log_head].kind = kind;
    log_ring[log_head].addr = addr;
    log_ring[log_head].reg = reg;
    log_ring[log_head].data = data;
    log_head = next;
    irq_unlock(key);
}

/**
 * @brief Build a PCA9555 input port byte.
 *
 * A real expander reports the pin level, which is the key state for an
 * input pin and the output latch for an output pin, with the polarity
 * inversion register applied on top.
 *
 * @param dev Expander to read.
 * @param port 0 or 1.
 * @return The byte the mainboard should see.
 */
static uint8_t pca9555_input(const pca9555_state_t *dev, uint8_t port) {
    uint8_t cfg = dev->config[port];
    uint8_t pins = (uint8_t)((dev->keys[port] & cfg)
                             | (dev->output[port] & (uint8_t)~cfg));

    return (uint8_t)(pins ^ dev->polarity[port]);
}

/**
 * @brief Read one PCA9555 register.
 * @param dev Expander to read.
 * @param reg Register index, already masked to 0x07.
 * @return Register contents.
 */
static uint8_t pca9555_read(pca9555_state_t *dev, uint8_t reg) {
    uint8_t port = (uint8_t)(reg & 0x01);

    switch (reg & 0x06) {
    case PCA9555_INPUT_0:
        /* Reading an input port clears the interrupt, exactly as the
         * real part does. */
        dev->int_pending = false;
        return pca9555_input(dev, port);
    case PCA9555_OUTPUT_0:
        return dev->output[port];
    case PCA9555_POLARITY_0:
        return dev->polarity[port];
    default:
        return dev->config[port];
    }
}

/**
 * @brief Write one PCA9555 register, ignoring the read-only ports.
 * @param dev Expander to write.
 * @param reg Register index, already masked to 0x07.
 * @param val Byte received from the mainboard.
 */
static void pca9555_write(pca9555_state_t *dev, uint8_t reg,
                          uint8_t val) {
    uint8_t port = (uint8_t)(reg & 0x01);

    switch (reg & 0x06) {
    case PCA9555_OUTPUT_0:
        dev->output[port] = val;
        break;
    case PCA9555_POLARITY_0:
        dev->polarity[port] = val;
        break;
    case PCA9555_CONFIG_0:
        dev->config[port] = val;
        break;
    default:
        /* The input ports are read-only. */
        break;
    }
}

/**
 * @brief Build a PCA9532 input port byte from the LED selectors.
 *
 * An LED pin is pulled low when its channel is driven and released
 * when the channel is off. The blink modes are reported as driven,
 * which is an approximation; the mainboard is not expected to read
 * these registers at all.
 *
 * @param port 0 for channels 0 to 7, 1 for channels 8 to 15.
 * @return The byte the mainboard should see.
 */
static uint8_t pca9532_input(uint8_t port) {
    uint8_t value = 0;
    uint8_t i = 0;

    for (i = 0; i < 8; i++) {
        uint8_t ch = (uint8_t)(port * 8 + i);
        uint8_t ls = led.reg[PCA9532_LS0 + ch / LS_LEDS_PER_REG];
        uint8_t shift =
            (uint8_t)((ch % LS_LEDS_PER_REG) * LS_BITS_PER_LED);

        if (((ls >> shift) & LS_FIELD_MASK) == LS_OFF) {
            value |= (uint8_t)(1u << i);
        }
    }
    return value;
}

/**
 * @brief Read one PCA9532 register.
 * @param reg Register index, already reduced to 0 through 9.
 * @return Register contents.
 */
static uint8_t pca9532_read(uint8_t reg) {
    if (reg == PCA9532_INPUT_0 || reg == PCA9532_INPUT_1) {
        return pca9532_input((uint8_t)(reg - PCA9532_INPUT_0));
    }
    return led.reg[reg];
}

/**
 * @brief Advance a register pointer after one byte has moved.
 *
 * A PCA9555 toggles between the two registers of the addressed pair. A
 * PCA9532 increments and wraps, but only when the command byte asked
 * for it.
 *
 * @param idx Device index.
 */
static void pointer_advance(uint8_t idx) {
    if (idx == DEV_LED) {
        if (led.auto_increment) {
            led.pointer =
                (uint8_t)((led.pointer + 1) % PCA9532_REG_COUNT);
        }
        return;
    }
    btn[idx].pointer = (uint8_t)(btn[idx].pointer ^ 0x01);
}

/**
 * @brief Zephyr callback, the mainboard has started writing to us.
 * @param config Target being addressed.
 * @return 0 to acknowledge.
 */
static int on_write_requested(struct i2c_target_config *config) {
    uint8_t idx = dev_index(config->address);

    if (idx == DEV_COUNT) {
        return -EIO;
    }
    if (idx == DEV_LED) {
        led.expect_pointer = true;
    } else {
        btn[idx].expect_pointer = true;
    }
    return 0;
}

/**
 * @brief Zephyr callback, a byte arrived from the mainboard.
 *
 * The first byte of a write is the command byte and sets the register
 * pointer; the bytes after it are register data.
 *
 * @param config Target being addressed.
 * @param val Byte received.
 * @return 0 to acknowledge.
 */
static int on_write_received(struct i2c_target_config *config,
                             uint8_t val) {
    uint8_t idx = dev_index(config->address);
    uint8_t reg = 0;

    if (idx == DEV_COUNT) {
        return -EIO;
    }

    if (idx == DEV_LED) {
        if (led.expect_pointer) {
            led.auto_increment =
                ((val & PCA9532_AUTO_INCREMENT) != 0);
            led.pointer =
                (uint8_t)((val & PCA9532_REG_MASK) % PCA9532_REG_COUNT);
            led.expect_pointer = false;
            return 0;
        }
        reg = led.pointer;
        if (reg >= PCA9532_PSC0) {
            led.reg[reg] = val;
            if (reg >= PCA9532_LS0) {
                led.ls_dirty = true;
            }
        }
        log_push(EV_WRITE, ADDR_LED, reg, val);
        rx_count[DEV_LED]++;
        pointer_advance(DEV_LED);
        return 0;
    }

    if (btn[idx].expect_pointer) {
        btn[idx].pointer = (uint8_t)(val & PCA9555_REG_MASK);
        btn[idx].expect_pointer = false;
        return 0;
    }
    reg = btn[idx].pointer;
    pca9555_write(&btn[idx], reg, val);
    log_push(EV_WRITE, (uint8_t)config->address, reg, val);
    rx_count[idx]++;
    pointer_advance(idx);
    return 0;
}

/**
 * @brief Serve one byte to the mainboard and note it in the log.
 * @param idx Device index.
 * @param addr Address being read, for the log line.
 * @param val Receives the byte to transmit.
 * @return 0 to continue the transfer.
 */
static int serve_read(uint8_t idx, uint8_t addr, uint8_t *val) {
    uint8_t reg = 0;
    uint8_t data = 0;

    if (idx == DEV_LED) {
        reg = led.pointer;
        data = pca9532_read(reg);
    } else {
        reg = btn[idx].pointer;
        data = pca9555_read(&btn[idx], reg);
    }

    *val = data;
    tx_count[idx]++;

    /* Everything is logged for the first few hundred reads so the
     * polling order is on record, then only the bytes that changed. */
    if (log_all_reads || log_reads_seen < LOG_READ_BURST
        || tx_last[idx][reg] != data) {
        log_push(EV_READ, addr, reg, data);
    }
    tx_last[idx][reg] = data;
    log_reads_seen++;

    pointer_advance(idx);
    return 0;
}

/**
 * @brief Zephyr callback, the mainboard wants the first byte.
 * @param config Target being addressed.
 * @param val Receives the byte to transmit.
 * @return 0 to acknowledge.
 */
static int on_read_requested(struct i2c_target_config *config,
                             uint8_t *val) {
    uint8_t idx = dev_index(config->address);

    if (idx == DEV_COUNT) {
        return -EIO;
    }
    return serve_read(idx, (uint8_t)config->address, val);
}

/**
 * @brief Zephyr callback, the mainboard wants another byte.
 * @param config Target being addressed.
 * @param val Receives the byte to transmit.
 * @return 0 to continue.
 */
static int on_read_processed(struct i2c_target_config *config,
                             uint8_t *val) {
    uint8_t idx = dev_index(config->address);

    if (idx == DEV_COUNT) {
        return -EIO;
    }
    return serve_read(idx, (uint8_t)config->address, val);
}

/**
 * @brief Zephyr callback, the transaction ended.
 * @param config Target being addressed.
 * @return 0 always.
 */
static int on_stop(struct i2c_target_config *config) {
    uint8_t idx = dev_index(config->address);

    if (idx == DEV_COUNT) {
        return 0;
    }
    if (idx == DEV_LED) {
        led.expect_pointer = true;
    } else {
        btn[idx].expect_pointer = true;
    }
    return 0;
}

/**
 * @brief Register one address as an I2C target.
 * @param idx Device index, which also indexes the config arrays.
 * @param bus Controller that should answer for it.
 * @param addr 7 bit address.
 */
static void register_target(uint8_t idx, const struct device *bus,
                            uint8_t addr) {
    /* The fields are assigned rather than brace-initialised, because
     * the callback struct grows extra members when
     * CONFIG_I2C_TARGET_BUFFER_MODE is set, as it is in this build.
     * Leaving those NULL keeps the driver on the byte-at-a-time path,
     * which is the only one that sees the register pointer in time. */
    target_cb[idx].write_requested = on_write_requested;
    target_cb[idx].read_requested = on_read_requested;
    target_cb[idx].write_received = on_write_received;
    target_cb[idx].read_processed = on_read_processed;
    target_cb[idx].stop = on_stop;

    target_cfg[idx].address = addr;
    target_cfg[idx].callbacks = &target_cb[idx];

    target_rc[idx] = i2c_target_register(bus, &target_cfg[idx]);

    Monitor.print("REG addr=0x");
    Monitor.print(addr, HEX);
    Monitor.print(" rc=");
    Monitor.println(target_rc[idx]);
}

/**
 * @brief Drive the shared interrupt line from the pending flags.
 *
 * The real line is open drain and wired to both expanders, so it is
 * pulled low while either has an unread change and released, never
 * driven high, otherwise.
 */
static void int_apply(void) {
#if INT_WIRED
    if (btn[DEV_BTN_A].int_pending || btn[DEV_BTN_B].int_pending) {
        pinMode(INT_PIN, OUTPUT);
        digitalWrite(INT_PIN, LOW);
    } else {
        pinMode(INT_PIN, INPUT);
    }
#endif
}

/**
 * @brief Print one byte as 0xNN with the leading zero kept.
 * @param value Byte to print.
 */
static void print_hex8(uint8_t value) {
    Monitor.print("0x");
    if (value < 0x10) {
        Monitor.print('0');
    }
    Monitor.print(value, HEX);
}

/**
 * @brief Print the LED selector registers as one readable row.
 *
 * Channel n of the PCA9532 is the LED of the key at 0x21 bit n, so
 * this row is the mainboard's own list of the keys it will accept
 * right now.
 */
static void print_leds(void) {
    uint8_t ch = 0;

    Monitor.print("LEDS ");
    for (ch = 0; ch < LED_CHANNEL_COUNT; ch++) {
        uint8_t ls = led.reg[PCA9532_LS0 + ch / LS_LEDS_PER_REG];
        uint8_t shift =
            (uint8_t)((ch % LS_LEDS_PER_REG) * LS_BITS_PER_LED);

        switch ((ls >> shift) & LS_FIELD_MASK) {
        case LS_ON:
            Monitor.print('*');
            break;
        case LS_PWM0:
            Monitor.print('0');
            break;
        case LS_PWM1:
            Monitor.print('1');
            break;
        default:
            Monitor.print('.');
            break;
        }
    }
    Monitor.print(" ms=");
    Monitor.println(millis());
}

/**
 * @brief Empty the log ring onto the Monitor link.
 */
static void drain_log(void) {
    while (log_tail != log_head) {
        uint16_t i = log_tail;

        Monitor.print(log_ring[i].kind == EV_WRITE ? "RX " : "TX ");
        print_hex8(log_ring[i].addr);
        Monitor.print(" reg=");
        print_hex8(log_ring[i].reg);
        Monitor.print(" data=");
        print_hex8(log_ring[i].data);
        Monitor.print(" ms=");
        Monitor.println(log_ring[i].ms);

        log_tail = (uint16_t)((i + 1) & LOG_RING_MASK);
    }
}

/**
 * @brief Print the traffic summary when any counter has moved.
 *
 * The period and the order of the mainboard's polling are the stage 2
 * data this project still needs, so the counters are reported even
 * when the log itself is quiet.
 */
static void report_poll(void) {
    uint32_t now = millis();
    uint8_t i = 0;
    bool changed = false;

    if ((int32_t)(now - poll_report_at) < 0) {
        return;
    }
    poll_report_at = now + POLL_REPORT_MS;

    for (i = 0; i < DEV_COUNT; i++) {
        if (rx_count[i] != rx_reported[i]
            || tx_count[i] != tx_reported[i]) {
            changed = true;
        }
    }
    if (!changed) {
        return;
    }

    Monitor.print("POLL ms=");
    Monitor.print(now);
    for (i = 0; i < DEV_COUNT; i++) {
        Monitor.print(i == DEV_BTN_A   ? " rx21="
                      : i == DEV_BTN_B ? " rx22="
                                       : " rx60=");
        Monitor.print(rx_count[i] - rx_reported[i]);
        Monitor.print(i == DEV_BTN_A   ? " tx21="
                      : i == DEV_BTN_B ? " tx22="
                                       : " tx60=");
        Monitor.print(tx_count[i] - tx_reported[i]);
        rx_reported[i] = rx_count[i];
        tx_reported[i] = tx_count[i];
    }
    Monitor.print(" drop=");
    Monitor.println(log_dropped);
}

/**
 * @brief Hold one key down.
 * @param name Key name for the log line.
 * @param port Port within expander 0x21.
 * @param bit Bit within that port.
 * @param hold_ms How long to keep it down.
 */
static void key_down(const char *name, uint8_t port, uint8_t bit,
                     uint32_t hold_ms) {
    btn[DEV_BTN_A].keys[port] =
        (uint8_t)(btn[DEV_BTN_A].keys[port] & ~(1u << bit));
    btn[DEV_BTN_A].int_pending = true;
    int_apply();

    press_port = port;
    press_bit = bit;
    press_name = name;
    press_end_ms = millis() + hold_ms;

    Monitor.print("KEY ");
    Monitor.print(name);
    Monitor.print(" down ms=");
    Monitor.println(millis());
}

/**
 * @brief Release the key that is currently held, if any.
 */
static void key_up(void) {
    if (press_port == PRESS_NONE) {
        return;
    }
    btn[DEV_BTN_A].keys[press_port] =
        (uint8_t)(btn[DEV_BTN_A].keys[press_port] | (1u << press_bit));
    btn[DEV_BTN_A].int_pending = true;
    int_apply();

    Monitor.print("KEY ");
    Monitor.print(press_name);
    Monitor.print(" up ms=");
    Monitor.println(millis());

    press_port = PRESS_NONE;
    press_name = "none";
}

/**
 * @brief Print the emulated state in one line.
 */
static void print_state(void) {
    Monitor.print("STATE a0=");
    print_hex8(btn[DEV_BTN_A].keys[0]);
    Monitor.print(" a1=");
    print_hex8(btn[DEV_BTN_A].keys[1]);
    Monitor.print(" b0=");
    print_hex8(btn[DEV_BTN_B].keys[0]);
    Monitor.print(" b1=");
    print_hex8(btn[DEV_BTN_B].keys[1]);
    Monitor.print(" cfgA0=");
    print_hex8(btn[DEV_BTN_A].config[0]);
    Monitor.print(" cfgA1=");
    print_hex8(btn[DEV_BTN_A].config[1]);
    Monitor.print(" int=");
    Monitor.print(btn[DEV_BTN_A].int_pending
                      || btn[DEV_BTN_B].int_pending
                  ? 0
                  : 1);
    Monitor.print(" press=");
    Monitor.println(press_name);
    print_leds();
}

/**
 * @brief Uppercase a token in place so commands are case insensitive.
 * @param text Null terminated token.
 */
static void to_upper(char *text) {
    while (*text != '\0') {
        if (*text >= 'a' && *text <= 'z') {
            *text = (char)(*text - 'a' + 'A');
        }
        text++;
    }
}

/**
 * @brief Parse and run one host command.
 *
 * The press allowlist lives here rather than in the host script, so a
 * mistyped or malicious command cannot reach START, STOP or VACUUM.
 *
 * @param line Null terminated command line, modified in place.
 * @return true when the command was accepted.
 */
static bool handle_line(char *line) {
    char *verb = strtok(line, " ");
    char *arg = NULL;

    if (verb == NULL) {
        return false;
    }
    to_upper(verb);

    if (strcmp(verb, "STATE") == 0) {
        print_state();
        return true;
    }
    if (strcmp(verb, "RELEASE") == 0) {
        key_up();
        return true;
    }
    if (strcmp(verb, "HELP") == 0) {
        Monitor.println("HELP PRESS UP|DOWN [ms] / RELEASE / STATE "
                        "/ LOG ON|OFF");
        return true;
    }
    if (strcmp(verb, "LOG") == 0) {
        arg = strtok(NULL, " ");
        if (arg == NULL) {
            return false;
        }
        to_upper(arg);
        if (strcmp(arg, "ON") == 0) {
            log_all_reads = true;
            return true;
        }
        if (strcmp(arg, "OFF") == 0) {
            log_all_reads = false;
            return true;
        }
        return false;
    }
    if (strcmp(verb, "PRESS") == 0) {
        char *name = strtok(NULL, " ");
        char *hold = NULL;
        uint32_t hold_ms = PRESS_HOLD_DEFAULT_MS;

        if (name == NULL) {
            return false;
        }
        to_upper(name);

        if (press_port != PRESS_NONE) {
            Monitor.println("BUSY");
            return false;
        }

        hold = strtok(NULL, " ");
        if (hold != NULL) {
            hold_ms = (uint32_t)strtoul(hold, NULL, DECIMAL_BASE);
            if (hold_ms < PRESS_HOLD_MIN_MS
                || hold_ms > PRESS_HOLD_MAX_MS) {
                return false;
            }
        }

        if (strcmp(name, "UP") == 0) {
            key_down("UP", KEY_UP_PORT, KEY_UP_BIT, hold_ms);
            return true;
        }
        if (strcmp(name, "DOWN") == 0) {
            key_down("DOWN", KEY_DOWN_PORT, KEY_DOWN_BIT, hold_ms);
            return true;
        }
        /* Every other key, START and STOP included, is refused here
         * on purpose. See the file header. */
        return false;
    }
    return false;
}

/**
 * @brief Collect Monitor input into lines and run the complete ones.
 */
static void service_host(void) {
    while (Monitor.available() > 0) {
        char c = (char)Monitor.read();

        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            char echo[LINE_BUF_SIZE];

            line_buf[line_len] = '\0';
            strncpy(echo, line_buf, sizeof(echo) - 1);
            echo[sizeof(echo) - 1] = '\0';
            line_len = 0;

            if (echo[0] == '\0') {
                continue;
            }
            if (handle_line(line_buf)) {
                Monitor.println("OK");
            } else {
                Monitor.print("ERR raw=[");
                Monitor.print(echo);
                Monitor.println(']');
            }
            continue;
        }
        if (line_len < (LINE_BUF_SIZE - 1)) {
            line_buf[line_len++] = c;
        }
    }
}

/**
 * @brief Load the power-on reset values into both register files.
 */
static void reset_state(void) {
    uint8_t i = 0;
    uint8_t p = 0;

    for (i = 0; i < 2; i++) {
        for (p = 0; p < PCA9555_PORT_COUNT; p++) {
            btn[i].keys[p] = KEYS_RELEASED;
            btn[i].output[p] = PCA9555_POR_OUTPUT;
            btn[i].polarity[p] = PCA9555_POR_POLARITY;
            btn[i].config[p] = PCA9555_POR_CONFIG;
        }
        btn[i].pointer = PCA9555_INPUT_0;
        btn[i].expect_pointer = true;
        btn[i].int_pending = false;
    }

    for (i = 0; i < PCA9532_REG_COUNT; i++) {
        led.reg[i] = PCA9532_POR_LS;
    }
    led.reg[PCA9532_PSC0] = PCA9532_POR_PSC;
    led.reg[PCA9532_PWM0] = PCA9532_POR_PWM;
    led.reg[PCA9532_PSC1] = PCA9532_POR_PSC;
    led.reg[PCA9532_PWM1] = PCA9532_POR_PWM;
    led.pointer = PCA9532_INPUT_0;
    led.auto_increment = false;
    led.expect_pointer = true;
    led.ls_dirty = false;
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    Bridge.begin();

    Monitor.begin(SERIAL_BAUD);
    while (!Monitor) {
        delay(MONITOR_RETRY_MS);
    }

    Monitor.println("BOOT pca9555_emu");

    reset_state();
    int_apply();

    /* Wire.begin() with no address only re-applies the pinctrl state;
     * it does not claim the bus as a master. The mainboard stays the
     * only master here. */
    Wire.begin();
    Wire2.begin();

    register_target(DEV_BTN_A, btn_bus, ADDR_BTN_A);
    register_target(DEV_BTN_B, btn_bus, ADDR_BTN_B);
    register_target(DEV_LED, led_bus, ADDR_LED);

    poll_report_at = millis() + POLL_REPORT_MS;

    Monitor.println("READY");
}

void loop() {
    if (press_port != PRESS_NONE
        && (int32_t)(millis() - press_end_ms) >= 0) {
        key_up();
    }

    if (led.ls_dirty) {
        led.ls_dirty = false;
        print_leds();
    }

    drain_log();
    report_poll();
    service_host();
}
