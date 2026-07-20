# HTML desktop themes

[Back to the main README](../README.md)

## Selected architecture

NixBench uses the "HTML only for decoration" model. Applications continue to
submit ordinary native Wayland, Xwayland, or SDL content. A browser engine is
used only to paint the desktop shell around that content:

- the desktop and wallpaper layer;
- window borders, title bars, controls, shadows, and resize affordances;
- the global bar and application menus; and
- optional transition and state animations.

Theme files are standard HTML5, CSS, and SVG. NixBench does not translate HTML
elements into native widgets, and applications do not become web applications.
The first renderer target is WebKitGTK because that engine is already exercised
by GTK applications on the NetBSD standalone compositor.

```text
ordinary-user processes

  native application surface       HTML theme renderer
  (Wayland, Xwayland, SDL)          (WebKitGTK)
             |                            |
             +------------+---------------+
                          |
                 NixBench desktop core
          policy, geometry, focus, stacking, input,
          clipping, damage, and final composition
                          |
             privileged wsdisplay helper
          device access and frame presentation only
```

The browser and its content processes always run with the ordinary user's
credentials. The privileged console helper never parses HTML, CSS, SVG, theme
manifests, or theme assets.

## Renderer atlas

One browser document maintains a packed, transparent render atlas. Each
desktop component has a named rectangular tile in that atlas:

```text
+----------------------+----------------------+
| window:7 decoration  | window:12 decoration|
+----------------------+----------------------+
| global bar and menus | desktop layer        |
+----------------------+----------------------+
```

The compositor copies or samples tiles independently. It can therefore draw,
for every window from bottom to top, that window's decoration followed by its
native client content. A single full-screen HTML overlay would not work: the
decoration of a lower window could incorrectly cover the client content of a
higher window.

The renderer surface authenticates the private chrome-atlas client and is never
managed as an application window. The core sends immutable window state and
geometry to the renderer. With protocol version 2, the renderer snapshots the
complete transparent atlas into a private file descriptor; the compositor
copies those Cairo ARGB32 pixels and atomically commits them with the matching
tile layout and hit regions. Explicit full snapshots ensure that transparent
pixels vacated during a resize cannot retain an older decoration.

### Window geometry contract

A window atlas tile has the exact dimensions of the outer NixBench frame. The
renderer injects these CSS custom properties from the native geometry model:

- `--nixbench-frame-border` (currently 3px);
- `--nixbench-title-height` (currently 24px);
- `--nixbench-menu-height` (22px for the CDE client menu band);
- `--nixbench-footer-height` (currently 20px);
- `--nixbench-gadget-margin` (currently 4px); and
- `--nixbench-control-size` (currently 16px).

The native application content begins after the compositor-owned title and
optional theme menu-band areas. Content is composited later and intentionally
covers the middle of the atlas tile. Bundled themes use the injected variables
so visible controls, separators, client-content insets, and native hit testing
remain aligned.

The first state vocabulary is:

- window identifier, title, size, scale, and active state;
- maximized, minimized, urgent, and fullscreen state;
- visibility and placement of minimize, maximize, and close controls;
- global-menu labels, item states, and open-menu state; and
- pointer hover and pressed action identifiers.

The compositor remains authoritative. HTML cannot move, resize, focus, close,
minimize, or maximize a window directly. Elements identify intent with
attributes such as `data-nixbench-action="close"`; the trusted renderer bridge
reports the element's bounds and action identifier, and the core applies the
normal window policy after its own hit test.

## Bundle format

Installed themes live below `${CMAKE_INSTALL_DATADIR}/nixbench/themes`.
Per-user themes will be discovered below
`~/.local/share/nixbench/themes`. A bundle contains:

```text
Fantasy/
|-- theme.conf
|-- desktop.html
|-- window.html
|-- menubar.html
|-- theme.css
`-- assets/
    |-- unicorn-window-frame.png
    |-- enchanted-dock.png
    `-- woods.png
```

`theme.conf` is a small, non-executable manifest. Format 1 declares the stable
theme identifier, display name, entry documents, stylesheet, and requested
capabilities. Entry documents are HTML fragments loaded into the renderer's
trusted atlas document rather than independent browsing contexts.

The repository begins with three personalities:

- **Fantasy**, an original storybook theme with a unicorn-and-rainbow window
  frame, an enchanted woodland dock, GTK application launchers, minimized
  window cells, and the project-provided `woods.png` wallpaper;
- **CDE**, an original recreation of the visual conventions of 1990s Unix
  workstations, with warm-gray bevels, orange focused title bars, slate
  inactive title bars, a textured blue-gray workspace, a right-click desktop
  menu, and a movable front panel; and
- **BeOS-inspired**, an original theme with compact offset title tabs and warm
  yellow controls.

Names describe the inspiration. Bundles must use original or compatibly
licensed artwork and may not copy proprietary system assets.

