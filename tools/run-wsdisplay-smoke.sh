#!/bin/sh

set -eu

state_path=/var/run/nixbench-wsdisplay-smoke.state

fail()
{
    echo "nixbench wsdisplay test: $*" >&2
    exit 1
}

require_vt_cycle=0
if [ "${1:-}" = "--vt-cycle" ]; then
    require_vt_cycle=1
    shift
fi
if [ "$#" -gt 1 ]; then
    fail "usage: $0 [--vt-cycle] [duration-ms]"
fi
until_exit=0
if [ "$require_vt_cycle" -eq 1 ] && [ "$#" -eq 0 ]; then
    until_exit=1
fi
duration_ms=
if [ "$until_exit" -eq 0 ]; then
    duration_ms=${1:-3000}
fi
jobs=${NIXBENCH_JOBS:-4}
build_type=${NIXBENCH_BUILD_TYPE:-RelWithDebInfo}
timeout_margin_seconds=10

if [ "$until_exit" -eq 0 ]; then
    case "$duration_ms" in
        ''|0*|*[!0-9]*)
            fail "duration must be an integer from 250 through 30000 ms"
            ;;
    esac
    if [ "${#duration_ms}" -gt 5 ] ||
       [ "$duration_ms" -lt 250 ] || [ "$duration_ms" -gt 30000 ]; then
        fail "duration must be from 250 through 30000 ms"
    fi
    duration_seconds=$(( (duration_ms + 999) / 1000 ))
    outer_timeout_seconds=$((duration_seconds + timeout_margin_seconds))
fi

