# Linux Wallpaper Engine

Linux renderer for Wallpaper Engine projects.

## Requirements

### Build tools

- GCC or Clang with C++20 support
- [xmake](https://xmake.io/)
- `pkg-config`, to detect the optional system libraries
- `git`, used by xmake to fetch a few dependencies

### Required libraries

- Vulkan loader and headers, plus a working Vulkan driver for your GPU
- [Slang](https://github.com/shader-slang/slang) compiler and runtime libraries
- X11: `libX11`, `libXcursor`, `libXi`
- FFmpeg: `libavformat`, `libavcodec`, `libavutil`, `libswscale`, `libswresample`
- VA-API: `libva`, `libva-drm`
- `libdrm`

### Optional libraries

Detected automatically and enabled when present. Turn any of them off with `xmake f --<option>=n` (for example `xmake f --web=n`).

- Qt6 WebEngine — web wallpapers
- `libwayland-client`, `wayland-scanner`, `wayland-protocols` — Wayland layer shell
- `libxkbcommon` — text input on a layer surface
- `libsystemd` — MPRIS media session

Qt6 WebEngine package names: `qt6-webengine` (Arch), `qt6-webengine-dev` (Debian/Ubuntu), `qt6-qtwebengine-devel` (Fedora), `qt6-webengine-devel` (openSUSE).

### Fetched automatically by xmake

These do not need to be installed by hand:

- Sokol, linmath.h, Vulkan-Headers, LZ4, cJSON, stb, miniaudio, QuickJS
- Dear ImGui (debug builds only)

### At runtime

- A local Wallpaper Engine installation with its original `assets/` data. Point at it with `WALLPAPER_ENGINE_PATH` or the `engine_path` key in `config.json`.
- A Vulkan driver for your GPU.

### Arch Linux / CachyOS

Required:

```bash
sudo pacman -S --needed \
    xmake \
    gcc \
    pkgconf \
    git \
    vulkan-icd-loader \
    shader-slang \
    libx11 \
    libxcursor \
    libxi \
    ffmpeg \
    libva \
    libdrm
```

A Vulkan driver for your GPU is also required, for example `vulkan-radeon`, `vulkan-intel`, or the appropriate NVIDIA driver.

Optional, by feature:

```bash
sudo pacman -S --needed qt6-webengine                            # web wallpapers
sudo pacman -S --needed wayland wayland-protocols libxkbcommon   # Wayland layer shell
sudo pacman -S --needed systemd                                  # MPRIS media session
```

For development checks (`xmake check`, `xmake format`):

```bash
sudo pacman -S --needed clang-format cppcheck
```

## Configuration

The engine reads `config.json` from the working directory or one of its parents; `config.example.json` is a template. Every key is optional, and command-line flags override the file.

| Key | Description |
| --- | --- |
| `engine_path` | Wallpaper Engine install root |
| `default_wallpaper` | Wallpaper to open when none is given on the command line |
| `transition` | Default transition shader (a name, `none`, `random`, or `0`-`26`) |
| `transition_duration_ms` | Default transition length in milliseconds |
| `transition_mode` | `freeze` or `continue` |
| `web_transport` | Web wallpaper transport: `auto`, `dma-buf`, `off-screen` or `snapshot` |

Environment variables:

- `WALLPAPER_ENGINE_PATH` — Wallpaper Engine install root
- `DEFAULT_WALLPAPER_PATH` or `WALLPAPER_PATH` — wallpaper to open when none is given

## Command-line options

The wallpaper path may come first or last. `-h` / `--help` prints this same reference from the binary.

### Wallpaper

| Option | Value | Description |
| --- | --- | --- |
| `--assets-dir` | `<path>` | Wallpaper Engine install root or its `assets/` directory |
| `--pkg` | `<path>` | Treat the path as a package |
| `--extract-only` | | Extract the package and exit |
| `--extract-dir` | `<path>` | Extraction target, with `--extract-only` |
| `--set-property` | `<name=value>` | Override a project property; repeatable |

### Graphics

| Option | Value | Description |
| --- | --- | --- |
| `--gpu` | `<id>` | Select a GPU by index or name |
| `--list-gpus` | | List the available GPUs and exit |
| `-f`, `--fps` | `<n>` | Frame-rate cap; `0` follows the display (default 60) |
| `--scaling` | `default\|fit\|fill\|stretch` | `fill` crops to cover, `fit` letterboxes |
| `--cover` | | Force cover scaling, ignoring the project |
| `--clamp` | `<mode>` | Accepted and ignored |
| `--video-ram` | | Load video files into RAM instead of streaming |
| `--script-profile` | | Log the slowest scripts every 10 seconds |

### Display

| Option | Value | Description |
| --- | --- | --- |
| `-r`, `--screen-root` | `<output>` | Draw as a `wlr-layer-shell` surface on this output |
| `--layer` | `background\|bottom\|top\|overlay` | Layer-shell layer (default `background`) |
| `--layer-size` | `<WxH>` | Debug: small anchored rectangle, e.g. `320x180` |
| `--layer-anchor` | `<edges>` | Debug: anchor edges for `--layer-size`, e.g. `top-left` |

Needs a build with `--layer_shell=y` and `WAYLAND_DISPLAY`; otherwise, or if the output is not found, the app runs in a window.

### Transition

| Option | Value | Description |
| --- | --- | --- |
| `--transition` | `<name\|none\|random>` | Transition shader (default `fade`) |
| `--transition-duration` | `<ms>` | Transition length (default 1000) |
| `--transition-mode` | `freeze\|continue` | What the outgoing wallpaper does during the fade |

### Control

| Option | Value | Description |
| --- | --- | --- |
| `--no-control` | | Don't hand off to or own a control socket |

### Audio

| Option | Value | Description |
| --- | --- | --- |
| `--no-audio` | | Disable audio |
| `-s`, `--silent`, `--mute` | | Disable audio (aliases of `--no-audio`) |
| `--volume` | `<n>` | Accepted for launcher compatibility; ignored |

### Web

| Option | Value | Description |
| --- | --- | --- |
| `--web-transport` | `auto\|dma-buf\|off-screen\|snapshot` | Frame transport (default `auto`) |
| `--no-web-devtools` | | Disable the localhost remote-debug server |
| `--web-devtools-port` | `<port>` | DevTools port (default 9222) |
| `--web-devtools-browser` | `<cmd>` | Browser command for DevTools (default `xdg-open`) |

`web_transport` in `config.json` sets the transport too; the CLI flag overrides it, and an unknown value is an error.

### Diagnostics (debug builds)

| Option | Value | Description |
| --- | --- | --- |
| `--diagnose`, `--diagnostics` | | Enable render diagnostics |
| `--diagnose-frame` | `<n>` | Frame to capture (default 100) |
| `--diagnose-final-only` | | Capture only the final output |
| `--diagnose-deterministic` | | Fixed 1/60 s step and seeded RNG |
| `--exit-after-diagnose` | | Quit once the capture completes |
| `--disable-effects` | `<pattern>` | Disable effects matching a substring (`*/all` = all) |
| `--disable-particles` | | Disable particles |
| `--disable-bloom` | | Disable bloom |
| `--sandbox` | | Run the debug effect sandbox |
| `--no-ui` | | Start without the ImGui UI |

### Particles

| Option | Value | Description |
| --- | --- | --- |
| `--particle-debug`, `--particle-debug-bounds` | | Draw particle bounds |
| `--particle-debug-velocity` | | Draw particle velocity vectors |
| `--particle-debug-velocity-scale` | `<f>` | Velocity vector scale |
| `--particle-debug-max-particles` | `<n>` | Cap the number of particles drawn |

Launching with a wallpaper for a display that already has a running instance hands the switch to that instance over a Unix control socket and exits, instead of starting a second process. The running instance crossfades from its current frame to the new wallpaper using Wallpaper Engine's own transition shaders (loaded from the install). Pass `--no-control` to opt out. The effect, duration and mode can also be set per switch on the second launch. Audio crossfades too: the outgoing wallpaper's sound and the incoming wallpaper's sound ramp against the transition progress (`--transition-mode freeze|continue`, default `freeze`).

## Build and run

The Wayland layer-shell protocol XML comes from the `lib/wlr-protocols` git submodule. Clone with `git clone --recurse-submodules`, or run `git submodule update --init` in an existing checkout; without it the `layer_shell` option is disabled.

| Goal                                                 | Command                                                               |
| ---------------------------------------------------- | --------------------------------------------------------------------- |
| Build the default configuration                      | `xmake`                                                               |
| Run a wallpaper project directory or `.pkg` file     | `xmake run linux-wallpaperengine "/path/to/wallpaper"`                |
| Run a video wallpaper                                | `xmake run linux-wallpaperengine "/path/to/video.mp4"`                |
| Configure and run a debug build                      | `xmake f -m debug` then `xmake run linux-wallpaperengine`             |
| Configure and run a release build                    | `xmake f -m release` then `xmake run linux-wallpaperengine`           |
| List available GPUs                                  | `bin/<mode>/linux-wallpaperengine --list-gpus`                         |
| Select a GPU                                         | `bin/<mode>/linux-wallpaperengine --gpu <index-or-name> "/path"`      |
| Extract a package                                    | `bin/<mode>/linux-wallpaperengine --extract-only "/path/to/scene.pkg"` |
| Keep a video file in RAM instead of streaming it     | `bin/<mode>/linux-wallpaperengine --video-ram "/path/to/video.mp4"`   |
| Clean build outputs                                  | `xmake clean`                                                         |
| Validate formatting and static analysis              | `xmake check`                                                         |
| Build and run all unit checks                        | `xmake test`                                                          |
| Format source files                                  | `xmake format`                                                        |
| Validate, build, and launch the debug effect sandbox | `xmake sandbox`                                                       |

Build outputs are written to `bin/<mode>/`.

Release builds read `scene.pkg` in place from a memory map, so nothing is extracted and startup does not wait on disk writes. Debug builds, `--extract-only` and packages without a `scene.json` extract to `extracted/`. Compiled shaders are cached in `$XDG_CACHE_HOME/linux-wallpaperengine/` (default `~/.cache/linux-wallpaperengine/`) and reused across launches.

### Render Diagnostics (Debug Mode)

Render diagnostics are available in debug builds and are opt-in. Enable them with `--diagnose` or `--diagnostics`:

```bash
bin/debug/linux-wallpaperengine --diagnose "/path/to/wallpaper"
```

Diagnostics can capture render-pipeline state including pass images, scene stages, render graphs, shader code, and uniforms. Use `--diagnose-frame <n>` to capture an earlier frame than the default 100 (a window that is not on screen advances frames slowly). Add `--diagnose-deterministic` to use a fixed 1/60 s step and a seeded random generator, so the same frame renders identically on every run (the window size and cursor position still affect the result).

## Feature support

See [docs/features.md](docs/features.md) for what is supported, partially supported and missing, with the details of every partial feature. [docs/wallpaper-engine-assets.md](docs/wallpaper-engine-assets.md) lists the content from a Wallpaper Engine installation that this project loads at runtime.

When you change behaviour, update `docs/features.md` in the same commit.
