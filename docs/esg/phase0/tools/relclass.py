import subprocess,sys,re,collections
def classify(o):
    out=subprocess.run(['m68k-linux-gnu-objdump','-dr',o],capture_output=True,text=True).stdout
    c=collections.Counter(); ex=collections.defaultdict(list)
    last=''
    for line in out.splitlines():
        m=re.match(r'\s+([0-9a-f]+): (R_68K_\w+)\s+(\S+)',line)
        if m:
            typ=m.group(2); sym=m.group(3)
            if 'PC' in typ: k='pcrel'
            elif typ=='R_68K_32':
                k='abs32'
            elif typ=='R_68K_16':
                ins=last.split('\t')[-1] if '\t' in last else last
                if '%a5@' in ins: k='a5disp16'
                elif '#' in ins: k='imm16'
                elif re.search(r'\.w\b|\(0x?[0-9a-f]+\)',ins) or 'Address' in ins: k='abs16/data?'
                else: k='other16'
            else: k=typ
            c[k]+=1
            if k in('abs32','other16','abs16/data?') and len(ex[k])<6: ex[k].append((m.group(1),sym,last.strip()[:90]))
            continue
        if re.match(r'\s+[0-9a-f]+:\t',line): last=line
    return c,ex
tot=collections.Counter()
for o in sys.argv[1:]:
    c,ex=classify(o)
    tot+=c
    print(o.split('/')[-1],dict(c))
    for k,v in ex.items():
        for e in v: print('   ',k,e)
print('TOTAL',dict(tot))
