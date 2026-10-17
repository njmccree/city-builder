#!/bin/bash
# Grep-based lint (docs/architecture.md §4). Run from anywhere; exits non-zero on violations.
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
# Diagnostics go through CS_LOG_* (citysim/core/log.hpp); stdout belongs to command-line tools.
check 'std::cout|std::cerr|std::clog|(^|[^A-Za-z0-9_])f?printf[[:space:]]*\(|(^|[^A-Za-z0-9_])puts[[:space:]]*\('

# Test names must be plain ASCII: ctest passes them back through the command line, and on Windows a
# character such as a section sign is mangled, so the test "matches nothing" and fails.
tests="$root/sim/tests"
if [ -d "$tests" ]; then
    bad_names=$(grep -rnP --include='*.cpp' 'TEST_CASE\("[^"]*[^\x00-\x7F]' "$tests" || true)
    if [ -n "$bad_names" ]; then
        echo "lint: non-ASCII character in a TEST_CASE name (breaks ctest on Windows):" >&2
        echo "$bad_names" >&2
        status=1
    fi
fi

[ "$status" -eq 0 ] && echo "determinism lint: OK"
exit "$status"
