#!/usr/bin/env python3
"""List the memory accesses in the recompiled code that the Switch fault emulation would not handle.

os/switch/exception_switch.cpp emulates a load or store that faults on an uncommitted page of the guest
window (every uncommitted byte reads as zero and is not written) and resumes the thread through x16. It
decodes the AArch64 load/store forms the compiler emits for guest memory; anything else stays fatal. Every
change of code generation (XenonRecomp, direct calls, LTO, PGO, compiler flags) can bring new forms, so this
script disassembles the recompiled functions (__imp__sub_XXXXXXXX and the __imp____save*/__imp____rest*
helpers, with any GCC suffix such as .cold or .lto_priv.0) of objects or a linked ELF and reports:

  - every load/store encoding EmulateAccess does not decode, by mnemonic, with examples;
  - exclusive and atomic accesses (lwarx/stwcx. and the relaxed or __sync compare-and-swap): fatal by design,
    counted separately;
  - every use of x16/w16, which must not happen when the recompiled code is built with -ffixed-x16 (the resume
    leaves the resume address in x16); a load into x16 or a write-back of x16 is refused by EmulateAccess.

Only accesses through a guest address can fault into the handler; the script does not know which base
register holds one, so it reports all of them (stack and PPCContext accesses are always of handled forms).
With LTO, recompiled bodies can also be inlined into app functions (hooks that call __imp__sub_X): scan a
linked ELF with --all-functions to include every function (app code has atomics and other forms of its own,
so read that report per function).

Usage:
    python tools/switch-check-fault-emulation.py build/switch-app/MarathonRecompLib/CMakeFiles/MarathonRecompLib.dir/ppc
    python tools/switch-check-fault-emulation.py --objdump aarch64-none-elf-objdump build/switch-app/MarathonRecomp/MarathonRecomp
Arguments are objects, ELF files or folders (searched for *.obj and *.o). Exit status 1 if an unhandled
non-atomic access or an x16 use was found (--allow-x16 to accept x16 uses, e.g. without -ffixed-x16).
"""

from __future__ import annotations

import argparse
import collections
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

RECOMPILED = re.compile(r"^__imp__(?:sub_[0-9A-Fa-f]{8}|__(?:save|rest)(?:gprlr|fpr|vmx)_\d+)(?:\.\w+)*$")
FUNCTION = re.compile(r"^[0-9a-fA-F]+ <([^>]+)>:$")
INSN = re.compile(r"^\s*([0-9a-fA-F]+):\s+([0-9a-fA-F]{8})\s+(\S+)\s*(.*)$")
X16 = re.compile(r"\b[wx]16\b")


def sign_extend(value: int, bits: int) -> int:
    sign = 1 << (bits - 1)
    return ((value & ((sign << 1) - 1)) ^ sign) - sign


