/* Test whether the transfers lost after an even command byte (#18) are
 * an ACK-timing fault the target's TIMINGR can fix (#21). One-off
 * probe; delete once #18 is settled.
 *
 * Serves the emulator's three addresses exactly as pca9555_emu_gui
 * does (0x21 and 0x22 on i2c2, 0x60 on i2c3, A4/A5 jumpered to
 * D20/D21) but never presses a key: every input reads 0xFF. Nothing is
 * printed unless asked, so the Monitor link never loads the CPU.
 *
 * The metric is the mainboard's own 50 ms poll. Each poll writes 0x21
 * [00] and 0x22 [00], and the read that should follow is lost today.
 * For every command byte the probe counts what followed it:
 *   data  a data byte in the same write
 *   read  a pointer-only write, then a read of the same address
 *   lost  a pointer-only write, then another write: the follow-up
 *         never arrived
 *
 * It also times command byte to STOP for pointer-only writes, split by
 * the command byte's parity and by outcome. If the STM32 is taking a
 * SDA rise at the end of bit 0 for a STOP (hypothesis 2), the lost
 * transfers stop within a microsecond or two; after a NACK the master
 * needs at least one more bit period. The [01] pointer writes that are
 * followed by a read are the reference.
 *
 * TIMINGR is changed at run time, with the peripheral disabled (PE=0)
 * while the bus is idle, as RM0456 requires. OAR1/OAR2 and the
 * interrupt enables survive PE=0.
 *
 * Host commands on Monitor:
 *   REGS                     CR1, TIMINGR, OAR1, OAR2 of both buses
 *   TIMING <2|3> <hex>       write TIMINGR
 *   SDADEL <2|3> <0..15>     change one field of TIMINGR
 *   SCLDEL <2|3> <0..15>
 *   PRESC  <2|3> <0..15>
 *   STAT                     print the counters, then "STAT end"
 *   CLEAR                    zero the counters
 */

#include <Arduino_RouterBridge.h>
#include <Wire.h>

#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#define SERIAL_BAUD 115200
#define MONITOR_RETRY_MS 500
#define LINE_BUF_SIZE 48

#define ADDR_BTN_A 0x21
#define ADDR_BTN_B 0x22
#define ADDR_LED 0x60

enum { DEV_BTN_A, DEV_BTN_B, DEV_LED, DEV_COUNT };
static const uint8_t DEV_ADDR[DEV_COUNT] = {ADDR_BTN_A, ADDR_BTN_B,
                                            ADDR_LED};

/* PCA9555: input 0/1, output 0/1, polarity 0/1, config 0/1. */
#define PCA9555_REG_COUNT 8
#define PCA9555_REG_MASK 0x07
/* PCA9532: input 0/1, PSC0, PWM0, PSC1, PWM1, LS0..LS3. */
#define PCA9532_REG_COUNT 10
#define PCA9532_REG_MASK 0x0F
#define PCA9532_AUTO_INC 0x10
#define PCA9532_PSC0 0x02
#define PCA9532_LS0 0x06

/* Counters are kept per command byte, low five bits. */
#define CMD_SLOTS 32
#define CMD_MASK 0x1F
#define NO_CMD (-1)

/* STM32 I2C v2 registers, RM0456. */
#define REG_CR1 0x00
#define REG_OAR1 0x08
#define REG_OAR2 0x0C
#define REG_TIMINGR 0x10
#define REG_ISR 0x18
#define CR1_PE (1u << 0)
#define ISR_BUSY (1u << 15)
#define PE_OFF_READS 8
#define IDLE_WAIT_TRIES 200000

#define FIELD_PRESC 28
#define FIELD_SCLDEL 20
#define FIELD_SDADEL 16
#define FIELD4_MASK 0x0Fu

#define I2C2_BASE_ADDR DT_REG_ADDR(DT_NODELABEL(i2c2))
#define I2C3_BASE_ADDR DT_REG_ADDR(DT_NODELABEL(i2c3))

typedef struct {
    uint32_t n;
    uint32_t min;
    uint32_t max;
    uint64_t sum;
} span_t;

enum { OUT_READ, OUT_LOST, OUT_COUNT };

typedef struct {
    /* Transfer in progress. */
    int16_t cmd;
    bool got_data;
    bool is_write;
    uint32_t cmd_cycles;
    /* Pointer-only write that ended, waiting to see what follows. */
    int16_t waiting_cmd;
    uint32_t waiting_span;
    /* Counters. */
    uint32_t n_cmd[CMD_SLOTS];
    uint32_t n_data[CMD_SLOTS];
    uint32_t n_read[CMD_SLOTS];
    uint32_t n_lost[CMD_SLOTS];
    span_t stop_span[2][OUT_COUNT];
    span_t byte_span;
    uint32_t n_err;
} dev_stats_t;

