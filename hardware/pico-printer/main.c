/*
 * GEOS-Genesis printer bridge -- RP2040 (Raspberry Pi Pico) firmware.
 *
 * Receives the framed print stream from the Genesis EXT port (controller C)
 * over UART at 4800 8N1, deframes it, and drives an HP DeskJet 340 over a
 * Centronics/IEEE-1284 parallel link using HP PCL 3.
 *
 *   Genesis EXT TxD (DE-9 pin 6) --[level shift 5V->3.3V]--> Pico UART RX
 *   Pico UART TX --[optional]--> Genesis EXT RxD (pin 9)   (XON/XOFF backpressure)
 *   Pico GPIO D0..D7 + STROBE/BUSY/ACK --> DeskJet 340 Centronics port
 *
 * Wire protocol (must match src/genesis/ext_serial.S):
 *   [0xA5 sync][mode][len_hi][len_lo][payload...][checksum]
 *   checksum = 8-bit sum of (mode + len_hi + len_lo + payload bytes)
 *   mode 0 = text     : payload is ASCII, printed then form-fed.
 *   mode 1 = raster   : payload = [w_hi][w_lo][h_hi][h_lo][rows...],
 *                       1bpp MSB-first, ceil(w/8) bytes per row, bit 1 = black.
 *
 * The whole frame is buffered in RAM before printing, so UART receive and the
 * (slower, BUSY-gated) Centronics output are decoupled -- no flow control is
 * needed for a single page at 4800 baud. UART RX is IRQ-fed into a ring so
 * bytes arriving during a print are never dropped.
 */

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ config */

#define UART_ID        uart0
#define UART_BAUD      4800
#define UART_RX_PIN    17          /* <- Genesis EXT TxD (pin 6), level-shifted */
#define UART_TX_PIN    16          /* -> Genesis EXT RxD (pin 9), optional      */

/* Centronics data lines D0..D7 must be 8 consecutive GPIOs starting here. */
#define CEN_DATA_BASE  0           /* GP0..GP7 = D0..D7 */
#define CEN_STROBE_PIN 8           /* out, active low  */
#define CEN_BUSY_PIN   9           /* in,  high = busy */
#define CEN_ACK_PIN    10          /* in,  active low  (optional, informational) */
#define CEN_INIT_PIN   11          /* out, active low  (printer reset)           */

#define LED_PIN        PICO_DEFAULT_LED_PIN

#define RASTER_DPI     100         /* 320 px -> 3.2" wide at 100 dpi */
#define INVERT_RASTER  0           /* set 1 if the page prints inverted */

#define FRAME_SYNC     0xA5
#define MODE_TEXT      0
#define MODE_RASTER    1

#define MAX_PAYLOAD    40000u      /* > one 320x200 screen (8004 B); RP2040 has 264 KB */
#define BUSY_TIMEOUT_US 2000000    /* give up on a stuck printer after 2 s */

/* ------------------------------------------------------------- UART RX ring */

#define RING_SZ 4096               /* power of two */
static volatile uint8_t  ring[RING_SZ];
static volatile uint32_t ring_head, ring_tail;

static void on_uart_rx(void) {
    while (uart_is_readable(UART_ID)) {
        uint8_t c = uart_getc(UART_ID);
        uint32_t next = (ring_head + 1) & (RING_SZ - 1);
        if (next != ring_tail) {   /* drop on overflow rather than block the ISR */
            ring[ring_head] = c;
            ring_head = next;
        }
    }
}

static int ring_getc_blocking(void) {
    while (ring_tail == ring_head) tight_loop_contents();
    uint8_t c = ring[ring_tail];
    ring_tail = (ring_tail + 1) & (RING_SZ - 1);
    return c;
}

/* --------------------------------------------------------- Centronics side */

static void centronics_init(void) {
    for (int i = 0; i < 8; i++) {
        gpio_init(CEN_DATA_BASE + i);
        gpio_set_dir(CEN_DATA_BASE + i, GPIO_OUT);
        gpio_put(CEN_DATA_BASE + i, 0);
    }
    gpio_init(CEN_STROBE_PIN); gpio_set_dir(CEN_STROBE_PIN, GPIO_OUT); gpio_put(CEN_STROBE_PIN, 1);
    gpio_init(CEN_INIT_PIN);   gpio_set_dir(CEN_INIT_PIN,   GPIO_OUT); gpio_put(CEN_INIT_PIN,   1);
    gpio_init(CEN_BUSY_PIN);   gpio_set_dir(CEN_BUSY_PIN,   GPIO_IN);
    gpio_init(CEN_ACK_PIN);    gpio_set_dir(CEN_ACK_PIN,    GPIO_IN);
    gpio_pull_up(CEN_BUSY_PIN);
    gpio_pull_up(CEN_ACK_PIN);

    /* Pulse /INIT low to reset the printer. */
    gpio_put(CEN_INIT_PIN, 0); sleep_us(50);
    gpio_put(CEN_INIT_PIN, 1); sleep_ms(50);
}

/* Send one byte with the compatibility-mode handshake. Returns false on BUSY
 * timeout (printer offline / out of paper). */
