import subprocess,sys,re,collections
BR=re.compile(r'^(b[a-z]{2,3}[sbwl]?|db[a-z]{1,2}|bsr[sbwl]?|bra[sbwl]?)$')
def cls(v):
    if 0xff0000<=v<=0xffffff: return 'FFxxxx'
    if 0xa00000<=v<=0xa0ffff: return 'Z80'
    if 0xa10000<=v<=0xa1ffff: return 'IO_A1'
    if 0xc00000<=v<=0xc0001f: return 'VDP'
    if 0x200000<=v<=0x20ffff: return 'SRAM'
    if 0x20000<=v<0x20400: return 'gate_API'
    if 0x20400<=v<0x20800: return 'gate_EXT'
    if 0x21000<=v<0x29000 or 0x218000<=v<0x219000: return 'kblob'
    if 0x400<=v<0x8000: return 'APPADDR'
    if v<0x400: return 'lowabs'
    return 'other'
tot=collections.Counter()
for e in sys.argv[1:]:
    out=subprocess.run(['m68k-linux-gnu-objdump','-d','--no-show-raw-insn','-j','.text',e],capture_output=True,text=True).stdout
    c=collections.Counter(); ex=collections.defaultdict(list)
    for line in out.splitlines():
        m=re.match(r'\s+([0-9a-f]+):\t(\S+)\s*(.*)',line)
        if not m: continue
        op,args=m.group(2),m.group(3)
        if op.startswith('.') or BR.match(op): continue
        # strip immediates, record FF-range immediates separately
        for n in re.findall(r'#(-?\d+)',args):
            v=int(n)&0xffffffff
            k=cls(v)
            if k in('FFxxxx','VDP','IO_A1','Z80','SRAM','kblob') and v>0xffff: c['imm_'+k]+=1; ex['imm_'+k].append(line.strip())
        a2=re.sub(r'#-?\d+','',args)
        a2=re.sub(r'<[^>]*>','',a2)
        for n in re.findall(r'(?<![@\w(])(?:0x)?([0-9a-f]+)\b(?!\))',a2):
            pass
        for tok in re.split(r',(?![^(]*\))',a2):
            tok=tok.strip()
            if not tok or '%' in tok: continue
            mm=re.fullmatch(r'(0x)?([0-9a-f]+)',tok)
            if mm:
                v=int(mm.group(2),16); k='abs_'+cls(v); c[k]+=1
                if len(ex[k])<5: ex[k].append(line.strip())
    tot+=c
    print(e.split('/')[-1],dict(c))
    for k,v in ex.items():
        if k.startswith('abs_gate'): continue
        for x in v[:4]: print('    ',k,x)
print('TOTAL',dict(tot))
