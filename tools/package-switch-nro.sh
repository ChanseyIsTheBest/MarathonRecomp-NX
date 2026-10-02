#!/usr/bin/env bash
# Packages the built Switch ELF into an NRO: nacptool (version from
# res/version.txt) + strip + elf2nro (with the app icon). Safe to re-run.
set -euo pipefail
# Git Bash exports DEVKITPRO=/opt/devkitpro even where that folder does not exist.
if [ -z "${DEVKITPRO:-}" ] || [ ! -d "$DEVKITPRO" ]; then
  for devkitpro_candidate in /opt/devkitpro /c/devkitPro; do
    if [ -d "$devkitpro_candidate" ]; then DEVKITPRO="$devkitpro_candidate"; break; fi
  done
fi
: "${DEVKITPRO:=/opt/devkitpro}"
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root_dir"

out="build/switch-app/MarathonRecomp"
elf="$out/MarathonRecomp"
[ -f "$elf" ] || { echo "ELF not found: $elf (build the app first)." >&2; exit 2; }

# App version from res/version.txt (VERSION_MAJOR.MINOR.REVISION)
# shellcheck disable=SC1091
. MarathonRecomp/res/version.txt
version="${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_REVISION}"

icon="MarathonRecomp/res/switch_icon.jpg"
[ -f "$icon" ] || icon="$DEVKITPRO/libnx/default_icon.jpg"

"$DEVKITPRO/tools/bin/nacptool" --create "Marathon Recompiled" "hedge-dev" "$version" "$out/MarathonRecomp.nacp"

# Keep an unstripped copy for crash-address symbolication, strip a copy for the NRO.
cp "$elf" "$out/MarathonRecomp.debug.elf"
cp "$elf" "$out/MarathonRecomp.stripped"
"$DEVKITPRO/devkitA64/bin/aarch64-none-elf-strip" --strip-all "$out/MarathonRecomp.stripped"
"$DEVKITPRO/tools/bin/elf2nro" "$out/MarathonRecomp.stripped" "$out/MarathonRecomp.nro" \
  --icon="$icon" --nacp="$out/MarathonRecomp.nacp"

mkdir -p dist/switch
cp "$out/MarathonRecomp.nro" dist/switch/MarathonRecomp.nro
echo "NRO: dist/switch/MarathonRecomp.nro  (v$version, icon: $(basename "$icon"))"
