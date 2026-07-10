# Parse Offsets.h + defines.h GLOBAL_PATTERN into Cheat-Engine AOB strings, written to
# patterns_aob.json (a generated export -- Offsets.h is the source of truth). Re-run after
# editing Offsets.h so the JSON can't go stale.
from __future__ import print_function
import re, json, io, os
from collections import OrderedDict

HERE = os.path.dirname(os.path.abspath(__file__))
off = io.open(os.path.join(HERE, "common", "Offsets.h"), encoding="utf-8", errors="ignore").read()
dfn = io.open(os.path.join(HERE, "MetinPythonLib", "defines.h"), encoding="utf-8", errors="ignore").read()

BS = chr(92)  # backslash


def unescape_bytes(s):
    # s is the raw C string content with \xHH escapes
    out = []
    i = 0
    while i < len(s):
        if s[i] == BS and i + 1 < len(s) and s[i+1] == 'x':
            out.append(int(s[i+2:i+4], 16))
            i += 4
        elif s[i] == BS:
            i += 2  # skip other escapes (none expected)
        else:
            out.append(ord(s[i]))
            i += 1
    return out


def to_aob(byts, mask):
    return " ".join("%02X" % b if m == 'x' else "??" for b, m in zip(byts, mask))


results = OrderedDict()  # keep Offsets.h source order so the JSON diffs cleanly

# Match: Pattern(STR(NAME), OFFSET, "....", "mask")
pat = re.compile(r'Pattern\(STR\((\w+)\)\s*,\s*(\w+)\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*\)')
for m in pat.finditer(off):
    name, offset, bstr, mask = m.group(1), m.group(2), m.group(3), m.group(4)
    byts = unescape_bytes(bstr)
    if len(byts) != len(mask):
        print("LEN MISMATCH %s: bytes=%d mask=%d" % (name, len(byts), len(mask)))
    results[name] = OrderedDict([("offset", offset), ("len", len(byts)), ("aob", to_aob(byts, mask))])

# GLOBAL_PATTERN from defines.h
gm = re.search(r'#define GLOBAL_PATTERN "((?:\\.|[^"\\])*)"', dfn)
gk = re.search(r'#define GLOBAL_PATTERN_MASK "((?:\\.|[^"\\])*)"', dfn)
if gm and gk:
    byts = unescape_bytes(gm.group(1)); mask = gk.group(1)
    if len(byts) != len(mask):
        print("LEN MISMATCH GLOBAL_PATTERN: bytes=%d mask=%d" % (len(byts), len(mask)))
    results["GLOBAL_PATTERN"] = OrderedDict([("offset", "0"), ("len", len(byts)), ("aob", to_aob(byts, mask))])

data = json.dumps(results, indent=1, separators=(",", ": ")).decode("utf-8")
with io.open(os.path.join(HERE, "patterns_aob.json"), "w", encoding="utf-8") as f:
    f.write(data)
print("parsed %d patterns" % len(results))
for k in results:
    print("%-32s len=%-3d off=%s" % (k, results[k]["len"], results[k]["offset"]))
