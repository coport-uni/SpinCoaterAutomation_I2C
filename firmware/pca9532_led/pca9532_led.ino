/**
 * @file pca9532_led.ino
 * @brief PCA9532 LED driver control for the spincoater keypad board.
 *
 * Lights the keypad LEDs one at a time so each LS register field can be
 * traced to a physical button LED. The keypad carries 18 LEDs but the
 * PCA9532 only drives 16, so the two buttons whose LED never lights
 * during a full walk are the ones wired elsewhere.
 *
 * All 16 LEDs are deliberately never lit at once so the bench supply is
 * not loaded with 16 LEDs, which is why an "all on" command is rejected
 * on purpose.
 *
 * Builds for two boards. On the UNO Q the host link is the Router
 * Bridge Monitor object, because that board's Serial does not reach the
 * host. On the UNO R4 Minima, and on any other ordinary board, it is
 * plain Serial over USB. HOST hides the difference.
 *
 * Commands, one per line, from the host:
 *   LED <n> on | off | pwm0 | pwm1   n is 0..15
 *   ALL off
 *   WALK <ms>                        light 0..15 in turn, ms each
 *   HALT                             stop walking and clear
 *
 * Output, one line each:
 *   OK
 *   ERR
 *   WALK <n>
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

/** Retry delay while the Monitor link is not up yet, ms. */
#define MONITOR_RETRY_MS 500

/** Time to wait for a USB CDC host to attach before giving up, ms. */
#define SERIAL_WAIT_MS 3000

/** Wire.endTransmission() result meaning the slave acknowledged. */
#define I2C_ACK 0

/** PCA9532 LED dimmer found by firmware/i2c_scan. */
#define PCA9532_ADDR 0x60

/** PCA9532 blink generator and LED selector registers. */
#define PCA9532_REG_PSC0 0x02
#define PCA9532_REG_PWM0 0x03
#define PCA9532_REG_PSC1 0x04
#define PCA9532_REG_PWM1 0x05
#define PCA9532_REG_LS0 0x06

/** LED channels and how they pack into the four LS registers. */
#define PCA9532_LED_COUNT 16
#define PCA9532_LS_COUNT 4
#define PCA9532_LEDS_PER_LS 4
#define PCA9532_LS_FIELD_BITS 2
#define PCA9532_LS_FIELD_MASK 0x03

/** Two bit LS field values. */
#define LS_OFF 0x00
#define LS_ON 0x01
#define LS_PWM0 0x02
#define LS_PWM1 0x03

/** Blink dividers. The period is (PSC + 1) / 152 seconds. */
#define PSC0_SLOW 151
#define PSC1_FAST 75

/** Duty cycle byte, 128 of 256 is 50 percent. */
#define PWM_HALF 128

/** Longest command line accepted, bytes. */
#define CMD_BUF_LEN 32

/** walk_period_ms value meaning the walk is stopped. */
#define WALK_OFF 0UL

/** Mirror of the four LS registers, which are write mostly here. */
static uint8_t ls_shadow[PCA9532_LS_COUNT];

/** Walk state. */
static unsigned long walk_period_ms = WALK_OFF;
static unsigned long walk_last_ms = 0;
static uint8_t walk_index = 0;

/**
 * @brief Write one 8 bit register on the PCA9532.
 *
 * Only the control byte and one data byte are sent, so the auto
 * increment bit stays clear and no neighbouring register is touched.
 *
 * @param reg Register index.
 * @param val Value to store.
 * @return true when the slave acknowledged.
 */
static bool pca9532_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(PCA9532_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == I2C_ACK;
}

/**
 * @brief Set the two bit LS field of one LED channel.
 * @param led Channel index, 0..15.
 * @param state One of LS_OFF, LS_ON, LS_PWM0, LS_PWM1.
 * @return true when the register write succeeded.
 */
static bool led_set(uint8_t led, uint8_t state) {
    uint8_t ls = led / PCA9532_LEDS_PER_LS;
    uint8_t shift = (led % PCA9532_LEDS_PER_LS) * PCA9532_LS_FIELD_BITS;
    uint8_t val = ls_shadow[ls];

    val &= ~(PCA9532_LS_FIELD_MASK << shift);
    val |= (state & PCA9532_LS_FIELD_MASK) << shift;

    if (!pca9532_write(PCA9532_REG_LS0 + ls, val)) {
        return false;
    }
    ls_shadow[ls] = val;
    return true;
}

/**
 * @brief Turn every LED channel off.
 * @return true when all four LS registers were written.
 */
static bool led_all_off(void) {
    bool ok = true;

    for (uint8_t ls = 0; ls < PCA9532_LS_COUNT; ls++) {
        if (!pca9532_write(PCA9532_REG_LS0 + ls, LS_OFF)) {
            ok = false;
        } else {
            ls_shadow[ls] = LS_OFF;
        }
    }
    return ok;
}

