#!/usr/bin/env python3
"""[Switch] Code generation options applied to the recompiled sources right after XenonRecomp writes them
(port of UnleashedRecomp-NX's round 11-14 tools/switch-codegen-pass.py, adapted to Marathon).

Each option is a build switch (tools/build-switch.sh, environment variables) and changes only how the compiler gets
to see the code, never what the guest code does:

  SWITCH_LEAF_LOCALS=1       In a function that calls nothing (a leaf: no direct, indirect or tail call, no mid-asm
                             hook, nothing else using the context), the context's registers become locals: read from
                             the PPCContext at entry, written back at every return, each one the function may write.
                             Marathon keeps every guest register in the context (Marathon.toml: no *_as_local option),
                             so this covers r0-r31, f0-f31, v0-v127, cr0-cr7, ctr, xer, reserved, lr and msr; the
                             FPSCR stays in the context (its helpers switch the host's floating-point mode). The values
                             at return are the same; in between, the registers are plain locals, which no guest store,
                             atomic or fence forces to memory.
  SWITCH_WIDE_DFORM=1        Loads and stores at register + displacement (the D form) as a 64-bit address
                             (PPC_LOAD_U32_D...), for displacements of 0 to 4095 or any from r1; see ppc_context.h,
                             the guard after the 4 GB window (kernel/memory.cpp) and its fault emulation
                             (os/switch/exception_switch.cpp, WrapGuestByte).
  SWITCH_CONST_VMX_TABLES=1  lvx/stvx/lvlx/lvrx's byte shuffles with a row of VectorMaskL/R (already constants in
                             ppc_context.h) as a plain NEON TBL (PPC_VECTOR_TABLE): no index masking per shuffle; the
                             full byte reversal (VectorMaskL's first row) as a constant permutation GCC folds
                             (PPC_VECTOR_REVERSE), so an lvx/stvx copy through registers is a plain load and store.
                             The register save/restore helpers (__savegprlr_N, __restfpr_N...) go through the pass as
                             well (wide D-form from r1, leaf locals).
  SWITCH_INLINE_FP_COMPARE=1 fcmpu/fcmpo's PPCCRRegister::compare(double, double) branchless and always inlined (a
                             define in ppc_config.h only; the sources are not touched). A comparison has no arithmetic
                             to round, so it applies to every function.
  SWITCH_INLINE_MEMCPY=1     A call of the hooked memcpy/memmove or memset (misc_impl.cpp) whose size the guest code
                             loads as a constant right before it (li r5,N, with nothing but straight-line statements
                             that leave r5 alone in between) becomes __builtin_memmove/__builtin_memset of that size on
                             the call's own line: the hook made the same copy with the same pointers (base + r3,
                             base + r4) and set r3 to r3's low 32 bits, which the line does too. memmove, as the hook
                             (GuestMemmove), so overlapping copies stay exact; only sizes GCC copies inline (no library
                             call, which would use x16: see os/switch/exception_switch.cpp). Applied before
                             SWITCH_LEAF_LOCALS, so a function whose only calls were these becomes a leaf.
  SWITCH_CALL_LOCALS=1       (perf7) Leaf locals for functions that call: the context's registers are locals between
                             calls. Before every call (direct, indirect through the function table, import, hook,
                             helper) each register the function writes anywhere is stored back to the PPCContext, and
                             after it every register the function uses is read again, so the callee sees, and leaves,
                             exactly the context it saw before; at every return each written register is stored back,
                             as in a leaf. Only for the functions listed in MarathonRecompLib/config/call_locals.txt
                             (profile-hot ones; a line "all" takes every function) that have no floating-point
                             multiplication of any kind (no fmul/fmadd/fmsub/fnmadd/fnmsub, single or double, no
                             vector floating point): with no product there is nothing GCC could fuse, wherever the
                             values live. The register save/restore helpers (__savegprlr_N/__restgprlr_N: the guest's
                             own stores and loads of r14-r31 and r12 at fixed offsets from r1) are inlined there as
                             the same statements on the locals, in the same order. Not with setjmp/longjmp or a mid-asm
                             hook, nor with any other use of the context.
  SWITCH_FPSCR_LOCALS=1      (perf7) In the functions SWITCH_LEAF_LOCALS or SWITCH_CALL_LOCALS localize and that have
                             no floating-point multiplication (as above), the FPSCR's host mode word too: a local copy
                             of ctx.fpscr, stored back before calls and returns and read again after calls. Its
                             helpers still switch the host's mode at the same points with the same values; GCC can see
                             that a second check of the mode in a loop finds it already set.
  SWITCH_PREFETCH_HINTS=1    (perf7) Host prefetches of guest objects a hot function reaches later (PREFETCH_HINTS
                             below: the scene tree walk's child and sibling nodes). The pointers are read with guest
                             loads of fields the function reads too (plain memory, no fault where it has none); a
                             prefetch changes no state.
  SWITCH_FP_LEAF_LOCALS=1    (perf7) Leaf locals for floating-point leaf functions in which no plain product (fmul,
                             fmuls, through moves, negations and selects) reaches an addend on any path
                             (contraction_safe): there is nothing GCC could fuse, wherever the values live.
  SWITCH_FP_SINGLE_MODE=1    (perf7) The functions of FP_SINGLE_MODE (Havok's MOPP query) in locals, compiled without
                             contraction with their guest fused operations spelled fused, and their vector arithmetic
                             in the scalar mode (no flush-to-zero switches around it): see FP_SINGLE_MODE below.
  SWITCH_FP_NO_CONTRACT=1    (perf7) The floating-point functions listed in MarathonRecompLib/config/fp_no_contract.txt
                             in locals (as leaves or between calls), compiled without contraction with their guest
                             fused operations spelled fused and their mode switches where they were: listed only where
                             the perf6 ELF has exactly as many fused instructions as guest fused operations and as many
                             plain multiplies as guest separate multiplies, i.e. where GCC fused nothing else, so the
                             same operations round the same way.
  SWITCH_FP_INT_LOCALS=1     (perf8) The other floating-point functions (double-precision or vector arithmetic, and the
                             calling functions with single-precision products SWITCH_CALL_LOCALS refuses) that
                             MarathonRecompLib/config/call_locals.txt lists get their integer registers (r, cr, ctr,
                             xer, the reservation, lr, msr) in locals, as leaves or between calls, while their
                             floating-point and vector registers and the FPSCR stay in the context exactly as
                             XenonRecomp wrote them: every floating-point value still goes through the same context
                             field stores and loads, so GCC sees the same floating-point data flow and fuses the same
                             operations (the integer fields lie at other offsets of the context, which GCC tells apart;
                             each listed one was compiled both ways with the same fused, multiply and add counts, and
                             tools/switch-fp-check.py checks the fused counts on each build).

A function with double-precision or vector floating-point arithmetic is left exactly as XenonRecomp wrote it: GCC
fuses a multiply and a later add when it sees the product's value flow straight into the add (-ffp-contract=fast),
and each option lets it see more of that flow, which could change how such a sum is rounded. Single-precision
instructions round their result to float, which no add can be fused across, and the product of two singles is exact
in double, so whether GCC fuses the multiply-add inside one of them gives the same result; integer code has nothing
to fuse.

The ppc/ folder is regenerated whenever these change (build-switch.sh puts them in its code generation stamp), so
this runs on XenonRecomp's own output, before tools/switch-direct-calls.py. It writes ppc/codegen_pass.txt with what
it applied and refuses to run twice.

Usage: python tools/switch-codegen-pass.py   (reads the variables above)
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PPC_DIR = Path(os.environ.get("SWITCH_CODEGEN_PPC_DIR", ROOT / "MarathonRecompLib" / "ppc"))  # override: tests
TOML = ROOT / "MarathonRecompLib" / "config" / "Marathon.toml"
STAMP = PPC_DIR / "codegen_pass.txt"
CALL_LOCALS_LIST = Path(os.environ.get("SWITCH_CALL_LOCALS_LIST", ROOT / "MarathonRecompLib" / "config" / "call_locals.txt"))
NO_CONTRACT_LIST = Path(os.environ.get("SWITCH_FP_NO_CONTRACT_LIST", ROOT / "MarathonRecompLib" / "config" / "fp_no_contract.txt"))

DEFINITION = re.compile(r"^PPC_(?:HOT_)?FUNC_IMPL\(__imp__(?:sub_([0-9A-F]{8})|__(?:save|rest)(?:gprlr|fpr|vmx)_\d+)\) \{$")
CONTEXT_REGISTER = re.compile(r"\bctx\.((?:r|f|v|cr)\d+|ctr|xer|reserved|lr|msr)\b")
CALL = re.compile(r"(?<![\w])(?:__imp__)?sub_[0-9A-F]{8}\(ctx, base\)")
LOCALIZABLE = ({f"r{i}" for i in range(32)} | {f"f{i}" for i in range(32)} | {f"v{i}" for i in range(128)} |
               {f"cr{i}" for i in range(8)} | {"ctr", "xer", "reserved", "lr", "msr"})
MNEMONIC = re.compile(r"^\t// ([a-z][a-z0-9.]*)")
DOUBLE_ARITHMETIC = {"fadd", "fsub", "fmul", "fmadd", "fmsub", "fnmadd", "fnmsub"}
# Every scalar floating-point instruction with a product (SWITCH_CALL_LOCALS, SWITCH_FPSCR_LOCALS).
FP_PRODUCTS = {"fmul", "fmuls", "fmadd", "fmadds", "fmsub", "fmsubs", "fnmadd", "fnmadds", "fnmsub", "fnmsubs"}
# A whole statement that calls a function with the context: a recompiled function, an import, a hook, a register
# save/restore helper (name(ctx, base);), or an indirect call through the function table (ppc_context.h).
CALL_STATEMENT = re.compile(r"^(\t+)((?:[A-Za-z_]\w*\(ctx, base\))|(?:PPC_CALL_INDIRECT_FUNC|PPC_CALL_FUNC)\([^;]*\));$")
CALL_ANYWHERE = re.compile(r"(?<![\w])[A-Za-z_]\w*\(ctx, base\)|\bPPC_CALL_(?:INDIRECT_)?FUNC\(")
HELPER_NAME = re.compile(r"^__(?:save|rest)gprlr_\d+$")
HELPER_DEFINITION = re.compile(r"^PPC_FUNC_IMPL\(__imp__(__(?:save|rest)gprlr_\d+)\) \{$")
LABEL = re.compile(r"^(loc_[0-9A-F]+):$")
# SWITCH_FP_INT_LOCALS: the register kinds a floating-point function keeps in locals (not f, v, nor the FPSCR).
INTEGER_KINDS = frozenset({"r", "cr", "ctr", "xer", "reserved", "lr", "msr"})
GOTO = re.compile(r"\bgoto (loc_[0-9A-F]+);")

# A plain read of a register: anything else (a method call such as cr6.compare, a field written, a register passed
# by reference, its address taken) counts as a possible write, so at worst an unchanged value is written back.
ASSIGNMENT = r"(?!\s*(?:=(?!=)|\+=|-=|\*=|/=|%=|&=|\|=|\^=|<<=|>>=|\+\+|--))"
SCALAR_FIELDS = r"(?:u8|u16|u32|u64|s8|s16|s32|s64|f32|f64)"
READ_PATTERNS = {
    "r": re.compile(r"ctx\.r\d+\." + SCALAR_FIELDS + r"\b" + ASSIGNMENT),
    "f": re.compile(r"ctx\.f\d+\." + SCALAR_FIELDS + r"\b" + ASSIGNMENT),
    "v": re.compile(r"ctx\.v\d+\." + SCALAR_FIELDS + r"\[[^\]]*\]" + ASSIGNMENT),
    "cr": re.compile(r"ctx\.cr\d+\.(?:lt|gt|eq|so|un)\b" + ASSIGNMENT),
    "ctr": re.compile(r"ctx\.ctr\." + SCALAR_FIELDS + r"\b" + ASSIGNMENT),
    "reserved": re.compile(r"ctx\.reserved\." + SCALAR_FIELDS + r"\b" + ASSIGNMENT),
    "xer": re.compile(r"ctx\.xer\.(?:so|ov|ca)\b" + ASSIGNMENT),
    "lr": re.compile(r"ctx\.lr\b(?!\s*\.)" + ASSIGNMENT),
    "msr": re.compile(r"ctx\.msr\b(?!\s*\.)" + ASSIGNMENT),
}


def kind(register: str) -> str:
    if register.startswith("cr"):
        return "cr"
    if register in ("ctr", "xer", "reserved", "lr", "msr"):
        return register
    return register[0]


def floating_point_sensitive(body: list[str]) -> bool:
    """Double-precision arithmetic, or any vector floating-point instruction (conservatively, every v...fp...)."""
    for line in body:
        m = MNEMONIC.match(line)
        if m:
            mnemonic = m.group(1).rstrip(".")
            if mnemonic in DOUBLE_ARITHMETIC or (mnemonic.startswith("v") and "fp" in mnemonic):
                return True
    return False


def has_fp_products(body: list[str]) -> bool:
    """Any floating-point multiplication (single or double precision), or any vector floating-point instruction."""
    for line in body:
        m = MNEMONIC.match(line)
        if m:
            mnemonic = m.group(1).rstrip(".")
            if mnemonic in FP_PRODUCTS or (mnemonic.startswith("v") and "fp" in mnemonic):
                return True
    return False


def option(name: str) -> bool:
    return os.environ.get(name, "0") == "1"


# SWITCH_FP_LEAF_LOCALS (perf7): a scalar floating-point leaf function in which no plain product (fmul/fmuls, through
# fmr/fneg/fabs/fnabs/frsp/fsel/fdiv/fsqrt) ever reaches an addend (fadd/fsub, or the B operand of fmadd/fmsub/fnmadd/
# fnmsub), on any path. Each guest fused operation is one expression either way; with no product meeting a sum
# outside them, there is nothing else GCC could fuse whether the registers live in the context or in locals.
FP_TAINT_PRODUCT = {"fmul", "fmuls"}
FP_TAINT_FUSED = {"fmadd", "fmadds", "fmsub", "fmsubs", "fnmadd", "fnmadds", "fnmsub", "fnmsubs"}
FP_TAINT_ADD = {"fadd", "fadds", "fsub", "fsubs"}
FP_TAINT_COPY = {"fmr", "fneg", "fabs", "fnabs", "frsp", "fsqrt", "fsqrts", "fres", "frsqrte"}
FP_TAINT_CLEAN = {"fctid", "fctidz", "fctiw", "fctiwz", "fcfid", "mffs", "lfs", "lfsx", "lfsu", "lfsux", "lfd", "lfdx",
                  "lfdu", "lfdux"}
FP_NO_DEST = {"stfs", "stfsx", "stfsu", "stfsux", "stfd", "stfdx", "stfdu", "stfdux", "stfiwx", "fcmpu", "fcmpo",
              "mtfsf", "mtfsfi", "mtfsb0", "mtfsb1"}
FP_INSTRUCTION = re.compile(r"^\t// ([a-z][a-z0-9.]*)\s*(.*)$")


def contraction_safe(body: list[str]) -> bool:
    """SWITCH_FP_LEAF_LOCALS: True when no plain product reaches an addend on any path (see above). Vector
    floating point, and any floating-point instruction not listed above, make it False."""
    lines = body
    count = len(lines)
    labels = {}
    for i, line in enumerate(lines):
        m = LABEL.match(line)
        if m:
            labels[m.group(1)] = i
    predecessors = [[i - 1] if i > 0 else [] for i in range(count)]
    for i, line in enumerate(lines):
        if line.lstrip().startswith("//"):
            continue
        for target in GOTO.findall(line):
            if target not in labels:
                return False
            predecessors[labels[target]].append(i)

    # Per instruction: (destination, sources whose taint the destination takes, sources that must not be tainted).
    effects: list[tuple[str | None, tuple[str, ...], tuple[str, ...], bool] | None] = []
    for line in lines:
        m = FP_INSTRUCTION.match(line)
        if not m:
            effects.append(None)
            continue
        mnemonic = m.group(1).rstrip(".")
        operands = [o.strip() for o in m.group(2).split(",")] if m.group(2) else []
        fp = [o for o in operands if re.fullmatch(r"f\d+", o)]
        if mnemonic.startswith("v") and "fp" in mnemonic:
            return False
        if mnemonic in FP_TAINT_PRODUCT:
            effects.append((fp[0], (), (), True))
        elif mnemonic in FP_TAINT_FUSED:
            if len(fp) != 4:
                return False
            effects.append((fp[0], (), (fp[3],), False))
        elif mnemonic in FP_TAINT_ADD:
            if len(fp) != 3:
                return False
            effects.append((fp[0], (), (fp[1], fp[2]), False))
        elif mnemonic in FP_TAINT_COPY:
            effects.append((fp[0], tuple(fp[1:]), (), False))
        elif mnemonic in ("fsel",):
            effects.append((fp[0], tuple(fp[2:]), (), False))
        elif mnemonic in ("fdiv", "fdivs"):
            effects.append((fp[0], tuple(fp[1:]), (), False))
        elif mnemonic in FP_TAINT_CLEAN:
            effects.append((fp[0] if fp else None, (), (), False))
        elif mnemonic in FP_NO_DEST:
            effects.append(None)
        elif mnemonic.startswith("f") or mnemonic.startswith("lf") or mnemonic.startswith("stf"):
            return False
        else:
            effects.append(None)

    empty: frozenset = frozenset()
    taint_in = [empty] * count
    taint_out = [empty] * count
    changed = True
    while changed:
        changed = False
        for i in range(count):
            incoming = empty.union(*(taint_out[p] for p in predecessors[i])) if predecessors[i] else empty
            effect = effects[i]
            outgoing = incoming
            if effect is not None:
                destination, sources, addends, product = effect
                if any(a in incoming for a in addends):
                    return False
                if destination is not None:
                    tainted = product or any(s in incoming for s in sources)
                    outgoing = (incoming | {destination}) if tainted else (incoming - {destination})
            if incoming != taint_in[i] or outgoing != taint_out[i]:
                taint_in[i] = incoming
                taint_out[i] = outgoing
                changed = True
    return True


def mid_asm_hook_names() -> list[str]:
    return re.findall(r'^name\s*=\s*"(\w+)"', TOML.read_text(encoding="utf-8"), re.MULTILINE)


# ------------------------------------------------------------------------------------------------ leaf locals

def written_registers(body: list[str], registers: set[str]) -> set[str]:
    written = set()
    for line in body:
        if line.lstrip().startswith("//"):
            continue
        for m in CONTEXT_REGISTER.finditer(line):
            register = m.group(1)
            start = m.start()
            before = line[max(0, start - 2):start]
            plain = READ_PATTERNS[kind(register)].match(line, start)
            if not plain or before.endswith("*)") or before.endswith("&") or before.endswith("&("):
                written.add(register)
    return written & registers


def register_order(register: str) -> tuple[int, int]:
    k = kind(register)
    rank = ["r", "f", "v", "cr", "ctr", "xer", "reserved", "lr", "msr"].index(k)
    digits = re.sub(r"\D", "", register)
    return (rank, int(digits) if digits else 0)


def localized_name(registers: set[str]):
    """CONTEXT_REGISTER.sub replacement: the local of a localized register, the context field of any other."""
    return lambda m: m.group(1) if m.group(1) in registers else m.group(0)


def localize_leaf(body: list[str], hooks: list[str], fpscr_locals: bool = False, force_fpscr: bool = False,
                  kinds: frozenset | None = None) -> list[str] | None:
    code_lines = [line for line in body if not line.lstrip().startswith("//")]
    text = "\n".join(code_lines)
    if CALL.search(text) or "PPC_CALL_INDIRECT_FUNC" in text or "PPC_CALL_FUNC" in text:
        return None
    if "setjmp" in text or "longjmp" in text:
        return None
    if any(re.search(rf"(?<![\w]){hook}\(", text) for hook in hooks):
        return None
    rest = CONTEXT_REGISTER.sub("", text).replace("ctx.fpscr", "")
    if re.search(r"\bctx\b", rest):
        return None

    registers = set(CONTEXT_REGISTER.findall(text))
    if not registers or not registers <= LOCALIZABLE:
        return None
    # SWITCH_FP_INT_LOCALS: only these kinds; the others stay context fields.
    if kinds is not None:
        registers = {r for r in registers if kind(r) in kinds}
        if not registers:
            return None
    # The local names must be free: no identifier of that name other than the context's field.
    for register in registers:
        if re.search(rf"(?<![\w.]){register}\b", text):
            return None

    # SWITCH_FPSCR_LOCALS: the mode word as a local too, in functions without floating-point products.
    fpscr = (fpscr_locals and kinds is None and "ctx.fpscr" in text and (force_fpscr or not has_fp_products(body)) and
             not re.search(r"(?<![\w.])fpscr\b", text))
    rename = localized_name(registers)

    used = sorted(registers, key=register_order)
    written = sorted(written_registers(body, registers), key=register_order)
    write_back = " ".join([f"ctx.{r} = {r};" for r in written] + (["ctx.fpscr = fpscr;"] if fpscr else []))

    if body[0].strip() != "PPC_FUNC_PROLOGUE();":
        return None
    out = [body[0], "\t// switch-codegen-pass: leaf locals" + (" (and the FPSCR's mode word)" if fpscr else "") +
           (" (integer registers)" if kinds is not None else "")]
    out += [f"\tdecltype(ctx.{r}) {r} = ctx.{r};" for r in used]
    if fpscr:
        out.append("\tPPCFPSCRRegister fpscr = ctx.fpscr;")
    for line in body[1:]:
        if line.lstrip().startswith("//"):
            out.append(line)
            continue
        line = CONTEXT_REGISTER.sub(rename, line)
        if fpscr:
            line = line.replace("ctx.fpscr", "fpscr")
        if write_back:
            line = re.sub(r"(?<![\w])return;", "{ " + write_back + " return; }", line)
        out.append(line)
    if write_back:
        out.append("\t" + write_back)
    return out


# ------------------------------------------------------------------------------------------------ call locals

def collect_helpers(files: list[Path]) -> dict[str, list[str]]:
    """The statements of XenonRecomp's __savegprlr_N/__restgprlr_N, as it wrote them (before this pass): loads and
    stores of r14-r31 and r12 at fixed offsets from r1, then the helper's return. A helper with anything else in it
    is left out, and calls of it stay calls."""
    helpers = {}
    plain = re.compile(r"^\t(?:PPC_STORE_U(?:32|64)\(ctx\.r1\.u32 \+ -\d+, ctx\.r\d+\.u(?:32|64)\);|"
                       r"ctx\.r\d+\.u64 = PPC_LOAD_U(?:32|64)\(ctx\.r1\.u32 \+ -\d+\);|ctx\.lr = ctx\.r12\.u64;)$")
    for path in files:
        lines = path.read_text(encoding="utf-8").split("\n")
        for i, line in enumerate(lines):
            m = HELPER_DEFINITION.match(line)
            if not m:
                continue
            end = lines.index("}", i)
            body = [b for b in lines[i + 1:end] if not b.lstrip().startswith("//")]
            if not body or body[0].strip() != "PPC_FUNC_PROLOGUE();" or body[-1] != "\treturn;":
                continue
            statements = body[1:-1]
            if statements and all(plain.match(s) for s in statements):
                helpers[m.group(1)] = statements
    return helpers


def localize_calls(body: list[str], hooks: list[str], helpers: dict[str, list[str]],
                   fpscr_locals: bool, single_mode: bool = False, allow_fp: bool = False,
                   kinds: frozenset | None = None) -> list[str] | None:
    """Locals between calls (SWITCH_CALL_LOCALS): see the docstring. None when the function does not qualify.
    single_mode (SWITCH_FP_SINGLE_MODE): a floating-point function compiled without contraction, see FP_SINGLE_MODE."""
    if body[0].strip() != "PPC_FUNC_PROLOGUE();":
        return None
    if has_fp_products(body) and not single_mode and not allow_fp and kinds is None:
        return None
    code_lines = [line for line in body if not line.lstrip().startswith("//")]
    text = "\n".join(code_lines)
    if "setjmp" in text or "longjmp" in text:
        return None
    if any(re.search(rf"(?<![\w]){hook}\(", text) for hook in hooks):
        return None

    # Every use of the context is a register field, the FPSCR or a call statement of its own.
    calls = 0
    rest_lines = []
    for line in code_lines:
        m = CALL_STATEMENT.match(line)
        if m:
            calls += 1
            if "ctx" in CONTEXT_REGISTER.sub("", m.group(2)).replace("(ctx, base)", ""):
                return None
            continue
        if CALL_ANYWHERE.search(line):
            return None  # a call that is not a statement of its own
        rest_lines.append(line)
    if calls == 0:
        return None
    rest = CONTEXT_REGISTER.sub("", "\n".join(rest_lines)).replace("ctx.fpscr", "")
    if re.search(r"\bctx\b", rest):
        return None

    registers = set(CONTEXT_REGISTER.findall(text))
    for line in code_lines:
        m = CALL_STATEMENT.match(line)
        name = m.group(2)[:-len("(ctx, base)")] if m and m.group(2).endswith("(ctx, base)") else None
        if name in helpers:
            for statement in helpers[name]:
                registers |= set(CONTEXT_REGISTER.findall(statement))
    if not registers or not registers <= LOCALIZABLE:
        return None
    if kinds is not None:
        registers = {r for r in registers if kind(r) in kinds}
        if not registers:
            return None
    for register in registers:
        if re.search(rf"(?<![\w.]){register}\b", text):
            return None

    fpscr = fpscr_locals and kinds is None and "ctx.fpscr" in text and not re.search(r"(?<![\w.])fpscr\b", text)
    rename = localized_name(registers)

    used = sorted(registers, key=register_order)

    # Which registers may differ from the context at each line ("dirty"): written since the function's entry or since
    # the last call, after which every local is read again from the context. A call stores back only those, and so
    # does a return. Conservatively every line also falls through to the next one, and a line with a goto
    # (conditional or not) may also go to its label: a register written on any path in counts.
    lines = body[1:]
    count = len(lines)
    labels = {}
    for i, line in enumerate(lines):
        m = LABEL.match(line)
        if m:
            labels[m.group(1)] = i
    predecessors = [[i - 1] if i > 0 else [] for i in range(count)]
    for i, line in enumerate(lines):
        if line.lstrip().startswith("//"):
            continue
        for target in GOTO.findall(line):
            if target not in labels:
                return None
            predecessors[labels[target]].append(i)
    writes: list[frozenset] = []
    sync: list[bool] = []
    helper_call: list[str | None] = []
    for line in lines:
        if line.lstrip().startswith("//"):
            writes.append(frozenset())
            sync.append(False)
            helper_call.append(None)
            continue
        m = CALL_STATEMENT.match(line)
        name = m.group(2)[:-len("(ctx, base)")] if m and m.group(2).endswith("(ctx, base)") else None
        if name in helpers:
            writes.append(frozenset(helper_written_by(helpers[name]) & registers))
            sync.append(False)
            helper_call.append(name)
            continue
        if m:
            writes.append(frozenset())
            sync.append(True)
            helper_call.append(None)
            continue
        w = set(written_registers([line], registers))
        if fpscr and "ctx.fpscr" in line:
            w.add("fpscr")
        writes.append(frozenset(w))
        sync.append(False)
        helper_call.append(None)

    empty: frozenset = frozenset()
    dirty_in = [empty] * count
    dirty_out = [empty] * count
    changed = True
    while changed:
        changed = False
        for i in range(count):
            incoming = empty.union(*(dirty_out[p] for p in predecessors[i])) if predecessors[i] else empty
            outgoing = empty if sync[i] else incoming | writes[i]
            if incoming != dirty_in[i] or outgoing != dirty_out[i]:
                dirty_in[i] = incoming
                dirty_out[i] = outgoing
                changed = True

    def store_back(dirty: frozenset) -> str:
        names = sorted((r for r in dirty if r != "fpscr"), key=register_order)
        return " ".join([f"ctx.{r} = {r};" for r in names] + (["ctx.fpscr = fpscr;"] if "fpscr" in dirty else []))

    reload = " ".join([f"{r} = ctx.{r};" for r in used] + (["fpscr = ctx.fpscr;"] if fpscr else []))

    out = [body[0], "\t// switch-codegen-pass: call locals" + (" (and the FPSCR's mode word)" if fpscr else "") +
           (" (integer registers)" if kinds is not None else "")]
    out += [f"\tdecltype(ctx.{r}) {r} = ctx.{r};" for r in used]
    if fpscr:
        out.append("\tPPCFPSCRRegister fpscr = ctx.fpscr;")
    for i, line in enumerate(lines):
        if line.lstrip().startswith("//"):
            out.append(line)
            continue
        if helper_call[i] is not None:
            # The helper's own statements on the locals, in its order (its return is the end of the call).
            indent = line[:len(line) - len(line.lstrip("\t"))]
            out.append(f"{indent}// switch-codegen-pass: {helper_call[i]} inlined")
            out += [indent + CONTEXT_REGISTER.sub(rename, s.lstrip("\t")) for s in helpers[helper_call[i]]]
            continue
        if sync[i]:
            m = CALL_STATEMENT.match(line)
            indent, call = m.group(1), CONTEXT_REGISTER.sub(rename, m.group(2))
            before = store_back(dirty_in[i])
            out.append(f"{indent}{{ {before + ' ' if before else ''}{call}; {reload} }}")
            continue
        line = CONTEXT_REGISTER.sub(rename, line)
        if fpscr:
            line = line.replace("ctx.fpscr", "fpscr")
        if single_mode:
            line = single_mode_line(line)
        at_return = store_back(dirty_in[i] | writes[i])
        if at_return:
            line = re.sub(r"(?<![\w])return;", "{ " + at_return + " return; }", line)
        out.append(line)
    if count and store_back(dirty_out[count - 1]):
        out.append("\t" + store_back(dirty_out[count - 1]))
    return out


# SWITCH_FP_SINGLE_MODE (perf7): profile-hot floating-point functions with scalar and vector code interleaved, compiled
# with their registers in locals (as SWITCH_CALL_LOCALS) and without contraction, so their results follow their
# statements alone: each guest fused operation is spelled fused (the double-precision fma of PPC_FMA/PPC_FMS, the per-lane
# single-precision fma of vmaddfp/vnmsubfp), every other operation rounds by itself. That is what GCC made of them before
# (checked against the perf6 ELF with tools/switch-fp-check.py: as many fused instructions as fused guest operations, so
# nothing else was fused; checked again on each build). Their vector arithmetic runs in the scalar code's mode (FPCR.FZ
# clear) instead of switching to the VMX flush-to-zero mode around it and back, the switches that made up the
# function's hottest lines: the same results for every operand and result that is not a denormal (below 2^-126, which
# collision tests of world coordinates do not produce); a denormal one is no longer flushed to zero. The scalar code's
# mode checks stay (after a call the mode may have changed), and find the mode set.
FP_SINGLE_MODE = {
    "828844E8",  # Havok's MOPP query: the hottest game-thread function of the perf6 profile (5 %)
}

FP_SINGLE_MODE_PROLOGUE = """// switch-codegen-pass: SWITCH_FP_SINGLE_MODE (the function below): no contraction, the guest's fused operations fused.
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#pragma push_macro("PPC_FMA")
#pragma push_macro("PPC_FMS")
#undef PPC_FMA
#undef PPC_FMS
#define PPC_FMA(a, b, c) __builtin_fma(a, b, c)
#define PPC_FMS(a, b, c) __builtin_fma(a, b, -(c))
#ifndef SWITCH_FP_SINGLE_MODE_HELPERS
#define SWITCH_FP_SINGLE_MODE_HELPERS
static inline simde__m128 switch_vmaddfp_fused(simde__m128 a, simde__m128 b, simde__m128 c)
{
    return simde__m128_from_neon_f32(vfmaq_f32(simde__m128_to_neon_f32(c), simde__m128_to_neon_f32(a), simde__m128_to_neon_f32(b)));
}
static inline simde__m128 switch_vnmsubfp_fused(simde__m128 a, simde__m128 b, simde__m128 c)
{
    const simde__m128 d = simde__m128_from_neon_f32(vfmaq_f32(vnegq_f32(simde__m128_to_neon_f32(c)), simde__m128_to_neon_f32(a), simde__m128_to_neon_f32(b)));
    return simde_mm_xor_ps(d, simde_mm_castsi128_ps(simde_mm_set1_epi32(int(0x80000000))));
}
#endif"""

