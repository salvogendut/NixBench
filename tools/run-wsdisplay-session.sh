#!/bin/sh

set -eu

state_path=/var/run/nixbench-wsdisplay-session.state
installed_mode=${NIXBENCH_INSTALLED_MODE:-0}
case "$installed_mode" in
    0|1) ;;
    *)
        echo "nixbench standalone session: invalid installed mode" >&2
        exit 1
        ;;
esac
if [ "$installed_mode" -eq 1 ]; then
    staged_session=${NIXBENCH_SESSION_HELPER:-}
else
    staged_session=/var/run/nixbench-wsdisplay-session
fi

fail()
{
    echo "nixbench standalone session: $*" >&2
    exit 1
}

if [ "$#" -ne 0 ]; then
    fail "usage: $0"
fi

jobs=${NIXBENCH_JOBS:-4}
build_type=${NIXBENCH_BUILD_TYPE:-RelWithDebInfo}
expect_supervisor_term=${NIXBENCH_EXPECT_SUPERVISOR_TERM:-0}
case "$expect_supervisor_term" in
    0|1) ;;
    *)
        fail "NIXBENCH_EXPECT_SUPERVISOR_TERM must be 0 or 1"
        ;;
esac
expect_core_failure=${NIXBENCH_EXPECT_CORE_FAILURE:-}
case "$expect_core_failure" in
    ''|crash|hang) ;;
    *)
        fail "NIXBENCH_EXPECT_CORE_FAILURE must be crash or hang"
        ;;
esac
if [ "$expect_supervisor_term" -eq 1 ] &&
   [ -n "$expect_core_failure" ]; then
    fail "supervisor SIGTERM and core-failure gates are mutually exclusive"
fi
application=${NIXBENCH_APPLICATION:-}
trace_wayland=${NIXBENCH_TRACE_WAYLAND:-}
trace_wayland_log=${NIXBENCH_TRACE_WAYLAND_LOG:-}
gtk_menu_bridge=${NIXBENCH_GTK_MENU_BRIDGE:-0}
case "$gtk_menu_bridge" in
    0|1) ;;
    *)
        fail "NIXBENCH_GTK_MENU_BRIDGE must be 0 or 1"
        ;;
esac
if [ -n "$application" ]; then
    case "$application" in
        /|/*/) fail "NIXBENCH_APPLICATION must name an absolute executable path" ;;
        /*) ;;
        *) fail "NIXBENCH_APPLICATION must be an absolute path" ;;
    esac
    if [ "${#application}" -ge 4096 ]; then
        fail "NIXBENCH_APPLICATION is too long"
    fi
    case "$application" in
        *[[:cntrl:]]*)
            fail "NIXBENCH_APPLICATION must not contain control characters"
            ;;
    esac
    if [ ! -f "$application" ] || [ ! -x "$application" ]; then
        fail "NIXBENCH_APPLICATION must be an executable regular file"
    fi
fi
unset NIXBENCH_TRACE_WAYLAND NIXBENCH_TRACE_WAYLAND_LOG

script_path=$0
case "$script_path" in
    */*) ;;
    *)
        script_path=$(command -v "$script_path") ||
            fail "could not locate the launcher script"
        ;;
