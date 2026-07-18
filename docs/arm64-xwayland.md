# ARM64 NetBSD Xwayland build notes

[Back to the main README](../README.md)

This note records the first attempt to build rootless Xwayland for NixBench on
an ARM64 NetBSD 10.1 host. It is a troubleshooting record, not yet a supported
installation recipe: at the time of writing, the final Xwayland configure step
is blocked by pkgsrc's native-X11 buildlink selection described below.

## Why X11 applications report `can't open display`

NixBench starts rootless Xwayland only when it can discover an executable named
`Xwayland`. If neither `/usr/pkg/bin/Xwayland` nor another configured path is
available, a standalone session correctly leaves `NIXBENCH_XWAYLAND` empty.
Native Wayland applications still work, but X11 clients such as `xclock` have
no `DISPLAY` and report that they cannot open one.

Check the executable before testing an X11 client:

```sh
command -v Xwayland
test -x /usr/pkg/bin/Xwayland && /usr/pkg/bin/Xwayland -version
```

After Xwayland is installed, restart the NixBench session. Its rootless bridge
creates and exports the private `DISPLAY` to applications launched from the
desktop, including terminals such as Sakura.

## Prefer binary dependencies

On the ARM64 host, `pkgin` supplied all substantial Xwayland build
dependencies. Installing them first avoids rebuilding Meson, gettext, Wayland,
and related libraries from source:

```sh
sudo pkgin install meson gettext-tools freetype2 libei libxcvt \
    wayland wayland-protocols xkbcomp
```

Use `sudo pkgin search NAME` when a pkgsrc directory name is rejected by
`pkgin`; pkgsrc package names and repository package names are not always the
same. In this validation, `input-headers` was not available as a binary
package. It is a small header-only package and can be installed from pkgsrc:

```sh
cd /usr/pkgsrc/devel/input-headers
sudo nice -n 19 make MAKE_JOBS=1 install
```

`xorgproto-2025.1` is similarly a small header package. It supplies
`presentproto` 1.4, required by Xwayland 24.1.12:

```sh
cd /usr/pkgsrc/x11/xorgproto
sudo nice -n 19 make MAKE_JOBS=1 install
```

## Thermal-safe source build

The ARM64 machine used for this work can overheat during sustained builds. Keep
the Xwayland build deliberately serial and low priority:

```sh
cd /usr/pkgsrc/wayland/xwayland
sudo nice -n 19 make MAKE_JOBS=1 install
```

Do not run NixBench, browsers, emulators, or other CPU-heavy work on that host
at the same time. A power loss or forced shutdown can leave Meson's build
directory locked. Once no Meson, Ninja, or pkgsrc Xwayland process remains,
remove only generated pkgsrc state and retry:

```sh
cd /usr/pkgsrc/wayland/xwayland
sudo make clean
sudo nice -n 19 make MAKE_JOBS=1 install
```

`make clean` affects the port's generated `work/` directory, not the NixBench
source checkout or a user's applications.

## Current native-X11 buildlink blocker

NetBSD 10.1's base X11 installation supplies `presentproto` 1.2. Xwayland
24.1.12 requires version 1.4 or newer. Although installing pkgsrc
`xorgproto-2025.1` provides `/usr/pkg/lib/pkgconfig/presentproto.pc` at version
1.4, the current Xwayland pkgsrc buildlink directory can still select
`/usr/X11R7` through `x11-links` and report version 1.2 during Meson
configuration.

Useful diagnostics are:

```sh
/usr/pkg/bin/pkg-config --modversion presentproto
grep -n . /usr/pkg/lib/pkgconfig/presentproto.pc
readlink /usr/pkgsrc/wayland/xwayland/work/.buildlink/lib/pkgconfig/presentproto.pc
```

The first two commands should report 1.4 after `xorgproto` is installed. If the
last command resolves to `/usr/pkg/share/x11-links/...`, Meson may still see
the base-system 1.2 metadata. The required next step is a pkgsrc buildlink-port
fix that makes this Xwayland configuration prefer the installed `xorgproto`
metadata over the native X11 compatibility links. Do not claim Xwayland support
on this ARM64 host until a clean build installs `/usr/pkg/bin/Xwayland` and a
fresh NixBench session successfully launches an X11 client.
