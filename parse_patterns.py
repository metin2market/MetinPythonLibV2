import re, json, sys

# Parse Offsets.h memPatterns + defines.h GLOBAL_PATTERN into CE AOB strings.
root = r"C:\Users\kevin\Desktop\MetinPythonLibV2"
off = open(root + r"\common\Offsets.h", encoding="utf-8", errors="ignore").read()
dfn = open(root + r"\MetinPythonLib\defines.h", encoding="utf-8", errors="ignore").read()

def unescape_bytes(s):
    # s is the raw C string content with \xHH escapes
    out = []
    i = 0
    while i < len(s):
        if s[i] == '\\' and i+1 < len(s) and s[i+1] == 'x':
            out.append(int(s[i+2:i+4], 16))
            i += 4
        elif s[i] == '\\':
            i += 2  # skip other escapes (none expected)
        else:
            out.append(ord(s[i]))
            i += 1
    return out

def to_aob(byts, mask):
    parts = []
    for b, m in zip(byts, mask):
        parts.append("%02X" % b if m == 'x' else "??")
    return " ".join(parts)

results = {}

# Match: Pattern(STR(NAME), OFFSET, "....", "mask")
pat = re.compile(r'Pattern\(STR\((\w+)\)\s*,\s*(\w+)\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*\)')
for m in pat.finditer(off):
    name, offset, bstr, mask = m.group(1), m.group(2), m.group(3), m.group(4)
    byts = unescape_bytes(bstr)
    if len(byts) != len(mask):
        print("LEN MISMATCH %s: bytes=%d mask=%d" % (name, len(byts), len(mask)), file=sys.stderr)
    results[name] = {"offset": offset, "len": len(byts), "aob": to_aob(byts, mask)}

# GLOBAL_PATTERN from defines.h
gm = re.search(r'#define GLOBAL_PATTERN "((?:\\.|[^"\\])*)"', dfn)
gk = re.search(r'#define GLOBAL_PATTERN_MASK "((?:\\.|[^"\\])*)"', dfn)
if gm and gk:
    byts = unescape_bytes(gm.group(1)); mask = gk.group(1)
    if len(byts) != len(mask):
        print("LEN MISMATCH GLOBAL_PATTERN: bytes=%d mask=%d" % (len(byts), len(mask)), file=sys.stderr)
    results["GLOBAL_PATTERN"] = {"offset": "0", "len": len(byts), "aob": to_aob(byts, mask)}

open(root + r"\patterns_aob.json", "w").write(json.dumps(results, indent=1))
print("parsed %d patterns" % len(results))
for k in results:
    print("%-32s len=%-3d off=%s" % (k, results[k]["len"], results[k]["offset"]))
