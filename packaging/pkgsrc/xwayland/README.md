# NetBSD pkgsrc Xwayland notes

NixBench's final X11 compatibility path is rootless Xwayland.  The initial
integration checkpoint runs Xwayland rootful, with `-shm`, so protocol and
input compatibility can be validated before NixBench grows its X window
manager.

On NetBSD 11, Xwayland 24.1.12 detects both `memfd_create(2)` and
`posix_fallocate(2)`. Two NetBSD-specific failures affect the shared-memory
backend:

- `posix_fallocate(2)` can reject an otherwise usable anonymous file with
  `EINVAL`; and
- a successfully sized memfd can still make `mmap(2)` return `EINVAL` for
  some rootless redirected-window pixmap sizes.

The latter leaves the X window at `RedirectDrawNone`, preventing Xwayland from
creating and publishing its Wayland surface. The local pkgsrc patch under
`../localpatches/wayland/xwayland` uses Xwayland's unlinked private-runtime
file on NetBSD and retains an `ftruncate(2)` fallback for filesystems which do
not implement preallocation. Other operating systems keep the upstream memfd
path.

Build the port while pointing pkgsrc at the tracked patch tree:

```sh
cd /usr/pkgsrc/wayland/xwayland
sudo make clean
sudo make LOCALPATCHES=/path/to/NixBench/packaging/pkgsrc/localpatches \
    package-install
```

Xwayland also expects the conventional X11 Unix-socket directory.  On a host
where the base X server has not created it since boot, prepare it before the
rootful probe:

```sh
sudo mkdir -p /tmp/.X11-unix
sudo chown root:wheel /tmp/.X11-unix
sudo chmod 1777 /tmp/.X11-unix
```

The rootful NixBench checkpoint can then be launched from the source tree with:

```sh
NIXBENCH_APPLICATION="$PWD/tools/run-xwayland-probe.sh" \
    ./tools/run-wsdisplay-session.sh
```

For the rootless path, enable the session service and launch the independent
X11 probe as the initial application:

```sh
NIXBENCH_XWAYLAND_ROOTLESS=1 \
NIXBENCH_APPLICATION="$PWD/tools/run-xwayland-rootless-probe.sh" \
    ./tools/run-wsdisplay-session.sh
```

Successful startup reports a `WL_SURFACE_SERIAL` followed by a rootless XWM
association. The X11 probe then appears as an ordinary managed NixBench
window rather than as a nested rootful desktop.