FP_SINGLE_MODE_EPILOGUE = """#pragma pop_macro("PPC_FMS")
#pragma pop_macro("PPC_FMA")
#pragma GCC pop_options"""


def no_contract_localized(body: list[str], hooks: list[str], helpers: dict[str, list[str]]) -> list[str] | None:
    """SWITCH_FP_NO_CONTRACT: the function in locals (as a leaf, or between calls), the FPSCR's mode word too, its
    vector fused operations explicit; emitted between FP_SINGLE_MODE_PROLOGUE and _EPILOGUE (no contraction, PPC_FMA and
    PPC_FMS as fma). Its mode switches stay where they are."""
    localized = localize_leaf(body, hooks, True, force_fpscr=True)
    if localized is None:
        localized = localize_calls(body, hooks, helpers, True, allow_fp=True)
    if localized is None:
        return None
    return [b if b.lstrip().startswith("//") else
            b.replace("simde_mm_vmaddfp(", "switch_vmaddfp_fused(").replace("simde_mm_vnmsubfp(", "switch_vnmsubfp_fused(")
            for b in localized]


def single_mode_line(line: str) -> str:
    """A statement of a SWITCH_FP_SINGLE_MODE function: no switch to the VMX mode, the switches back conditional (they
    find the mode set), the vector fused operations explicit."""
    line = re.sub(r"\bfpscr\.enableFlushMode(?:Unconditional)?\(\);", "", line)
    line = line.replace("fpscr.disableFlushModeUnconditional();", "fpscr.disableFlushMode();")
    line = line.replace("simde_mm_vmaddfp(", "switch_vmaddfp_fused(").replace("simde_mm_vnmsubfp(", "switch_vnmsubfp_fused(")
    return line


