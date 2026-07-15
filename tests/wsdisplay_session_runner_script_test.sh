#!/bin/sh

set -eu

runner=${1:?missing standalone-session runner path}
temporary=$(mktemp -d "${TMPDIR:-/tmp}/nixbench-session-runner-test.XXXXXX")
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
            "-n /bin/test ! -e "*)
                exit "${NB_ABSENCE_STATUS:-0}"
                ;;
            "-n /usr/sbin/wsconscfg -g")
                vt_query_count=0
                if [ -f "$NB_VT_COUNT_FILE" ]; then
                    IFS= read -r vt_query_count <"$NB_VT_COUNT_FILE"
                fi
                vt_query_count=$((vt_query_count + 1))
                printf '%s\n' "$vt_query_count" >"$NB_VT_COUNT_FILE"
                if [ "$vt_query_count" -eq 1 ]; then
                    printf '%s\n' "${NB_INITIAL_VT:-1}"
                else
                    printf '%s\n' "${NB_RESTORED_VT:-1}"
                fi
                ;;
            "-n /var/run/nixbench-wsdisplay-session --acknowledge-console-takeover"*)
                exit "${NB_LAUNCH_STATUS:-0}"
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

unset NIXBENCH_EXPECT_SUPERVISOR_TERM || :

run_session()
{
    mode=$1
    launch_status=$2
    absence_status=$3
    restored_vt=$4
    log=$5
    output=$6

    if [ "$mode" = unset ]; then
        printf '%s\n' START-NIXBENCH |
            env PATH="$temporary/bin:/usr/bin:/bin" \
                SSH_CONNECTION='test test test test' \
                NIXBENCH_BUILD_DIR="$temporary/build" \
                NB_LAUNCH_STATUS="$launch_status" \
                NB_ABSENCE_STATUS="$absence_status" \
                NB_INITIAL_VT=1 \
                NB_RESTORED_VT="$restored_vt" \
                NB_VT_COUNT_FILE="$log.vt-count" \
                NB_TEST_LOG="$log" \
                "$runner" >"$output" 2>&1
    else
        printf '%s\n' START-NIXBENCH |
            env PATH="$temporary/bin:/usr/bin:/bin" \
                SSH_CONNECTION='test test test test' \
                NIXBENCH_BUILD_DIR="$temporary/build" \
                NIXBENCH_EXPECT_SUPERVISOR_TERM="$mode" \
                NB_LAUNCH_STATUS="$launch_status" \
                NB_ABSENCE_STATUS="$absence_status" \
                NB_INITIAL_VT=1 \
                NB_RESTORED_VT="$restored_vt" \
                NB_VT_COUNT_FILE="$log.vt-count" \
                NB_TEST_LOG="$log" \
                "$runner" >"$output" 2>&1
    fi
}

expected_log=$temporary/expected.log
expected_output=$temporary/expected.out
run_session 1 0 0 1 "$expected_log" "$expected_output"
grep -F -- '--require-supervisor-sigterm' "$expected_log" >/dev/null
grep -F -- 'Success' "$expected_output" >/dev/null
if grep -F -- '/usr/sbin/wsconscfg -s' "$expected_output" >/dev/null; then
    echo "supervisor-termination trial printed VT-switch instructions" >&2
    exit 1
fi

failed_log=$temporary/failed.log
failed_output=$temporary/failed.out
if run_session 1 7 0 1 "$failed_log" "$failed_output"; then
    echo "supervisor-termination trial accepted a failed launcher" >&2
    exit 1
fi
grep -F -- '--require-supervisor-sigterm' "$failed_log" >/dev/null
if grep -F -- 'Success' "$failed_output" >/dev/null; then
    echo "failed supervisor-termination trial reported success" >&2
    exit 1
fi

normal_log=$temporary/normal.log
normal_output=$temporary/normal.out
run_session unset 0 0 1 "$normal_log" "$normal_output"
grep -F -- '/usr/sbin/wsconscfg -s 2' "$normal_output" >/dev/null
grep -F -- '/usr/sbin/wsconscfg -s 1' "$normal_output" >/dev/null
if grep -F -- '--require-supervisor-sigterm' "$normal_log" >/dev/null; then
    echo "normal standalone session required supervisor termination" >&2
    exit 1
fi

invalid_log=$temporary/invalid.log
invalid_output=$temporary/invalid.out
if run_session bogus 0 0 1 "$invalid_log" "$invalid_output"; then
    echo "invalid NIXBENCH_EXPECT_SUPERVISOR_TERM value was accepted" >&2
    exit 1
fi

absence_log=$temporary/absence.log
absence_output=$temporary/absence.out
if run_session 1 0 9 1 "$absence_log" "$absence_output"; then
    echo "failed recovery-record absence check was accepted" >&2
    exit 1
fi
grep -F -- 'could not independently verify recovery-record removal' \
    "$absence_output" >/dev/null

changed_vt_log=$temporary/changed-vt.log
changed_vt_output=$temporary/changed-vt.out
if run_session 1 0 0 2 "$changed_vt_log" "$changed_vt_output"; then
    echo "restored VT mismatch was accepted" >&2
    exit 1
fi
grep -F -- 'active VT changed from 1 to 2' "$changed_vt_output" >/dev/null

echo "wsdisplay session runner script tests: ok"
