#!/bin/bash
# Runs the cached Godot editor binary (see .claude/hooks/session-start.sh); passes all args through.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
version=$(awk -F'|' '$2 ~ /^ *Godot *$/ {gsub(/ /, "", $3); print $3}' "$root/docs/versions.md")
if [ -z "$version" ] || [ "$version" = "TBD" ]; then
    echo "Godot version is not pinned in docs/versions.md" >&2
    exit 1
fi
bin=$(ls "$HOME/.cache/citysim/godot/$version"/Godot_v*linux.x86_64 2>/dev/null | head -n1 || true)
if [ -z "$bin" ]; then
    echo "Godot $version is not cached; run .claude/hooks/session-start.sh" >&2
    exit 1
fi
exec "$bin" "$@"
