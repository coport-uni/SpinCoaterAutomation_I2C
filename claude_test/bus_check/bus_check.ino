/**
 * @file bus_check.ino
 * @brief Read the I2C line levels without ever touching Wire.
 *
 * Written because every sketch that calls Wire.begin() on the UNO R4
 * Minima failed to boot: the board stayed in DFU and never enumerated
 * as a CDC port, while a sketch with no Wire at all booted normally.
 * That points at the bus rather than at the sketches, so this one reads
 * SDA and SCL as plain GPIO and reports what it sees.
 *
 * Each line is sampled twice. Floating, externally pulled up and held
 * low are three different faults and they need different fixes:
 *
 *   hiz=1 pup=1   an external pull-up is present, the line is healthy
 *   hiz=0 pup=1   no external pull-up, the line floats
 *   hiz=0 pup=0   something is holding the line down
 *
 * A healthy idle I2C bus reads hiz=1 pup=1 on both lines. Anything else
 * explains why every transfer is answered with a NACK.
 *
 * Output, one line each:
 *   SDA hiz=<0|1> pup=<0|1>
 *   SCL hiz=<0|1> pup=<0|1>
 *   INT hiz=<0|1> pup=<0|1>
 */

/** Serial line speed shared by every sketch in this project. */
#define SERIAL_BAUD 115200

/** Time to wait for a USB CDC host to attach before giving up, ms. */
#define SERIAL_WAIT_MS 3000

/** Settle time after changing a pin mode before sampling, ms. */
#define SETTLE_MS 5

/** Delay between report passes, ms. */
#define REPORT_PERIOD_MS 1000

/** Interrupt line from the keypad board, active low. */
#define PIN_KEYPAD_INT 2

/**
 * @brief Sample one pin high impedance, then with the internal pull-up.
 * @param name Label printed in front of the reading.
 * @param pin Pin to sample.
 */
static void report_pin(const char *name, uint8_t pin) {
    int hiz = 0;
    int pup = 0;

    pinMode(pin, INPUT);
    delay(SETTLE_MS);
    hiz = digitalRead(pin);

    pinMode(pin, INPUT_PULLUP);
    delay(SETTLE_MS);
    pup = digitalRead(pin);

    /* Left high impedance so the pin does not load the bus. */
    pinMode(pin, INPUT);

    Serial.print(name);
    Serial.print(" hiz=");
    Serial.print(hiz);
    Serial.print(" pup=");
    Serial.println(pup);
}

void setup() {
    unsigned long start_ms = millis();

    Serial.begin(SERIAL_BAUD);
    while (!Serial && (millis() - start_ms) < SERIAL_WAIT_MS) {
    }

    Serial.println("BOOT bus_check");
}

void loop() {
    report_pin("SDA", SDA);
    report_pin("SCL", SCL);
    report_pin("INT", PIN_KEYPAD_INT);
    Serial.println("---");

    delay(REPORT_PERIOD_MS);
}
