#!/usr/bin/env python3
"""Manx Aztec C 68k assembler source -> GNU as (m68k, --register-prefix-optional).

Aztec's dialect, as Cyber Paint uses it:
  labels in column 0 with no colon          -> label:
  NAME equ VALUE, NAME set VALUE           -> .set NAME, VALUE
  NAME reg d2/d3                            -> expanded where NAME is used
  public a,b                                -> .globl a,b
  cseg / dseg                               -> .text / .data
  bss name,size / global name,size          -> .lcomm / .comm
  far data, END                             -> dropped
  ; comments anywhere, * comments in col 0  -> dropped
  $1F hex                                   -> 0x1F
"""
import re, sys

def strip_comment(line):
    out, q = [], None
    for ch in line:
        if q:
            out.append(ch)
            if ch == q:
                q = None
        elif ch in '"\'':
            q = ch; out.append(ch)
        elif ch == ';':
            break
        else:
            out.append(ch)
    return ''.join(out).rstrip()

src = open(sys.argv[1], encoding='latin-1').read().replace('\r', '').split('\n')
regs = {}
for line in src:
    m = re.match(r'^(\w+)\s+reg\s+(\S+)', strip_comment(line), re.I)
    if m:
        regs[m.group(1)] = m.group(2)

out = []
for line in src:
    if line[:1] == '*':
        out.append(''); continue
    line = strip_comment(line)
    s = line.strip()
    low = s.lower()
    if not s:
        out.append(''); continue
    if low == 'cseg':
        out.append('\t.text'); continue
    if low == 'dseg':
        out.append('\t.data'); continue
    if re.match(r'far\s', low) or low == 'end':
        out.append(''); continue
    m = re.match(r'(public|global|bss)\s+(.*)$', s, re.I)
    if m:
        kw, args = m.group(1).lower(), m.group(2)
        if kw == 'public':
            out.append('\t.globl ' + args.replace(' ', '')); continue
        name, size = [a.strip() for a in args.split(',')]
        out.append(('\t.comm %s,%s' if kw == 'global' else '\t.lcomm %s,%s') % (name, size))
        continue
    if re.match(r'^\w+\s+reg\s', s, re.I) and not line[:1].isspace():
        out.append(''); continue
    m = re.match(r'^(\w+)\s+(equ|set)\s+(.*)$', s, re.I)
    if m and not line[:1].isspace():
        v = re.sub(r'\$([0-9A-Fa-f]+)', r'0x\1', m.group(3))
        out.append('\t.set %s, %s' % (m.group(1), v)); continue
    label = ''
    if not line[:1].isspace():
        m = re.match(r'^(\w+):?\s*(.*)$', line)
        label, line = m.group(1) + ':', '\t' + m.group(2)
    # hex, and register lists named with reg
    line = re.sub(r'\$([0-9A-Fa-f]+)', r'0x\1', line)
    for n, v in regs.items():
        line = re.sub(r'\b%s\b' % n, v, line)
    out.append(label + line)
print('\n'.join(out))
