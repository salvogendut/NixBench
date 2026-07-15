#!/bin/sh

set -eu

state_path=/var/run/nixbench-wsdisplay-session.state
staged_session=/var/run/nixbench-wsdisplay-session

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
repo_dir=$(CDPATH= cd "$script_dir/.." && pwd)
build_dir=${NIXBENCH_BUILD_DIR:-$repo_dir/build}
case "$build_dir" in
    /*) ;;
    *) build_dir=$repo_dir/$build_dir ;;
esac
built_session=$build_dir/nixbench-wsdisplay-session
core=$build_dir/nixbench-session-core

[ "$(uname -s)" = NetBSD ] || fail "this session is NetBSD-only"
[ -n "${SSH_CONNECTION:-}" ] ||
    fail "run this script over SSH while watching the physical console"
command -v cmake >/dev/null 2>&1 || fail "cmake is not installed"
command -v ctest >/dev/null 2>&1 || fail "ctest is not installed"

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

echo "==> Query-only console preflight"
sudo -n "$staged_session" --preflight

cat <<EOF

Ready to start the privilege-separated NixBench desktop on the active
wsdisplay console.

The root recovery parent and device helper retain only wsdisplay, wscons, VT,
frame presentation, heartbeat, and restoration duties. The NixBench desktop,
its private Wayland server, and NixClock run as your ordinary sudo user. They
receive no framebuffer, keyboard, mouse, recovery, or VT descriptor.

NixClock will open automatically. Its menus are installed into the global bar:
use NixClock -> Quit to close the clock, and Settings -> Show seconds to toggle
the seconds hand. After closing it, use the desktop's NixBench -> Quit command,
or press Escape when no Wayland client owns keyboard focus, to end the session.

Keep a second SSH session open. The launcher prints its supervisor PID and an
exact SIGTERM command. If orderly cancellation fails, verify that no
nixbench-wsdisplay-session helper remains, then recover with:

  sudo $staged_session --recover

The session has no automatic deadline. The recovery record is retained until
the core and helper have exited and the saved console state has been restored
and independently verified.
EOF

printf "Type START-NIXBENCH to continue: "
answer=
if ! IFS= read -r answer || [ "$answer" != "START-NIXBENCH" ]; then
    fail "cancelled without changing display state"
fi

echo "==> Starting NixBench and NixClock"
set +e
sudo -n "$staged_session" --acknowledge-console-takeover --core "$core"
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

echo "==> Verifying the restored console independently"
sudo -n "$staged_session" --preflight

echo "==> Active wsdisplay VT (one-based)"
sudo -n /usr/sbin/wsconscfg -g

if [ "$run_status" -ne 0 ]; then
    fail "the session returned status $run_status, but restoration completed"
fi

echo "==> Success: NixBench exited and console restoration was verified"
