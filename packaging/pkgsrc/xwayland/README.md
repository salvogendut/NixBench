# NetBSD pkgsrc Xwayland notes

NixBench's final X11 compatibility path is rootless Xwayland.  The initial
integration checkpoint runs Xwayland rootful, with `-shm`, so protocol and
input compatibility can be validated before NixBench grows its X window
manager.

On NetBSD 11, Xwayland 24.1.12 detects both `memfd_create(2)` and
`posix_fallocate(2)`.  The latter returns `EINVAL` for a memfd, although
`ftruncate(2)` and `mmap(2)` work.  Upstream Xwayland treats the result as fatal
and reports `failed to create screen resources`.  The local pkgsrc patch under
`../localpatches/wayland/xwayland` adds the same runtime fallback used when
`posix_fallocate(2)` is unavailable at build time.

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

The rootful NixBench checkpoint is then launched from the source tree with:

```sh
NIXBENCH_APPLICATION="$PWD/tools/run-xwayland-probe.sh" \
    ./tools/run-wsdisplay-session.sh
```