esac
while [ -L "$script_path" ]; do
    link_dir=$(CDPATH= cd "$(dirname "$script_path")" && pwd)
    link_target=$(readlink "$script_path") ||
        fail "could not resolve $script_path"
    case "$link_target" in
        /*) script_path=$link_target ;;
        *) script_path=$link_dir/$link_target ;;
    esac
done

script_dir=$(CDPATH= cd "$(dirname "$script_path")" && pwd)
if [ "$installed_mode" -eq 1 ]; then
    core=${NIXBENCH_SESSION_CORE:-}
    if [ -z "$staged_session" ] || [ -z "$core" ]; then
        fail "installed helper and core paths were not configured"
    fi
    if [ ! -f "$staged_session" ] || [ ! -x "$staged_session" ] ||
       [ ! -f "$core" ] || [ ! -x "$core" ]; then
        fail "installed helper or core is missing or not executable"
    fi
else
    repo_dir=$(CDPATH= cd "$script_dir/.." && pwd)
    build_dir=${NIXBENCH_BUILD_DIR:-$repo_dir/build}
    case "$build_dir" in
        /*) ;;
        *) build_dir=$repo_dir/$build_dir ;;
    esac
    built_session=$build_dir/nixbench-wsdisplay-session
    core=$build_dir/nixbench-session-core
fi

[ "$(uname -s)" = NetBSD ] || fail "this session is NetBSD-only"
[ -n "${SSH_CONNECTION:-}" ] ||
    fail "run this script over SSH while watching the physical console"
if [ "$installed_mode" -eq 0 ]; then
    command -v cmake >/dev/null 2>&1 || fail "cmake is not installed"
    command -v ctest >/dev/null 2>&1 || fail "ctest is not installed"
fi

echo "==> Checking passwordless recovery access"
sudo -n /usr/bin/true ||
    fail "passwordless sudo is required for supervised recovery"

if sudo -n /bin/test -e "$state_path"; then
    cat >&2 <<EOF
An earlier standalone recovery record still exists at:
  $state_path

Do not start another session. Check that no session helper remains, then use:
  sudo $staged_session --recover
EOF
    exit 1
fi

if [ "$installed_mode" -eq 0 ]; then
    echo "==> Configuring the privilege-separated session ($build_type)"
    cmake -S "$repo_dir" -B "$build_dir" \
        -DCMAKE_BUILD_TYPE="$build_type" \
        -DNIXBENCH_WAYLAND=ON \
        -DNIXBENCH_BUILD_APPLICATIONS=ON \
        -DNIXBENCH_BUILD_WSDISPLAY_SESSION=ON

    echo "==> Building NixBench"
    cmake --build "$build_dir" --parallel "$jobs"

    echo "==> Running the non-destructive test suite"
    ctest --test-dir "$build_dir" --output-on-failure

    echo "==> Staging the privileged launcher as a root-owned executable"
    sudo -n /usr/bin/install -o root -g wheel -m 0555 \
        "$built_session" "$staged_session"
fi

echo "==> Query-only console preflight"
sudo -n "$staged_session" --preflight

original_vt=$(sudo -n /usr/sbin/wsconscfg -g) ||
    fail "could not query the active wsdisplay VT"
case "$original_vt" in
    ''|0|0*|*[!0-9]*)
        fail "active wsdisplay VT is not a positive one-based number: $original_vt"
        ;;
esac
if [ "${#original_vt}" -gt 3 ] || [ "$original_vt" -gt 256 ]; then
    fail "active wsdisplay VT number is outside the supported range"
fi

away_vt=
if [ "$expect_supervisor_term" -eq 0 ] &&
   [ -z "$expect_core_failure" ]; then
    away_vt=${NIXBENCH_VT_AWAY:-}
    if [ -z "$away_vt" ]; then
        if [ "$original_vt" -eq 2 ]; then
            away_vt=1
        else
            away_vt=2
        fi
    fi
    case "$away_vt" in
        ''|0|0*|*[!0-9]*)
            fail "NIXBENCH_VT_AWAY must be a positive one-based VT number"
            ;;
    esac
    if [ "${#away_vt}" -gt 3 ] || [ "$away_vt" -gt 256 ]; then
        fail "NIXBENCH_VT_AWAY is outside the supported range"
    fi
    if [ "$away_vt" -eq "$original_vt" ]; then
        fail "NIXBENCH_VT_AWAY must differ from active VT $original_vt"
    fi
fi

cat <<EOF

Ready to start the privilege-separated NixBench desktop on the active
wsdisplay console.

The root recovery parent and device helper retain only wsdisplay, wscons, VT,
frame presentation, heartbeat, and restoration duties. The NixBench desktop,
its private Wayland server, and applications run as your ordinary sudo user.
They receive no framebuffer, keyboard, mouse, recovery, or VT descriptor.
EOF

if [ -n "$application" ]; then
    cat <<EOF
The selected initial application will open automatically:
  $application
Its execution occurs only after the desktop core has dropped privileges.
EOF
    if [ "${application##*/}" = run-midori-content-probe.sh ]; then
        cat <<EOF
After the fixed page appears, test the PC-XT keyboard path with Ctrl-L. Type:

  data:text/plain,nixbench-keyboard

Use Backspace or the arrow keys to edit it, then press Return. Midori should
replace the page with the text "nixbench-keyboard" without using the network.
EOF
    elif [ "${application##*/}" = run-sakura-probe.sh ]; then
        cat <<EOF
Sakura should open as a terminal prompt. Type a simple command such as:

  printf 'nixbench-sakura\\n'

