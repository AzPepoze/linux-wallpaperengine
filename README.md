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

| Key | Value | Default | Description |
| --- | --- | --- | --- |
| `engine_path` | `<path>` | auto-detected | Wallpaper Engine install root |
| `default_wallpaper` | `<path>` | — | Wallpaper to open when none is given (`wallpaper_path` is an accepted alias) |
| `scaling_mode` | `default\|fit\|fill\|stretch` | fit | Scaling mode; same as `--scaling` |
| `parallax_smoothing` | `<seconds>` | 0.1 | Parallax response time; overrides the scene's value |
| `parallax_scale` | `<factor>` | 50.0 | Particle parallax multiplier |
| `transition` | `<name\|none\|random>` | fade | Transition shader (also `0`-`26`) |
| `transition_duration_ms` | `<ms>` | 1000 | Transition length |
| `transition_mode` | `freeze\|continue` | freeze | What the outgoing wallpaper does during the fade |
| `web_transport` | `auto\|dma-buf\|off-screen\|snapshot` | auto | Web wallpaper frame transport |
| `web_devtools_port` | `<port>` | 9222 | DevTools port |
| `web_devtools_browser` | `<cmd>` | xdg-open | Browser command for DevTools |

Environment variables:

- `WALLPAPER_ENGINE_PATH` — Wallpaper Engine install root
- `DEFAULT_WALLPAPER_PATH` or `WALLPAPER_PATH` — wallpaper to open when none is given

## Command-line options

The wallpaper path may come first or last. `-h` / `--help` prints this same reference from the binary.

### Wallpaper

| Option | Value | Default | Description |
| --- | --- | --- | --- |
| `--assets-dir` | `<path>` | auto-detected | Wallpaper Engine install root or its `assets/` directory |
| `--pkg` | `<path>` | — | Treat the path as a package |
| `--extract-only` | | off | Extract the package and exit |
| `--extract-dir` | `<path>` | — | Extraction target, with `--extract-only` |
| `--set-property` | `<name=value>` | — | Override a project property; repeatable |

### Graphics

| Option | Value | Default | Description |
| --- | --- | --- | --- |
| `--gpu` | `<id>` | auto | Select a GPU by index or name |
| `--list-gpus` | | off | List the available GPUs and exit |
| `-f`, `--fps` | `<n>` | display rate | Frame-rate cap; `0` or omitted follows the display (vsync) |
| `--scaling` | `default\|fit\|fill\|stretch` | fit | `fill` crops to cover, `fit` letterboxes, `stretch` fills without preserving aspect ratio; `default` uses cover |
| `--cover` | | off | Force cover scaling, ignoring the project |
| `--clamp` | `<mode>` | ignored | Accepted and ignored |
| `--video-ram` | | off | Load video files into RAM instead of streaming |
| `--performance-profile` | | off | Log presented FPS, mean/p95 frame intervals, CPU work/acquisition/presentation, and asynchronous GPU frame/image-effect spans after a 5-second warmup. GPU spans can include queue dependencies; window presentation timing may be unavailable. |
| `--effect-resolution` | `auto\|native` | auto | Size verified shake chains to their displayed size; native preserves source resolution. Also configurable as `effect_resolution` in config.json. |
| `--script-profile` | | off | Log the slowest scripts every 10 seconds |

### Display

| Option | Value | Default | Description |
| --- | --- | --- | --- |
| `-r`, `--screen-root` | `<output>` | — | Draw as a `wlr-layer-shell` surface on this output |
| `--layer` | `background\|bottom\|top\|overlay` | background | Layer-shell layer |
| `--layer-size` | `<WxH>` | — | Debug: small anchored rectangle, e.g. `320x180` |
| `--layer-anchor` | `<edges>` | — | Debug: anchor edges for `--layer-size`, e.g. `top-left` |

Needs a build with `--layer_shell=y` and `WAYLAND_DISPLAY`; otherwise, or if the output is not found, the app runs in a window.

### Transition

| Option | Value | Default | Description |
| --- | --- | --- | --- |
| `--transition` | `<name\|none\|random>` | fade | Transition shader |
| `--transition-duration` | `<ms>` | 1000 | Transition length |
| `--transition-mode` | `freeze\|continue` | freeze | What the outgoing wallpaper does during the fade |

