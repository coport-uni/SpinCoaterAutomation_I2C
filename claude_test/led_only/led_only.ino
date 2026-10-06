/* Serve 0x60 (PCA9532) and 0x21 (PCA9555, no key pressed) from i2c2
 * alone, and record every target callback silently, to see whether LS2
 * and LS0 keep losing their data byte when no second controller shares
 * the bus (#18). One-off probe; delete once #18 is settled.
 *
 * Run 1 served 0x60 only and the mainboard never addressed it, most
 * likely because it skips the keypad when 0x21 stays silent. A single
 * STM32 controller holds two own addresses, so this run adds 0x21 and
 * leaves 0x22 unanswered.
 *
 * Wiring: i2c2 (D20 SDA, D21 SCL) is the side the PCA9306 is on. Pull
 * the A4/A5 to D20/D21 jumpers; i2c3 is never started.
 *
 * Nothing is printed for CAPTURE_MS after boot, so the Monitor link
 * stays quiet while the mainboard starts. Then the whole record is
 * printed once, and STATE / DUMP / NEW work on request.
 */

#include <Arduino_RouterBridge.h>
#include <Wire.h>

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#define SERIAL_BAUD 115200
#define MONITOR_RETRY_MS 500
#define ADDR_BTN 0x21
#define ADDR_LED 0x60

/* Long enough for the upload to finish, the operator to power the spin
 * coater on, and the mainboard to reach Select Process (about 45 s). */
#define CAPTURE_MS 180000UL

#define LED_REG_COUNT 10
#define LED_REG_MASK 0x0F
#define AUTO_INCREMENT 0x10
#define REG_PSC0 0x02
#define REG_LS0 0x06
#define BTN_REG_MASK 0x07

#define RING_SIZE 8192
/* 0x21 is polled every 50 ms; only its first events are kept so the
 * ring cannot fill before the LED selectors are written. */
#define BTN_EVENT_CAP 2000
#define LINE_BUF_SIZE 32

enum { EV_WREQ, EV_CMD, EV_RX, EV_RREQ, EV_TX, EV_STOP, EV_ERR };
static const char *const KIND_NAMES[] = {
    "WREQ", "CMD", "RX", "RREQ", "TX", "STOP", "ERR",
};

typedef struct {
    uint32_t ms;
    uint8_t kind;
    uint8_t addr;
    uint8_t reg;
    uint8_t data;
} event_t;

static volatile event_t ring[RING_SIZE];
static volatile uint16_t head = 0;
static volatile uint32_t dropped = 0;
static volatile uint32_t btn_events = 0;
static uint16_t printed = 0;

/* PCA9532 at 0x60. */
static volatile uint8_t led_regs[LED_REG_COUNT];
static volatile uint8_t led_ptr = 0;
static volatile bool led_ai = false;
static volatile bool led_expect = true;

/* PCA9555 at 0x21: inputs read 0xFF, nothing pressed. */
static volatile uint8_t btn_regs[8] = {0xFF, 0xFF, 0xFF, 0xFF,
                                       0x00, 0x00, 0xFF, 0xFF};
static volatile uint8_t btn_ptr = 0;
static volatile bool btn_expect = true;

static struct i2c_target_callbacks cb;
static struct i2c_target_config cfg_btn;
static struct i2c_target_config cfg_led;
static int rc_btn = -1;
static int rc_led = -1;
static const struct device *const bus = DEVICE_DT_GET(DT_NODELABEL(i2c2));

static bool captured = false;
static char line_buf[LINE_BUF_SIZE];
static uint8_t line_len = 0;

static void push(uint8_t kind, uint8_t addr, uint8_t reg, uint8_t data) {
    unsigned int key = irq_lock();

    if (addr == ADDR_BTN) {
        if (btn_events >= BTN_EVENT_CAP) {
            irq_unlock(key);
            return;
        }
        btn_events++;
    }
    if (head == RING_SIZE) {
        dropped++;
    } else {
        ring[head].ms = millis();
        ring[head].kind = kind;
        ring[head].addr = addr;
        ring[head].reg = reg;
        ring[head].data = data;
        head++;
    }
    irq_unlock(key);
}

/* PCA9532 INPUT0/1 read back as pin levels: 1 for an off channel. */
static uint8_t led_read(uint8_t reg) {
    if (reg < REG_PSC0) {
        uint8_t value = 0;
        for (uint8_t i = 0; i < 8; i++) {
            uint8_t ch = (uint8_t)(reg * 8 + i);
            uint8_t ls = led_regs[REG_LS0 + ch / 4];
            if (((ls >> ((ch % 4) * 2)) & 0x03) == 0) {
                value |= (uint8_t)(1u << i);
            }
        }
        return value;
    }
    return led_regs[reg];
}

static int on_write_requested(struct i2c_target_config *config) {
    uint8_t addr = (uint8_t)config->address;

    if (addr == ADDR_LED) {
        led_expect = true;
    } else {
        btn_expect = true;
    }
    push(EV_WREQ, addr, 0, 0);
    return 0;
}

