#!/bin/bash
# Downloads the Godot editor pinned in docs/versions.md into ~/.cache/citysim/godot/<version>/ (once).
# Used by the cloud SessionStart hook and by CI; tools/godot.sh runs the result.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
version=$(awk -F'|' '$2 ~ /^ *Godot *$/ {gsub(/ /, "", $3); print $3}' "$root/docs/versions.md")
if [ -z "$version" ] || [ "$version" = "TBD" ]; then
    echo "Godot version is not pinned in docs/versions.md" >&2
    exit 1
fi
dir="$HOME/.cache/citysim/godot/$version"
if ls "$dir"/Godot_v*linux.x86_64 >/dev/null 2>&1; then
    echo "Godot $version already cached in $dir"
    exit 0
fi
mkdir -p "$dir"
zip="Godot_v${version}-stable_linux.x86_64.zip"
url="https://github.com/godotengine/godot/releases/download/${version}-stable/${zip}"
trap 'rm -f "$dir/$zip"' EXIT
curl -fsSL "$url" -o "$dir/$zip"
python3 -I -m zipfile -e "$dir/$zip" "$dir"
chmod +x "$dir"/Godot_v*linux.x86_64
echo "Godot $version cached in $dir"
