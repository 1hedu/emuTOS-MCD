/* Input for the IOFW: Sega Mouse + pad-as-mouse, delivered to EmuTOS
 * through the comm registers as relative deltas + buttons.
 *
 * The mouse TH/TR nibble handshake is ported from SGDK's joy.c
 * (Stephane Dallongeville, MIT license) — see SGDK/src/joy.c. Both the
 * US Mega Mouse and the JP/EU Sega Mouse speak it; they differ in
 * button complement and Y polarity (MOUSE_Y_INVERT below, validated on
 * real hardware at the E3 gate).
 *
 * Comm protocol (written every vblank, before INT2 is raised):
 *   CMD1 = raw pad state (debug + future key mapping)
 *   CMD2 = (dx << 8) | (dy & 0xFF)   signed bytes, ST orientation
 *   CMD3 = mouse buttons: bit1 = left, bit0 = right
 */
#include "hw.h"

#define MOUSE_Y_INVERT 0    /* set 1 for the JP/EU Sega Mouse */

static uint16_t retry;
static uint8_t phase;

static uint8_t th_handshake(volatile uint8_t *pb, uint8_t ph)
{
    uint8_t val = 0, hs;

    *pb = ph;
    hs = (uint8_t)((ph >> 1) & 0x10);
    while (retry) {
        val = *pb;
        if ((val & 0x10) == hs)
            break;
        retry--;
    }
    return (uint8_t)(val & 0x0F);
}

static int16_t start3lhs(uint16_t port, uint8_t *hdr, uint16_t len)
{
    volatile uint8_t *pb;
    uint16_t i;

    retry = 255;
    phase = 0x20;

    VU8(IO_CTRL1 + port * 2) = 0x60;    /* TH+TR outputs */
    pb = (volatile uint8_t *)(IO_DATA1 + port * 2);
    *pb = 0x60;                          /* deselect: phase 0 */
    __asm__ volatile("nop\n\tnop");

    i = (uint16_t)(*pb & 0x0F);
    if (i != 0 && i != 3)
        return -1;                       /* nothing mouse-like here */

    hdr[0] = th_handshake(pb, 0x60);
    if (retry) {
        for (i = 1; i < len; i++) {
            hdr[i] = th_handshake(pb, phase);
            phase ^= 0x20;
            if (!retry)
                break;
        }
    }
    if (!retry)
        *pb = 0x60;
    return retry ? 0 : -1;
}

/* Read a Sega Mouse on the given port (0/1).
 * Returns 1 with deltas+buttons, 0 if absent/timeout. */
static uint16_t mouse_read(uint16_t port, int16_t *dx, int16_t *dy,
                           uint16_t *buttons)
{
    volatile uint8_t *pb = (volatile uint8_t *)(IO_DATA1 + port * 2);
    uint8_t hdr[4], md[6];
    uint16_t i, mx, my;

    if (start3lhs(port, hdr, 4) != 0 || hdr[0] != 0x00 || hdr[1] != 0x0B) {
        *pb = 0x60;
        return 0;
    }

    for (i = 0; i < 6; i++) {
        md[i] = th_handshake(pb, phase);
        phase ^= 0x20;
        if (!retry)
            break;
    }
    *pb = 0x60;                          /* end request */
    if (i != 6)
        return 0;

    mx = (md[0] & 0x04) ? 256u : (uint16_t)((md[2] << 4) | md[3]);
    if (md[0] & 0x01) mx |= mx ? 0xFF00 : 0xFFFF;
    my = (md[0] & 0x08) ? 256u : (uint16_t)((md[4] << 4) | md[5]);
    if (md[0] & 0x02) my |= my ? 0xFF00 : 0xFFFF;

    *dx = (int16_t)mx;
#if MOUSE_Y_INVERT
    *dy = (int16_t)my;                   /* JP/EU polarity */
#else
    *dy = (int16_t)-(int16_t)my;         /* Sega Y+ is up; ST dy+ is down */
#endif
    /* Sega L/R/M/Start in md[1] bits 0-3 -> ST left(bit1)/right(bit0) */
    *buttons = (uint16_t)(((md[1] & 1) ? 2 : 0) | ((md[1] & 2) ? 1 : 0));
    return 1;
}