static volatile dev_stats_t stats[DEV_COUNT];

typedef struct {
    uint8_t reg[PCA9532_REG_COUNT];
    uint8_t pointer;
    bool expect_pointer;
    bool auto_inc;
} regs_t;

static volatile regs_t regs[DEV_COUNT];

static struct i2c_target_callbacks target_cb[DEV_COUNT];
static struct i2c_target_config target_cfg[DEV_COUNT];
static int target_rc[DEV_COUNT];

static const struct device *const btn_bus =
    DEVICE_DT_GET(DT_NODELABEL(i2c2));
static const struct device *const led_bus =
    DEVICE_DT_GET(DT_NODELABEL(i2c3));

static char line_buf[LINE_BUF_SIZE];
static uint8_t line_len = 0;

static uint8_t dev_index(uint16_t addr) {
    if (addr == ADDR_BTN_A) {
        return DEV_BTN_A;
    }
    if (addr == ADDR_BTN_B) {
        return DEV_BTN_B;
    }
    return DEV_LED;
}

static void span_add(volatile span_t *s, uint32_t cycles) {
    if (s->n == 0 || cycles < s->min) {
        s->min = cycles;
    }
    if (cycles > s->max) {
        s->max = cycles;
    }
    s->sum += cycles;
    s->n++;
}

/* A pointer-only write ended earlier; the next event on the same
 * address decides whether its follow-up arrived. */
static void settle_waiting(volatile dev_stats_t *st, uint8_t outcome) {
    int16_t cmd = st->waiting_cmd;

    if (cmd == NO_CMD) {
        return;
    }
    if (outcome == OUT_READ) {
        st->n_read[cmd]++;
    } else {
        st->n_lost[cmd]++;
    }
    span_add(&st->stop_span[cmd & 0x01][outcome], st->waiting_span);
    st->waiting_cmd = NO_CMD;
}

static uint8_t pca9532_input(volatile regs_t *r, uint8_t reg) {
    uint8_t value = 0;

    for (uint8_t i = 0; i < 8; i++) {
        uint8_t ch = (uint8_t)(reg * 8 + i);
        uint8_t ls = r->reg[PCA9532_LS0 + ch / 4];
        if (((ls >> ((ch % 4) * 2)) & 0x03) == 0) {
            value |= (uint8_t)(1u << i);
        }
    }
    return value;
}

static uint8_t read_reg(uint8_t idx) {
    volatile regs_t *r = &regs[idx];
    uint8_t ptr = r->pointer;

    if (idx == DEV_LED) {
        uint8_t v = ptr < PCA9532_PSC0 ? pca9532_input(r, ptr) : r->reg[ptr];
        if (r->auto_inc) {
            r->pointer = (uint8_t)((ptr + 1) % PCA9532_REG_COUNT);
        }
        return v;
    }
    /* No key is ever pressed: an input pin reads high unless it is an
     * output, where it reads back what the mainboard drove. */
    uint8_t v = r->reg[ptr];
    if (ptr < 2) {
        uint8_t cfg = r->reg[6 + ptr];
        uint8_t out = r->reg[2 + ptr];
        v = (uint8_t)(cfg | (out & (uint8_t)~cfg));
    }
    r->pointer = (uint8_t)(ptr ^ 0x01);
    return v;
}

static void write_reg(uint8_t idx, uint8_t val) {
    volatile regs_t *r = &regs[idx];
    uint8_t ptr = r->pointer;

    if (idx == DEV_LED) {
        if (ptr >= PCA9532_PSC0) {
            r->reg[ptr] = val;
        }
        if (r->auto_inc) {
            r->pointer = (uint8_t)((ptr + 1) % PCA9532_REG_COUNT);
        }
        return;
    }
    if (ptr >= 2) {
        r->reg[ptr] = val;
    }
    r->pointer = (uint8_t)(ptr ^ 0x01);
}

static int on_write_requested(struct i2c_target_config *config) {
    uint8_t idx = dev_index(config->address);
    volatile dev_stats_t *st = &stats[idx];

    settle_waiting(st, OUT_LOST);
    st->cmd = NO_CMD;
    st->got_data = false;
    st->is_write = true;
    regs[idx].expect_pointer = true;
    return 0;
}