script_path=$0
case "$script_path" in
    */*) ;;
    *)
        script_path=$(command -v "$script_path") ||
            fail "could not locate the runner script"
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
repo_dir=$(CDPATH= cd "$script_dir/.." && pwd)
build_dir=${NIXBENCH_BUILD_DIR:-$repo_dir/build}
smoke=$build_dir/nixbench-wsdisplay-smoke

[ "$(uname -s)" = NetBSD ] || fail "this test is NetBSD-only"
[ -n "${SSH_CONNECTION:-}" ] ||
    fail "run this script over SSH while watching the physical console"
command -v cmake >/dev/null 2>&1 || fail "cmake is not installed"
command -v ctest >/dev/null 2>&1 || fail "ctest is not installed"
if [ "$until_exit" -eq 0 ]; then
    [ -x /usr/bin/timeout ] || fail "/usr/bin/timeout is unavailable"
fi

echo "==> Checking passwordless recovery access"
sudo -n /usr/bin/true ||
    fail "passwordless sudo is required for this hardware test"

if sudo -n /bin/test -e "$state_path"; then
    cat >&2 <<EOF
An earlier recovery record still exists at:
  $state_path

Do not start another test. Check that no smoke harness is running, then use:
  sudo $smoke --recover
EOF
    exit 1
fi

echo "==> Configuring the opt-in harness ($build_type)"
cmake -S "$repo_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DNIXBENCH_BUILD_WSDISPLAY_SMOKE=ON

echo "==> Building NixBench"
cmake --build "$build_dir" --parallel "$jobs"

echo "==> Running the non-destructive test suite"
ctest --test-dir "$build_dir" --output-on-failure

echo "==> Query-only console preflight"
sudo -n "$smoke" --preflight-only

original_vt=
away_vt=
if [ "$require_vt_cycle" -eq 1 ]; then
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
        fail "NIXBENCH_VT_AWAY must differ from the active VT $original_vt"
    fi
fi

if [ "$until_exit" -eq 1 ]; then
    lifetime_description="until you press Escape with no menu open"
else
    lifetime_description="for at most $duration_ms ms"
fi

cat <<EOF

Ready to show the shared NixBench desktop runtime on the active wsdisplay
console $lifetime_description.

The supervised worker will temporarily open /dev/wskbd and /dev/wsmouse.
Move the software cursor, drag the NixInfo window by its title bar, or use its
application menus in the global bar. F10 opens the global menu; use the arrows,
Return or keypad Enter, and Escape to navigate or dismiss it. Press Escape with
no menu open to finish early. General console text translation and the usual
keyboard VT-switch shortcuts remain unavailable while the worker owns
/dev/wskbd; input is closed before every acknowledged VT release and on exit.

This trial enables the adaptive profile only for raw wscons relative motion;
hosted SDL input is unchanged. It also uses the readiness-driven blocking input
wait. At exit it prints wait calls, input and signal-pipe readiness,
simultaneous readiness, host events, timeouts, and interruptions alongside raw-
motion, adaptive-gain, active-keymap control, and keyboard-event statistics. It
also reports VT release/acquire and input suspend/resume counts and timing, then
splits userspace-read-to-framebuffer-copy-complete time into render, present,
and event-delivery stages for comparison with later trials.

Keep a second SSH session open. Press Ctrl-C in the launching SSH session to
request supervised cancellation if physical input is unavailable. An
until-exit harness also prints its supervisor PID and an exact second-SSH
SIGTERM command after launch. A cancelled validation returns failure status but
still attempts full restoration.

If this script does not restore the console, first verify that no
nixbench-wsdisplay-smoke process remains, then run in the second session:

  sudo $smoke --recover

By continuing, you acknowledge the console takeover and that failure of the
supervisor itself can require the manual recovery command above.
EOF

if [ "$require_vt_cycle" -eq 1 ]; then
    cat <<EOF

This run must observe a complete VT release and reacquire. Once the desktop is
visible, use the second SSH session to switch away and back (the arguments are
one-based USL VT numbers):

  sudo -n /usr/sbin/wsconscfg -s $away_vt
  sudo -n /usr/sbin/wsconscfg -s $original_vt

Pause long enough between the commands to see VT $away_vt. This script captured
VT $original_vt as the originating console; set NIXBENCH_VT_AWAY before starting
the script if VT $away_vt is not a configured, idle text console. The worker
closes wscons input before acknowledging release. After acquisition it remaps
and reconfigures, reopens input, and then submits the required full redraw. The
run fails if that complete cycle is not recorded together with a post-acquire
frame.
EOF
fi

confirmation=TAKEOVER
if [ "$until_exit" -eq 1 ]; then
    confirmation=TAKEOVER-UNTIL-EXIT
    cat <<EOF

This run has no automatic presentation deadline. It remains supervised and
retains the recovery record until you exit with Escape or cancel the
supervisor. While switched away, return to VT $original_vt from the second SSH
session before attempting a physical-keyboard exit.
EOF
fi

printf "Type %s to continue: " "$confirmation"
answer=
if ! IFS= read -r answer || [ "$answer" != "$confirmation" ]; then
    fail "cancelled without changing display state"
fi
if [ "$require_vt_cycle" -eq 1 ]; then
    confirmed_vt=$(sudo -n /usr/sbin/wsconscfg -g) ||
        fail "could not re-query the active wsdisplay VT"
    if [ "$confirmed_vt" != "$original_vt" ]; then
        fail "active VT changed from $original_vt to $confirmed_vt while awaiting confirmation; no takeover was started"
    fi
fi

echo "==> Presenting the shared NixBench desktop runtime"
set -- \
    "$smoke" \
    --acknowledge-console-takeover \
    --acknowledge-no-crash-watchdog \
    --runtime-preview \
    --wscons-pointer-profile adaptive \
    --wscons-input-stats
if [ "$require_vt_cycle" -eq 1 ]; then
    set -- "$@" --require-vt-cycle
fi
if [ "$until_exit" -eq 1 ]; then
    set -- "$@" --until-exit
else
    set -- "$@" --duration-ms "$duration_ms"
fi
set +e
if [ "$until_exit" -eq 1 ]; then
    sudo -n "$@"
else
    sudo -n /usr/bin/timeout -s SIGTERM -k 15s \
        "${outer_timeout_seconds}s" "$@"
fi
run_status=$?
set -e

if sudo -n /bin/test -e "$state_path"; then
    cat >&2 <<EOF

RECOVERY RECORD REMAINS: $state_path

From the second SSH session, first verify that no harness process remains:
  ps -ax | grep '[n]ixbench-wsdisplay-smoke'

Only after it has exited, restore with:
  sudo $smoke --recover
EOF
    exit 1
fi

echo "==> Verifying the restored console independently"
sudo -n "$smoke" --preflight-only

echo "==> Active wsdisplay VT (one-based)"
sudo -n /usr/sbin/wsconscfg -g

if [ "$run_status" -ne 0 ]; then
    fail "the harness returned status $run_status, but no recovery record remains"
fi

echo "==> Success: desktop runtime completed and console restoration was verified"
