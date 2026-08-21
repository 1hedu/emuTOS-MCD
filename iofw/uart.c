/* uart.c - a real keyboard on the Mega Drive's serial port.
 *
 * The 315-5309 I/O chip can put any of its three ports into serial
 * mode, and on a Model 1 the EXT connector is where that is brought
 * out. So a terminal keyboard -- anything that speaks 8N1 at one of the
 * four rates the chip offers -- can be typed on directly, today, with
 * no adapter to build.
 *
 * On a Model 1 every port has the engine, not just EXT, so UART_PORT
 * below picks between them and the three register sets are derived from
 * it rather than written out three times. A Model 2 has none of them: it
 * is not that the EXT connector is missing, it is that the serial engine
 * is, so pointing this file at port 1 or 2 buys nothing there. Those
 * machines need a bridge that presents itself as an ordinary pad and is
 * read like one, which is a different file to this one. Nothing below
 * is a head start on it.
 *
 * Register map from two independent sources that agree:
 * joncampbell123/sega-genesis reference/gen.h and
 * rhargreaves/mega-drive-serial-port src/serial.h --
 *
 *   port      data     ctrl     TxData   RxData   S-Ctrl
 *   1 (left)  A10003   A10009   A1000F   A10011   A10013
 *   2 (right) A10005   A1000B   A10015   A10017   A10019
 *   EXT       A10007   A1000D   A1001B   A1001D   A1001F
 *
 * S-Ctrl, from the same two: bits 7-6 select the rate (00 4800,
 * 01 2400, 10 1200, 11 300), bit 5 SIN enables the receiver, bit 4
 * SOUT enables the transmitter, and the low three are read-only status
 * -- bit 2 RERR a framing or overrun error, bit 1 RRDY a byte waiting,
 * bit 0 TFUL the transmit register still busy. Bit 3 is the receive
 * interrupt enable, which this file does not use: the servant has run
 * with the interrupt mask at 7 since it was written, and polling once
 * a frame is enough for a person typing.
 *
 * Enough, but not by a lot, and that is why there is a queue. At 4800
 * baud a byte takes about 2 ms and a frame is 16, so eight can arrive
 * between two looks at the port -- and the sub CPU is handed one
 * scancode per frame through a single comm register, because that is
 * the protocol the on-screen keyboard already uses and this is not the
 * change to widen it. Held keys and pasted text would otherwise be
 * dropped on the floor rather than merely arriving slowly.
 *
 * This file is distributed under the GPL, version 2 or at your option
 * any later version.  See doc/license.txt for details.
 */

#include "hw.h"
#include "uart_keymap.h"

/* 1 left, 2 right, 3 EXT. The three sets are two bytes apart in each of
 * the four register groups, which is what the table above says and what
 * makes deriving them safe: one number to change, and no way to leave a
 * build reading one port's status and another's data. */
#ifndef UART_PORT
#define UART_PORT 3
#endif

#define UART_CTRL   (0xA10009u + 2u * (UART_PORT - 1))
#define UART_TXDATA (0xA1000Fu + 6u * (UART_PORT - 1))
#define UART_RXDATA (0xA10011u + 6u * (UART_PORT - 1))
#define UART_SCTRL  (0xA10013u + 6u * (UART_PORT - 1))

#define SCTRL_4800  0x00u
#define SCTRL_SIN   0x20u
#define SCTRL_SOUT  0x10u
#define SCTRL_RERR  0x04u
#define SCTRL_RRDY  0x02u
#define SCTRL_TFUL  0x01u

/* The transmit tap, in the servant's telemetry block. See uart_send. */
#define UART_TAP    0xFF0E80u

/* Scancodes waiting to go to the sub, one per frame. A capital letter
 * is three of them -- shift, the key, shift again -- so this holds a
 * little over ten characters, which is a second of fast typing. */
#define QLEN 64
static uint8_t q[QLEN];
static uint8_t qhead, qtail;

static uint8_t uart_on;
static uint16_t uart_rx;        /* bytes taken off the port */
static uint16_t uart_err;       /* ...and how many arrived broken */
static uint16_t uart_drop;      /* scancodes the queue had no room for */

static void push(uint8_t sc)
{
    uint8_t n = (uint8_t)((qhead + 1) % QLEN);
    if (n == qtail) { uart_drop++; return; }
    q[qhead] = sc;
    qhead = n;
}