static int on_write_received(struct i2c_target_config *config, uint8_t val) {
    uint8_t addr = (uint8_t)config->address;

    if (addr == ADDR_LED) {
        if (led_expect) {
            led_ai = (val & AUTO_INCREMENT) != 0;
            led_ptr = (uint8_t)((val & LED_REG_MASK) % LED_REG_COUNT);
            led_expect = false;
            push(EV_CMD, addr, led_ptr, val);
            return 0;
        }
        if (led_ptr >= REG_PSC0) {
            led_regs[led_ptr] = val;
        }
        push(EV_RX, addr, led_ptr, val);
        if (led_ai) {
            led_ptr = (uint8_t)((led_ptr + 1) % LED_REG_COUNT);
        }
        return 0;
    }

    if (btn_expect) {
        btn_ptr = (uint8_t)(val & BTN_REG_MASK);
        btn_expect = false;
        push(EV_CMD, addr, btn_ptr, val);
        return 0;
    }
    if (btn_ptr >= 2) {
        btn_regs[btn_ptr] = val;
    }
    push(EV_RX, addr, btn_ptr, val);
    btn_ptr = (uint8_t)(btn_ptr ^ 0x01);
    return 0;
}

static int serve(uint8_t addr, uint8_t *val) {
    if (addr == ADDR_LED) {
        *val = led_read(led_ptr);
        push(EV_TX, addr, led_ptr, *val);
        if (led_ai) {
            led_ptr = (uint8_t)((led_ptr + 1) % LED_REG_COUNT);
        }
        return 0;
    }
    *val = btn_regs[btn_ptr];
    push(EV_TX, addr, btn_ptr, *val);
    btn_ptr = (uint8_t)(btn_ptr ^ 0x01);
    return 0;
}

static int on_read_requested(struct i2c_target_config *config, uint8_t *val) {
    uint8_t addr = (uint8_t)config->address;

    push(EV_RREQ, addr, 0, 0);
    return serve(addr, val);
}

static int on_read_processed(struct i2c_target_config *config, uint8_t *val) {
    return serve((uint8_t)config->address, val);
}

static int on_stop(struct i2c_target_config *config) {
    uint8_t addr = (uint8_t)config->address;

    if (addr == ADDR_LED) {
        led_expect = true;
    } else {
        btn_expect = true;
    }
    push(EV_STOP, addr, 0, 0);
    return 0;
}

static void on_error(struct i2c_target_config *config,
                     enum i2c_error_reason error_code) {
    push(EV_ERR, (uint8_t)config->address, 0, (uint8_t)error_code);
}

static void print_hex8(uint8_t value) {
    Monitor.print("0x");
    if (value < 0x10) {
        Monitor.print('0');
    }
    Monitor.print(value, HEX);
}

static void print_state(void) {
    Monitor.print("STATE rc21=");
    Monitor.print(rc_btn);
    Monitor.print(" rc60=");
    Monitor.print(rc_led);
    for (uint8_t r = REG_PSC0; r < LED_REG_COUNT; r++) {
        Monitor.print(' ');
        print_hex8(r);
        Monitor.print('=');
        print_hex8(led_regs[r]);
    }
    Monitor.print(" events=");
    Monitor.print(head);
    Monitor.print(" btn_events=");
    Monitor.print(btn_events);
    Monitor.print(" dropped=");
    Monitor.println(dropped);
}

static void print_from(uint16_t start) {
    uint16_t end = head;

    for (uint16_t i = start; i < end; i++) {
        uint8_t kind = ring[i].kind;

        Monitor.print(KIND_NAMES[kind]);
        Monitor.print(' ');
        print_hex8(ring[i].addr);
        if (kind == EV_CMD || kind == EV_RX || kind == EV_TX) {
            Monitor.print(" reg=");
            print_hex8(ring[i].reg);
            Monitor.print(" data=");
            print_hex8(ring[i].data);
        } else if (kind == EV_ERR) {
            Monitor.print(" code=");
            Monitor.print(ring[i].data);
        }
        Monitor.print(" ms=");
        Monitor.println(ring[i].ms);
    }
    printed = end;
}

static void service_host(void) {
    while (Monitor.available() > 0) {
        char c = (char)Monitor.read();

        if (c == '\r') {
            continue;
        }
        if (c != '\n') {
            if (line_len < LINE_BUF_SIZE - 1) {
                line_buf[line_len++] = c;
            }
            continue;
        }
        line_buf[line_len] = '\0';
        line_len = 0;
        if (strcmp(line_buf, "STATE") == 0) {
            print_state();
        } else if (strcmp(line_buf, "DUMP") == 0) {
            print_from(0);
            print_state();
        } else if (strcmp(line_buf, "NEW") == 0) {
            print_from(printed);
            print_state();
        }
    }
}

static void register_one(struct i2c_target_config *cfg, uint8_t addr,
                         int *rc) {
    cfg->address = addr;
    cfg->callbacks = &cb;
    *rc = i2c_target_register(bus, cfg);
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    Bridge.begin();
    Monitor.begin(SERIAL_BAUD);
    while (!Monitor) {
        delay(MONITOR_RETRY_MS);
    }
    Monitor.println("BOOT led_only (0x21 + 0x60 on i2c2)");

    led_regs[0x02] = 0xFF; /* PSC0 */
    led_regs[0x03] = 0x80; /* PWM0 */
    led_regs[0x04] = 0xFF; /* PSC1 */
    led_regs[0x05] = 0x80; /* PWM1 */

    /* Applies the i2c2 pinctrl only; never acts as a master. */
    Wire.begin();

    cb.write_requested = on_write_requested;
    cb.write_received = on_write_received;
    cb.read_requested = on_read_requested;
    cb.read_processed = on_read_processed;
    cb.stop = on_stop;
    cb.error = on_error;

    register_one(&cfg_btn, ADDR_BTN, &rc_btn);
    register_one(&cfg_led, ADDR_LED, &rc_led);
    print_state();
}

void loop() {
    if (!captured) {
        if (millis() < CAPTURE_MS) {
            return;
        }
        captured = true;
        Monitor.println("CAPTURE end");
        print_from(0);
        print_state();
    }
    service_host();
}
