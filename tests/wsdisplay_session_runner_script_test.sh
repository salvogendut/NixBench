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
        previous_argument=
        for argument do
            if [ "$previous_argument" = application ]; then
                printf 'application-argument=%s\n' "$argument" \
                    >>"$NB_TEST_LOG"
                previous_argument=
            elif [ "$argument" = --application ]; then
                previous_argument=application
            fi
        done
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
            *"nixbench-wsdisplay-session --acknowledge-console-takeover"*)
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
test_application="$temporary/Midori Browser"
touch "$test_application"
chmod +x "$test_application"
keyboard_application="$temporary/run-midori-content-probe.sh"
touch "$keyboard_application"
chmod +x "$keyboard_application"

unset NIXBENCH_EXPECT_SUPERVISOR_TERM || :
unset NIXBENCH_EXPECT_CORE_FAILURE || :
unset NIXBENCH_APPLICATION || :

run_session()
{
    supervisor_mode=$1
    core_failure=$2
    launch_status=$3
    absence_status=$4
    restored_vt=$5
    log=$6
    output=$7
    application=${8-}
    gtk_menu_bridge=${9-0}

    printf '%s\n' START-NIXBENCH |
        env -u NIXBENCH_TRACE_WAYLAND \
            -u NIXBENCH_TRACE_WAYLAND_LOG \
            PATH="$temporary/bin:/usr/bin:/bin" \
            SSH_CONNECTION='test test test test' \
            NIXBENCH_BUILD_DIR="$temporary/build" \
            NIXBENCH_EXPECT_SUPERVISOR_TERM="$supervisor_mode" \
            NIXBENCH_EXPECT_CORE_FAILURE="$core_failure" \
            NIXBENCH_APPLICATION="$application" \
            NIXBENCH_GTK_MENU_BRIDGE="$gtk_menu_bridge" \
            NB_LAUNCH_STATUS="$launch_status" \
            NB_ABSENCE_STATUS="$absence_status" \
            NB_INITIAL_VT=1 \
            NB_RESTORED_VT="$restored_vt" \
            NB_VT_COUNT_FILE="$log.vt-count" \
            NB_TEST_LOG="$log" \
            "$runner" >"$output" 2>&1
}

expected_log=$temporary/expected.log
expected_output=$temporary/expected.out
run_session 1 '' 0 0 1 "$expected_log" "$expected_output" \
    "$test_application"
grep -F -- '--require-supervisor-sigterm' "$expected_log" >/dev/null
grep -F -- "application-argument=$test_application" "$expected_log" \
    >/dev/null
grep -F -- 'Success: supervisor SIGTERM recovery' "$expected_output" >/dev/null
if grep -F -- '/usr/sbin/wsconscfg -s' "$expected_output" >/dev/null; then
    echo "supervisor-termination trial printed VT-switch instructions" >&2
    exit 1
fi
if grep -F -- '--require-core-' "$expected_log" >/dev/null; then
    echo "supervisor-termination trial also required a core-failure gate" >&2
    exit 1
fi

failed_log=$temporary/failed.log
failed_output=$temporary/failed.out
if run_session 1 '' 7 0 1 "$failed_log" "$failed_output"; then
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
run_session 0 '' 0 0 1 "$normal_log" "$normal_output"
grep -F -- '/usr/sbin/wsconscfg -s 2' "$normal_output" >/dev/null
grep -F -- '/usr/sbin/wsconscfg -s 1' "$normal_output" >/dev/null
if grep -F -- '--require-supervisor-sigterm' "$normal_log" >/dev/null; then
    echo "normal standalone session required supervisor termination" >&2
    exit 1
fi
if grep -F -- '--require-core-' "$normal_log" >/dev/null; then
    echo "normal standalone session required a core-failure gate" >&2
    exit 1
fi
if grep -F -- '--application' "$normal_log" >/dev/null; then
    echo "default standalone session selected a custom application" >&2
    exit 1
fi
grep -F -- 'NixBench will start with an empty desktop' \
    "$normal_output" >/dev/null
grep -F -- 'Starting NixBench with an empty desktop' \
    "$normal_output" >/dev/null
if grep -F -- 'NixClock will open automatically' \
    "$normal_output" >/dev/null; then
    echo "default standalone session still described NixClock autostart" >&2
    exit 1
fi

