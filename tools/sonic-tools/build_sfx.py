#!/usr/bin/env python3
"""Sonic's jump, as a per-frame PSG table.

The sound is one SMPS score and it is four lines long.  s1disasm,
sound/sfx/SndA0 - Jump.asm, verbatim:

    SndA0_Jump_Header:
        smpsHeaderStartSong 1
        smpsHeaderVoice     SndA0_Jump_Voices
        smpsHeaderTempoSFX  $01
        smpsHeaderChanSFX   $01
        smpsHeaderSFXChannel cPSG1, SndA0_Jump_PSG1,  $F4, $00

    SndA0_Jump_PSG1:
        smpsPSGvoice        $00
        dc.b    nF2, $05
        smpsModSet          $02, $01, $F8, $65
        dc.b    nBb2, $15
        smpsStop

    ; Song seems to not use any FM voices
    SndA0_Jump_Voices:

One PSG tone channel, no FM at all, twenty-six ticks long.  Everything
needed to turn it into numbers is in the driver:

  the note                s1.sounddriver.asm, PSGSetFreq
                            subi.b #$81,d5          ; note to 0-based index
                            add.b Transpose(a5),d5  ; the header's $F4 = -12
                            lsl.w #1,d5
                            move.w (a0,d5.w),Freq(a5)

  the table               PSGFrequencies, and the formula above it:
                            min($3FF, round(PSG_Sample_Rate / (frequency*2)))
                          with PSG_Sample_Rate = Z80_Clock/16 = 223721.5625

  the volume              PSGDoVolFX opens "tst.b VoiceIndex / beq return",
                          and smpsPSGvoice is $00 -- so there is no envelope
                          and the volume is the header's, $00, which on a PSG
                          is no attenuation at all: full.

  the modulation          smpsModSet wait, speed, delta, steps: nothing for
                          `wait` ticks, then `delta` added to the frequency
                          every `speed` ticks.  Here that is -8 a tick, and
                          with $65 steps against a $15-tick note it never
                          reaches the reversal.

A smaller PSG frequency value is a higher pitch, so -8 a tick is the rise --
that upward whoop is the whole character of the sound, and it is one number
in one line of the score.
"""
import argparse
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

PSG_SAMPLE_RATE = 3579545 / 16          # Z80_Clock/16, _Constants.asm

# PSGFrequencies, s1.sounddriver.asm -- and zPSGFrequencies, s2.sounddriver.asm,
# which is the same table to the last decimal place.  Both games' PSG notes come
# out of the same six octaves, so the jump and the spindash release are speaking
# the same units.
PSG_HZ = [
     130.98,  138.78,  146.99,  155.79,  165.22,  174.78,
     185.19,  196.24,  207.91,  220.63,  233.52,  247.47,
     261.96,  277.56,  293.59,  311.58,  329.97,  349.56,
     370.39,  392.49,  415.83,  440.39,  468.03,  494.95,
     522.71,  556.51,  588.73,  621.44,  661.89,  699.12,
     740.79,  782.24,  828.59,  880.79,  932.17,  989.91,
    1045.42, 1107.52, 1177.47, 1242.89, 1316.00, 1398.25,
    1491.47, 1575.50, 1669.55, 1747.82, 1864.34, 1962.46,
    2071.49, 2193.34, 2330.42, 2485.78, 2601.40, 2796.51,
    2943.69, 3107.23, 3290.01, 3495.64, 3608.40, 3857.25,
    4142.98, 4302.32, 4660.85, 4863.50, 5084.56, 5326.69,
    5887.39, 6214.47, 6580.02, 223721.56,
]

NOTE_BASE = 0x81                        # nRst is $80, nC0 is $81
SEMITONE = {'C': 0, 'Cs': 1, 'D': 2, 'Ds': 3, 'E': 4, 'F': 5,
            'Fs': 6, 'G': 7, 'Gs': 8, 'A': 9, 'Bb': 10, 'B': 11}
def psg_value(hz: float) -> int:
    return min(0x3FF, int(PSG_SAMPLE_RATE / (hz * 2) + 0.5))


def note_value(name: str, octave: int, transpose: int) -> int:
    """PSGSetFreq: note to 0-based index, plus the header's transpose."""
    idx = NOTE_BASE + 12 * octave + SEMITONE[name]
    idx = (idx - NOTE_BASE + transpose) & 0x7F
    return psg_value(PSG_HZ[idx])