/**
 * @brief Map a state word from the host to an LS field value.
 * @param word The words on, off, pwm0 or pwm1.
 * @param out Receives the LS field value.
 * @return true when the word was recognised.
 */
static bool parse_state(const char *word, uint8_t *out) {
    if (strcmp(word, "on") == 0) {
        *out = LS_ON;
    } else if (strcmp(word, "off") == 0) {
        *out = LS_OFF;
    } else if (strcmp(word, "pwm0") == 0) {
        *out = LS_PWM0;
    } else if (strcmp(word, "pwm1") == 0) {
        *out = LS_PWM1;
    } else {
        return false;
    }
    return true;
}

/**
 * @brief Execute one command line.
 * @param line Mutable, null terminated command line.
 * @return true when the command was understood and applied.
 */
static bool handle_line(char *line) {
    char *verb = strtok(line, " ");

    if (verb == NULL) {
        return false;
    }

    if (strcmp(verb, "LED") == 0) {
        char *num = strtok(NULL, " ");
        char *word = strtok(NULL, " ");
        uint8_t state = LS_OFF;
        int led = 0;

        if (num == NULL || word == NULL) {
            return false;
        }
        led = atoi(num);
        if (led < 0 || led >= PCA9532_LED_COUNT) {
            return false;
        }
        if (!parse_state(word, &state)) {
            return false;
        }
        walk_period_ms = WALK_OFF;
        return led_set((uint8_t)led, state);
    }

    if (strcmp(verb, "ALL") == 0) {
        char *word = strtok(NULL, " ");

        /* Only the off form exists. Lighting all 16 at once is refused
         * so the 3.3 V bench supply is not loaded with 16 LEDs. */
        if (word == NULL || strcmp(word, "off") != 0) {
            return false;
        }
        walk_period_ms = WALK_OFF;
        return led_all_off();
    }

    if (strcmp(verb, "WALK") == 0) {
        char *num = strtok(NULL, " ");
        long period = 0;

        if (num == NULL) {
            return false;
        }
        period = atol(num);
        if (period <= 0) {
            return false;
        }
        if (!led_all_off()) {
            return false;
        }
        walk_index = 0;
        walk_period_ms = (unsigned long)period;
        walk_last_ms = millis();
        return led_set(walk_index, LS_ON);
    }

    if (strcmp(verb, "HALT") == 0) {
        walk_period_ms = WALK_OFF;
        return led_all_off();
    }

    return false;
}

/**
 * @brief Read a pending command line from the host and answer it.
 */
static void service_host(void) {
    char buf[CMD_BUF_LEN];
    String line;
    size_t len = 0;

    if (!HOST.available()) {
        return;
    }

    line = HOST.readStringUntil('\n');
    line.trim();
    len = line.length();
    if (len == 0 || len >= CMD_BUF_LEN) {
        /* The echo names what was actually received, which is the only
         * way to tell a dropped line from a misparsed one over USB. */
        HOST.print("ERR len=");
        HOST.print(len);
        HOST.print(" raw=[");
        HOST.print(line);
        HOST.println(']');
        return;
    }

    line.toCharArray(buf, CMD_BUF_LEN);
    if (handle_line(buf)) {
        HOST.println("OK");
    } else {
        HOST.print("ERR raw=[");
        HOST.print(line);
        HOST.println(']');
    }
}

/**
 * @brief Advance the walk when its dwell time has elapsed.
 */
static void service_walk(void) {
    if (walk_period_ms == WALK_OFF) {
        return;
    }
    if ((millis() - walk_last_ms) < walk_period_ms) {
        return;
    }

    led_set(walk_index, LS_OFF);
    walk_index = (walk_index + 1) % PCA9532_LED_COUNT;
    led_set(walk_index, LS_ON);
    walk_last_ms = millis();

    HOST.print("WALK ");
    HOST.println(walk_index);
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

void setup() {
    host_begin();

    /* Announced before any I2C traffic. An unterminated bus can stall
     * Wire, and knowing whether BOOT was reached separates a dead link
     * from a stalled transfer. */
    HOST.println("BOOT pca9532_led");

    Wire.begin();
    HOST.println("I2C up");

    /* Known blink rates for the two PWM sources, then a clean slate. */
    pca9532_write(PCA9532_REG_PSC0, PSC0_SLOW);
    pca9532_write(PCA9532_REG_PWM0, PWM_HALF);
    pca9532_write(PCA9532_REG_PSC1, PSC1_FAST);
    pca9532_write(PCA9532_REG_PWM1, PWM_HALF);
    led_all_off();

    HOST.println("READY");
}

void loop() {
    service_host();
    service_walk();
}
