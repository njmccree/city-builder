#!/bin/bash
# Prepares game/ for headless Godot runs on a fresh checkout (CI and cloud sessions): lists the GDExtension so
# Godot loads it from the first start, then lets the editor import the project once.
#
# Why not just `godot --import`: Godot 4.7.2 aborts at the end of the very first scan that discovers a
# GDExtension registering a class (it wants a restart). Writing .godot/extension_list.cfg, which Godot would
# write itself, avoids that. Idempotent.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$root/game/.godot"
list="$root/game/.godot/extension_list.cfg"
: > "$list"
for descriptor in "$root"/game/bin/*.gdextension; do
    echo "res://bin/$(basename "$descriptor")" >> "$list"
done
"$root/tools/godot.sh" --headless --editor --path "$root/game" --quit