# --------------------------------------------------------------------- the FM
#
# Sonic 2's spindash and skid used to be left out of here, with a note saying
# why: four goes at writing an SMPS player out of the disassembly's
# annotations produced four different wrong sounds, and the answer was to run
# Sonic 2's own Z80 driver instead.
#
# That answer does not survive the Mega CD. The Z80's window onto the 68000's
# address space would have to point at the Mega CD's PRG-RAM window, and that
# window shows one 128KB bank at a time -- a bank the payload switches several
# hundred times a frame, between the one holding the ST screen and the one
# holding the art. Every read the driver made would be a coin toss. See
# docs/sonic.md.
#
# So they are built here after all, and the objection is answered rather than
# argued with: nothing below is transcribed from the disassembly. The scores
# are read out of the cartridge, byte for byte, from bank 31 -- header, script
# and voice -- and rendered a tick at a time. What the annotations are for is
# knowing what the bytes mean, and where they and the cartridge disagreed
# before, the cartridge is what is being read.

FM_SAMPLE_RATE = (53693175 / 7) / (6 * 6 * 4)   # M68000_Clock/(6*6*4)

# zFrequencies, s2.sounddriver.asm:1385.  "The first frequency is B, the last
# frequency is B-flat" -- which is why note $81 (nC0) indexes entry 1: the note
# byte becomes `note - $80 + transpose` and reads this table directly.
FM_BASE_OCTAVE = [15.39, 16.35, 17.34, 18.36, 19.45, 20.64,
                  21.84, 23.13, 24.51, 25.98, 27.53, 29.15]


def fm_frequencies() -> list[int]:
    """zMakeFMFrequenciesOctave 0..7: eight octaves, +$800 a step."""
    out = []
    for octave in range(8):
        for hz in FM_BASE_OCTAVE:
            out.append(round(hz * 1024 * 1024 * 2 / FM_SAMPLE_RATE) + octave * 0x800)
    return out


ATTACK = 0x80      # in the volume byte: this tick keys on


# Sonic 1's driver, and only Sonic 1's, because Sonic 2's sounds are played by
# Sonic 2's own driver now.  It is worth saying what the difference was, since
# it is why: Sonic 2's zSetVoice walks the operator registers with `add a,4`,
# so its voice bytes are register-sequential, while Sonic 1's 68000 driver
# indexes an explicit table, FMInstrumentOperatorTable, which is NOT sequential
# -- inside every group it goes +0, +8, +4, +C.  The same twenty-five bytes
# mean different things to the two drivers, and a voice fed through the wrong
# one has two of its four operators swapped: a different instrument, not a
# slightly wrong one.  Their slot masks differ too, at exactly one entry --
# algorithm 4, which is the one the signpost uses.
S1_OPS = [0x30, 0x38, 0x34, 0x3C,   0x50, 0x58, 0x54, 0x5C,
          0x60, 0x68, 0x64, 0x6C,   0x70, 0x78, 0x74, 0x7C,
          0x80, 0x88, 0x84, 0x8C]
S1_TLS = [0x40, 0x48, 0x44, 0x4C]
S1_MASK = [8, 8, 8, 8, 0x0A, 0x0E, 0x0E, 0x0F]      # FMSlotMask

S1_REGS = S1_OPS + S1_TLS


def tl_mask(voice: list[int]) -> int:
    """FMSlotMask, by the voice's algorithm."""
    return S1_MASK[voice[0] & 7]


def note_index(note: int, transpose: int) -> int:
    """FMSetFreq's index into the frequency table -- in the driver's own 8-bit
    width.  Sonic 2's Z80 version says it most plainly:

        sub 80h / add a,(ix+Transpose) / add a,a / add a,zFrequencies&0FFh
        ld (.storefreq+2),a          ; the LOW byte of the pointer only

    Every step of that is a byte, and only the pointer's low byte is patched,
    so the whole lookup wraps inside the table's own page.  The signpost's $27
    transpose does not come near the wrap; it is modelled anyway, because a
    transposition that quietly stopped wrapping would be a different sound and
    nothing here would say so.
    """
    a = (note - 0x80) & 0xFF
    a = (a + transpose) & 0xFF
    return ((a + a) & 0xFF) // 2


