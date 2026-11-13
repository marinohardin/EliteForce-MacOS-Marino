#!/usr/bin/env python3
"""Regenerate efgame/src/game/g_savetranscode.cpp from clang record-layout dumps.

Savegames store the raw-struct chunks GENT/GCLI/GNPC/LVLC in the canonical 32-bit
(ILP32, retail PC / armv7) layout. On a 64-bit build the struct layout differs
(8-byte pointers, alignment padding), so g_savetranscode.cpp carries a flat table of
copy records {native offset, disk offset, nbytes}, one per scalar leaf, plus
static_asserts on offsetof() so a struct edit that drifts from the table fails to
compile.

Commands:
  dump   - runs clang -fdump-record-layouts-canonical for target triple(s) on a TU that
           includes g_local.h/g_shared.h with efgame's include path/defines/force-include,
           keeps the four chunk structs (and every record they reference) and writes
           tools/savelayout/layouts/<triple>.json (committed, so `gen` needs no toolchain).
  gen    - flattens native vs disk layouts into copy records and writes the .cpp.
           One section per native ABI; ABIs with identical tables share a section
           (their #if conditions are OR-ed).
  check  - like gen, but compares with the existing file; exit 1 on difference.
  tables - print only the data section for one native triple (for diffing).

See README.md.
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(REPO, "efgame", "src")
AND = os.path.join(REPO, "efgame", "android")
LAYOUTS = os.path.join(HERE, "layouts")
OUT = os.path.join(SRC, "game", "g_savetranscode.cpp")

# chid, root record (as clang names it), C type for offsetof/sizeof, end field (exclusive) or None
CHUNKS = [
    ("GENT", "struct gentity_s", "gentity_t", None),
    ("GCLI", "struct gclient_s", "gclient_t", None),
    ("GNPC", "gNPC_t", "gNPC_t", None),
    ("LVLC", "level_locals_t", "level_locals_t", "logFile"),
]

# Canonical on-disk layout: retail PC (MSVC i386). i686-w64-mingw32 uses MSVC's record
# layout rules (8-byte aligned double/int64 members, unlike i386 SysV).
DISK_TRIPLE = "i686-w64-mingw32"

# Native ABIs that get a table, with the preprocessor condition that selects it.
NATIVE_ABIS = [
    ("aarch64-apple-darwin", "defined(__LP64__)"),
    ("x86_64-linux-gnu", "defined(__LP64__)"),
    ("x86_64-w64-mingw32", "defined(_WIN64)"),
]

# The force-included compat header renames identifiers on non-Windows targets
# (`#define random ef_frandom`); the generated source spells the source name.
NAME_ALIASES = {"ef_frandom": "random"}

MINGW = os.environ.get("MINGW_PREFIX", "/opt/homebrew/opt/mingw-w64")

TU = ('#include "g_local.h"\n#include "g_shared.h"\n'
      "int sg_sizes[] = { sizeof(gentity_t), sizeof(gclient_t), sizeof(gNPC_t), sizeof(level_locals_t) };\n")


def clang_args(triple, sysroot):
    win = "mingw32" in triple or "windows" in triple
    inc = [] if win else [AND]  # on _WIN32 the android/ shim dir must not shadow <windows.h>
    inc += [os.path.join(SRC, d) for d in ("game", "cgame", "ui", "icarus", "qcommon", "client", "renderer")]
    a = ["-target", triple, "-x", "c++", "-fsyntax-only", "-Wno-everything", "-fcommon",
         "-DEF_DYLIB_MODULES", "-DNDEBUG", "-include", os.path.join(AND, "ef_android_compat.h")]
    if win:
        a += ["-DWIN32_LEAN_AND_MEAN", "-DNOMINMAX"]
        a += ["--sysroot=" + (sysroot or os.path.join(MINGW, "toolchain-" + triple.split("-")[0]))]
    elif sysroot:
        a += ["--sysroot=" + sysroot]
    a += ["-I" + d for d in inc]
    a += ["-Xclang", "-fdump-record-layouts-canonical"]
    return a


ANON_RE = re.compile(r"\((anonymous|unnamed) at ([^)]*)\)")


def norm_anon(s):
    # "(anonymous at /abs/path/g_shared.h:562:2)" -> "(anonymous at g_shared.h:562:2)"
    return ANON_RE.sub(lambda m: "(%s at %s)" % (m.group(1), os.path.basename(m.group(2))), s)


LINE_RE = re.compile(r"^\s*(\d+)(?::(\d+)-(\d+))? \| (\s*)(.*)$")


def parse_dump(text):
    """-> {record name: {"size": n, "align": n, "fields": [[off, depth, type, name], ...]}}"""
    recs = {}
    for b in text.split("*** Dumping AST Record Layout")[1:]:
        lines = [l for l in b.splitlines() if l.strip()]
        m = LINE_RE.match(lines[0])
        name = norm_anon(m.group(5).strip())
        fields, size, align = [], None, None
        for l in lines[1:]:
            if "[sizeof=" in l:
                size = int(re.search(r"sizeof=(\d+)", l).group(1))
                align = int(re.search(r"\balign=(\d+)", l).group(1))
                continue
            if "nvsize=" in l:
                continue
            m = LINE_RE.match(l)
            if not m:
                raise SystemExit("unparsed layout line: %r" % l)
            off = int(m.group(1))
            depth = len(m.group(4)) // 2
            body = norm_anon(m.group(5).rstrip())
            if m.group(2) is not None:
                body = "BITFIELD " + body
            if body.endswith(")") or " " not in body:   # anonymous record member: no name
                typ, fname = body, ""
            else:
                typ, fname = body.rsplit(" ", 1)
            fields.append([off, depth, typ.strip(), fname])
        recs.setdefault(name, {"size": size, "align": align, "fields": fields})
    return recs


def reachable(recs, roots):
    keep, todo = {}, list(roots)
    while todo:
        n = todo.pop()
        if n in keep or n not in recs:
            continue
        keep[n] = recs[n]
        for f in recs[n]["fields"]:
            base = re.sub(r"(\[\d+\])+$", "", f[2])
            if base in recs:
                todo.append(base)
    return keep


BUILTIN_RE = re.compile(r"^(?:(?:un)?signed |const |volatile )*(char|short|int|long|long long|float|double|bool|_Bool)$")


def probe_enums(args, triple, tu, keep):
    """The canonical dump still spells typedefs of *anonymous* enums by their typedef name
    (team_t, trType_t, ...). Prove each such leaf type is an enum and record its size, by
    compiling a probe TU: static_assert(__is_enum(T)) + struct { T v; char end; } layout."""
    names = set()
    for r in keep.values():
        for f in r["fields"]:
            t = re.sub(r"(\[\d+\])+$", "", f[2]).strip()
            if t.endswith("*") or "(*)" in t or t in keep or t.startswith(("struct ", "union ", "class ", "enum ", "BITFIELD")):
                continue
            if BUILTIN_RE.match(t):
                continue
            names.add(t)
    if not names:
        return {}
    probe = os.path.join(LAYOUTS, ".sg_enum_probe.cpp")
    with open(probe, "w") as f:
        f.write(TU)
        for n in sorted(names):
            f.write('static_assert(__is_enum(%s), "%s is not an enum");\n' % (n, n))
            f.write("struct sg_probe_%s { %s v; char end; };\nint sg_probe_sz_%s = sizeof(sg_probe_%s);\n" % (n, n, n, n))
    try:
        p = subprocess.run([args.clang] + clang_args(triple, args.sysroot) + [probe], capture_output=True, text=True)
        if p.returncode != 0:
            sys.stderr.write(p.stderr)
            raise SystemExit("enum probe failed for " + triple)
        recs = parse_dump(p.stdout)
        out = {}
        for n in sorted(names):
            r = recs["struct sg_probe_%s" % n]
            out[n] = [f[0] for f in r["fields"] if f[3] == "end"][0]
        return out
    finally:
        os.remove(probe)


def cmd_dump(args):
    os.makedirs(LAYOUTS, exist_ok=True)
    tu = os.path.join(LAYOUTS, ".sg_layout_tu.cpp")
    with open(tu, "w") as f:
        f.write(TU)
    try:
        for triple in args.triples:
            cc = [args.clang] + clang_args(triple, args.sysroot) + [tu]
            p = subprocess.run(cc, capture_output=True, text=True)
            if p.returncode != 0:
                sys.stderr.write(p.stderr)
                raise SystemExit("clang failed for " + triple)
            recs = parse_dump(p.stdout)
            missing = [c[1] for c in CHUNKS if c[1] not in recs]
            if missing:
                raise SystemExit("%s: missing records %s" % (triple, missing))
            keep = reachable(recs, [c[1] for c in CHUNKS])
            enums = probe_enums(args, triple, tu, keep)
            ptr = 8 if re.match(r"(x86_64|aarch64|arm64)", triple) else 4
            win = "mingw32" in triple or "windows" in triple
            longsz = 8 if ptr == 8 and not win else 4
            out = {"triple": triple, "pointer_size": ptr, "long_size": longsz, "records": keep, "enums": enums}
            path = os.path.join(LAYOUTS, triple + ".json")
            with open(path, "w") as f:
                json.dump(out, f, indent=1, sort_keys=True)
                f.write("\n")
            print("wrote", os.path.relpath(path, REPO), "(%d records)" % len(keep))
    finally:
        os.remove(tu)


SCALARS = {
    "char": 1, "signed char": 1, "unsigned char": 1, "bool": 1, "_Bool": 1,
    "short": 2, "unsigned short": 2, "int": 4, "unsigned int": 4, "float": 4,
    "long long": 8, "unsigned long long": 8, "double": 8,
}
ARRAY_RE = re.compile(r"^(.*?)\[(\d+)\]((?:\[\d+\])*)$")


class Flattener:
    def __init__(self, lay):
        self.recs = lay["records"]
        self.ptr = lay["pointer_size"]
        self.long = lay["long_size"]
        self.enums = lay.get("enums", {})
        self.flags = []   # notes about risky leaves (long, 8-byte scalars)

    def scalar(self, t, path):
        t = re.sub(r"\b(const|volatile)\b", "", t).strip()
        t = re.sub(r"\s+", " ", t)
        if t.endswith("*") or "(*)" in t:
            return "ptr", self.ptr
        if t.startswith("enum "):
            return "scalar", 4
        if t in self.enums:
            return "scalar", self.enums[t]
        if t in ("long", "unsigned long"):
            self.flags.append("%s: %s (%d bytes on this ABI)" % (path, t, self.long))
            return "long", self.long
        if t in SCALARS:
            if SCALARS[t] == 8:
                self.flags.append("%s: %s (8-byte scalar: alignment differs between ABIs)" % (path, t))
            return "scalar", SCALARS[t]
        raise SystemExit("unknown leaf type %r at %s" % (t, path))

    def sizeof(self, t, path):
        m = ARRAY_RE.match(t)
        if m:
            return int(m.group(2)) * self.sizeof(m.group(1) + m.group(3), path)
        if t in self.recs:
            return self.recs[t]["size"]
        return self.scalar(t, path)[1]

    def flatten_type(self, t, base, path):
        """Yield (path, offset, size, kind) leaves for an object of type t at offset base."""
        m = ARRAY_RE.match(t)
        if m:
            elem = m.group(1) + m.group(3)
            stride = self.sizeof(elem, path)
            for i in range(int(m.group(2))):
                yield from self.flatten_type(elem, base + i * stride, "%s[%d]" % (path, i))
            return
        if t.startswith("BITFIELD"):
            raise SystemExit("bit-field in saved struct at %s; generator does not support it" % path)
        if t in self.recs:
            yield from self.flatten_record(t, base, path)
            return
        if t.startswith(("struct ", "union ", "class ")) and not t.endswith("*"):
            raise SystemExit("record %r (at %s) has no layout in the dump" % (t, path))
        kind, size = self.scalar(t, path)
        yield (path, base, size, kind)

    def flatten_record(self, name, base, path):
        fields = self.recs[name]["fields"]
        members = [f for f in fields if f[1] == 1]   # direct members; deeper lines are inline expansions
        if any(f[2].endswith("(base)") or "vtable" in f[2] for f in members):
            raise SystemExit("%s: C++ bases/vtables not supported" % name)
        if name.startswith("union "):
            # copy the largest member (first on ties): it covers every byte any member uses
            best = None
            for f in members:
                sz = self.sizeof(f[2], path)
                if best is None or sz > best[0]:
                    best = (sz, f)
            members = [best[1]]
        for k, (off, _, t, fname) in enumerate(members):
            fname = NAME_ALIASES.get(fname, fname)
            sub = "%s.%s" % (path, fname) if fname else "%s.<anon#%d>" % (path, k)
            yield from self.flatten_type(t, base + off, sub)

    def top_fields(self, root):
        return [(NAME_ALIASES.get(f[3], f[3]), f[0], f[2]) for f in self.recs[root]["fields"] if f[1] == 1]


def load(triple):
    with open(os.path.join(LAYOUTS, triple + ".json")) as f:
        return json.load(f)


def assert_fields(nat, dsk, root, nend):
    """Every named direct member of the root inside the saved range. (The original, lost
    generator asserted all of GENT's members but only a hand-picked few of GCLI/GNPC/LVLC's;
    asserting all of them is strictly stronger and keeps the rule mechanical.)"""
    return [(n, o) for n, o, t in nat.top_fields(root) if n and (nend is None or o < nend)]


def build_tables(native_triple, disk_triple=DISK_TRIPLE):
    nat, dsk = Flattener(load(native_triple)), Flattener(load(disk_triple))
    out = []
    for chid, root, ctype, endf in CHUNKS:
        nl = list(nat.flatten_record(root, 0, chid))
        dl = list(dsk.flatten_record(root, 0, chid))
        if [l[0] for l in nl] != [l[0] for l in dl]:
            raise SystemExit("%s: native/disk leaf sequences differ" % chid)
        nend = dend = None
        if endf:
            nend = [o for n, o, t in nat.top_fields(root) if n == endf][0]
            dend = [o for n, o, t in dsk.top_fields(root) if n == endf][0]
        recs = []
        for (p, no, ns, nk), (_, do, ds, dk) in zip(nl, dl):
            if nend is not None and no >= nend:
                break
            if nk == "ptr" or dk == "ptr":
                if not nk == dk == "ptr":
                    raise SystemExit("%s: pointer/non-pointer mismatch" % p)
                nb = ds   # saved pointers hold a 32-bit index/handle: copy the low (LE) bytes
            elif ns != ds:
                raise SystemExit("%s: leaf size native %d != disk %d (%s/%s)" % (p, ns, ds, nk, dk))
            else:
                nb = ns
            recs.append((no, do, nb))
        nsize = nend if nend is not None else nat.recs[root]["size"]
        dsize = dend if dend is not None else dsk.recs[root]["size"]
        out.append({"chid": chid, "ctype": ctype, "endf": endf, "recs": recs,
                    "nsize": nsize, "dsize": dsize, "asserts": assert_fields(nat, dsk, root, nend),
                    "flags": [f for f in nat.flags + dsk.flags if f.startswith(chid)]})
    return out


def wrap_recs(recs, width=110):
    lines, cur = [], "  "
    for r in recs:
        item = "{%d,%d,%d}," % r
        if len(cur) + len(item) > width and cur.strip():
            lines.append(cur)
            cur = "  "
        cur += item
    if cur.strip():
        lines.append(cur)
    return lines


def emit_section(tables):
    L = []
    for t in tables:
        L.append("static const sgRec_t sg_%s_recs[] = {" % t["chid"])
        L += wrap_recs(t["recs"])
        L.append("};")
    L.append("")
    L.append("static const sgDesc_t sg_descs[] = {")
    for t in tables:
        c = t["chid"]
        L.append("  { (('%s'<<24)|('%s'<<16)|('%s'<<8)|'%s'), sg_%s_recs, %d, %d, %d, \"%s\" }," %
                 (c[0], c[1], c[2], c[3], c, len(t["recs"]), t["dsize"], t["nsize"], c))
    L.append("};")
    L.append("static const int sg_ndescs = sizeof(sg_descs)/sizeof(sg_descs[0]);")
    L.append("")
    L.append("static void SG_TranscodeSelfCheck(void){")
    L.append("  static int done=0; if(done) return; done=1;")
    for t in tables:
        expr = "offsetof(%s,%s)" % (t["ctype"], t["endf"]) if t["endf"] else "sizeof(%s)" % t["ctype"]
        L.append('  if((int)(%s) != %d) gi.Error(ERR_DROP, "SG transcode %s: computed nativeSize %d != %%d (%s)", (int)(%s));'
                 % (expr, t["nsize"], t["chid"], t["nsize"], expr, expr))
    L.append("}")
    L.append("")
    for t in tables:
        expr = "offsetof(%s,%s)" % (t["ctype"], t["endf"]) if t["endf"] else "sizeof(%s)" % t["ctype"]
        L.append('static_assert(%s == %d, "SG transcode %s: native size %d != %s (struct drifted from tools/savelayout/layouts; rerun gen_savetranscode.py)");'
                 % (expr, t["nsize"], t["chid"], t["nsize"], expr))
    for t in tables:
        for n, o in t["asserts"]:
            L.append('static_assert(offsetof(%s, %s) == %d, "SG transcode %s.%s: native offset %d != offsetof '
                     '(struct drifted from tools/savelayout/layouts; rerun gen_savetranscode.py)");' % (t["ctype"], n, o, t["chid"], n, o))
    return L


HEADER = """// AUTO-GENERATED by tools/savelayout/gen_savetranscode.py (do not hand-edit; see tools/savelayout/README.md).
// ILP32<->64-bit savegame transcoder: flattened copy-records so the on-disk layout is the
// canonical 32-bit (PC retail / armv7) byte layout regardless of build ABI. 64-bit builds only
// (LP64 macOS/Linux, LLP64 Win64); EF_SAVE_TRANSCODE is defined in g_savetranscode.h.
#include "g_local.h"
#include "g_shared.h"
#include "g_savetranscode.h"
#include <stddef.h>
#include <string.h>

