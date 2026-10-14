import struct, sys, capstone
path=sys.argv[1]; start=int(sys.argv[2],16); count=int(sys.argv[3])
d=open(path,"rb").read()
shoff=struct.unpack_from("<I",d,32)[0]; shnum=struct.unpack_from("<H",d,48)[0]; shstr=struct.unpack_from("<H",d,50)[0]
secs=[struct.unpack_from("<10I",d,shoff+i*40) for i in range(shnum)]
names=d[secs[shstr][4]:secs[shstr][4]+secs[shstr][5]]
nm=lambda i: names[secs[i][0]:names.index(b"\0",secs[i][0])].decode()
S={nm(i):s for i,s in enumerate(secs)}
text=bytearray(d[S[".text"][4]:S[".text"][4]+S[".text"][5]])
ro=d[S[".rodata"][4]:S[".rodata"][4]+S[".rodata"][5]]; roaddr=S[".rodata"][3]
# apply relocations on text (base 0): REL entries (offset,info)
rel=d[S[".rel.text"][4]:S[".rel.text"][4]+S[".rel.text"][5]]
hi={}
# import stubs
stubs={}
i=0
while i+20<=len(text):
    if struct.unpack_from("<I",text,i)[0]==0x41e00000:
        lib=bytes(text[i+12:i+20]).split(b"\0")[0].decode('latin1'); j=i+20
        while j+8<=len(text):
            a,b=struct.unpack_from("<II",text,j)
            if a==0x03e00008 and (b>>16)==0x2400: stubs[j]=f"{lib}:{b&0xffff}"; j+=8
            else: break
        i=j
    else: i+=4
def rostr(a):
    if roaddr<=a<roaddr+len(ro):
        s=ro[a-roaddr:].split(b"\0")[0]
        try: return s.decode()
        except: return None
md=capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32+capstone.CS_MODE_LITTLE_ENDIAN)
regs={}
for ins in md.disasm(bytes(text[start:start+count*4]), start):
    note=""
    w=struct.unpack_from("<I",text,ins.address)[0]
    op=w>>26
    if op==3: # jal
        t=(w&0x3ffffff)<<2
        note=stubs.get(t, f"sub_{t:x}")
    if op==0xf: regs[(w>>16)&31]=(w&0xffff)<<16
    if op==9: # addiu
        rs=(w>>21)&31; imm=w&0xffff; imm=imm-0x10000 if imm&0x8000 else imm
        if rs in regs:
            a=(regs[rs]+imm)&0xffffffff; s=rostr(a)
            if s: note=repr(s)
    print(f"{ins.address:05x}: {ins.mnemonic:8s} {ins.op_str:28s} {note}")