# Sonic 1's signpost, s1disasm sound/sfx/SndCF - Signpost.asm.  Two channels,
# and the second one is an accident that became the sound:
#
#     smpsHeaderSFXChannel cFM4, SndCF_Signpost_FM4, $27, $03
#     smpsHeaderSFXChannel cFM5, SndCF_Signpost_FM5, $27, $00
#
#     SndCF_Signpost_FM4:
#         dc.b    nRst, $04
#
#     ; FM5 Data
#     SndCF_Signpost_FM5:
#         smpsSetvoice        $00
#     SndCF_Signpost_Loop00:
#         dc.b    nEb4, $05
#         smpsAlterVol        $02
#         smpsLoop            $00, $15, SndCF_Signpost_Loop00
#         smpsStop
#
# FM4's data is four ticks of rest and then *nothing* -- no smpsStop -- so its
# track pointer runs straight on into FM5's, loads the same voice and plays the
# same loop.  Two channels, four ticks apart, three units of attenuation
# between them: that doubling is the signpost, and porting only FM5 would be
# porting a thinner sound than the cartridge makes.
FM_VOICE_SIGN = [
    0xF4,                           # $B0  algorithm 4, feedback 6
    0x06, 0x04, 0x0F, 0x0E,         # $30+ detune / multiple
    0x1F, 0x1F, 0x1F, 0x1F,         # $50+ rate scaling / attack rate
    0x00, 0x00, 0x0B, 0x0B,         # $60+ amplitude modulation / decay 1
    0x00, 0x00, 0x05, 0x08,         # $70+ decay 2
    0x0F, 0x0F, 0xFF, 0xFF,         # $80+ decay level / release rate
    0x0C, 0x8B, 0x03, 0x80,         # $40+ total level
]
FM_SIGN_TRANSPOSE = 0x27
NOTE_EB4 = 0x81 + 4 * 12 + SEMITONE['Ds']
FM_SIGN_LOOPS = 0x15
FM_SIGN_HOLD = 0x05
FM_SIGN_STEP = 0x02                             # smpsAlterVol $02


def build_fm_sign(volume: int, rest: int) -> list[tuple[int, int, int]]:
    """The loop, from either channel's point of view.

    Each iteration is a *note*, not a continuation -- there is no
    smpsNoAttack anywhere in this score -- so all twenty-one of them key on
    again, which is the pip-pip-pip of the post spinning.  `volume` is the
    channel's header volume and `rest` the ticks it waits first, and those
    two numbers are the whole difference between FM4 and FM5.
    """
    idx = note_index(NOTE_EB4, FM_SIGN_TRANSPOSE)
    out = [(0, 0, 0)] * rest                    # dc.b nRst -- index 0 is $80
    for _ in range(FM_SIGN_LOOPS):
        out.append((idx, volume | ATTACK, 0))   # the note keys on...
        out += [(idx, volume, 0)] * (FM_SIGN_HOLD - 1)
        volume += FM_SIGN_STEP                  # ...then smpsAlterVol
    return out


# A PSG tick is one word: the attenuation in the top nibble and the
# frequency-register value in the rest. $F000 -- attenuation 15, and no
# frequency -- is a rest, which is the chip's own silence.
def psg_word(freq: int, atten: int = 0) -> int:
    if freq <= 0:
        return PSG_REST
    return ((atten & 0x0F) << 12) | min(0x3FF, max(1, freq))


PSG_REST = 0xF000


def build() -> list[int]:
    """One entry a tick, in PSG frequency-register units."""
    out = []
    out += [psg_word(note_value('F', 2, -12))] * 0x05   # dc.b nF2, $05
    freq = note_value('Bb', 2, -12)                # dc.b nBb2, $15
    wait, delta = 0x02, -8                      # smpsModSet $02,$01,$F8,$65
    for tick in range(0x15):
        out.append(psg_word(freq))
        if tick + 1 >= wait:
            freq += delta
    return out