def helper_written_by(statements: list[str]) -> set[str]:
    """The registers a save/restore helper's statements assign."""
    written = set()
    for statement in statements:
        target = re.match(r"^\tctx\.(\w+)(?:\.u64)? = ", statement)
        if target:
            written.add(target.group(1))
    return written


# SWITCH_PREFETCH_HINTS (perf7): host prefetches of guest objects a function will reach later, inserted right after a
# statement of it. Each entry: the function, the statement (as SWITCH_LEAF_LOCALS/SWITCH_CALL_LOCALS leave it, or
# as XenonRecomp wrote it), and the 32-bit pointer fields (register, offset) whose targets are prefetched. The fields
# are read as guest loads (no side effect: plain memory) and lie inside the object between offsets the function itself
# reads, so they cannot fault where it would not; a prefetch changes no state.
PREFETCH_HINTS = {
    # The scene tree's dirty-flag walk (recursive: the node's first child at 28, its next sibling at 20, read after
    # the whole subtree): its hottest line is the first touch of each node (perf5/perf6 profiles). The node is read at
    # 8 and 36 on entry.
    "82594D30": (("\tr31.u64 = r3.u64;", "\tctx.r31.u64 = ctx.r3.u64;"), (("r31", 28), ("r31", 20))),
}


def prefetch_hints(address: str, body: list[str]) -> tuple[list[str], int]:
    hint = PREFETCH_HINTS.get(address)
    if hint is None:
        return body, 0
    statements, fields = hint
    for i, line in enumerate(body):
        if line in statements:
            local = line.startswith("\tr")
            inserted = []
            for register, offset in fields:
                name = register if local else f"ctx.{register}"
                inserted.append(f"\t__builtin_prefetch(base + PPC_LOAD_U32({name}.u32 + {offset})); // switch-codegen-pass: prefetch hint")
            return body[:i + 1] + inserted + body[i + 1:], 1
    return body, 0


