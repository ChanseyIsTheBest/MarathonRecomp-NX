#!/usr/bin/env python3
"""Compile single sources of the Switch build with its exact flags, without running ninja.

Several people (or tools) can check their changes at the same time: nothing is written into
build/switch-app, only the object (or nothing, with the default -fsyntax-only) into a scratch folder.
The flags come from the configured build (`ninja -t compdb`), so build/switch-app must have been
configured once by tools/build-switch.sh. A source the build does not know yet (a new file) takes the
flags of a sibling of the same target.

    python tools/switch-compile-check.py MarathonRecomp/gpu/video.cpp [more files...]
    python tools/switch-compile-check.py --full MarathonRecomp/kernel/imports.cpp   # real -c, keeps the .o

Exit status is 0 only if every file compiled.
"""

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BUILD = os.path.join(ROOT, "build", "switch-app")
DEVKITPRO = os.environ.get("SWITCH_DEVKITPRO_WIN", "C:/devkitPro")
NINJA_CANDIDATES = [
    os.environ.get("NINJA", ""),
    "C:/devkitPro/msys2/clang64/bin/ninja.exe",
    "C:/msys64/clang64/bin/ninja.exe",
    "ninja",
]

# Reference sources whose flags a new file of the same target borrows.
REFERENCES = {
    "MarathonRecomp": "MarathonRecomp/os/switch/egl_stub.cpp",
    "MarathonRecompLib": "MarathonRecompLib/ppc/ppc_recomp.0.cpp",
}


def norm(path):
    return os.path.normcase(os.path.abspath(path)).replace("\\", "/")


def load_compdb():
    for ninja in NINJA_CANDIDATES:
        if not ninja:
            continue
        try:
            out = subprocess.run([ninja, "-C", BUILD, "-t", "compdb"], capture_output=True, check=True)
            return json.loads(out.stdout.decode("utf-8", "replace"))
        except (OSError, subprocess.CalledProcessError, json.JSONDecodeError):
            continue
    sys.exit("switch-compile-check: could not read the compile database of build/switch-app "
             "(configure it once with tools/build-switch.sh).")


def split_command(command):
    # The commands are Windows command lines produced by CMake; posix=False keeps backslashes and quotes.
    return shlex.split(command, posix=False)


def target_of(path):
    rel = os.path.relpath(os.path.abspath(path), ROOT).replace("\\", "/")
    return rel.split("/", 1)[0], rel


def find_entry(db, path):
    wanted = norm(path)
    for entry in db:
        if norm(os.path.join(entry["directory"], entry["file"])) == wanted:
            return entry, False
    target, rel = target_of(path)
    is_c = path.endswith(".c")
    candidates = [e for e in db if e["output"].replace("\\", "/").startswith(target + "/CMakeFiles/")]
    if is_c:
        c_entries = [e for e in candidates if e["file"].endswith(".c")]
        if c_entries:
            return c_entries[0], True
    ref = REFERENCES.get(target)
    if ref:
        for entry in candidates:
            if norm(os.path.join(entry["directory"], entry["file"])) == norm(os.path.join(ROOT, ref)):
                return entry, True
    if candidates:
        return candidates[0], True
    return None, False


def rewrite(entry, path, full, out_dir, extra):
    args = split_command(entry["command"])
    result = []
    skip = 0
    for i, arg in enumerate(args):
        if skip:
            skip -= 1
            continue
        if arg in ("-MT", "-MF", "-o"):
            skip = 1
            continue
        if arg in ("-MD", "-MMD", "-c"):
            continue
        result.append(arg)
    # Replace the source (last argument) with the requested file.
    src = os.path.abspath(path).replace("\\", "/")
    if result and norm(os.path.join(entry["directory"], result[-1].strip('"'))) == norm(os.path.join(entry["directory"], entry["file"])):
        result[-1] = src
    else:
        result.append(src)
    if full:
        obj = os.path.join(out_dir, re.sub(r"[^A-Za-z0-9_.-]", "_", os.path.relpath(src, ROOT)) + ".o")
        result[-1:-1] = ["-c", "-o", obj.replace("\\", "/")]
    else:
        result.insert(1, "-fsyntax-only")
    result[1:1] = extra
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="+")
    parser.add_argument("--full", action="store_true", help="compile to an object instead of -fsyntax-only")
    parser.add_argument("--out-dir", default=None, help="where --full writes objects (default: a temp folder)")
    parser.add_argument("--extra", action="append", default=[], help="extra compiler argument (repeatable)")
    parser.add_argument("--print", action="store_true", help="print the command lines")
    parser.add_argument("--warnings", action="store_true", help="show warnings (hidden by default: the base code has many)")
    opts = parser.parse_args()

    db = load_compdb()
    out_dir = opts.out_dir or tempfile.mkdtemp(prefix="switch-compile-check-")
    env = dict(os.environ)
    env["DEVKITPRO"] = DEVKITPRO
    env["PATH"] = DEVKITPRO.replace("/", os.sep) + os.sep + "devkitA64" + os.sep + "bin" + os.pathsep + env.get("PATH", "")

    failures = 0
    for path in opts.files:
        path = os.path.join(ROOT, path) if not os.path.isabs(path) else path
        entry, borrowed = find_entry(db, path)
        if entry is None:
            print(f"[compile-check] {path}: no compile flags found for its target", file=sys.stderr)
            failures += 1
            continue
        cmd = rewrite(entry, path, opts.full, out_dir, opts.extra + ([] if opts.warnings else ["-w"]))
        if opts.print:
            print(" ".join(cmd))
        note = f" (flags of {entry['file']})" if borrowed else ""
        proc = subprocess.run(" ".join(cmd), cwd=entry["directory"], env=env, shell=True,
                              capture_output=True, text=True, errors="replace")
        output = (proc.stdout + proc.stderr).strip()
        status = "OK" if proc.returncode == 0 else "FAILED"
        print(f"[compile-check] {os.path.relpath(path, ROOT)}{note}: {status}")
        if output:
            print(output)
        if proc.returncode != 0:
            failures += 1
    if opts.full:
        print(f"[compile-check] objects in {out_dir}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