static bool centronics_putc(uint8_t b) {
    absolute_time_t deadline = make_timeout_time_us(BUSY_TIMEOUT_US);
    while (gpio_get(CEN_BUSY_PIN)) {           /* wait until printer not busy */
        if (time_reached(deadline)) return false;
    }
    /* Drive the 8 data bits (contiguous, so one masked write). */
    gpio_put_masked(0xFFu << CEN_DATA_BASE, (uint32_t)b << CEN_DATA_BASE);
    sleep_us(1);                                /* data setup */
    gpio_put(CEN_STROBE_PIN, 0);                /* assert /STROBE */
    sleep_us(1);                                /* strobe width >= 0.5us */
    gpio_put(CEN_STROBE_PIN, 1);                /* deassert */
    /* The printer raises BUSY in response to the strobe; give it a moment to
     * assert before we poll, otherwise a fast printer's BUSY pulse can be
     * missed and we'd clock the next byte too early. Then wait for the /ACK
     * low pulse that confirms the byte was accepted. */
    sleep_us(2);
    deadline = make_timeout_time_us(BUSY_TIMEOUT_US);
    while (gpio_get(CEN_BUSY_PIN)) {            /* wait out the BUSY window */
        if (time_reached(deadline)) return false;
    }
    return true;
}

static bool centronics_write(const uint8_t *p, uint32_t n) {
    while (n--) if (!centronics_putc(*p++)) return false;
    return true;
}

static bool centronics_str(const char *s) {
    return centronics_write((const uint8_t *)s, (uint32_t)strlen(s));
}

/* --------------------------------------------------------------- PCL 3 out */

static void pcl_reset(void)     { centronics_str("\x1B" "E"); }       /* ESC E   */
static void pcl_formfeed(void)  { centronics_putc(0x0C); }            /* FF      */

static void print_text(const uint8_t *payload, uint32_t len) {
    pcl_reset();
    centronics_write(payload, len);
    pcl_formfeed();
    pcl_reset();
}

/* payload = [w_hi][w_lo][h_hi][h_lo][rows...] */
static void print_raster(const uint8_t *payload, uint32_t len) {
    if (len < 4) return;
    uint32_t w = ((uint32_t)payload[0] << 8) | payload[1];
    uint32_t h = ((uint32_t)payload[2] << 8) | payload[3];
    uint32_t rowbytes = (w + 7) / 8;
    if (rowbytes == 0 || (uint64_t)rowbytes * h + 4 > len) return;   /* malformed */

    char cmd[24];
    pcl_reset();
    snprintf(cmd, sizeof cmd, "\x1B" "*t%uR", (unsigned)RASTER_DPI); /* raster res */
    centronics_str(cmd);
    centronics_str("\x1B" "*r0A");                                   /* start, left margin */
    centronics_str("\x1B" "*b0M");                                   /* uncompressed */

    const uint8_t *row = payload + 4;
    for (uint32_t y = 0; y < h; y++, row += rowbytes) {
        snprintf(cmd, sizeof cmd, "\x1B" "*b%uW", (unsigned)rowbytes);
        centronics_str(cmd);
#if INVERT_RASTER
        for (uint32_t x = 0; x < rowbytes; x++) centronics_putc(~row[x]);
#else
        centronics_write(row, rowbytes);
#endif
    }
    centronics_str("\x1B" "*rC");                                    /* end raster */
    pcl_formfeed();
    pcl_reset();
}

/* ------------------------------------------------------------- deframer */

static uint8_t frame[MAX_PAYLOAD];

static void led(bool on) { gpio_put(LED_PIN, on); }
static void blink(int n) { for (int i = 0; i < n; i++) { led(1); sleep_ms(80); led(0); sleep_ms(120); } }

/* Read and process exactly one frame. */
static void handle_one_frame(void) {
    /* Resync to SYNC. */
    while (ring_getc_blocking() != FRAME_SYNC) { /* skip noise */ }

    uint8_t  mode   = ring_getc_blocking();
    uint8_t  len_hi = ring_getc_blocking();
    uint8_t  len_lo = ring_getc_blocking();
    uint32_t len    = ((uint32_t)len_hi << 8) | len_lo;
    uint8_t  sum    = (uint8_t)(mode + len_hi + len_lo);

    if (len > MAX_PAYLOAD) {                     /* absurd length: drain + reject */
        for (uint32_t i = 0; i < len; i++) sum += (uint8_t)ring_getc_blocking();
        (void)ring_getc_blocking();              /* checksum byte */
        blink(4);
        return;
    }
    for (uint32_t i = 0; i < len; i++) {
        uint8_t b = (uint8_t)ring_getc_blocking();
        frame[i] = b;
        sum += b;
    }
    uint8_t chk = (uint8_t)ring_getc_blocking();
    if (chk != sum) { blink(4); return; }        /* checksum fail: fast quadruple blink */

    led(1);
    switch (mode) {
        case MODE_TEXT:   print_text(frame, len);   break;
        case MODE_RASTER: print_raster(frame, len); break;
        default: break;                           /* unknown mode: ignore */
    }
    led(0);
}

/* ------------------------------------------------------------------- main */

int main(void) {
    gpio_init(LED_PIN); gpio_set_dir(LED_PIN, GPIO_OUT);

    uart_init(UART_ID, UART_BAUD);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_ID, true);
    uart_set_hw_flow(UART_ID, false, false);

    irq_set_exclusive_handler(UART0_IRQ, on_uart_rx);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(UART_ID, true, false);   /* RX IRQ only */

    centronics_init();
    blink(2);                                     /* alive */

    for (;;) handle_one_frame();
}