def read_address_list(path: Path) -> tuple[set[str], bool]:
    """Addresses of a list file (one per line, 0x82... or sub_82..., '#' comments); a line 'all' means every one."""
    addresses = set()
    everything = False
    if not path.exists():
        return addresses, everything
    for raw in path.read_text(encoding="utf-8").split("\n"):
        entry = raw.split("#", 1)[0].strip()
        if not entry:
            continue
        if entry.lower() == "all":
            everything = True
            continue
        m = re.fullmatch(r"(?:0x|sub_)?([0-9A-Fa-f]{8})", entry)
        if m:
            addresses.add(m.group(1).upper())
    return addresses, everything


# ------------------------------------------------------------------------------------------------ fixed-size copies

MEMMOVE_HOOKS = {"826DF680", "826DE940"}   # GuestMemmove (misc_impl.cpp)
MEMSET_HOOKS = {"826DFD40"}                # memset
MAX_INLINE_MOVE = 64                       # GCC copies these inline (loads first, then stores), never a library call
MAX_INLINE_SET = 256
GUEST_CALL = re.compile(r"^\t(?:__imp__)?sub_([0-9A-F]{8})\(ctx, base\);$")
FIXED_SIZE = re.compile(r"^\tctx\.r5\.s64 = (\d+);$")
STRAIGHT = re.compile(r"^\t[^;{}:?]*;$")
CONTROL = re.compile(r"\b(?:goto|return|if|else|while|for|switch|sub_[0-9A-F]{8}|__imp__sub_[0-9A-F]{8}|PPC_CALL\w*|setjmp|longjmp)\b")