and press Return to verify keyboard input and text rendering.
EOF
    elif [ "${application##*/}" = run-xwayland-probe.sh ]; then
        cat <<EOF
This probe starts Xwayland inside the NixBench Wayland session and then
opens a native X11 client. If the window appears, press keys to verify input
and use Escape or q to close it.
EOF
    fi
else
    cat <<EOF
NixBench will start with an empty desktop. Use the Applications menu in the
global bar to launch NixClock, Sakura Terminal, or Midori Web Browser.
EOF
fi

if [ "$expect_supervisor_term" -eq 1 ]; then
    cat <<EOF
This is the supervised SIGTERM recovery gate. Once the desktop is visible, do
not quit it and do not switch VTs. In the retained second SSH session, run the
exact sudo kill -TERM command printed with the supervisor PID after takeover.

The privileged launcher succeeds only if SIGTERM drives the shutdown, no
independent supervision failure occurs, the worker and ordinary-user
core/application session are gone, the saved console state is restored and
verified, and the recovery record is removed. A normal desktop exit, a
different signal, or incomplete recovery fails this trial.
EOF
elif [ "$expect_core_failure" = crash ]; then
    cat <<EOF
This is the core-crash recovery gate. Once the desktop is visible, do not quit
it and do not switch VTs. Wait until the privileged launcher reports that the
core-failure trigger is armed, then run the exact sudo kill -USR1 command it
prints from the retained second SSH session. Signal the supervisor only; do not
look up or signal the ordinary-user core PID yourself.

SIGUSR1 asks the supervisor to inject an abrupt crash into its validated,
ordinary-user core. The launcher succeeds only if that crash is observed, no
independent supervision failure occurs, the worker and complete
core/application session are gone, the saved console state is restored and
verified, and the recovery record is removed. A normal exit, a heartbeat
timeout, a different supervisor signal, or incomplete recovery fails this
trial.
EOF
elif [ "$expect_core_failure" = hang ]; then
    cat <<EOF
This is the core-hang watchdog gate. Once the desktop is visible, do not quit
it and do not switch VTs. Wait until the privileged launcher reports that the
core-failure trigger is armed, then run the exact sudo kill -USR1 command it
prints from the retained second SSH session. Signal the supervisor only; do not
look up or signal the ordinary-user core PID yourself.

SIGUSR1 asks the supervisor to stop its validated, ordinary-user core. The
desktop should freeze until the heartbeat watchdog detects the hang and drives
bounded cleanup; do not send SIGCONT. The launcher succeeds only if the
heartbeat timeout is observed, no independent supervision failure occurs, the
worker and complete core/application session are gone, the saved console state
is restored and verified, and the recovery record is removed. A normal exit,
a core crash, a different supervisor signal, or incomplete recovery fails this
trial.
EOF
else
    if [ -n "$application" ]; then
        cat <<EOF
After closing the initial application, click the empty desktop to return focus
to the shell. Then use NixBench -> Quit, or press Escape, to end the session.

For the VT lifecycle gate, once the desktop is visible use the second SSH
session to switch away and back (these are one-based VT numbers):
EOF
    else
        cat <<EOF
Use NixBench -> Quit, or press Escape while the desktop owns keyboard focus,
to end the session.

For the VT lifecycle gate, once the desktop is visible use the second SSH
session to switch away and back (these are one-based VT numbers):
EOF
    fi

    cat <<EOF

  sudo -n /usr/sbin/wsconscfg -s $away_vt
  sudo -n /usr/sbin/wsconscfg -s $original_vt

Pause long enough to see VT $away_vt before returning to VT $original_vt. If VT
$away_vt is not a configured idle text console, cancel before takeover and set
NIXBENCH_VT_AWAY to one that is. On normal exit, the launcher reports completed
release/acquire counts; this trial should report 1 and 1.
EOF
fi

cat <<EOF
Keep a second SSH session open. The launcher prints its supervisor PID and an
exact SIGTERM cancellation command. In a core-failure gate it also prints the
exact SIGUSR1 trigger command once the ordinary-user core is validated and the
trigger is armed. If orderly cancellation fails, verify that no
nixbench-wsdisplay-session helper remains, then recover with:

  sudo $staged_session --recover

The session has no automatic deadline. The recovery record is retained until
the core and helper have exited and the saved console state has been restored
and independently verified.
EOF

