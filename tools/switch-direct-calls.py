#!/usr/bin/env python3
"""Let GCC inline calls between recompiled functions (Switch build).

XenonRecomp (built with Clang, so with XENON_RECOMP_USE_ALIAS) emits every guest function as

    __attribute__((alias("__imp__sub_X"))) PPC_WEAK_FUNC(sub_X);
    PPC_FUNC_IMPL(__imp__sub_X) { ... }

and every call site as `sub_X(ctx, base);`. PPC_WEAK_FUNC is `weak, noinline` so that a hook in the app
(GUEST_FUNCTION_HOOK, GUEST_FUNCTION_STUB, PPC_FUNC overrides, ...) can replace sub_X at link time. The
price is that no call between recompiled functions can ever be inlined, not even inside one file and not
with LTO.

`apply` rewrites `sub_X(ctx, base);` into `__imp__sub_X(ctx, base);` for every X that has no hook. That is
exactly the same function body, only not through the weak alias. Hooked functions keep going through
sub_X, and the indirect-call table (ppc_func_mapping.cpp) is never touched, so indirect calls still reach
hooks. Mid-asm hooks run inside the __imp__ bodies and are unaffected.

An address counts as hooked if it appears anywhere in the hook sources (any 0x82xxxxxx/0x83xxxxxx literal,
sub_XXXXXXXX or __imp__sub_XXXXXXXX name, Marathon.toml/switch_table.toml entry): more than strictly
needed, and safe. Hooks added by anyone are picked up at the next run, which is why the build runs it
every time.

The pass is idempotent: it undoes its previous run (from ppc/ppc_direct_calls.txt, a name the ppc/
.gitignore already ignores) in memory and then applies the current hook list, writing only the files whose
text changes, so an unchanged run leaves every file (and Ninja) alone.

Usage (from the repository root):
    python tools/switch-direct-calls.py apply --ppc MarathonRecompLib/ppc --hook-sources MarathonRecomp MarathonRecompLib/config
    python tools/switch-direct-calls.py undo --ppc MarathonRecompLib/ppc
    python tools/switch-direct-calls.py verify-elf --elf build/switch-app/MarathonRecomp/MarathonRecomp --nm aarch64-none-elf-nm

`verify-elf` checks the linked program: the hooks (sub_X symbols that are not the alias of their own
generated body) must exist, and none of them may be among the rewritten calls.

Profile-hot functions (MarathonRecompLib/config/hot_functions.txt, one hex address per line; since perf5 the 600
hottest integer-only functions of the perf4 CPU profile) are defined as PPC_HOT_FUNC_IMPL (ppc_context.h): GCC optimises them
harder and groups them in .text.hot. SWITCH_HOT_FUNCTIONS=0 or --no-hot leaves every definition plain.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PPC = ROOT / "MarathonRecompLib" / "ppc"
DEFAULT_HOOK_SOURCES = [ROOT / "MarathonRecomp", ROOT / "MarathonRecompLib" / "config"]
DEFAULT_HOT_LIST = ROOT / "MarathonRecompLib" / "config" / "hot_functions.txt"
MANIFEST_NAME = "ppc_direct_calls.txt"
SHARED_HEADER_NAME = "ppc_recomp_shared.h"

HOOK_SUFFIXES = {".cpp", ".cc", ".cxx", ".c", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".toml"}

# Guest code lives at 0x82160000-0x82B0ABB8 in this game; every 8-digit hex token starting with 82 or 83
# counts (also inside sub_/__imp__sub_ names and toml entries).
ADDRESS = re.compile(r"(?<![0-9A-Fa-f])(8[23][0-9A-Fa-f]{6})(?![0-9A-Fa-f])")
CALL = re.compile(r"(?<![\w])sub_([0-9A-F]{8})\(ctx, base\);")
DIRECT_CALL = re.compile(r"(?<![\w])__imp__sub_([0-9A-F]{8})\(ctx, base\);")
DEFINITION = re.compile(r"PPC_(?:HOT_)?FUNC_IMPL\(__imp__sub_([0-9A-F]{8})\)\s*\{")
PLAIN_DEFINITION = re.compile(r"^PPC_FUNC_IMPL\(__imp__sub_([0-9A-F]{8})\) \{$", re.MULTILINE)
HOT_DEFINITION = re.compile(r"^PPC_HOT_FUNC_IMPL\(__imp__sub_([0-9A-F]{8})\) \{$", re.MULTILINE)

DECLARATIONS_BEGIN = "// BEGIN switch-direct-calls.py declarations (generated, do not edit)"
DECLARATIONS_END = "// END switch-direct-calls.py declarations"


def generated_sources(ppc: Path) -> list[Path]:
    files = sorted(ppc.glob("ppc_recomp.*.cpp"), key=lambda p: int(p.name.split(".")[1]))
    if not files:
        sys.exit(f"switch-direct-calls: no generated sources in {ppc}; run XenonRecomp first.")
    return files


def hooked_addresses(sources: list[Path]) -> set[str]:
    hooked: set[str] = set()
    for base in sources:
        if base.is_file():
            paths = [base]
        elif base.is_dir():
            paths = [p for p in base.rglob("*") if p.is_file() and p.suffix.lower() in HOOK_SUFFIXES]
        else:
            sys.exit(f"switch-direct-calls: hook source {base} does not exist.")
        for path in paths:
            text = path.read_text(encoding="utf-8", errors="ignore")
            hooked.update(match.upper() for match in ADDRESS.findall(text))
    return hooked


def read_manifest(ppc: Path) -> set[str]:
    manifest = ppc / MANIFEST_NAME
    if not manifest.exists():
        return set()
    return {line.strip().upper() for line in manifest.read_text(encoding="utf-8").split() if line.strip()}


def read_text(path: Path) -> str:
    with open(path, "r", encoding="utf-8", newline="") as f:
        return f.read()


def write_if_changed(path: Path, text: str) -> bool:
    if path.exists() and read_text(path) == text:
        return False
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    return True


def strip_declarations(text: str, header: Path) -> str:
    start = text.find(DECLARATIONS_BEGIN)
    if start < 0:
        return text
    # apply() separates the block with one blank line; take it out as well.
    if text[max(0, start - 2):start] == "\n\n":
        start -= 1
    end = text.find(DECLARATIONS_END, start)
    if end < 0:
        sys.exit(f"switch-direct-calls: {header}: unterminated declaration block")
    end += len(DECLARATIONS_END)
    if end < len(text) and text[end] == "\n":
        end += 1
    return text[:start] + text[end:]


def restore_calls(text: str, previous: set[str]) -> tuple[str, int]:
    """The text with the calls rewritten by the previous run (the manifest) put back."""
    restored = 0

    def restore(match: re.Match) -> str:
        nonlocal restored
        if match.group(1) in previous:
            restored += 1
            return f"sub_{match.group(1)}(ctx, base);"
        return match.group(0)

    return DIRECT_CALL.sub(restore, text), restored


def read_hot_list(path: Path) -> set[str]:
    if not path.exists():
        return set()
    hot: set[str] = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        if line.lower().startswith("0x"):
            line = line[2:]
        if line.lower().startswith("sub_"):
            line = line[4:]
        hot.add(line.upper())
    return hot


def mark_hot(text: str, hot: set[str]) -> tuple[str, int]:
    """The text with exactly the functions of `hot` defined as PPC_HOT_FUNC_IMPL."""
    marked = 0

    def plain(match: re.Match) -> str:
        return f"PPC_FUNC_IMPL(__imp__sub_{match.group(1)}) {{"

    def maybe_hot(match: re.Match) -> str:
        nonlocal marked
        if match.group(1) in hot:
            marked += 1
            return f"PPC_HOT_FUNC_IMPL(__imp__sub_{match.group(1)}) {{"
        return match.group(0)

    text = HOT_DEFINITION.sub(plain, text)
    return (PLAIN_DEFINITION.sub(maybe_hot, text) if hot else text), marked


def undo(ppc: Path) -> None:
    files = generated_sources(ppc)
    previous = read_manifest(ppc)
    restored = 0
    for path in files:
        text, count = restore_calls(read_text(path), previous)
        text, _ = mark_hot(text, set())
        restored += count
        write_if_changed(path, text)

    header = ppc / SHARED_HEADER_NAME
    if header.exists():
        write_if_changed(header, strip_declarations(read_text(header), header))
    manifest = ppc / MANIFEST_NAME
    if manifest.exists():
        manifest.unlink()
    print(f"switch-direct-calls: {restored} calls restored.")


def apply(ppc: Path, hook_sources: list[Path], hot: set[str]) -> None:
    files = generated_sources(ppc)
    sample = read_text(files[0])
    if "__attribute__((alias(" not in sample:
        # Without alias mode the weak wrappers contain `__imp__sub_X(ctx, base);` themselves and this
        # pass could not tell them apart from rewritten call sites.
        sys.exit("switch-direct-calls: generated code does not use XENON_RECOMP_USE_ALIAS; refusing to rewrite.")

    # The previous run is undone in memory only: a file (or the shared header) whose final text is what it
    # already holds is not written, so Ninja does not recompile all the generated code after every build.
    previous = read_manifest(ppc)
    restored = 0

    hooked = hooked_addresses(hook_sources)
    defined: set[str] = set()
    texts: dict[Path, str] = {}
    for path in files:
        text, count = restore_calls(read_text(path), previous)
        restored += count
        texts[path] = text
        defined.update(DEFINITION.findall(text))

    rewritten_targets: set[str] = set()
    calls = 0
    kept = 0
    changed_files = 0
    hot_marked = 0
    for path, text in texts.items():
        def rewrite(match: re.Match) -> str:
            nonlocal calls, kept
            address = match.group(1)
            if address in hooked or address not in defined:
                kept += 1
                return match.group(0)
            calls += 1
            rewritten_targets.add(address)
            return f"__imp__sub_{address}(ctx, base);"

        text, marked = mark_hot(CALL.sub(rewrite, text), hot)
        hot_marked += marked
        if write_if_changed(path, text):
            changed_files += 1

    # The shared header only declares sub_X; the rewritten calls need __imp__sub_X declared.
    header_path = ppc / SHARED_HEADER_NAME
    header = strip_declarations(read_text(header_path), header_path).rstrip("\n") + "\n"
    if rewritten_targets:
        declarations = "".join(f"PPC_FUNC_IMPL(__imp__sub_{address});\n" for address in sorted(rewritten_targets))
        header += f"\n{DECLARATIONS_BEGIN}\n{declarations}{DECLARATIONS_END}\n"
    if write_if_changed(header_path, header):
        changed_files += 1
    write_if_changed(ppc / MANIFEST_NAME, "".join(f"{address}\n" for address in sorted(rewritten_targets)))

    print(f"switch-direct-calls: {calls} calls to {len(rewritten_targets)} functions made direct, "
          f"{kept} calls left through sub_X, {len(hooked)} hooked addresses respected, "
          f"{hot_marked} functions marked hot ({restored} previous rewrites undone first, {changed_files} files written).")


def verify_elf(elf: Path, nm: str, ppc: Path) -> None:
    """Every sub_X in the final ELF that is not its own generated body is a hook; none may have been rewritten."""
    rewritten = read_manifest(ppc)
    if not rewritten:
        sys.exit(f"switch-direct-calls: no manifest ({ppc / MANIFEST_NAME}); nothing to verify.")
    if not elf.exists():
        sys.exit(f"switch-direct-calls: {elf} does not exist.")
    output = subprocess.run([nm, str(elf)], check=True, capture_output=True, text=True).stdout
    # The guest functions are C++ functions, so their symbols are mangled (sub_82161B40 is
    # _Z12sub_82161B40R10PPCContextPh); plain names are accepted too. A generated sub_X is an alias of
    # __imp__sub_X, so it sits at the address of that body; a hook is a sub_X anywhere else. The binding
    # cannot tell them apart: the aliases are weak (W) in a normal link, but LTO turns the ones that prevail
    # into ordinary T symbols. __imp__sub_X may carry an LTO suffix (__imp__sub_X.lto_priv.0), or be gone
    # when a hook replaced it and nothing calls it.
    symbol = re.compile(r"^(?:_Z\d+)?sub_([0-9A-Fa-f]{8})(?![0-9A-Fa-f])")
    body = re.compile(r"^__imp__sub_([0-9A-Fa-f]{8})(?![0-9A-Fa-f])")
    functions: dict[str, set[int]] = {}
    bodies: dict[str, set[int]] = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) != 3 or parts[1] not in ("T", "t", "W", "w"):
            continue
        match = body.match(parts[2])
        if match:
            bodies.setdefault(match.group(1).upper(), set()).add(int(parts[0], 16))
            continue
        match = symbol.match(parts[2])
        if match:
            functions.setdefault(match.group(1).upper(), set()).add(int(parts[0], 16))
    strong = {address for address, places in functions.items() if places - bodies.get(address, set())}
    if not strong:
        sys.exit("switch-direct-calls: no hook (strong sub_X symbol) found in the ELF: the check would prove nothing.")
    bad = sorted(strong & rewritten)
    if bad:
        sys.exit("switch-direct-calls: hooked functions were called directly (hook bypassed): "
                 + ", ".join(f"sub_{address}" for address in bad[:20])
                 + (" ..." if len(bad) > 20 else ""))
    print(f"switch-direct-calls: OK, {len(strong)} hooks in the ELF, none of them bypassed "
          f"({len(rewritten)} functions called directly).")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)

    apply_parser = commands.add_parser("apply", help="rewrite the calls of unhooked functions (idempotent)")
    apply_parser.add_argument("--ppc", type=Path, default=DEFAULT_PPC, help="generated ppc/ folder")
    apply_parser.add_argument("--hook-sources", type=Path, nargs="+", default=DEFAULT_HOOK_SOURCES,
                              help="folders (or files) whose 0x82/0x83 addresses count as hooked")
    apply_parser.add_argument("--hot-list", type=Path, default=DEFAULT_HOT_LIST,
                              help="profile-hot functions, one address per line")
    apply_parser.add_argument("--no-hot", action="store_true", help="do not mark hot functions")

    undo_parser = commands.add_parser("undo", help="restore the generated calls")
    undo_parser.add_argument("--ppc", type=Path, default=DEFAULT_PPC)

    verify_parser = commands.add_parser("verify-elf", help="check a linked ELF against the manifest")
    verify_parser.add_argument("--elf", type=Path, required=True)
    verify_parser.add_argument("--nm", default="aarch64-none-elf-nm")
    verify_parser.add_argument("--ppc", type=Path, default=DEFAULT_PPC)

    args = parser.parse_args()
    if args.command == "apply":
        use_hot = not args.no_hot and os.environ.get("SWITCH_HOT_FUNCTIONS", "1") != "0"
        apply(args.ppc, args.hook_sources, read_hot_list(args.hot_list) if use_hot else set())
    elif args.command == "undo":
        undo(args.ppc)
    else:
        verify_elf(args.elf, args.nm, args.ppc)


if __name__ == "__main__":
    main()
