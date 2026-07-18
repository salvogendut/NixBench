# ARM64 NetBSD Xwayland build notes

[Back to the main README](../README.md)

This note records the first attempt to build rootless Xwayland for NixBench on
an ARM64 NetBSD 10.1 host. It is a troubleshooting record, not yet a supported
installation recipe. Xwayland's Present protocol buildlink issue was resolved
locally; a newer libdrm remains required before the final compile can proceed.

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

## Native-X11 Present protocol buildlink

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
the base-system 1.2 metadata. On this host, the local Xwayland port needed a
stricter buildlink requirement before its existing `xorgproto` include:

```make
BUILDLINK_API_DEPENDS.xorgproto+= xorgproto>=2025.1
```

After a `sudo make clean`, the generated link then resolved to
`/usr/pkg/lib/pkgconfig/presentproto.pc` and Meson accepted Present 1.4. This
is a local pkgsrc-port adjustment that should be proposed upstream separately;
it is not a NixBench source change.

## libdrm requirement after Present is fixed

Xwayland 24.1.12 next requires `libdrm >= 2.4.116`. NetBSD 10.1 base provides
only 2.4.109. On this ARM64 package repository, `pkgin search libdrm` did not
provide a newer binary package, so the pkgsrc `x11/libdrm` port is necessary.

That port deliberately refuses a native-X11 package set. Build it in modular
mode, serially and at low priority:

```sh
cd /usr/pkgsrc/x11/libdrm
sudo env X11_TYPE=modular nice -n 19 make MAKE_JOBS=1 install
```

Before starting, check the port's dependencies and install available binary
packages first. In particular, the generated documentation path can request
`py313-docutils` and `py313-pygments`; prefer:

```sh
sudo pkgin install py313-docutils py313-pygments
```

If pkgin has no suitable binary package, stop and reassess before allowing
pkgsrc to build a broad Python dependency chain on the thermally constrained
host. Do not claim Xwayland support on this ARM64 host until a clean build
installs `/usr/pkg/bin/Xwayland` and a fresh NixBench session successfully
launches an X11 client.