installed_directory=$temporary/installed/libexec/nixbench
mkdir -p "$installed_directory"
installed_helper=$installed_directory/nixbench-wsdisplay-session
installed_core=$installed_directory/nixbench-session-core
touch "$installed_helper" "$installed_core"
chmod +x "$installed_helper" "$installed_core"
installed_log=$temporary/installed.log
installed_output=$temporary/installed.out
env PATH="$temporary/bin:/usr/bin:/bin" \
    SSH_CONNECTION='test test test test' \
    NIXBENCH_INSTALLED_MODE=1 \
    NIXBENCH_SESSION_HELPER="$installed_helper" \
    NIXBENCH_SESSION_CORE="$installed_core" \
    NIXBENCH_GTK_MENU_BRIDGE=1 \
    NIXBENCH_HTML_THEME=cde \
    NB_LAUNCH_STATUS=0 \
    NB_ABSENCE_STATUS=0 \
    NB_INITIAL_VT=1 \
    NB_RESTORED_VT=1 \
    NB_VT_COUNT_FILE="$installed_log.vt-count" \
    NB_TEST_LOG="$installed_log" \
    "$runner" >"$installed_output" 2>&1
grep -F -- "$installed_helper --acknowledge-console-takeover" \
    "$installed_log" >/dev/null
grep -F -- 'NIXBENCH_GTK_MENU_BRIDGE=1' "$installed_log" >/dev/null
grep -F -- 'NIXBENCH_HTML_THEME=cde' "$installed_log" >/dev/null
grep -F -- 'NIXBENCH_XWAYLAND_ROOTLESS=1' "$installed_log" >/dev/null
grep -F -- 'NIXBENCH_XWAYLAND_LEGACY_ASSOCIATION=0' \
    "$installed_log" >/dev/null
if grep -F -- '/usr/bin/install' "$installed_log" >/dev/null ||
   grep -F -- 'Configuring the privilege-separated session' \
       "$installed_output" >/dev/null; then
    echo "installed launcher rebuilt or restaged the session helper" >&2
    exit 1
fi
if grep -F -- 'Type START-NIXBENCH' "$installed_output" >/dev/null; then
    echo "installed launcher requested redundant takeover confirmation" >&2
    exit 1
fi

local_log=$temporary/local.log
local_output=$temporary/local.out
env -u SSH_CONNECTION \
    PATH="$temporary/bin:/usr/bin:/bin" \
    NIXBENCH_INSTALLED_MODE=1 \
    NIXBENCH_ALLOW_LOCAL=1 \
    NIXBENCH_SESSION_HELPER="$installed_helper" \
    NIXBENCH_SESSION_CORE="$installed_core" \
    NIXBENCH_GTK_MENU_BRIDGE=1 \
    NB_LAUNCH_STATUS=0 \
    NB_ABSENCE_STATUS=0 \
    NB_INITIAL_VT=1 \
    NB_RESTORED_VT=1 \
    NB_VT_COUNT_FILE="$local_log.vt-count" \
    NB_TEST_LOG="$local_log" \
    "$runner" >"$local_output" 2>&1
grep -F -- 'LOCAL CONSOLE LAUNCH' "$local_output" >/dev/null
grep -F -- "$installed_helper --acknowledge-console-takeover" \
    "$local_log" >/dev/null

local_rejected_log=$temporary/local-rejected.log
local_rejected_output=$temporary/local-rejected.out
if env -u SSH_CONNECTION \
    PATH="$temporary/bin:/usr/bin:/bin" \
    NIXBENCH_INSTALLED_MODE=1 \
    NIXBENCH_SESSION_HELPER="$installed_helper" \
    NIXBENCH_SESSION_CORE="$installed_core" \
    NB_TEST_LOG="$local_rejected_log" \
    "$runner" >"$local_rejected_output" 2>&1; then
    echo "local launch without --local opt-in was accepted" >&2
    exit 1
fi
grep -F -- 'use the installed nixbench-session --local command' \
    "$local_rejected_output" >/dev/null

application_log=$temporary/application.log
application_output=$temporary/application.out
run_session 0 '' 0 0 1 "$application_log" "$application_output" \
    "$test_application" 1
grep -F -- "--application $test_application" "$application_log" >/dev/null
grep -F -- "application-argument=$test_application" "$application_log" \
    >/dev/null
grep -F -- 'NIXBENCH_GTK_MENU_BRIDGE=1' "$application_log" >/dev/null
grep -F -- 'NIXBENCH_XWAYLAND_ROOTLESS=1' "$application_log" >/dev/null
grep -F -- 'NIXBENCH_XWAYLAND_LEGACY_ASSOCIATION=0' \
    "$application_log" >/dev/null