static int on_write_received(struct i2c_target_config *config,
                             uint8_t val) {
    uint32_t now = k_cycle_get_32();
    uint8_t idx = dev_index(config->address);
    volatile dev_stats_t *st = &stats[idx];
    volatile regs_t *r = &regs[idx];

    if (r->expect_pointer) {
        r->expect_pointer = false;
        if (idx == DEV_LED) {
            r->auto_inc = (val & PCA9532_AUTO_INC) != 0;
            r->pointer = (uint8_t)((val & PCA9532_REG_MASK)
                                   % PCA9532_REG_COUNT);
        } else {
            r->pointer = (uint8_t)(val & PCA9555_REG_MASK);
        }
        st->cmd = (int16_t)(val & CMD_MASK);
        st->cmd_cycles = now;
        st->n_cmd[st->cmd]++;
        return 0;
    }
    if (!st->got_data && st->cmd != NO_CMD) {
        st->got_data = true;
        st->n_data[st->cmd]++;
        span_add(&st->byte_span, now - st->cmd_cycles);
    }
    write_reg(idx, val);
    return 0;
}

static int on_read_requested(struct i2c_target_config *config,
                             uint8_t *val) {
    uint8_t idx = dev_index(config->address);
    volatile dev_stats_t *st = &stats[idx];

    settle_waiting(st, OUT_READ);
    /* Repeated start with no STOP in between: still a delivered read. */
    if (st->is_write && st->cmd != NO_CMD && !st->got_data) {
        st->n_read[st->cmd]++;
    }
    st->cmd = NO_CMD;
    st->is_write = false;
    *val = read_reg(idx);
    return 0;
}

static int on_read_processed(struct i2c_target_config *config,
                             uint8_t *val) {
    *val = read_reg(dev_index(config->address));
    return 0;
}

static int on_stop(struct i2c_target_config *config) {
    uint32_t now = k_cycle_get_32();
    uint8_t idx = dev_index(config->address);
    volatile dev_stats_t *st = &stats[idx];

    if (st->is_write && st->cmd != NO_CMD && !st->got_data) {
        st->waiting_cmd = st->cmd;
        st->waiting_span = now - st->cmd_cycles;
    }
    st->cmd = NO_CMD;
    st->is_write = false;
    regs[idx].expect_pointer = true;
    return 0;
}

static void on_error(struct i2c_target_config *config,
                     enum i2c_error_reason error_code) {
    (void)error_code;
    stats[dev_index(config->address)].n_err++;
}

static volatile uint32_t *bus_reg(uint8_t bus, uint32_t offset) {
    uintptr_t base = bus == 3 ? I2C3_BASE_ADDR : I2C2_BASE_ADDR;
    return (volatile uint32_t *)(base + offset);
}

/* Write TIMINGR with the peripheral off, between transfers. Returns
 * false if the bus never went idle. */
static bool set_timing(uint8_t bus, uint32_t value) {
    volatile uint32_t *cr1 = bus_reg(bus, REG_CR1);
    volatile uint32_t *isr = bus_reg(bus, REG_ISR);
    volatile uint32_t *timingr = bus_reg(bus, REG_TIMINGR);

    for (uint32_t i = 0; i < IDLE_WAIT_TRIES; i++) {
        unsigned int key = irq_lock();

        if ((*isr & ISR_BUSY) == 0) {
            *cr1 &= ~CR1_PE;
            /* PE must stay low for at least three APB clocks. */
            for (uint8_t j = 0; j < PE_OFF_READS; j++) {
                (void)*cr1;
            }
            *timingr = value;
            *cr1 |= CR1_PE;
            irq_unlock(key);
            return true;
        }
        irq_unlock(key);
    }
    return false;
}

static void print_hex8(uint8_t value) {
    Monitor.print("0x");
    if (value < 0x10) {
        Monitor.print('0');
    }
    Monitor.print(value, HEX);
}

static void print_hex32(uint32_t value) {
    Monitor.print("0x");
    for (int8_t shift = 28; shift >= 0; shift -= 4) {
        Monitor.print((value >> shift) & 0x0F, HEX);
    }
}