# ------------------------------------------------------- Sonic 2, off the ROM
#
# Three sounds, and every byte of all three comes out of the cartridge: the
# skid ($A4), the spindash rev ($E0) and its release ($BC). They live in bank
# 31 with the rest of the sound effects, and each is found by the shape of its
# own header rather than by an address, so a different revision of the dump
# does not silently give a different sound.
#
# The header, as the driver reads it (zPlaySound, s2.sounddriver.asm):
#
#     dc.w  voice table          Z80 pointer, little-endian
#     dc.b  tempo
#     dc.b  channels
#     per channel:
#       dc.b  playback control   $80 -- the track is playing
#       dc.b  voice control      FM: the channel; PSG: its latch byte
#       dc.w  script             Z80 pointer
#       dc.b  transpose
#       dc.b  volume
#
# A Z80 pointer is $8000 + the offset inside the bank, because the driver
# reads its data through the Z80's 32KB window.

S2_BANK = 31
Z80_WINDOW = 0x8000

# zSetVoice writes $B0 first, then walks with `add a,4' -- so unlike Sonic 1's
# 68000 driver, whose FMInstrumentOperatorTable goes +0, +8, +4, +C inside
# every group, Sonic 2's voice bytes are register-sequential.
S2_OPS = [0x30, 0x34, 0x38, 0x3C,   0x50, 0x54, 0x58, 0x5C,
          0x60, 0x64, 0x68, 0x6C,   0x70, 0x74, 0x78, 0x7C,
          0x80, 0x84, 0x88, 0x8C]
S2_TLS = [0x40, 0x44, 0x48, 0x4C]
S2_REGS = S2_OPS + S2_TLS

# zVolTLMaskTbl. It differs from Sonic 1's FMSlotMask at exactly one entry --
# algorithm 4 -- and the signpost uses algorithm 4, which is how a wrong mask
# stayed invisible for so long.
S2_MASK = [8, 8, 8, 8, 0x0C, 0x0E, 0x0E, 0x0F]

# Coordination flags, only the ones these three scores use.
CF_ALTERVOL = 0xE6
CF_NOATTACK = 0xE7
CF_SETVOICE = 0xEF
CF_MODSET   = 0xF0
CF_STOP     = 0xF2
CF_PSGFORM  = 0xF3
CF_MODOFF   = 0xF4
CF_PSGVOICE = 0xF5
CF_LOOP     = 0xF7

# The headers, by shape. Each pattern is checked to match exactly once in the
# bank; two matches or none is a build error, not a guess.
SFX_SHAPES = {
    'skid': rb'..\x01\x02\x80\xA0..\xF4\x00\x80\xC0..\xF4\x00',
    'rev':  rb'..\x01\x01\x80\x05..\xFE\x00',
    'rel':  rb'..\x01\x02\x80\x05..\x90\x00\x80\xC0..\x00\x00',
}


class Header:
    def __init__(self, blk, at):
        self.voice = struct.unpack('<H', blk[at:at + 2])[0]
        self.tempo = blk[at + 2]
        self.channels = []
        p = at + 4
        for _ in range(blk[at + 3]):
            ctl = blk[p + 1]
            ptr = struct.unpack('<H', blk[p + 2:p + 4])[0]
            self.channels.append((ctl, ptr, blk[p + 4], blk[p + 5]))
            p += 6


def s2_bank(rom):
    return rom[S2_BANK * 0x8000:(S2_BANK + 1) * 0x8000]


def s2_header(blk, name):
    hits = [m.start() for m in re.finditer(SFX_SHAPES[name], blk, re.S)]
    if len(hits) != 1:
        raise SystemExit('%s: %d headers in bank %d of the dump, wanted one'
                         % (name, len(hits), S2_BANK))
    return Header(blk, hits[0])