grep -F -- 'selected initial application will open automatically' \
    "$application_output" >/dev/null
grep -F -- 'execution occurs only after the desktop core has dropped privileges' \
    "$application_output" >/dev/null
if grep -F -- 'NixClock will open automatically' \
    "$application_output" >/dev/null; then
    echo "custom-application session described NixClock as initial" >&2
    exit 1
fi

keyboard_log=$temporary/keyboard.log
keyboard_output=$temporary/keyboard.out
run_session 0 '' 0 0 1 "$keyboard_log" "$keyboard_output" \
    "$keyboard_application"
grep -F -- 'test the PC-XT keyboard path with Ctrl-L' \
    "$keyboard_output" >/dev/null
grep -F -- 'data:text/plain,nixbench-keyboard' \
    "$keyboard_output" >/dev/null

crash_log=$temporary/crash.log
crash_output=$temporary/crash.out
run_session 0 crash 0 0 1 "$crash_log" "$crash_output" \
    "$test_application"
grep -F -- '--require-core-crash' "$crash_log" >/dev/null
grep -F -- "application-argument=$test_application" "$crash_log" >/dev/null
grep -F -- 'exact sudo kill -USR1 command' "$crash_output" >/dev/null
grep -F -- 'Signal the supervisor only' "$crash_output" >/dev/null
grep -F -- 'Success: core-crash recovery' "$crash_output" >/dev/null
if grep -F -- '/usr/sbin/wsconscfg -s' "$crash_output" >/dev/null; then
    echo "core-crash trial printed VT-switch instructions" >&2
    exit 1
fi
if grep -F -- '--require-supervisor-sigterm' "$crash_log" >/dev/null; then
    echo "core-crash trial also required supervisor termination" >&2
    exit 1
fi
if grep -F -- '--require-core-hang' "$crash_log" >/dev/null; then
    echo "core-crash trial also required a core hang" >&2
    exit 1
fi

hang_log=$temporary/hang.log
hang_output=$temporary/hang.out
run_session 0 hang 0 0 1 "$hang_log" "$hang_output" \
    "$test_application"
grep -F -- '--require-core-hang' "$hang_log" >/dev/null
grep -F -- "application-argument=$test_application" "$hang_log" >/dev/null
grep -F -- 'exact sudo kill -USR1 command' "$hang_output" >/dev/null
grep -F -- 'do not send SIGCONT' "$hang_output" >/dev/null
grep -F -- 'Success: core-hang watchdog recovery' "$hang_output" >/dev/null
if grep -F -- '/usr/sbin/wsconscfg -s' "$hang_output" >/dev/null; then
    echo "core-hang trial printed VT-switch instructions" >&2
    exit 1
fi
if grep -F -- '--require-supervisor-sigterm' "$hang_log" >/dev/null; then
    echo "core-hang trial also required supervisor termination" >&2
    exit 1
fi
if grep -F -- '--require-core-crash' "$hang_log" >/dev/null; then
    echo "core-hang trial also required a core crash" >&2
    exit 1
fi

crash_failed_log=$temporary/crash-failed.log
crash_failed_output=$temporary/crash-failed.out
if run_session 0 crash 7 0 1 \
    "$crash_failed_log" "$crash_failed_output"; then
    echo "core-crash trial accepted a failed launcher" >&2
    exit 1
fi
grep -F -- '--require-core-crash' "$crash_failed_log" >/dev/null
if grep -F -- 'Success:' "$crash_failed_output" >/dev/null; then
    echo "failed core-crash trial reported success" >&2
    exit 1
fi

hang_failed_log=$temporary/hang-failed.log
hang_failed_output=$temporary/hang-failed.out
if run_session 0 hang 7 0 1 \
    "$hang_failed_log" "$hang_failed_output"; then
    echo "core-hang trial accepted a failed launcher" >&2
    exit 1
fi
grep -F -- '--require-core-hang' "$hang_failed_log" >/dev/null
if grep -F -- 'Success:' "$hang_failed_output" >/dev/null; then
    echo "failed core-hang trial reported success" >&2
    exit 1
fi

invalid_log=$temporary/invalid.log
invalid_output=$temporary/invalid.out
if run_session bogus '' 0 0 1 "$invalid_log" "$invalid_output"; then
    echo "invalid NIXBENCH_EXPECT_SUPERVISOR_TERM value was accepted" >&2
    exit 1
fi