static void print_regs(void) {
    for (uint8_t bus = 2; bus <= 3; bus++) {
        uint32_t t = *bus_reg(bus, REG_TIMINGR);

        Monitor.print("REGS i2c");
        Monitor.print(bus);
        Monitor.print(" cr1=");
        print_hex32(*bus_reg(bus, REG_CR1));
        Monitor.print(" timingr=");
        print_hex32(t);
        Monitor.print(" presc=");
        Monitor.print((t >> FIELD_PRESC) & FIELD4_MASK);
        Monitor.print(" scldel=");
        Monitor.print((t >> FIELD_SCLDEL) & FIELD4_MASK);
        Monitor.print(" sdadel=");
        Monitor.print((t >> FIELD_SDADEL) & FIELD4_MASK);
        Monitor.print(" sclh=");
        Monitor.print((t >> 8) & 0xFF);
        Monitor.print(" scll=");
        Monitor.print(t & 0xFF);
        Monitor.print(" oar1=");
        print_hex32(*bus_reg(bus, REG_OAR1));
        Monitor.print(" oar2=");
        print_hex32(*bus_reg(bus, REG_OAR2));
        Monitor.println();
    }
}

static uint32_t cycles_to_ns(uint64_t cycles) {
    return (uint32_t)(cycles * 1000000000ULL / sys_clock_hw_cycles_per_sec());
}

static void print_span(const char *label, const volatile span_t *s) {
    Monitor.print(label);
    Monitor.print(" n=");
    Monitor.print(s->n);
    if (s->n > 0) {
        Monitor.print(" min_ns=");
        Monitor.print(cycles_to_ns(s->min));
        Monitor.print(" avg_ns=");
        Monitor.print(cycles_to_ns(s->sum / s->n));
        Monitor.print(" max_ns=");
        Monitor.print(cycles_to_ns(s->max));
    }
    Monitor.println();
}

static void print_stats(void) {
    static const char *const PARITY[2] = {"even", "odd"};
    static const char *const OUTCOME[OUT_COUNT] = {"read", "lost"};
    dev_stats_t snap;

    for (uint8_t idx = 0; idx < DEV_COUNT; idx++) {
        unsigned int key = irq_lock();
        memcpy(&snap, (const void *)&stats[idx], sizeof(snap));
        irq_unlock(key);

        for (uint8_t c = 0; c < CMD_SLOTS; c++) {
            if (snap.n_cmd[c] == 0) {
                continue;
            }
            Monitor.print("STAT ");
            print_hex8(DEV_ADDR[idx]);
            Monitor.print(" cmd=");
            print_hex8(c);
            Monitor.print(" n=");
            Monitor.print(snap.n_cmd[c]);
            Monitor.print(" data=");
            Monitor.print(snap.n_data[c]);
            Monitor.print(" read=");
            Monitor.print(snap.n_read[c]);
            Monitor.print(" lost=");
            Monitor.println(snap.n_lost[c]);
        }
        for (uint8_t p = 0; p < 2; p++) {
            for (uint8_t o = 0; o < OUT_COUNT; o++) {
                if (snap.stop_span[p][o].n == 0) {
                    continue;
                }
                Monitor.print("SPAN ");
                print_hex8(DEV_ADDR[idx]);
                Monitor.print(' ');
                Monitor.print(PARITY[p]);
                Monitor.print(' ');
                Monitor.print(OUTCOME[o]);
                print_span(" cmd_to_stop", &snap.stop_span[p][o]);
            }
        }
        if (snap.byte_span.n > 0) {
            Monitor.print("SPAN ");
            print_hex8(DEV_ADDR[idx]);
            print_span(" cmd_to_data", &snap.byte_span);
        }
        if (snap.n_err > 0) {
            Monitor.print("ERR ");
            print_hex8(DEV_ADDR[idx]);
            Monitor.print(" n=");
            Monitor.println(snap.n_err);
        }
    }
    Monitor.print("STAT end ms=");
    Monitor.println(millis());
}

static void clear_stats(void) {
    unsigned int key = irq_lock();

    for (uint8_t idx = 0; idx < DEV_COUNT; idx++) {
        volatile dev_stats_t *st = &stats[idx];
        int16_t cmd = st->cmd;
        bool got_data = st->got_data;
        bool is_write = st->is_write;
        uint32_t cmd_cycles = st->cmd_cycles;

        memset((void *)st, 0, sizeof(*st));
        /* Keep the transfer in flight so its outcome is still counted. */
        st->cmd = cmd;
        st->got_data = got_data;
        st->is_write = is_write;
        st->cmd_cycles = cmd_cycles;
        st->waiting_cmd = NO_CMD;
    }
    irq_unlock(key);
    Monitor.println("CLEAR ok");
}