def inline_fixed_copies(body: list[str]) -> tuple[list[str], int]:
    out = list(body)
    count = 0
    for i, line in enumerate(body):
        m = GUEST_CALL.match(line)
        if not m or m.group(1) not in MEMMOVE_HOOKS | MEMSET_HOOKS:
            continue
        size = None
        for j in range(i - 1, -1, -1):
            prev = body[j]
            if prev.lstrip().startswith("//"):
                continue
            s = FIXED_SIZE.match(prev)
            if s:
                size = int(s.group(1))
                break
            # Anything but a plain statement that leaves r5 alone (a label, a branch, a call, a statement that hands
            # the whole context to something) ends the search without a size.
            if not STRAIGHT.match(prev) or "r5" in prev or CONTROL.search(prev):
                break
            if re.search(r"\bctx\b", CONTEXT_REGISTER.sub("", prev).replace("ctx.fpscr", "")):
                break
        if size is None or size == 0:
            continue
        if m.group(1) in MEMMOVE_HOOKS:
            if size > MAX_INLINE_MOVE:
                continue
            call = f"__builtin_memmove(base + ctx.r3.u32, base + ctx.r4.u32, {size});"
        else:
            if size > MAX_INLINE_SET:
                continue
            call = f"__builtin_memset(base + ctx.r3.u32, ctx.r4.s32, {size});"
        out[i] = f"\t{call} ctx.r3.u64 = ctx.r3.u32;"
        count += 1
    return out, count