def classify(insn: int) -> tuple[str, bool, bool]:
    """(class, emulated, writes x16) for a load/store encoding; class '' for anything else."""
    rt = insn & 31
    rn = (insn >> 5) & 31

    # DC ZVA
    if (insn & 0xFFFFFFE0) == 0xD50B7420:
        return "dc zva", True, False

    # Only the load/store group from here on: bit 27 = 1, bit 25 = 0.
    if (insn & 0x0A000000) != 0x08000000:
        return "", False, False

    # Load/store register (integer or SIMD&FP): bits 29:27 = 111, bit 25 = 0.
    if (insn & 0x3A000000) == 0x38000000:
        size = insn >> 30
        opc = (insn >> 22) & 3
        vector = (insn >> 26) & 1
        if vector:
            if not ((size == 0 and opc & 2) or not (opc & 2)):
                return "unallocated", False, False
            load = bool(opc & 1)
        else:
            if opc == 2 and size == 3:
                return "prefetch", True, False
            if opc == 3 and size >= 2:
                return "unallocated", False, False
            load = opc != 0
        if insn & 0x01000000:
            form, writeback = "register (unsigned offset)", False
        elif insn & 0x00200000:
            if (insn >> 10) & 3 != 2:
                return "atomic memory operation", False, False
            if ((insn >> 13) & 7) not in (2, 3, 6, 7):
                return "unallocated", False, False
            form, writeback = "register (register offset)", False
        else:
            mode = (insn >> 10) & 3
            form = ["register (unscaled)", "register (post-index)", "register (unprivileged)", "register (pre-index)"][mode]
            writeback = mode in (1, 3)
        writes_x16 = (writeback and rn == 16) or (load and not vector and rt == 16)
        return form + (" simd" if vector else ""), True, writes_x16

    # Load/store pair: bits 29:27 = 101, bit 25 = 0.
    if (insn & 0x3A000000) == 0x28000000:
        opc = insn >> 30
        vector = (insn >> 26) & 1
        load = bool((insn >> 22) & 1)
        mode = (insn >> 23) & 3
        if (vector and opc == 3) or (not vector and (opc == 3 or (opc == 1 and not load))):
            return "unallocated pair", False, False
        writeback = mode in (1, 3)
        rt2 = (insn >> 10) & 31
        writes_x16 = (writeback and rn == 16) or (load and not vector and 16 in (rt, rt2))
        return "pair" + (" simd" if vector else ""), True, writes_x16

    # ASIMD load/store multiple structures, no write-back / post-index.
    if (insn & 0xBFBF0000) == 0x0C000000 or (insn & 0xBFA00000) == 0x0C800000:
        opcode = (insn >> 12) & 0xF
        q = (insn >> 30) & 1
        size = (insn >> 10) & 3
        if opcode not in (0x0, 0x2, 0x4, 0x6, 0x7, 0x8, 0xA) or (size == 3 and not q and opcode in (0x0, 0x4, 0x8)):
            return "unallocated structure", False, False
        writeback = (insn & 0x00800000) != 0
        return "multiple structures", True, writeback and rn == 16

    # ASIMD load/store single structure, no write-back / post-index.
    if (insn & 0xBF9F0000) == 0x0D000000 or (insn & 0xBF800000) == 0x0D800000:
        opcode = (insn >> 13) & 7
        l = (insn >> 22) & 1
        s = (insn >> 12) & 1
        size = (insn >> 10) & 3
        scale = opcode >> 1
        ok = True
        if scale == 3:
            ok = l == 1 and s == 0
        elif scale == 1:
            ok = (size & 1) == 0
        elif scale == 2:
            ok = (size & 2) == 0 and not ((size & 1) and s)
        if not ok:
            return "unallocated structure", False, False
        writeback = (insn & 0x00800000) != 0
        return ("load and replicate" if scale == 3 else "single structure"), True, writeback and rn == 16

    # Exclusives, load-acquire/store-release: bits 29:24 = 001000.
    if (insn & 0x3F000000) == 0x08000000:
        return "exclusive/ordered", False, False

    # Load literal (PC-relative: the literal pool, never guest memory).
    if (insn & 0x3B000000) == 0x18000000:
        return "literal", True, False

    return "other load/store", False, False


def objdump_functions(objdump: str, path: Path, all_functions: bool = False):
    """Yields (function, offset, encoding, text) for the recompiled functions of one file."""
    proc = subprocess.run([objdump, "-d", "-z", str(path)], capture_output=True, text=True, errors="replace")
    if proc.returncode != 0:
        raise RuntimeError(f"{objdump} failed on {path}: {proc.stderr.strip()[:500]}")
    current = None
    for line in proc.stdout.splitlines():
        m = FUNCTION.match(line)
        if m:
            current = m.group(1) if all_functions or RECOMPILED.match(m.group(1)) else None
            continue
        if current is None:
            continue
        m = INSN.match(line)
        if m:
            yield current, int(m.group(1), 16), int(m.group(2), 16), (m.group(3) + " " + m.group(4)).strip()