#ifdef EF_SAVE_TRANSCODE
typedef struct { int noff, doff, nbytes; } sgRec_t;
typedef struct { unsigned long chid; const sgRec_t* recs; int nrecs; int diskSize; int nativeSize; const char* name; } sgDesc_t;
"""

FOOTER = """
const void* SG_TranscodeDesc(unsigned long chid){
  SG_TranscodeSelfCheck();
  for(int i=0;i<sg_ndescs;i++) if(sg_descs[i].chid==chid) return &sg_descs[i];
  return 0;
}
// pack native (64-bit) struct -> ILP32 disk blob (caller appends diskSize bytes). returns diskSize.
int SG_TranscodePack(const void* descV, const unsigned char* native, unsigned char* disk){
  const sgDesc_t* d=(const sgDesc_t*)descV; memset(disk,0,d->diskSize);
  for(int i=0;i<d->nrecs;i++){ const sgRec_t* r=&d->recs[i]; memcpy(disk+r->doff, native+r->noff, r->nbytes); }
  return d->diskSize;
}
// unpack ILP32 disk blob -> native (64-bit) struct (zeroes native first).
void SG_TranscodeUnpack(const void* descV, const unsigned char* disk, unsigned char* native){
  const sgDesc_t* d=(const sgDesc_t*)descV; memset(native,0,d->nativeSize);
  for(int i=0;i<d->nrecs;i++){ const sgRec_t* r=&d->recs[i]; memcpy(native+r->noff, disk+r->doff, r->nbytes); }
}
int SG_TranscodeDiskSize(const void* descV){ return ((const sgDesc_t*)descV)->diskSize; }
int SG_TranscodeNativeSize(const void* descV){ return ((const sgDesc_t*)descV)->nativeSize; }
#endif // EF_SAVE_TRANSCODE
"""


def render(abis):
    sections = []   # [conds, body, triples]
    for triple, cond in abis:
        body = emit_section(build_tables(triple))
        for s in sections:
            if s[1] == body:
                if cond not in s[0]:
                    s[0].append(cond)
                s[2].append(triple)
                break
        else:
            sections.append([[cond], body, [triple]])
    L = [HEADER.rstrip("\n")]
    if len(sections) == 1:
        L.append("// Native ABIs (identical tables): %s. Disk layout: %s." % (", ".join(sections[0][2]), DISK_TRIPLE))
        L.append("")
        L += sections[0][1]
    else:
        for i, (conds, body, triples) in enumerate(sections):
            L.append("%s %s   // native ABIs: %s" % ("#if" if i == 0 else "#elif", " || ".join(conds), ", ".join(triples)))
            L.append("")
            L += body
        L.append("#else")
        L.append('#error "no savegame transcode table for this 64-bit ABI: run tools/savelayout/gen_savetranscode.py"')
        L.append("#endif")
    return "\n".join(L) + "\n" + FOOTER


def cmd_gen(args):
    abis = [a for a in NATIVE_ABIS if os.path.exists(os.path.join(LAYOUTS, a[0] + ".json"))]
    skipped = [a[0] for a in NATIVE_ABIS if a not in abis]
    if skipped:
        print("warning: no layout dump for %s (run `dump` for it)" % ", ".join(skipped), file=sys.stderr)
    for triple, _ in abis:
        for t in build_tables(triple):
            for f in t["flags"]:
                print("NOTE %s: %s" % (triple, f))
    text = render(abis)
    if args.cmd == "check":
        with open(OUT) as f:
            cur = f.read()
        if cur != text:
            print("g_savetranscode.cpp is out of date (run gen)", file=sys.stderr)
            return 1
        print("g_savetranscode.cpp is up to date")
        return 0
    out = args.output or OUT
    with open(out, "w") as f:
        f.write(text)
    print("wrote", out)
    return 0


def cmd_verify(args):
    """Compile g_savetranscode.cpp + g_savegame.cpp -fsyntax-only for each 64-bit target so the
    static_asserts are evaluated, and check EF_SAVE_TRANSCODE is on there (and off on i686)."""
    files = [OUT, os.path.join(SRC, "game", "g_savegame.cpp")]
    probe = '#include "g_savetranscode.h"\n#ifndef EF_SAVE_TRANSCODE\n#error EF_SAVE_TRANSCODE off\n#endif\n'
    jobs = []   # (label, argv, stdin, expect_ok)
    for triple in ["aarch64-apple-darwin", "x86_64-w64-mingw32"] + (["x86_64-linux-gnu"] if args.linux_sysroot else []):
        base = [args.clang] + [a for a in clang_args(triple, args.linux_sysroot if "linux" in triple else None)
                               if a not in ("-Xclang", "-fdump-record-layouts-canonical")]
        for f in files:
            jobs.append(("clang %s %s" % (triple, os.path.basename(f)), base + [f], None, True))
        jobs.append(("clang %s EF_SAVE_TRANSCODE on" % triple, base + ["-"], probe, True))
    base = [args.clang] + [a for a in clang_args("i686-w64-mingw32", None) if a not in ("-Xclang", "-fdump-record-layouts-canonical")]
    jobs.append(("clang i686-w64-mingw32 EF_SAVE_TRANSCODE off", base + ["-"], probe, False))
    gxx = args.mingw_gxx
    if gxx:
        inc = ["-I" + os.path.join(SRC, d) for d in ("game", "cgame", "ui", "icarus", "qcommon", "client", "renderer")]
        g = [gxx, "-fsyntax-only", "-w", "-fpermissive", "-fcommon", "-DEF_DYLIB_MODULES", "-DNDEBUG",
             "-DWIN32_LEAN_AND_MEAN", "-DNOMINMAX", "-include", os.path.join(AND, "ef_android_compat.h")] + inc
        for f in files:
            jobs.append(("%s %s" % (os.path.basename(gxx), os.path.basename(f)), g + [f], None, True))
    bad = 0
    for label, argv, stdin, expect_ok in jobs:
        p = subprocess.run(argv, input=stdin, capture_output=True, text=True)
        ok = (p.returncode == 0) == expect_ok
        print("%-4s %s" % ("ok" if ok else "FAIL", label))
        if not ok:
            bad += 1
            sys.stderr.write(p.stderr[-4000:])
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    d = sp.add_parser("dump", help="run clang and write layouts/<triple>.json")
    d.add_argument("triples", nargs="*", default=[DISK_TRIPLE] + [a[0] for a in NATIVE_ABIS])
    d.add_argument("--clang", default=os.environ.get("CLANG", "clang++"))
    d.add_argument("--sysroot", default=None, help="sysroot for the triple(s) (Linux: a glibc+libstdc++ root)")
    g = sp.add_parser("gen", help="write efgame/src/game/g_savetranscode.cpp")
    g.add_argument("-o", "--output")
    sp.add_parser("check", help="exit 1 if g_savetranscode.cpp differs from what gen writes")
    t = sp.add_parser("tables", help="print the data section for one native triple")
    t.add_argument("triple")
    v = sp.add_parser("verify", help="syntax-check the transcoder + g_savegame.cpp per 64-bit target")
    v.add_argument("--clang", default=os.environ.get("CLANG", "clang++"))
    v.add_argument("--linux-sysroot", default=None, help="also check x86_64-linux-gnu with this sysroot")
    v.add_argument("--mingw-gxx", default=None, help="also check with this x86_64-w64-mingw32-g++")
    args = ap.parse_args()
    if args.cmd == "verify":
        return cmd_verify(args)
    if args.cmd == "dump":
        cmd_dump(args)
        return 0
    if args.cmd == "tables":
        sys.stdout.write("\n".join(emit_section(build_tables(args.triple))) + "\n")
        return 0
    return cmd_gen(args)


if __name__ == "__main__":
    sys.exit(main())
