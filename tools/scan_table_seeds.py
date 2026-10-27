"""scan_table_seeds.py - find function pointers in data sections that were not lifted.

    py -3 -I tools/scan_table_seeds.py <default.xbe> <gen dir> <seeds out.json>

Functions only reached through a table (C++ vtables, XPP class-driver tables,
callback arrays) are invisible to the disassembler until an indirect call hits
them at run time -- the ICALL feedback finds them one crash or hang at a time.
This scans every non-code section for dwords that point into a code section at
a likely function start and are not a lifted function yet:

  - 16-byte aligned (MSVC aligns functions to 16),
  - the byte before is padding or a return (0x90, 0xCC, 0xC3), or the dword
    before is a jump-table entry of the function in front (a code address at
    most 16 KB below: MSVC puts switch tables right after the function, so a
    method behind one has no padding in front -- 0x00144FB0, 0x00115300),
  - the byte itself is not padding or zero.

Mid-function addresses would split a real function, so the test is strict on
purpose; a miss is caught later by the ICALL feedback. Writes a seed file in the
toolkit's --seed-functions format (tools/lift.ps1 picks up seeds/*_seeds.json).
Section table: parsed from the XBE header.
"""
import glob
import json
import re
import struct
import sys

CODE = {'D3D', 'D3DX', 'DSOUND', 'WMADECXM', '.text', 'WMADEC', 'XGRPH', 'XNET', 'XPP'}


def sections(d):
    base = struct.unpack_from('<I', d, 0x104)[0]
    count = struct.unpack_from('<I', d, 0x11C)[0]
    hdr = struct.unpack_from('<I', d, 0x120)[0] - base
    out = []
    for i in range(count):
        flags, va, vsize, raw, rsize, name_va = struct.unpack_from('<6I', d, hdr + i * 56)
        n = name_va - base
        name = d[n:d.index(b'\0', n)].decode('ascii', 'replace')
        out.append((name, va, vsize, raw, rsize))
    return out


def main():
    xbe, gen, out = sys.argv[1:4]
    d = open(xbe, 'rb').read()
    secs = sections(d)
    lifted = set()
    for f in glob.glob(gen + '/recomp_[0-9]*.c'):
        text = open(f, encoding='utf-8', errors='ignore').read()
        lifted.update(int(m, 16) for m in re.findall(r'^void sub_([0-9A-F]{8})(?:_gen)?\(void\)', text, re.M))

    def raw_of(v):
        for name, va, vsize, raw, rsize in secs:
            if va <= v < va + rsize:
                return raw + v - va, name
        return None, None

    found = {}
    for name, va, vsize, raw, rsize in secs:
        if name in CODE:
            continue
        for off in range(0, rsize - 3, 4):
            v = struct.unpack_from('<I', d, raw + off)[0]
            if v % 16 or v in lifted:
                continue
            rv, sn = raw_of(v)
            if sn not in CODE or rv is None:
                continue
            prev = struct.unpack_from('<I', d, rv - 4)[0]
            if (d[rv - 1] in (0x90, 0xCC, 0xC3) or v - 0x4000 < prev < v) and d[rv] not in (0x90, 0xCC, 0x00):
                found.setdefault(v, (name, va + off))
    seeds = [{'start': '0x%08X' % v,
              'source': 'table scan: pointer in %s at 0x%08X (tools/scan_table_seeds.py)' % found[v]}
             for v in sorted(found)]
    with open(out, 'w', newline='\n') as f:
        json.dump(seeds, f, indent=1)
        f.write('\n')
    print('%d unlifted function pointers -> %s' % (len(seeds), out))


if __name__ == '__main__':
    main()