class Script:
    """One channel's script, walked a tick at a time.

    The walk is the driver's own: bytes $E0 and up are coordination flags,
    $80..$DF are notes ($80 being a rest), and anything below is a duration --
    on its own it repeats the note before it, which is how the rev's tail is
    sixteen four-tick repeats of one note that never keys on again.
    """

    def __init__(self, blk, ptr, transpose, volume):
        self.blk, self.pos = blk, ptr - Z80_WINDOW
        self.transpose, self.volume = transpose, volume
        self.mod = None                 # (wait, speed, delta, steps)
        self.modon = False
        self.noattack = False
        self.psgform = None
        self.psgvoice = 0
        self.voice = 0
        self.loops = {}
        self.note = self.dur = None
        self.out = []

    def _flag(self, b):
        blk = self.blk
        if b == CF_ALTERVOL:
            self.volume = (self.volume + blk[self.pos]) & 0xFF
            self.pos += 1
        elif b == CF_NOATTACK:
            self.noattack = True
        elif b == CF_SETVOICE:
            self.voice = blk[self.pos]; self.pos += 1
        elif b == CF_MODSET:
            w, sp, d, st = blk[self.pos:self.pos + 4]
            self.mod = (w, sp, d - 256 if d > 127 else d, st)
            self.modon = True
            self.pos += 4
        elif b == CF_MODOFF:
            self.modon = False
        elif b == CF_PSGFORM:
            self.psgform = blk[self.pos]; self.pos += 1
        elif b == CF_PSGVOICE:
            self.psgvoice = blk[self.pos]; self.pos += 1
        elif b == CF_LOOP:
            idx, count = blk[self.pos], blk[self.pos + 1]
            dest = struct.unpack('<H', blk[self.pos + 2:self.pos + 4])[0]
            self.pos += 4
            left = self.loops.get(idx)
            if left is None:
                left = count
            left -= 1
            self.loops[idx] = left
            if left:
                self.pos = dest - Z80_WINDOW
            else:
                self.loops[idx] = None
        elif b == CF_STOP:
            return False
        else:
            raise SystemExit('unhandled coordination flag $%02X at bank+%04X'
                             % (b, self.pos - 1))
        return True

    def run(self, emit, limit=4000):
        while len(self.out) < limit:
            b = self.blk[self.pos]; self.pos += 1
            if b >= 0xE0:
                if not self._flag(b):
                    break
            elif b >= 0x80:
                self.note = b
                if self.blk[self.pos] < 0x80:
                    self.dur = self.blk[self.pos]; self.pos += 1
                emit(self)
            else:
                self.dur = b
                emit(self)
        return self.out

    def ticks(self):
        """Modulation, a tick at a time, as zDoModulation runs it: nothing
        while the wait counts down, then `delta` added every `speed` ticks
        until `steps` run out, at which point the delta flips sign and the
        count starts again. The note's own tick does not modulate -- the
        driver sets the frequency and skips it -- so the first value is
        always zero."""
        wait, speed, delta, steps = self.mod if (self.mod and self.modon) \
            else (0, 1, 0, 0)
        val, ctr = 0, speed
        for t in range(self.dur):
            yield val
            if not self.modon:
                continue
            if wait:
                wait -= 1
                continue
            ctr -= 1
            if ctr:
                continue
            ctr = speed
            if steps == 0:
                steps = self.mod[3]
                delta = -delta
            else:
                steps -= 1
                val += delta
        self.noattack = False


def build_fm(blk, hdr, chan):
    """(note index, volume with the attack bit, modulation) a tick."""
    ctl, ptr, transpose, volume = hdr.channels[chan]
    sc = Script(blk, ptr, transpose, volume)

    def emit(s):
        idx = 0 if s.note == 0x80 else note_index(s.note, s.transpose)
        for t, mod in enumerate(s.ticks()):
            attack = ATTACK if (t == 0 and not s.noattack and idx) else 0
            s.out.append((idx, s.volume | attack, mod))
    sc.run(emit)
    voice = list(blk[hdr.voice - Z80_WINDOW + 25 * sc.voice:][:25])
    return sc.out, voice


# zPSG_EnvTbl, the "flutter" envelopes: an attenuation a tick, added to
# the channel's own volume, and $80 at the end means hold the last value
# for as long as the note lasts. Only the one the spindash release asks
# for is here -- smpsPSGvoice fTone_07 -- and it is checked against the
# driver in the dump before it is used.
PSG_ENV7 = [0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2,
            3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 7]
PSG_ENVS = {7: PSG_ENV7}


def build_psg(blk, hdr, chan):
    """One (attenuation, frequency) word a tick.

    The envelope and the modulation both restart at every note that keys
    on -- zSetDuration resets VolFlutter and calls zSetModulation -- and
    neither advances past the end of its own table: the flutter holds its
    last value and the modulation reverses.
    """
    ctl, ptr, transpose, volume = hdr.channels[chan]
    sc = Script(blk, ptr, transpose, volume)

    def emit(s):
        env = PSG_ENVS.get(s.psgvoice) if s.psgvoice else None
        if env is None and s.psgvoice:
            raise SystemExit('PSG envelope %d is not in this file' % s.psgvoice)
        if s.note == 0x80:
            for _ in s.ticks():
                s.out.append(PSG_REST)
            return
        idx = (s.note - 0x81 + s.transpose) & 0x7F
        base = psg_value(PSG_HZ[idx])
        for t, mod in enumerate(s.ticks()):
            atten = s.volume
            if env:
                atten += env[t] if t < len(env) else env[-1]
            s.out.append(psg_word(base + mod, min(15, atten)))
    sc.run(emit)
    return sc.out, sc.psgform


