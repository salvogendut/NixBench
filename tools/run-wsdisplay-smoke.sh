#!/bin/sh

set -eu

state_path=/var/run/nixbench-wsdisplay-smoke.state
duration_ms=${1:-3000}
jobs=${NIXBENCH_JOBS:-4}

fail()
{
    echo "nixbench wsdisplay test: $*" >&2
    exit 1
}

if [ "$#" -gt 1 ]; then
    fail "usage: $0 [duration-ms]"
fi

case "$duration_ms" in
    ''|*[!0-9]*)
        fail "duration must be an integer from 250 through 5000 ms"
        ;;
esac
if [ "${#duration_ms}" -gt 4 ] ||
   [ "$duration_ms" -lt 250 ] || [ "$duration_ms" -gt 5000 ]; then
    fail "duration must be from 250 through 5000 ms"
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
[ -x /usr/bin/timeout ] || fail "/usr/bin/timeout is unavailable"

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

echo "==> Configuring the opt-in harness"
cmake -S "$repo_dir" -B "$build_dir" \
    -DNIXBENCH_BUILD_WSDISPLAY_SMOKE=ON

echo "==> Building NixBench"
cmake --build "$build_dir" --parallel "$jobs"

echo "==> Running the non-destructive test suite"
ctest --test-dir "$build_dir" --output-on-failure

echo "==> Query-only console preflight"
sudo -n "$smoke" --preflight-only

cat <<EOF

Ready to take over the active wsdisplay console for $duration_ms ms.

Keep a second SSH session open. If this script does not restore the console,
wait for its timeout, verify no nixbench-wsdisplay-smoke process remains, then
run in that second session:

  sudo $smoke --recover

By continuing, you acknowledge the console takeover and that failure of the
supervisor itself can require the manual recovery command above.
EOF

printf "Type TAKEOVER to continue: "
answer=
if ! IFS= read -r answer || [ "$answer" != TAKEOVER ]; then
    fail "cancelled without changing display state"
fi

echo "==> Presenting the diagnostic framebuffer"
set +e
sudo -n /usr/bin/timeout -s SIGTERM -k 15s 10s \
    "$smoke" \
    --acknowledge-console-takeover \
    --acknowledge-no-crash-watchdog \
    --duration-ms "$duration_ms"
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
sudo -n wsconscfg -g

if [ "$run_status" -ne 0 ]; then
    fail "the harness returned status $run_status, but no recovery record remains"
fi

echo "==> Success: presentation completed and console restoration was verified"
