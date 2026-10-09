#!/bin/bash
# SessionStart hook for Claude Code cloud sessions: installs missing tools and pre-builds linux-debug.
# Idempotent; fails soft (warnings) so a network problem never blocks the session.
set -uo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
    exit 0
fi

cd "${CLAUDE_PROJECT_DIR:-$(cd "$(dirname "$0")/../.." && pwd)}"

log() { echo "[session-start] $*"; }
warn() { echo "[session-start] WARNING: $*" >&2; }

version_ge() { [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" = "$2" ]; }

SUDO=""
if [ "$(id -u)" -ne 0 ]; then SUDO="sudo"; fi

apt_updated=0
apt_install() {
    if ! command -v apt-get >/dev/null 2>&1; then
        warn "no apt-get; install manually: $*"
        return 1
    fi
    if [ "$apt_updated" -eq 0 ]; then
        $SUDO apt-get update -qq || warn "apt-get update failed"
        apt_updated=1
    fi
    DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y -qq "$@" || warn "failed to install: $*"
}

# 1. Tools ---------------------------------------------------------------------------------------
need_cmake=1
if command -v cmake >/dev/null 2>&1; then
    v=$(cmake --version | head -n1 | awk '{print $3}')
    if version_ge "$v" 3.25; then need_cmake=0; else warn "cmake $v < 3.25"; fi
fi
[ "$need_cmake" -eq 1 ] && apt_install cmake
command -v ninja >/dev/null 2>&1 || apt_install ninja-build
command -v git >/dev/null 2>&1 || apt_install git
command -v python3 >/dev/null 2>&1 || apt_install python3

have_cxx=0
if command -v g++ >/dev/null 2>&1 && version_ge "$(g++ -dumpversion)" 12; then have_cxx=1; fi
if command -v clang++ >/dev/null 2>&1 && version_ge "$(clang++ -dumpversion)" 15; then have_cxx=1; fi
[ "$have_cxx" -eq 0 ] && apt_install g++
command -v clang-format >/dev/null 2>&1 || apt_install clang-format

for t in cmake ninja git python3; do
    command -v "$t" >/dev/null 2>&1 && log "found $t" || warn "$t is still missing"
done

# 2. Submodules ----------------------------------------------------------------------------------
git submodule update --init --recursive || warn "submodule update failed"

# 3. Pre-build linux-debug (tests are not run here to keep startup fast) -------------------------
if cmake --preset linux-debug && cmake --build --preset linux-debug; then
    log "linux-debug built"
else
    warn "linux-debug configure/build failed (network issue fetching dependencies?)"
fi

# 4. Godot editor (only once docs/versions.md pins a version) ------------------------------------
godot_version=$(awk -F'|' '$2 ~ /^ *Godot *$/ {gsub(/ /, "", $3); print $3}' docs/versions.md)
if [ -z "$godot_version" ] || [ "$godot_version" = "TBD" ]; then
    log "Godot version not pinned in docs/versions.md (TBD); skipping Godot download"
else
    dir="$HOME/.cache/citysim/godot/$godot_version"
    if ls "$dir"/Godot_v* >/dev/null 2>&1; then
        log "Godot $godot_version already cached"
    else
        mkdir -p "$dir"
        zip="Godot_v${godot_version}-stable_linux.x86_64.zip"
        url="https://github.com/godotengine/godot/releases/download/${godot_version}-stable/${zip}"
        if curl -fsSL "$url" -o "$dir/$zip" && python3 -I -m zipfile -e "$dir/$zip" "$dir" 2>/dev/null; then
            chmod +x "$dir"/Godot_v*linux.x86_64 2>/dev/null || true
            rm -f "$dir/$zip"
            log "Godot $godot_version cached in $dir"
        else
            rm -f "$dir/$zip"
            warn "could not download Godot $godot_version"
        fi
    fi
fi

exit 0