invalid_core_log=$temporary/invalid-core.log
invalid_core_output=$temporary/invalid-core.out
if run_session 0 stall 0 0 1 \
    "$invalid_core_log" "$invalid_core_output"; then
    echo "invalid NIXBENCH_EXPECT_CORE_FAILURE value was accepted" >&2
    exit 1
fi
grep -F -- 'NIXBENCH_EXPECT_CORE_FAILURE must be crash or hang' \
    "$invalid_core_output" >/dev/null

invalid_theme_log=$temporary/invalid-theme.log
invalid_theme_output=$temporary/invalid-theme.out
if printf '%s\n' START-NIXBENCH | \
    env PATH="$temporary/bin:/usr/bin:/bin" \
        SSH_CONNECTION='test test test test' \
        NIXBENCH_BUILD_DIR="$temporary/build" \
        NIXBENCH_HTML_THEME='../unsafe' \
        NB_TEST_LOG="$invalid_theme_log" \
        "$runner" >"$invalid_theme_output" 2>&1; then
    echo "invalid NIXBENCH_HTML_THEME value was accepted" >&2
    exit 1
fi
grep -F -- 'NIXBENCH_HTML_THEME must be classic, fantasy, cde, or beos' \
    "$invalid_theme_output" >/dev/null

relative_application_log=$temporary/relative-application.log
relative_application_output=$temporary/relative-application.out
if run_session 0 '' 0 0 1 \
    "$relative_application_log" "$relative_application_output" midori; then
    echo "relative NIXBENCH_APPLICATION was accepted" >&2
    exit 1
fi
grep -F -- 'NIXBENCH_APPLICATION must be an absolute path' \
    "$relative_application_output" >/dev/null

directory_application_log=$temporary/directory-application.log
directory_application_output=$temporary/directory-application.out
if run_session 0 '' 0 0 1 \
    "$directory_application_log" "$directory_application_output" \
    /usr/pkg/bin/; then
    echo "directory NIXBENCH_APPLICATION was accepted" >&2
    exit 1
fi
grep -F -- 'NIXBENCH_APPLICATION must name an absolute executable path' \
    "$directory_application_output" >/dev/null

nonexecutable_application="$temporary/not-executable"
touch "$nonexecutable_application"
nonexecutable_application_log=$temporary/nonexecutable-application.log
nonexecutable_application_output=$temporary/nonexecutable-application.out
if run_session 0 '' 0 0 1 \
    "$nonexecutable_application_log" "$nonexecutable_application_output" \
    "$nonexecutable_application"; then
    echo "non-executable NIXBENCH_APPLICATION was accepted" >&2
    exit 1
fi
grep -F -- 'NIXBENCH_APPLICATION must be an executable regular file' \
    "$nonexecutable_application_output" >/dev/null

control_application=$(printf '%s\t%s' "$temporary/control" application)
touch "$control_application"
chmod +x "$control_application"
control_application_log=$temporary/control-application.log
control_application_output=$temporary/control-application.out
if run_session 0 '' 0 0 1 \
    "$control_application_log" "$control_application_output" \
    "$control_application"; then
    echo "control-character NIXBENCH_APPLICATION was accepted" >&2
    exit 1
fi
grep -F -- 'NIXBENCH_APPLICATION must not contain control characters' \
    "$control_application_output" >/dev/null

conflict_log=$temporary/conflict.log
conflict_output=$temporary/conflict.out
if run_session 1 crash 0 0 1 "$conflict_log" "$conflict_output"; then
    echo "conflicting recovery gates were accepted" >&2
    exit 1
fi
grep -F -- 'supervisor SIGTERM and core-failure gates are mutually exclusive' \
    "$conflict_output" >/dev/null

absence_log=$temporary/absence.log
absence_output=$temporary/absence.out
if run_session 1 '' 0 9 1 "$absence_log" "$absence_output"; then
    echo "failed recovery-record absence check was accepted" >&2
    exit 1
fi
grep -F -- 'could not independently verify recovery-record removal' \
    "$absence_output" >/dev/null

changed_vt_log=$temporary/changed-vt.log
changed_vt_output=$temporary/changed-vt.out
if run_session 1 '' 0 0 2 "$changed_vt_log" "$changed_vt_output"; then
    echo "restored VT mismatch was accepted" >&2
    exit 1
fi
grep -F -- 'active VT changed from 1 to 2' "$changed_vt_output" >/dev/null

echo "wsdisplay session runner script tests: ok"