/* Apply TIMING / SDADEL / SCLDEL / PRESC. */
static void handle_timing(const char *name, const char *args) {
    char *end = NULL;
    long bus = strtol(args, &end, 10);

    if ((bus != 2 && bus != 3) || end == args) {
        Monitor.println("ERR bus must be 2 or 3");
        return;
    }
    uint32_t value = *bus_reg((uint8_t)bus, REG_TIMINGR);
    unsigned long arg = strtoul(end, &end, strcmp(name, "TIMING") == 0 ? 16 : 10);

    if (strcmp(name, "TIMING") == 0) {
        value = (uint32_t)arg;
    } else {
        uint8_t shift = FIELD_SDADEL;
        if (strcmp(name, "SCLDEL") == 0) {
            shift = FIELD_SCLDEL;
        } else if (strcmp(name, "PRESC") == 0) {
            shift = FIELD_PRESC;
        }
        if (arg > FIELD4_MASK) {
            Monitor.println("ERR field range is 0..15");
            return;
        }
        value = (value & ~(FIELD4_MASK << shift)) | ((uint32_t)arg << shift);
    }
    if (!set_timing((uint8_t)bus, value)) {
        Monitor.println("ERR bus never idle, TIMINGR unchanged");
        return;
    }
    print_regs();
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

        char *space = strchr(line_buf, ' ');
        const char *args = "";
        if (space != NULL) {
            *space = '\0';
            args = space + 1;
        }
        if (strcmp(line_buf, "REGS") == 0) {
            print_regs();
        } else if (strcmp(line_buf, "STAT") == 0) {
            print_stats();
        } else if (strcmp(line_buf, "CLEAR") == 0) {
            clear_stats();
        } else if (strcmp(line_buf, "TIMING") == 0
                   || strcmp(line_buf, "SDADEL") == 0
                   || strcmp(line_buf, "SCLDEL") == 0
                   || strcmp(line_buf, "PRESC") == 0) {
            handle_timing(line_buf, args);
        } else if (line_buf[0] != '\0') {
            Monitor.print("ERR unknown ");
            Monitor.println(line_buf);
        }
    }
}

static void register_target(uint8_t idx, const struct device *bus) {
    /* Assigned field by field: buffer-mode members stay NULL, which
     * keeps the driver on the byte-at-a-time path. */
    target_cb[idx].write_requested = on_write_requested;
    target_cb[idx].read_requested = on_read_requested;
    target_cb[idx].write_received = on_write_received;
    target_cb[idx].read_processed = on_read_processed;
    target_cb[idx].stop = on_stop;
    target_cb[idx].error = on_error;
    target_cfg[idx].address = DEV_ADDR[idx];
    target_cfg[idx].callbacks = &target_cb[idx];
    target_rc[idx] = i2c_target_register(bus, &target_cfg[idx]);

    Monitor.print("REG addr=");
    print_hex8(DEV_ADDR[idx]);
    Monitor.print(" rc=");
    Monitor.println(target_rc[idx]);
}

static void reset_regs(void) {
    for (uint8_t idx = 0; idx < DEV_COUNT; idx++) {
        volatile regs_t *r = &regs[idx];

        memset((void *)r, 0, sizeof(*r));
        r->expect_pointer = true;
        stats[idx].cmd = NO_CMD;
        stats[idx].waiting_cmd = NO_CMD;
    }
    /* PCA9555 power-on: outputs high, polarity 0, all inputs. */
    for (uint8_t idx = DEV_BTN_A; idx <= DEV_BTN_B; idx++) {
        regs[idx].reg[0] = 0xFF;
        regs[idx].reg[1] = 0xFF;
        regs[idx].reg[2] = 0xFF;
        regs[idx].reg[3] = 0xFF;
        regs[idx].reg[6] = 0xFF;
        regs[idx].reg[7] = 0xFF;
    }
    /* PCA9532 power-on: PSC 0xFF, PWM 0x80, LS all off. */
    regs[DEV_LED].reg[2] = 0xFF;
    regs[DEV_LED].reg[3] = 0x80;
    regs[DEV_LED].reg[4] = 0xFF;
    regs[DEV_LED].reg[5] = 0x80;
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    Bridge.begin();
    Monitor.begin(SERIAL_BAUD);
    while (!Monitor) {
        delay(MONITOR_RETRY_MS);
    }
    Monitor.println("BOOT ack_timing");

    reset_regs();

    /* Pinctrl only; the mainboard stays the only master. Both buses
     * must be started, or i2c3's idle pins hold the shared bus. */
    Wire.begin();
    Wire2.begin();

    register_target(DEV_BTN_A, btn_bus);
    register_target(DEV_BTN_B, btn_bus);
    register_target(DEV_LED, led_bus);

    Monitor.print("CLOCK hz=");
    Monitor.println(sys_clock_hw_cycles_per_sec());
    print_regs();
    Monitor.println("READY");
}

void loop() {
    service_host();
}