void uart_enable(uint8_t on)
{
    uart_on = on ? 1 : 0;
    if (uart_on) {
        /* S-Ctrl before Ctrl, and Ctrl 0x7F, both as
         * rhargreaves/mega-drive-serial-port's serial_init does it --
         * that order and that value are what a working example uses,
         * and this is not a place to improvise. 0x7F is every pin an
         * output with the port interrupt off; SIN and SOUT override the
         * direction for the two pins the serial engine drives. */
        VU8(UART_SCTRL) = SCTRL_4800 | SCTRL_SIN | SCTRL_SOUT;
        VU8(UART_CTRL) = 0x7F;
        (void)VU8(UART_RXDATA);         /* drop anything already latched */
        qhead = qtail = 0;
    } else {
        VU8(UART_SCTRL) = 0x00;         /* back to parallel */
        VU8(UART_CTRL) = 0x00;          /* ...and every pin an input again */
    }
}

uint8_t uart_active(void) { return uart_on; }

/* Drain the port into the queue. Called once a frame. */
void uart_poll(void)
{
    uint8_t guard;

    if (!uart_on)
        return;

    /* Bounded: a port stuck with RRDY asserted must not take the frame
     * with it. Ten is more than the eight a frame can actually carry at
     * 4800 baud. */
    for (guard = 0; guard < 10; guard++) {
        uint8_t st = VU8(UART_SCTRL);
        uint8_t ch, sc;

        if (st & SCTRL_RERR) {
            (void)VU8(UART_RXDATA);     /* reading it clears the error */
            uart_err++;
            continue;
        }
        if (!(st & SCTRL_RRDY))
            break;

        ch = VU8(UART_RXDATA);
        uart_rx++;
        if (ch >= 128)
            continue;
        sc = uart_keymap[ch];
        if (!sc)
            continue;
        /* The sub treats 0x2A as a latch it toggles, so a shifted key
         * is shift, the key, shift again -- which leaves the latch
         * where it found it whatever else is going on. */
        if (sc & 0x80) {
            push(0x2A);
            push((uint8_t)(sc & 0x7F));
            push(0x2A);
        } else {
            push(sc);
        }
    }
}

/* One scancode, or 0 if the queue is empty. The caller owns the comm
 * register and the sequence number; this only decides what goes in it. */
uint8_t uart_next(void)
{
    uint8_t sc;
    if (qtail == qhead)
        return 0;
    sc = q[qtail];
    qtail = (uint8_t)((qtail + 1) % QLEN);
    return sc;
}

/* ---- transmit --------------------------------------------------------
 *
 * The same engine, the other way. uart_enable() already sets SOUT
 * beside SIN -- the port has been full duplex since it was written, and
 * nothing had anything to say.
 *
 * Non-blocking, and it has to be: 4800 baud is a byte every two
 * milliseconds and a frame is sixteen, so a loop that waits for the
 * transmitter would spend the whole frame doing it and the screen would
 * stop. The caller offers a byte, this takes it or refuses, and the
 * caller comes back. Fed from inside wait_vblank(), the time between
 * finishing a frame's work and the next blank is enough to keep the
 * wire saturated without taking anything from the pump.
 *
 * TFUL (bit 0) is set while the transmit register still holds a byte.
 * Clear means the shift register has taken the last one and there is
 * room. */
static uint16_t uart_tx;        /* bytes handed to the port */

uint8_t uart_send(uint8_t b)
{
    if (!uart_on)
        return 0;
    if (VU8(UART_SCTRL) & SCTRL_TFUL)
        return 0;
    VU8(UART_TXDATA) = b;
    /* The transmitted stream, mirrored where a test can read it.
     *
     * Genesis Plus GX and PicoDrive both model these registers as plain
     * storage: the write lands, TFUL never sets, and the byte goes
     * nowhere because there is no peer to receive it. So an emulator can
     * prove what was emitted and nothing else, and this window is how it
     * proves it. Sixty-four bytes, wrapping, with a count -- enough to
     * check a frame header and a checksum, which is what the framing
     * needs proving about. */
    VU8(UART_TAP + 2u + (uart_tx & 63u)) = b;
    VU16(UART_TAP) = ++uart_tx;
    return 1;
}

void uart_stats(uint16_t *rx, uint16_t *err, uint16_t *drop)
{
    *rx = uart_rx;
    *err = uart_err;
    *drop = uart_drop;
}