# ------------------------------------------------------------------------------------------------ wide D form

LOAD_D = re.compile(r"PPC_LOAD_U(8|16|32|64)\(((?:ctx\.)?r(\d+))\.u32 \+ (-?\d+)\)")
STORE_D = re.compile(r"PPC_STORE_U(8|16|32|64)\(((?:ctx\.)?r(\d+))\.u32 \+ (-?\d+), ")


def wide_ok(register: str, displacement: int) -> bool:
    return 0 <= displacement <= 4095 or register == "1"


def wide_dform(line: str) -> str:
    def load(m: re.Match) -> str:
        if not wide_ok(m.group(3), int(m.group(4))):
            return m.group(0)
        return f"PPC_LOAD_U{m.group(1)}_D({m.group(2)}.u32, {m.group(4)})"

    def store(m: re.Match) -> str:
        if not wide_ok(m.group(3), int(m.group(4))):
            return m.group(0)
        return f"PPC_STORE_U{m.group(1)}_D({m.group(2)}.u32, {m.group(4)}, "

    return STORE_D.sub(store, LOAD_D.sub(load, line))


# ------------------------------------------------------------------------------------------------ vector tables

# VectorMaskL's first row, the full byte reversal (PPC_VECTOR_REVERSE in ppc_context.h).
FULL_REVERSAL = "simde_mm_load_si128((simde__m128i*)VectorMaskL)"
TABLE_INDEX = ("simde_mm_load_si128((simde__m128i*)VectorMaskL)", "simde_mm_load_si128((simde__m128i*)&VectorMaskL[",
               "simde_mm_load_si128((simde__m128i*)VectorMaskR)", "simde_mm_load_si128((simde__m128i*)&VectorMaskR[")