if [ "$installed_mode" -eq 0 ]; then
    printf "Type START-NIXBENCH to continue: "
    answer=
    if ! IFS= read -r answer || [ "$answer" != "START-NIXBENCH" ]; then
        fail "cancelled without changing display state"
    fi
fi

if [ -n "$application" ]; then
    echo "==> Starting NixBench and $application"
else
    echo "==> Starting NixBench with an empty desktop"
fi

run_privileged_session()
{
    if [ -n "$application" ]; then
        if [ -n "$trace_wayland" ]; then
            if [ -n "$trace_wayland_log" ]; then
                sudo -n env \
                    NIXBENCH_TRACE_WAYLAND="$trace_wayland" \
                    NIXBENCH_GTK_MENU_BRIDGE="$gtk_menu_bridge" \
                    NIXBENCH_TRACE_WAYLAND_LOG="$trace_wayland_log" \
                    "$staged_session" --acknowledge-console-takeover \
                    --core "$core" --application "$application" "$@"
            else
                sudo -n env \
                    NIXBENCH_TRACE_WAYLAND="$trace_wayland" \
                    NIXBENCH_GTK_MENU_BRIDGE="$gtk_menu_bridge" \
                    "$staged_session" --acknowledge-console-takeover \
                    --core "$core" --application "$application" "$@"
            fi
        else
            sudo -n env \
                NIXBENCH_GTK_MENU_BRIDGE="$gtk_menu_bridge" \
                "$staged_session" --acknowledge-console-takeover \
                --core "$core" --application "$application" "$@"
        fi
    else
        if [ -n "$trace_wayland" ]; then
            if [ -n "$trace_wayland_log" ]; then
                sudo -n env \
                    NIXBENCH_TRACE_WAYLAND="$trace_wayland" \
                    NIXBENCH_GTK_MENU_BRIDGE="$gtk_menu_bridge" \
                    NIXBENCH_TRACE_WAYLAND_LOG="$trace_wayland_log" \
                    "$staged_session" --acknowledge-console-takeover \
                    --core "$core" "$@"
            else
                sudo -n env \
                    NIXBENCH_TRACE_WAYLAND="$trace_wayland" \
                    NIXBENCH_GTK_MENU_BRIDGE="$gtk_menu_bridge" \
                    "$staged_session" --acknowledge-console-takeover \
                    --core "$core" "$@"
            fi
        else
            sudo -n env \
                NIXBENCH_GTK_MENU_BRIDGE="$gtk_menu_bridge" \
                "$staged_session" --acknowledge-console-takeover \
                --core "$core" "$@"
        fi
    fi
}

set +e
if [ "$expect_supervisor_term" -eq 1 ]; then
    run_privileged_session --require-supervisor-sigterm
elif [ "$expect_core_failure" = crash ]; then
    run_privileged_session --require-core-crash
elif [ "$expect_core_failure" = hang ]; then
    run_privileged_session --require-core-hang
else
    run_privileged_session
fi
run_status=$?
set -e

if sudo -n /bin/test -e "$state_path"; then
    cat >&2 <<EOF

RECOVERY RECORD REMAINS: $state_path

From the second SSH session, first verify that no session helper remains:
  ps -ax | grep '[n]ixbench-wsdisplay-session'

Only after it has exited, restore with:
  sudo $staged_session --recover
EOF
    exit 1
fi
if ! sudo -n /bin/test ! -e "$state_path"; then
    fail "could not independently verify recovery-record removal"
fi

echo "==> Verifying the restored console independently"
sudo -n "$staged_session" --preflight

echo "==> Active wsdisplay VT (one-based)"
restored_vt=$(sudo -n /usr/sbin/wsconscfg -g) ||
    fail "could not query the restored wsdisplay VT"
echo "$restored_vt"
if [ "$restored_vt" != "$original_vt" ]; then
    fail "active VT changed from $original_vt to $restored_vt"
fi

if [ "$run_status" -ne 0 ]; then
    fail "the session returned status $run_status, but restoration completed"
fi

if [ "$expect_supervisor_term" -eq 1 ]; then
    echo "==> Success: supervisor SIGTERM recovery and console restoration were verified"
elif [ "$expect_core_failure" = crash ]; then
    echo "==> Success: core-crash recovery and console restoration were verified"
elif [ "$expect_core_failure" = hang ]; then
    echo "==> Success: core-hang watchdog recovery and console restoration were verified"
else
    echo "==> Success: NixBench exited and console restoration was verified"
fi
