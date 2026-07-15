#!/bin/sh

set -eu

runner=${1:?missing guided-runner path}
temporary=$(mktemp -d "${TMPDIR:-/tmp}/nixbench-runner-test.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir "$temporary/bin"

cat >"$temporary/bin/stub" <<'EOF'
#!/bin/sh

case "${0##*/}" in
    uname)
        printf '%s\n' NetBSD
        ;;
    cmake|ctest)
        ;;
    sudo)
        printf '%s\n' "$*" >>"$NB_TEST_LOG"
        case "$*" in
            "-n /bin/test -e "*)
                exit 1
                ;;
            "-n /usr/sbin/wsconscfg -g")
                printf '%s\n' 1
                ;;
        esac
        ;;
    *)
        exit 2
        ;;
esac
EOF
chmod +x "$temporary/bin/stub"
ln -s stub "$temporary/bin/uname"
ln -s stub "$temporary/bin/cmake"
ln -s stub "$temporary/bin/ctest"
ln -s stub "$temporary/bin/sudo"

run_guided()
{
    confirmation=$1
    log=$2
    output=$3
    shift 3
    printf '%s\n' "$confirmation" |
        env PATH="$temporary/bin:/usr/bin:/bin" \
            SSH_CONNECTION='test test test test' \
            NIXBENCH_BUILD_DIR="$temporary/build" \
            NB_TEST_LOG="$log" \
            "$runner" "$@" >"$output" 2>&1
}

unbounded_log=$temporary/unbounded.log
unbounded_output=$temporary/unbounded.out
run_guided TAKEOVER-UNTIL-EXIT \
    "$unbounded_log" "$unbounded_output" --vt-cycle
unbounded_command="-n $temporary/build/nixbench-wsdisplay-smoke"
unbounded_command="$unbounded_command --acknowledge-console-takeover"
unbounded_command="$unbounded_command --acknowledge-no-crash-watchdog"
unbounded_command="$unbounded_command --runtime-preview"
unbounded_command="$unbounded_command --wscons-pointer-profile adaptive"
unbounded_command="$unbounded_command --wscons-input-stats"
unbounded_command="$unbounded_command --require-vt-cycle --until-exit"
grep -F -x -- "$unbounded_command" "$unbounded_log" >/dev/null
grep -F -- '--until-exit' "$unbounded_log" >/dev/null
grep -F -- '--require-vt-cycle' "$unbounded_log" >/dev/null
grep -F -- 'TAKEOVER-UNTIL-EXIT' "$unbounded_output" >/dev/null
if grep -F -- '/usr/bin/timeout' "$unbounded_log" >/dev/null ||
   grep -F -- '--duration-ms' "$unbounded_log" >/dev/null; then
    echo "unbounded VT run unexpectedly used a deadline" >&2
    exit 1
fi

bounded_log=$temporary/bounded.log
bounded_output=$temporary/bounded.out
run_guided TAKEOVER "$bounded_log" "$bounded_output" --vt-cycle 30000
grep -F -- '/usr/bin/timeout' "$bounded_log" >/dev/null
grep -F -- '--duration-ms 30000' "$bounded_log" >/dev/null
grep -F -- '--require-vt-cycle' "$bounded_log" >/dev/null
if grep -F -- '--until-exit' "$bounded_log" >/dev/null; then
    echo "bounded VT run unexpectedly used --until-exit" >&2
    exit 1
fi

normal_log=$temporary/normal.log
normal_output=$temporary/normal.out
run_guided TAKEOVER "$normal_log" "$normal_output"
grep -F -- '/usr/bin/timeout' "$normal_log" >/dev/null
grep -F -- '--duration-ms 3000' "$normal_log" >/dev/null
if grep -F -- '--until-exit' "$normal_log" >/dev/null ||
   grep -F -- '--require-vt-cycle' "$normal_log" >/dev/null; then
    echo "normal guided run unexpectedly used VT-cycle lifetime options" >&2
    exit 1
fi

if env PATH="$temporary/bin:/usr/bin:/bin" \
       SSH_CONNECTION='test test test test' \
       NIXBENCH_BUILD_DIR="$temporary/build" \
       NB_TEST_LOG="$temporary/rejected.log" \
       "$runner" --vt-cycle 0 >"$temporary/rejected.out" 2>&1; then
    echo "invalid bounded duration was accepted" >&2
    exit 1
fi

echo "wsdisplay runner script tests: ok"
