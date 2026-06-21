import re, sys
root = r"C:\Users\kevin\Desktop\MetinPythonLib-WalkerPath"
off = open(root + r"\common\Offsets.h", encoding="utf-8", errors="ignore").read()

def unescape_bytes(s):
    out=[]; i=0
    while i < len(s):
        if s[i]=='\\' and i+1<len(s) and s[i+1]=='x':
            out.append(int(s[i+2:i+4],16)); i+=4
        elif s[i]=='\\': i+=2
        else: out.append(ord(s[i])); i+=1
    return out

def to_aob(byts,mask):
    return " ".join("%02X"%b if m=='x' else "??" for b,m in zip(byts,mask))

pat=re.compile(r'Pattern\(STR\((\w+)\)\s*,\s*(\w+)\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*\)')
want={
 "INSTANCEBASE_MOVETODEST":"55 8B EC 83 EC ?? A1 ?? ?? ?? ?? 33 C5 89 45 FC 89 4D EC 8D 45 ?? 50 8B 4D EC E8 ?? ?? ?? ?? 8B 4D 08 51 8D 55 ?? 52 8B 4D EC E8 ?? ?? ?? ?? D9 5D",
 "PYTHONAPP_PROCESS":"55 8B EC 6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 83 EC ?? A1 ?? ?? ?? ?? 33 C5 89 45 ?? 50 8D 45 ?? 64 A3 00 00 00 00 89 4D ?? E8 ?? ?? ?? ?? 89 45 ?? 8D 45 ?? 50 8B 4D ?? 83 C1 20",
 "PEEK_FUNCTION":"55 8B EC 56 8B F1 E8 ?? ?? ?? ?? 8B 4D 08 3B C1 7D 07 32 C0 5E 5D C2 08 00 51 FF 76 38 8D 4E 1C E8",
}
got={}
for m in pat.finditer(off):
    name,offset,bstr,mask=m.groups()
    byts=unescape_bytes(bstr)
    if len(byts)!=len(mask):
        print("LEN MISMATCH %s bytes=%d mask=%d"%(name,len(byts),len(mask)))
    got[name]=to_aob(byts,mask)

ok=True
for k,v in want.items():
    a=got.get(k,"<MISSING>")
    match = (a==v)
    ok = ok and match
    print(("OK  " if match else "FAIL")+" "+k)
    if not match:
        print("  want:",v)
        print("  got :",a)
print("ALL_MATCH" if ok else "MISMATCH")