### Control

| Option | Value | Default | Description |
| --- | --- | --- | --- |
| `--no-control` | | off | Don't hand off to or own a control socket |

### Audio

| Option | Value | Default | Description |
| --- | --- | --- | --- |
| `--no-audio` | | off | Disable audio |
| `-s`, `--silent`, `--mute` | | off | Disable audio (aliases of `--no-audio`) |
| `--volume` | `<n>` | ignored | Accepted for launcher compatibility; ignored |

### Web

| Option | Value | Default | Description |
| --- | --- | --- | --- |
| `--web-transport` | `auto\|dma-buf\|off-screen\|snapshot` | auto | Frame transport |
| `--no-web-devtools` | | off | Disable the localhost remote-debug server |
| `--web-devtools-port` | `<port>` | 9222 | DevTools port |
| `--web-devtools-browser` | `<cmd>` | xdg-open | Browser command for DevTools |

`web_transport` in `config.json` sets the transport too; the CLI flag overrides it, and an unknown value is an error.

### Diagnostics (debug builds)

| Option | Value | Default | Description |
| --- | --- | --- | --- |
| `--diagnose`, `--diagnostics` | | off | Enable render diagnostics |
| `--diagnose-frame` | `<n>` | 100 | Frame to capture |
| `--diagnose-final-only` | | off | Capture only the final output |
| `--diagnose-deterministic` | | off | Fixed 1/60 s step and seeded RNG |
| `--exit-after-diagnose` | | off | Quit once the capture completes |
| `--disable-effects` | `<pattern>` | — | Disable effects matching a substring (`*/all` = all) |
| `--disable-particles` | | off | Disable particles |
| `--disable-bloom` | | off | Disable bloom |
| `--sandbox` | | off | Run the debug effect sandbox |
| `--no-ui` | | off | Start without the ImGui UI |

### Particles

| Option | Value | Default | Description |
| --- | --- | --- | --- |
| `--particle-debug`, `--particle-debug-bounds` | | off | Draw particle bounds |
| `--particle-debug-velocity` | | off | Draw particle velocity vectors |
| `--particle-debug-velocity-scale` | `<f>` | 0.05 | Velocity vector scale |
| `--particle-debug-max-particles` | `<n>` | 128 | Cap the number of particles drawn |

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
| Run static analysis (cppcheck)                       | `xmake check`                                                         |
| Build and run all unit checks                        | `xmake test`                                                          |
| Format source files                                  | `xmake format`                                                        |
| Build and launch the debug effect sandbox            | `xmake sandbox`                                                       |

Build outputs are written to `bin/<mode>/`.

Release builds read `scene.pkg` in place from a memory map, so nothing is extracted and startup does not wait on disk writes. Debug runtime loads extract to `extracted/<wallpaper-id>/` and preserve the adjacent `project.json`; release loads read that metadata beside the original package. `--extract-only` uses its requested extraction directory. Compiled shaders are cached in `$XDG_CACHE_HOME/linux-wallpaperengine/` (default `~/.cache/linux-wallpaperengine/`) and reused across launches.

### Render Diagnostics (Debug Mode)

Render diagnostics are available in debug builds and are opt-in. Enable them with `--diagnose` or `--diagnostics`:

```bash
bin/debug/linux-wallpaperengine --diagnose "/path/to/wallpaper"
```

Diagnostics can capture render-pipeline state including pass images, scene stages, render graphs, shader code, and uniforms. Use `--diagnose-frame <n>` to capture an earlier frame than the default 100 (a window that is not on screen advances frames slowly). Add `--diagnose-deterministic` to use a fixed 1/60 s step and a seeded random generator, so the same frame renders identically on every run (the window size and cursor position still affect the result).

## Feature support

See [docs/features.md](docs/features.md) for what is supported, partially supported and missing, with the details of every partial feature. [docs/wallpaper-engine-assets.md](docs/wallpaper-engine-assets.md) lists the content from a Wallpaper Engine installation that this project loads at runtime.

When you change behaviour, update `docs/features.md` in the same commit.