The CDE personality also takes visual-direction cues from the MIT-licensed
[CDE/Motif desktop HTML simulation](https://github.com/igtoth/cde-motif-desktop),
while retaining NixBench's own markup, geometry contract, compositor policy,
and implementation.

## Safety policy

Downloadable themes are untrusted presentation data. The initial renderer
enforces all of the following:

- theme-supplied JavaScript is disabled;
- HTTP, HTTPS, WebSocket, and other network requests are denied;
- cookies, persistent web storage, service workers, forms, navigation, media
  capture, downloads, and browser plug-ins are disabled;
- only files inside the selected, canonicalized theme directory may be read;
- document, decoded-image, SVG, atlas-dimension, memory, and animation-rate
  limits are enforced; and
- renderer failure or timeout preserves the last good pixels briefly and then
  falls back to the built-in native Classic renderer.

NixBench may inject its own fixed bridge script to update state, collect
`data-nixbench-action` rectangles, and observe animation damage. That script is
part of NixBench, not the theme, and exposes no general system API.

## Performance rules

Static tiles are cached and rendered only when their state, size, assets, or
CSS animation changes. Animations are paused for minimized and fully obscured
windows and can be rate-limited independently of pointer and client-surface
updates. The existing desktop damage path remains responsible for copying only
changed output regions to `wsdisplay`.

The browser renderer is asynchronous. Window movement, input routing, VT
switching, recovery, and application presentation must never wait for an HTML
frame. A late renderer frame is skipped rather than stalling the desktop.

## Delivery milestones

1. Install and discover validated bundles while continuing to draw Classic
   native decorations.
2. Add the private renderer-atlas state and buffer protocol.
3. Render one title-bar tile through WebKitGTK behind an explicit experimental
   option, with native hit testing and Classic fallback.
4. Render complete per-window frames and controls, with native input policy.
5. Add the CDE desktop tile, floating right-click menu, client menu inset, and
   movable front panel with its HTML/CSS analog clock.
6. Connect front-panel launchers, minimized-window affordances, and workspace
   controls, then add the Settings theme selector.
7. Enable bounded CSS transitions and SVG animation after idle and animated
   CPU measurements on amd64 and arm64 NetBSD hardware.

The Fantasy milestone exercises an additional compositor path: its irregular
frame artwork is composited after the native client surface so the unicorn
head, mane, and tail can surround the client opening without hiding its
contents. Per-side fractional decoration insets define that safe opening; the
shell expands the outer frame to preserve the client's requested logical size,
and reconfigures a client if the output edge forces the outer silhouette to be
clamped. The Classic rectangular base is omitted so transparent corners reveal
the wallpaper. Native geometry, clipping, control hit testing, and application
input remain authoritative. Its shadowed dock is also treated as a topmost
input region, matching the HTML pixels drawn above application windows.

The first hardware gate requires unchanged behavior for HTML/CSS NixClock,
GTK/Wayland applications, SDL/Wayland applications, and Xwayland windows. A
theme-renderer crash must not terminate the desktop or compromise console
restoration.

## Renderer preview

When WebKitGTK support is detected, the build includes the experimental
`nixbench-html-theme-renderer`. Before atlas integration is enabled it can
render any validated component in a normal preview window or save a transparent
PNG through WebKitGTK's snapshot API:

```sh
./build/nixbench-html-theme-renderer \
  --theme ./themes/Fantasy \
  --component window \
  --size 640x96

./build/nixbench-html-theme-renderer \
  --theme ./themes/CDE \
  --component window \
  --size 640x96 \
  --snapshot /tmp/nixbench-cde-title.png
```

The private compositor endpoint authenticates one atlas renderer, keeps its
WebKit surface out of the managed application list, and atomically validates
matching pixel, tile, and action-region generations. The same executable is
also the live renderer. The session core creates a fresh 256-bit token, enables
the endpoint, and starts the renderer with the token kept out of the
environment.

The live renderer is intentionally opt-in and paints every eligible visible
window frame from one packed atlas. Native NixBench geometry and hit testing remain in use,
and the native Classic frame remains underneath as an immediate fallback. In
a development checkout, select a bundle for one standalone session with:

```sh
NIXBENCH_HTML_THEME=cde ./tools/run-wsdisplay-session.sh
```

The development launcher forwards this selector explicitly across the
privilege boundary and, like the installed `nixbench-session` command, enables
rootless Xwayland by default when an Xwayland executable is available.

The accepted identifiers are `fantasy`, `cde`, and `beos`; legacy `motif`
configuration is migrated to `cde`. A non-Classic
`windows.theme` value in the user configuration selects the same renderer;
the environment variable takes precedence for short experiments. Unknown or
unavailable bundles, renderer startup failure, and renderer disconnect all
leave Classic decorations operational. Front-panel application wiring,
renderer restart policy, and the Settings selector remain later milestones.
