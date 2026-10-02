#!/usr/bin/env python3
"""[Switch] Compares GCC's multiply-add choices in the floating-point game functions of two NRO ELFs.

The code generation keeps every recompiled function with double-precision or vector floating-point arithmetic as
XenonRecomp wrote it (tools/switch-codegen-pass.py), because GCC fuses a multiply and an add it sees in one basic block
(-ffp-contract=fast), and a change elsewhere (a mode switch fewer, a function inlined differently, the hot attribute)
can still let it see more. This disassembles both ELFs, counts in each such function the fused instructions (fmla,
fmls, fmadd, fmsub, fnmadd, fnmsub) and the unfused ones (fmul, fadd, fsub, fnmul), and lists the functions whose
counts differ. Code duplicated or no longer duplicated (both sides of a mode check, an unrolled loop) changes the
counts without changing any result; a different proportion of fused operations is what to look at.

    python tools/switch-fp-check.py old.debug.elf new.debug.elf [--ppc MarathonRecompLib/ppc] [--objdump PATH]
"""

from __future__ import annotations

import argparse
import collections
import importlib.util
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SYMBOL = re.compile(r"^[0-9a-f]+ <(.+)>:$")
GUEST = re.compile(r"sub_([0-9A-F]{8})")
FUSED = {"fmla", "fmls", "fmadd", "fmsub", "fnmadd", "fnmsub"}
UNFUSED = {"fmul", "fadd", "fsub", "fnmul"}


def floating_point_functions(ppc: Path) -> set[str]:
    spec = importlib.util.spec_from_file_location("codegen_pass", ROOT / "tools" / "switch-codegen-pass.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    result = set()
    for path in ppc.glob("ppc_recomp.*.cpp"):
        lines = path.read_text(encoding="utf-8").split("\n")
        i = 0
        while i < len(lines):
            m = module.DEFINITION.match(lines[i])
            if not m:
                i += 1
                continue
            end = lines.index("}", i)
            if module.floating_point_sensitive(lines[i + 1:end]):
                result.add(m.group(1))
            i = end + 1
    return result


def counts(objdump: str, elf: Path, wanted: set[str]) -> dict[str, collections.Counter]:
    out = subprocess.run([objdump, "-d", "--no-show-raw-insn", str(elf)], capture_output=True, text=True,
                         errors="replace").stdout
    result: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    current = None
    for line in out.split("\n"):
        m = SYMBOL.match(line)
        if m:
            g = GUEST.search(m.group(1))
            current = g.group(1) if g and g.group(1) in wanted else None
            continue
        if current is None:
            continue
        parts = line.split()
        if len(parts) >= 2 and parts[0].endswith(":"):
            mnemonic = parts[1]
            if mnemonic in FUSED:
                result[current]["fused"] += 1
            elif mnemonic in UNFUSED:
                result[current]["unfused"] += 1
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("old", type=Path)
    parser.add_argument("new", type=Path)
    parser.add_argument("--ppc", type=Path, default=ROOT / "MarathonRecompLib" / "ppc")
    parser.add_argument("--objdump", default="C:/devkitPro/devkitA64/bin/aarch64-none-elf-objdump.exe")
    args = parser.parse_args()

    wanted = floating_point_functions(args.ppc)
    old = counts(args.objdump, args.old, wanted)
    new = counts(args.objdump, args.new, wanted)
    different = []
    proportion = []
    for function in sorted(wanted):
        a, b = old.get(function, collections.Counter()), new.get(function, collections.Counter())
        if (a["fused"], a["unfused"]) == (b["fused"], b["unfused"]):
            continue
        different.append(function)
        ra = a["fused"] / max(1, a["fused"] + a["unfused"])
        rb = b["fused"] / max(1, b["fused"] + b["unfused"])
        if abs(ra - rb) > 1e-9:
            proportion.append((function, a, b))

    print(f"{len(wanted)} floating-point functions; {len(different)} with different counts, {len(proportion)} of them "
          f"with a different proportion of fused operations")
    for function, a, b in proportion[:60]:
        print(f"  sub_{function}: fused {a['fused']} unfused {a['unfused']} -> fused {b['fused']} unfused {b['unfused']}")


if __name__ == "__main__":
    main()
