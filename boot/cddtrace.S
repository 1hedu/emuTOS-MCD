/* cddtrace.S -- record what the CDBIOS actually says to the drive.
 *
 * Every stage-3 and stage-4 failure has come from inferring, off a
 * disassembly, what Sega's code would do at runtime -- and then being
 * wrong in a way only the console could show. This stops that at the
 * source.
 *
 * The console reads this disc perfectly several times every boot,
 * before the BIOS is evicted: the SP pulls the volume descriptor, the
 * root directory and every boot file off it, on the user's own drive,
 * and has never once failed. That is a working read, on the exact
 * hardware, and nobody has ever looked at what it consists of.
 *
 * So watch it. The BIOS keeps its outgoing command packet at $5864 and
 * the last status packet the drive sent at $586E; the state machine
 * latches the drive's state nibble at $5878 and its own mode at $5820.
 * Sampling those four while the SP waits for a sector costs nothing and
 * disturbs nothing -- there is no hook into the BIOS at all, no vector
 * taken, no flag cleared. It only reads.
 *
 * The sampling is from ReadCD's own wait loop rather than from the
 * drive's level-4 interrupt on purpose. Chaining that vector means
 * calling a handler that ends in rte from inside another handler, which
 * works only if you fake an exception frame for it, and being wrong
 * about that costs a disc. The wait loop is our code, runs for
 * essentially the whole of every read, and cannot upset anything.
 *
 * Sampling at CPU speed against a link that moves at 75 Hz would record
 * the same packet a hundred thousand times, so an entry is only opened
 * when the conversation actually changes: the command packet, the
 * drive's state and report-type nibbles, and the BIOS's mode byte. The
 * position nibbles are deliberately excluded from that comparison --
 * they advance every exchange, so including them would make every
 * sample a change and fill the log with one frame of a single read.
 * Instead each entry keeps the position it opened with, the position
 * last seen, and a count of how many times that position moved, which
 * is the number of exchanges the entry covers.
 *
 * Command 2 is excluded from it too, past its first nibble. Between
 * reads the BIOS rotates through "where are you", "where in the track"
 * and "which track" one per exchange, forever; treating each of those
 * as news filled the whole log with half a second of idling. Collapsed,
 * that rotation becomes a single entry whose position and count say how
 * far the drive drifted while nobody was asking it for anything --
 * which is the interesting part of it anyway.
 *
 * Every file on this disc lives within a few seconds of the others, so
 * a trace of an ordinary boot shows the drive nudging forward and never
 * shows what Sega does about a real seek -- and a real seek is exactly
 * the case our own driver fails at. Hence the probe below, three reads
 * that go somewhere. It is op 6, asked for by the main CPU only when
 * Start is held, and not part of booting: an ordinary boot must put
 * exactly as much wear on the drive as it did before this file existed,
 * and must have exactly as many places to hang.
 *
 * The log lives inside the SP's own image, before SectorBuffer, because
 * everything past that label is overwritten with disc sectors as files
 * load -- which is exactly the mistake the first CDD test made. The
 * main CPU finds it by its tag rather than by an address, so nothing
 * has to agree about the SP's layout.
 */

        .equ    CMDBLK,     0x5864      | ten nibbles going out
        .equ    STATBLK,    0x586E      | ten nibbles coming back
        .equ    DRVSTATE,   0x5878      | status nibble 0, as the BIOS keeps it
        .equ    DRVMODE,    0x5820      | 1/2/3, set by the state handlers

        .equ    TRACE_ENTS, 60
        .equ    TRACE_SIZE, 32

        .equ    PROBE_FAR,  1600        | the D: image: a real seek away
        .equ    PROBE_NEAR, 16          | the volume descriptor: all the
                                        | way back to the start
        .equ    PROBE_DEST, 0x20000     | scratch: past the directory
                                        | buffer, below the ramdisk

/* Entry layout, 32 bytes:
 *    0..9   command packet, as it stood when the entry opened
 *   10..19  status packet, likewise
 *   20..25  status nibbles 2..7 as last seen: where the drive got to
 *   26      $5820, the BIOS's mode
 *   27      $5878, the latched drive state
 *   28..29  exchanges: how often the position moved
 *   30..31  reserved
 */

        .globl  CddTraceLog
        .globl  CddTraceProbe

/* Op 6: three reads that go somewhere, so that whatever else the trace
 * catches it has at least caught a seek. Out to the far end of the
 * disc, back to the very start, then out again -- the shape of every
 * read our own D: driver does and cannot get right.
 *
 * Not into SectorBuffer: that still holds the root directory, and op 5
 * has to find two files in it afterwards. PRG 0x20000 is past the
 * directory buffer and well below the ramdisk at 0x60000. */