def scan(objdump: str, path: Path, all_functions: bool = False):
    classes = collections.Counter()
    unhandled = collections.defaultdict(list)
    atomics = collections.defaultdict(list)
    x16_uses = []
    x16_refused = []
    functions = set()
    for function, offset, insn, text in objdump_functions(objdump, path, all_functions):
        functions.add(function)
        cls, emulated, writes_x16 = classify(insn)
        where = f"{path.name}:{function}+0x{offset:x}"
        if X16.search(text):
            x16_uses.append((where, text))
        if not cls:
            continue
        classes[cls] += 1
        if writes_x16:
            x16_refused.append((where, text))
        if not emulated:
            mnemonic = text.split()[0]
            (atomics if cls in ("exclusive/ordered", "atomic memory operation") else unhandled)[mnemonic].append((where, text))
    return classes, unhandled, atomics, x16_uses, x16_refused, functions


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--objdump", default=None, help="aarch64-none-elf-objdump (default: from DEVKITPRO or PATH)")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--examples", type=int, default=5)
    parser.add_argument("--allow-x16", action="store_true", help="do not fail on x16 uses (build without -ffixed-x16)")
    parser.add_argument("--all-functions", action="store_true",
                        help="scan every function, not only the recompiled ones (LTO: guest code inlined into hooks)")
    args = parser.parse_args()

    objdump = args.objdump
    if objdump is None:
        roots = [os.environ.get("DEVKITPRO", ""), "C:/devkitPro", "/opt/devkitpro"]
        for root in filter(None, roots):
            candidate = Path(root) / "devkitA64" / "bin" / "aarch64-none-elf-objdump"
            for c in (candidate, candidate.with_suffix(".exe")):
                if c.exists():
                    objdump = str(c)
                    break
            if objdump:
                break
        objdump = objdump or shutil.which("aarch64-none-elf-objdump") or "aarch64-none-elf-objdump"

    files: list[Path] = []
    for p in args.paths:
        if p.is_dir():
            files += sorted(q for q in p.rglob("*") if q.suffix in (".obj", ".o"))
        elif p.exists():
            files.append(p)
        else:
            sys.exit(f"switch-check-fault-emulation: {p} does not exist")
    if not files:
        sys.exit("switch-check-fault-emulation: no object files found")

    classes = collections.Counter()
    unhandled = collections.defaultdict(list)
    atomics = collections.defaultdict(list)
    x16_uses, x16_refused = [], []
    functions = set()
    with ThreadPoolExecutor(max(1, args.jobs)) as pool:
        for c, u, a, xu, xr, fn in pool.map(lambda f: scan(objdump, f, args.all_functions), files):
            classes.update(c)
            for k, v in u.items():
                unhandled[k] += v
            for k, v in a.items():
                atomics[k] += v
            x16_uses += xu
            x16_refused += xr
            functions |= fn

    print(f"switch-check-fault-emulation: {len(files)} files, {len(functions)} "
          f"{'functions' if args.all_functions else 'recompiled functions'}, "
          f"{sum(classes.values())} loads/stores")
    for cls, count in sorted(classes.items(), key=lambda kv: -kv[1]):
        print(f"  {count:9d}  {cls}")

    def report(title, groups):
        total = sum(len(v) for v in groups.values())
        print(f"{title}: {total}")
        for mnemonic, sites in sorted(groups.items(), key=lambda kv: -len(kv[1])):
            print(f"  {len(sites):7d}  {mnemonic}")
            for where, text in sites[:args.examples]:
                print(f"             {where}: {text}")
        return total

    bad = report("NOT EMULATED (would be fatal)", unhandled)
    report("exclusive/atomic accesses (fatal by design: guest lwarx/stwcx. on an uncommitted page)", atomics)
    print(f"x16 uses: {len(x16_uses)} (EmulateAccess refuses {len(x16_refused)} of them: load into or write-back of x16)")
    for where, text in (x16_refused or x16_uses)[:args.examples]:
        print(f"             {where}: {text}")

    if bad or (x16_uses and not args.allow_x16):
        sys.exit(1)


if __name__ == "__main__":
    main()
