#!/bin/bash
# Grep-based determinism lint (docs/architecture.md §4). Run from anywhere; exits non-zero on violations.
# runner/ and profiling/ may use wall-clock time for pacing and profiling.
set -uo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/sim/src"
[ -d "$src" ] || { echo "no sim/src; nothing to lint"; exit 0; }

status=0
check() {
    local pattern="$1" matches
    matches=$(grep -rnE --include='*.cpp' --include='*.hpp' --include='*.h' --include='*.cc' \
        --exclude-dir=runner --exclude-dir=profiling -e "$pattern" "$src" || true)
    if [ -n "$matches" ]; then
        echo "determinism lint: forbidden '$pattern' in sim/src:" >&2
        echo "$matches" >&2
        status=1
    fi
}

check '(^|[^A-Za-z0-9_])rand[[:space:]]*\('
check 'std::random_device'
check '#[[:space:]]*include[[:space:]]*<random>'
check 'std::chrono'

[ "$status" -eq 0 ] && echo "determinism lint: OK"
exit "$status"