CddTraceProbe:
        movem.l %d0-%d1/%a0,-(%sp)
        /* Start the log here. Loading the boot files fills sixty entries
         * on its own, and keeping the first sixty is right -- but then
         * the probe would have nowhere to go, and the probe is the whole
         * reason for the probe. */
        clr.w   trace_count
        clr.l   trace_last
        move.l  #TraceBuf,trace_ptr
        move.l  #PROBE_FAR,%d0
        moveq   #1,%d1
        move.l  #PROBE_DEST,%a0
        bsr     ReadCD
        move.l  #PROBE_NEAR,%d0
        moveq   #1,%d1
        move.l  #PROBE_DEST,%a0
        bsr     ReadCD
        move.l  #PROBE_FAR+1,%d0
        moveq   #1,%d1
        move.l  #PROBE_DEST,%a0
        bsr     ReadCD
        movem.l (%sp)+,%d0-%d1/%a0
        rts

CddTraceLog:
        movem.l %d0-%d2/%a0-%a2,-(%sp)
        move.l  trace_last,%d0
        beq     tl_new                  | nothing open yet
        movea.l %d0,%a2

        move.b  CMDBLK,%d0              | same command at all?
        cmp.b   (%a2),%d0
        bne     tl_new
        move.b  STATBLK,%d1             | same drive state?
        cmp.b   10(%a2),%d1
        bne     tl_new
        move.b  DRVMODE,%d1
        cmp.b   26(%a2),%d1
        bne     tl_new
        cmpi.b  #2,%d0
        beq     tl_same                 | a report request: the rotation of
                                        | sub-command and reply is not news

        lea     CMDBLK+1,%a0            | same command in every nibble?
        lea     1(%a2),%a1
        moveq   #8,%d0
1:      cmpm.b  (%a0)+,(%a1)+
        bne     tl_new
        dbf     %d0,1b
        move.b  STATBLK+1,%d0           | same kind of report?
        cmp.b   11(%a2),%d0
        bne     tl_new

tl_same:
        /* Still the same conversation. Follow the position, and count a
         * move as one exchange -- the drive sends exactly one packet per
         * 1/75 s, so a count here is a duration in frames. */
        lea     STATBLK+2,%a0
        lea     20(%a2),%a1
        moveq   #0,%d1
        moveq   #5,%d0
2:      move.b  (%a0)+,%d2
        cmp.b   (%a1),%d2
        beq     3f
        moveq   #1,%d1
        move.b  %d2,(%a1)
3:      addq.l  #1,%a1
        dbf     %d0,2b
        tst.b   %d1
        beq     tl_done
        addq.w  #1,28(%a2)
        bra     tl_done

        /* Keep the first N, not the last: the beginning of a read is
         * where the seek and the state changes are, and a ring buffer
         * would have thrown all of that away by the time anyone looked. */
tl_new:
        cmpi.w  #TRACE_ENTS,trace_count
        bcc     tl_done
        movea.l trace_ptr,%a2
        movea.l %a2,%a1

        lea     CMDBLK,%a0
        moveq   #9,%d0
4:      move.b  (%a0)+,(%a1)+
        dbf     %d0,4b
        lea     STATBLK,%a0
        moveq   #9,%d0
5:      move.b  (%a0)+,(%a1)+
        dbf     %d0,5b
        lea     STATBLK+2,%a0           | +20: position, as it stands now
        moveq   #5,%d0
6:      move.b  (%a0)+,(%a1)+
        dbf     %d0,6b
        move.b  DRVMODE,(%a1)+          | +26
        move.b  DRVSTATE,(%a1)+         | +27
        move.w  #1,(%a1)+               | +28: this exchange
        clr.w   (%a1)+                  | +30

        move.l  %a1,trace_ptr
        move.l  %a2,trace_last
        addq.w  #1,trace_count

tl_done:
        movem.l (%sp)+,%d0-%d2/%a0-%a2
        rts

/* Kept in .text, like cddtest.S and for the same reason: this is a flat
 * binary loaded as a BIOS module, and a .data section would be placed
 * wherever the linker liked, outside the bytes the BIOS actually loads.
 * PRG-RAM is RAM, so initialised data here simply arrives correct and
 * needs no init call. */
        .even
trace_last:     .long   0               | entry currently open, 0 = none
trace_ptr:      .long   TraceBuf

/* Tagged, and found by its tag. A dump that finds nothing can then say
 * so, instead of showing forty-eight rows of plausible zeroes. */
        .even
TraceTag:
        .ascii  "CDTRACE1"
trace_count:    .word   0
trace_rsvd:     .word   0
TraceBuf:
        .space  TRACE_ENTS * TRACE_SIZE, 0
        .even