/* Not static: the CDD trace screen reads the pad directly rather than
 * out of a comm register. */
uint16_t pad_read(void)
{
    uint8_t hi, lo;
    VU8(IO_CTRL1) = 0x40;
    VU8(IO_DATA1) = 0x40;
    __asm__ volatile("nop\n\tnop");
    hi = VU8(IO_DATA1);
    VU8(IO_DATA1) = 0x00;
    __asm__ volatile("nop\n\tnop");
    lo = VU8(IO_DATA1);
    VU8(IO_DATA1) = 0x40;
    return (uint16_t)((uint8_t)(~hi & 0x3F) | (uint8_t)((~lo & 0x30) << 2));
}

static int8_t clamp8(int16_t v)
{
    if (v > 127) return 127;
    if (v < -128) return -128;
    return (int8_t)v;
}

uint8_t osk_active(void);   /* osk.c */

/* set once a real Sega Mouse answers the handshake; shown on the
 * hardware status line, since a mouse may simply not be plugged in */
uint8_t mouse_seen;

/* Sample pad + mouse, synthesize deltas, publish to the comm registers.
 * Called once per vblank, before INT2 is raised. Returns the raw pad so
 * the caller can drive the OSK. While the OSK is up the d-pad feeds keys,
 * not the pointer — but a real Sega Mouse still moves it. */
uint16_t input_update(void)
{
    static uint16_t held;
    uint16_t pad = pad_read();
    int16_t dx = 0, dy = 0;
    uint16_t buttons = 0;
    int16_t mdx, mdy;
    uint16_t mbut;
    uint16_t speed;

    /* pad-as-mouse: d-pad with acceleration, A = left, B = right.
     * Suppressed while the OSK owns the d-pad. */
    if (!osk_active()) {
        if (pad & 0x0F) held++; else held = 0;
        speed = (uint16_t)(1 + (held >> 3));
        if (speed > 4) speed = 4;
        if (pad & 0x04) dx -= (int16_t)speed;    /* L */
        if (pad & 0x08) dx += (int16_t)speed;    /* R */
        if (pad & 0x01) dy -= (int16_t)speed;    /* U */
        if (pad & 0x02) dy += (int16_t)speed;    /* D */
        if (pad & 0x40) buttons |= 2;            /* A -> left button */
        if (pad & 0x10) buttons |= 1;            /* B -> right button */
    } else {
        held = 0;
    }

    /* real Sega Mouse in port 2, if present */
    if (mouse_read(1, &mdx, &mdy, &mbut)) {
        mouse_seen = 1;
        dx = (int16_t)(dx + mdx);
        dy = (int16_t)(dy + mdy);
        buttons |= mbut;
    }

    /* The pad rides in the top byte of GA_CMD3, beside the two bits of
     * mouse button, and GA_CMD1 is left alone.
     *
     * It used to be published into GA_CMD1, which is also where the
     * buttons-held-at-power-on word lives -- one register carrying two
     * things, with the winner decided by boot timing. In Mode 1 the
     * cartridge writes the flags and EmuTOS reads them before this loop
     * ever runs, so the flags won and nobody noticed. On a CD boot
     * EmuTOS starts *after* the servant is already pumping, so the pad
     * word overwrote the flags before they were read, and
     * segacd_cdd_init() sat in its 200000-iteration wait for a stamp
     * that had been there and gone. That is a boot that never reaches a
     * desktop -- every CD boot from this branch, for its whole life.
     *
     * The CD branch fixed this in its own tree and the Mode 1 line
     * never carried the fix across, which is precisely the kind of
     * thing two trees cost and one tree does not.
     *
     * pad_read() returns eight bits and the sub takes the buttons as
     * `& 3`, so the two fit in one word with a byte to spare. */
    VU16(GA_CMD2) = (uint16_t)(((uint8_t)clamp8(dx) << 8)
                               | (uint8_t)clamp8(dy));
    VU16(GA_CMD3) = (uint16_t)(buttons | (pad << 8));
    return pad;
}