def vector_tables(line: str) -> str:
    out = []
    i = 0
    name = "simde_mm_shuffle_epi8("
    while True:
        j = line.find(name, i)
        if j < 0:
            out.append(line[i:])
            return "".join(out)
        # The argument list, and its top-level comma.
        depth, k, comma = 1, j + len(name), -1
        while k < len(line) and depth > 0:
            c = line[k]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            elif c == "," and depth == 1 and comma < 0:
                comma = k
            k += 1
        second = line[comma + 1:k - 1].strip() if comma >= 0 else ""
        out.append(line[i:j])
        if second == FULL_REVERSAL:
            # PPC_VECTOR_REVERSE(first): the first argument stays, the second (the table row) goes.
            out.append("PPC_VECTOR_REVERSE(" + vector_tables(line[j + len(name):comma]) + ")")
            i = k
            continue
        out.append("PPC_VECTOR_TABLE(" if any(second.startswith(t) for t in TABLE_INDEX) else name)
        i = j + len(name)


# ------------------------------------------------------------------------------------------------ main

def main() -> None:
    options = {
        "SWITCH_LEAF_LOCALS": option("SWITCH_LEAF_LOCALS"),
        "SWITCH_WIDE_DFORM": option("SWITCH_WIDE_DFORM"),
        "SWITCH_CONST_VMX_TABLES": option("SWITCH_CONST_VMX_TABLES"),
        "SWITCH_INLINE_FP_COMPARE": option("SWITCH_INLINE_FP_COMPARE"),
        "SWITCH_INLINE_MEMCPY": option("SWITCH_INLINE_MEMCPY"),
        "SWITCH_NARROW_BARRIER": option("SWITCH_NARROW_BARRIER"),
        "SWITCH_CALL_LOCALS": option("SWITCH_CALL_LOCALS"),
        "SWITCH_FPSCR_LOCALS": option("SWITCH_FPSCR_LOCALS"),
        "SWITCH_PREFETCH_HINTS": option("SWITCH_PREFETCH_HINTS"),
        "SWITCH_FP_LEAF_LOCALS": option("SWITCH_FP_LEAF_LOCALS"),
        "SWITCH_FP_SINGLE_MODE": option("SWITCH_FP_SINGLE_MODE"),
        "SWITCH_FP_NO_CONTRACT": option("SWITCH_FP_NO_CONTRACT"),
        "SWITCH_FP_INT_LOCALS": option("SWITCH_FP_INT_LOCALS"),
    }
    summary = " ".join(f"{k}={int(v)}" for k, v in options.items())
    if STAMP.exists():
        applied = STAMP.read_text(encoding="utf-8").strip()
        if applied != summary:
            sys.exit(f"switch-codegen-pass: ppc/ already has '{applied}', not '{summary}'; regenerate it")
        print(f"switch-codegen-pass: already applied ({summary})")
        return
    if not any(options.values()):
        STAMP.write_text(summary + "\n", encoding="utf-8")
        print("switch-codegen-pass: nothing to apply")
        return

    hooks = mid_asm_hook_names()
    files = sorted(PPC_DIR.glob("ppc_recomp.*.cpp"), key=lambda p: int(p.name.split(".")[1]))
    if not files:
        sys.exit("switch-codegen-pass: no generated sources")

    # SWITCH_CALL_LOCALS: the helpers as XenonRecomp wrote them (read before any file is rewritten) and the functions
    # it applies to.
    helpers = (collect_helpers(files) if options["SWITCH_CALL_LOCALS"] or options["SWITCH_FP_SINGLE_MODE"] or
               options["SWITCH_FP_NO_CONTRACT"] else {})
    no_contract_set, _ = read_address_list(NO_CONTRACT_LIST) if options["SWITCH_FP_NO_CONTRACT"] else (set(), False)
    call_set, call_everything = read_address_list(CALL_LOCALS_LIST) if options["SWITCH_CALL_LOCALS"] else (set(), False)
    if options["SWITCH_CALL_LOCALS"] and not call_set and not call_everything:
        print(f"switch-codegen-pass: SWITCH_CALL_LOCALS=1 but {CALL_LOCALS_LIST} lists no function")

    leaves = wide = tables = sensitive = copies = callers = hints = fp_leaves = single_modes = no_contracts = 0
    fp_int_leaves = fp_int_callers = 0
    for path in files:
        original = path.read_text(encoding="utf-8")
        lines = original.split("\n")
        out = []
        i = 0
        while i < len(lines):
            line = lines[i]
            m = DEFINITION.match(line)
            if not m:
                out.append(line)
                i += 1
                continue
            end = lines.index("}", i)
            body = lines[i + 1:end]
            address = m.group(1)
            no_contract = options["SWITCH_FP_NO_CONTRACT"] and address is not None and address in no_contract_set
            listed_caller = address is not None and (call_everything or address in call_set)
            wrapped = False
            int_localized = False
            if floating_point_sensitive(body):
                if options["SWITCH_NARROW_BARRIER"]:
                    body = [b.replace("PPC_LOOP_BARRIER()", "PPC_LOOP_BARRIER_FULL()") if not b.lstrip().startswith("//") else b
                            for b in body]
                sensitive += 1
                localized = None
                if options["SWITCH_FP_SINGLE_MODE"] and address in FP_SINGLE_MODE:
                    localized = localize_calls(body, hooks, helpers, True, single_mode=True)
                    if localized is not None:
                        single_modes += 1
                if localized is None and no_contract:
                    localized = no_contract_localized(body, hooks, helpers)
                    if localized is not None:
                        no_contracts += 1
                if localized is not None:
                    body = localized
                    wrapped = True
                elif options["SWITCH_FP_LEAF_LOCALS"] and options["SWITCH_LEAF_LOCALS"] and contraction_safe(body):
                    localized = localize_leaf(body, hooks)
                    if localized is not None:
                        body = localized
                        fp_leaves += 1
                # SWITCH_FP_INT_LOCALS: the integer registers only, the floating-point ones left in the context.
                if localized is None and options["SWITCH_FP_INT_LOCALS"] and listed_caller:
                    if options["SWITCH_LEAF_LOCALS"]:
                        localized = localize_leaf(body, hooks, kinds=INTEGER_KINDS)
                        if localized is not None:
                            fp_int_leaves += 1
                    if localized is None and options["SWITCH_CALL_LOCALS"] and listed_caller:
                        localized = localize_calls(body, hooks, helpers, False, kinds=INTEGER_KINDS)
                        if localized is not None:
                            fp_int_callers += 1
                    if localized is not None:
                        body = localized
                        int_localized = True
            else:
                if options["SWITCH_INLINE_MEMCPY"]:
                    body, n = inline_fixed_copies(body)
                    copies += n
                localized = None
                if options["SWITCH_LEAF_LOCALS"]:
                    localized = localize_leaf(body, hooks, options["SWITCH_FPSCR_LOCALS"])
                    if localized is not None:
                        body = localized
                        leaves += 1
                if localized is None and options["SWITCH_CALL_LOCALS"] and listed_caller:
                    localized = localize_calls(body, hooks, helpers, options["SWITCH_FPSCR_LOCALS"])
                    if localized is not None:
                        body = localized
                        callers += 1
                # A calling function with single-precision products (call locals refused it) that is exact without
                # contraction.
                if localized is None and no_contract:
                    localized = no_contract_localized(body, hooks, helpers)
                    if localized is not None:
                        body = localized
                        wrapped = True
                        no_contracts += 1
                # SWITCH_FP_INT_LOCALS: a calling function with single-precision products that is not exact without
                # contraction: its integer registers between calls, the floating-point ones left in the context.
                if (localized is None and options["SWITCH_FP_INT_LOCALS"] and options["SWITCH_CALL_LOCALS"] and
                        listed_caller):
                    localized = localize_calls(body, hooks, helpers, False, kinds=INTEGER_KINDS)
                    if localized is not None:
                        body = localized
                        fp_int_callers += 1
                if options["SWITCH_PREFETCH_HINTS"] and address is not None:
                    body, n = prefetch_hints(address, body)
                    hints += n
            # Addresses and byte shuffles (no arithmetic in them): every function but the floating-point ones left as
            # XenonRecomp wrote them.
            if wrapped or int_localized or not floating_point_sensitive(body):
                if options["SWITCH_WIDE_DFORM"]:
                    new = [wide_dform(b) if not b.lstrip().startswith("//") else b for b in body]
                    wide += sum(a != b for a, b in zip(body, new))
                    body = new
                if options["SWITCH_CONST_VMX_TABLES"]:
                    new = [vector_tables(b) if not b.lstrip().startswith("//") else b for b in body]
                    tables += sum(a != b for a, b in zip(body, new))
                    body = new
            if wrapped:
                out.append(FP_SINGLE_MODE_PROLOGUE)
                out.append(line)
                out += body
                out.append("}")
                out.append(FP_SINGLE_MODE_EPILOGUE)
                i = end + 1
                continue
            out.append(line)
            out += body
            out.append("}")
            i = end + 1
        text = "\n".join(out)
        if text != original:
            path.write_text(text, encoding="utf-8", newline="\n")

    defines = []
    if options["SWITCH_INLINE_FP_COMPARE"]:
        defines.append("#define PPC_CONFIG_INLINE_FP_COMPARE")
    if options["SWITCH_NARROW_BARRIER"]:
        defines.append("#define PPC_CONFIG_NARROW_BARRIER")
    if defines:
        config = PPC_DIR / "ppc_config.h"
        text = config.read_text(encoding="utf-8")
        anchor = "#ifdef PPC_INCLUDE_DETAIL"
        if anchor not in text:
            sys.exit("switch-codegen-pass: unexpected ppc_config.h layout")
        text = text.replace(anchor, "// switch-codegen-pass\n" + "\n".join(defines) + "\n\n" + anchor, 1)
        config.write_text(text, encoding="utf-8", newline="\n")

    STAMP.write_text(summary + "\n", encoding="utf-8")
    print(f"switch-codegen-pass: {summary}: {copies} fixed-size memmove/memset calls inlined, {leaves} leaf functions "
          f"with local registers, {callers} calling functions with local registers between calls ({len(helpers)} "
          f"save/restore helpers inlined where they are called), {hints} prefetch hints, {fp_leaves} floating-point "
          f"leaf functions with local registers (no product reaches a sum), {single_modes} floating-point functions in one "
          f"mode without contraction, {no_contracts} more without contraction (SWITCH_FP_NO_CONTRACT), {fp_int_leaves} "
          f"floating-point leaf and {fp_int_callers} calling functions with integer locals (SWITCH_FP_INT_LOCALS), "
          f"{wide} lines with wide D-form accesses, {tables} lines with table "
          f"shuffles as TBL; {sensitive} functions with double-precision or vector floating-point arithmetic left as "
          f"they were")


if __name__ == "__main__":
    main()