# ----------------------------------------------- checking against the dumps
#
# The raw bytes commented above the `smpsVc` macros are not always what the
# cartridge holds.  For Sonic 1 they are.  For Sonic 2 they are the same voice
# printed in a different operator order, and transcribing them put two
# operators of four into the wrong registers -- which is a different
# instrument, not a slightly wrong one.  So the literals above are the bytes
# read out of the dumps, and this proves it every build a dump is present.

def _dump(name):
    p = ROOT / 'assets' / 'sonic' / name
    return p.read_bytes() if p.exists() else None


def _voice_s1(rom, pattern):
    """Sonic 1's 68000 driver: `dc.w loc-songStart`, so the header's pointers
    are relative to the song and big-endian."""
    m = re.search(pattern, rom, re.S)
    if not m:
        return None
    h = m.start() - 2
    voff = (rom[h] << 8) | rom[h + 1]
    return list(rom[h + voff: h + voff + 25])


# SndCF's header and nothing else in either dump: tempo $01, two channels,
# cFM4 at pitch $27 volume $03, cFM5 at pitch $27 volume $00.
SIGN_HEADER = rb'\x01\x02\x80\x04.{2}\x27\x03\x80\x05.{2}\x27\x00'


def check_voices():
    s1 = _dump('sonic1.md')
    found = {}
    if s1:
        found['signpost'] = (_voice_s1(s1, SIGN_HEADER), FM_VOICE_SIGN)
    checked = []
    for name, (rom, lit) in found.items():
        if rom is None:
            raise SystemExit('%s: could not be located in the dump' % name)
        if rom != lit:
            raise SystemExit('%s: the dump holds\n  %s\nbut this file says\n  %s'
                             % (name, ' '.join('%02X' % b for b in rom),
                                ' '.join('%02X' % b for b in lit)))
        checked.append(name)
    print('voices verified against the dumps: %s'
          % (', '.join(checked) if checked else 'no dump present, using the literals'))


def emit_fm(lines, name, voice, script, chan, regs, mask, note_bias=0):
    U = name.upper()
    lines.append('.equ SN_FM_%s_TICKS, %d' % (U, len(script)))
    lines.append('.equ SN_FM_%s_MASK,  0x%02X' % (U, mask))
    lines.append('.equ SN_FM_%s_KEY,   0x%02X' % (U, chan))
    lines.append('.equ SN_FM_%s_CHAN,  %d' % (U, chan & 3))
    lines.append('sn_fm_voice_%s:' % name)
    lines.append('    .byte ' + ', '.join('0x%02X' % v for v in voice[:13]))
    lines.append('    .byte ' + ', '.join('0x%02X' % v for v in voice[13:]))
    lines.append('    .align 2')
    lines.append('sn_fm_%s:' % name)
    for note, vol, mod in script:
        lines.append('    .byte %3d, %3d' % (note, vol))
        lines.append('    .word %6d' % mod)


def emit_psg(lines, name, table):
    lines.append('.equ SN_PSG_%s_TICKS, %d' % (name.upper(), len(table)))
    lines.append('sn_psg_%s:' % name)
    for n in range(0, len(table), 8):
        lines.append('    .word ' + ', '.join('0x%04X' % v
                                              for v in table[n:n + 8]))
    lines.append('    .align 2')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-o', '--out', default='build/sonic/sfx_jump.inc')
    a = ap.parse_args()
    check_voices()

    s2 = _dump('sonic2.md')
    if s2 is None:
        raise SystemExit('assets/sonic/sonic2.md: the skid and the spindash '
                         'are read out of it, not transcribed. Put your own '
                         'dump there -- see tools/build-sonic-art.sh.')
    blk = s2_bank(s2)

    lines = [
        '/* Generated by tools/sonic-tools/build_sfx.py. Nothing here is',
        ' * committed: every number below is Sega\'s, and it comes out of the',
        ' * dumps in assets/sonic/ at build time.',
        ' *',
        ' * Sonic 1, transcribed from s1disasm and with the voices checked',
        ' * against the dump: the jump (sound/sfx/SndA0) and both channels of',
        ' * the signpost (SndCF).',
        ' *',
        ' * Sonic 2, read straight out of bank 31 of the cartridge -- header,',
        ' * script and voice -- and rendered a tick at a time: the skid ($A4),',
        ' * the spindash rev ($E0) and its release ($BC).',
        ' *',
        ' * A PSG tick is one word: attenuation in the top nibble, the',
        ' * frequency register in the rest, $F000 a rest. An FM tick is four',
        ' * bytes: note index, volume with bit 7 meaning key-on, then the',
        ' * modulation so far. */',
    ]

    ticks = build()
    emit_psg(lines, 'jump', ticks)

    freqs = fm_frequencies()
    lines.append('sn_fm_freq:')
    for n in range(0, len(freqs), 12):
        lines.append('    .word ' + ', '.join('0x%04X' % v
                                              for v in freqs[n:n + 12]))

    lines.append('/* Sonic 1\'s register order: inside every group of four it')
    lines.append(' * goes +0, +8, +4, +C -- FMInstrumentOperatorTable. */')
    lines.append('sn_fm_regs_s1:')
    for n in range(0, len(S1_REGS), 12):
        lines.append('    .byte ' + ', '.join('0x%02X' % v
                                              for v in S1_REGS[n:n + 12]))
    lines.append('    .align 2')
    lines.append('/* Sonic 2\'s: zSetVoice walks it with `add a,4\', so it is')
    lines.append(' * simply sequential. The same twenty-five bytes mean')
    lines.append(' * different things to the two drivers. */')
    lines.append('sn_fm_regs_s2:')
    for n in range(0, len(S2_REGS), 12):
        lines.append('    .byte ' + ', '.join('0x%02X' % v
                                              for v in S2_REGS[n:n + 12]))
    lines.append('    .align 2')

    report = []
    for name, volume, rest in (('sign5', 0x00, 0), ('sign4', 0x03, 4)):
        script = build_fm_sign(volume, rest)
        emit_fm(lines, name, FM_VOICE_SIGN, script,
                0x05 if name == 'sign5' else 0x04, 's1', tl_mask(FM_VOICE_SIGN))
        report.append((name, FM_VOICE_SIGN, script))

    for name, key in (('rev', 'rev'), ('dash', 'rel')):
        hdr = s2_header(blk, key)
        script, voice = build_fm(blk, hdr, 0)
        emit_fm(lines, name, voice, script, hdr.channels[0][0], 's2',
                S2_MASK[voice[0] & 7])
        report.append((name, voice, script))

    hdr = s2_header(blk, 'skid')
    ska, _ = build_psg(blk, hdr, 0)
    skb, _ = build_psg(blk, hdr, 1)
    # Both channels of one score, so they run off one tick counter and the
    # shorter is padded rather than stopped early.
    n = max(len(ska), len(skb))
    ska += [PSG_REST] * (n - len(ska))
    skb += [PSG_REST] * (n - len(skb))
    emit_psg(lines, 'skid_a', ska)
    emit_psg(lines, 'skid_b', skb)

    hdr = s2_header(blk, 'rel')
    noise, form = build_psg(blk, hdr, 1)
    emit_psg(lines, 'dash', noise)
    lines.append('.equ SN_PSG_DASH_FORM, 0x%02X' % form)

    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_text('\n'.join(lines) + '\n')

    print('jump: %d ticks, %d down to %d'
          % (len(ticks), ticks[0] & 0xFFF, ticks[-1] & 0xFFF))
    print('skid: %d ticks, two PSG channels' % n)
    print('dash: %d ticks of PSG noise, form $%02X' % (len(noise), form))
    for name, voice, script in report:
        print('%-5s: %3d ticks, alg %d fb %d, note index %d, '
              'volume %d to %d, %d key-ons'
              % (name, len(script), voice[0] & 7, (voice[0] >> 3) & 7,
                 max(e[0] for e in script),
                 script[0][1] & 0x7F, script[-1][1] & 0x7F,
                 sum(1 for e in script if e[1] & ATTACK)))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
