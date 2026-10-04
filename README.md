# Linux Wallpaper Engine

Linux renderer for Wallpaper Engine projects.

## Requirements

### System requirements

- Linux
- GCC or Clang with C++20 support
- [xmake](https://xmake.io/)
- Vulkan development libraries and a working Vulkan GPU driver
- [Slang](https://github.com/shader-slang/slang) compiler/runtime libraries
- X11 development libraries (`libX11`, `libXcursor`, `libXi`)
- FFmpeg development libraries (`libavformat`, `libavcodec`, `libavutil`, `libswscale`, `libswresample`)
- VA-API development libraries (`libva`, `libva-drm`)
- `libdrm`
- Optional, for web wallpapers: Qt6 WebEngine (`qt6-webengine`). Detected automatically and enabled by default when installed; disable with `xmake f --web=n`. Distro package names: `qt6-webengine` (Arch), `qt6-webengine-dev` (Debian/Ubuntu), `qt6-qtwebengine-devel` (Fedora), `qt6-webengine-devel` (openSUSE).
- A local Wallpaper Engine installation with its original `assets/` data (required at runtime; set `WALLPAPER_ENGINE_PATH` or `engine_path` in `config.json`)

### Dependencies managed by xmake

The following dependencies are fetched by xmake and normally do not need to be installed manually:

- Sokol
- linmath.h
- Vulkan-Headers
- LZ4
- miniaudio
- cJSON
- stb
- QuickJS (SceneScript text objects)
- Dear ImGui (debug builds only)
- libwayland-client, `wayland-scanner` (optional, for the desktop layer backend)

### Arch Linux / CachyOS

```bash
sudo pacman -S --needed \
    xmake \
    gcc \
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

Optional, for rendering on the desktop layer under Wayland (`xmake f --layer_shell=y`, enabled automatically when found):

```bash
sudo pacman -S --needed wayland wayland-protocols
```

Optional, for web wallpapers:

```bash
sudo pacman -S --needed qt6-webengine
```

For development checks:

```bash
sudo pacman -S --needed clang cppcheck
```

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

Launcher-compatible options (the wallpaper path may come first or last):

| Option | Effect |
| --- | --- |
| `--assets-dir <path>` | Wallpaper Engine install root or its `assets/` directory |
| `-f, --fps <n>` | Cap the frame rate (default 60; `0` renders at the display rate) |
| `-s, --silent` | Disable audio |
| `--script-profile` | Log the scripts that use the most time every 10 seconds |
| `--video-ram` | Load video files into RAM (one shared copy). By default video is streamed from disk, so large 4K files stay out of memory. `LWE_VIDEO_RAM=1` does the same |
| `--scaling <default\|fit\|fill\|stretch>` | `fill` crops to cover, `fit` letterboxes (`stretch` currently behaves like `fit`) |
| `--clamp <mode>` | Accepted and ignored |
| `-r, --screen-root <output>`, `--layer <background\|bottom\|top\|overlay>` | Draw as a `wlr-layer-shell` wallpaper surface on the named output (default layer `background`), anchored to all edges with pointer parallax. Needs a build with `--layer_shell=y` and `WAYLAND_DISPLAY`; otherwise, or if the output is not found, the app logs the reason and runs in a window |
| `--layer-size <WxH>`, `--layer-anchor <edges>` | Debug builds only: use a small anchored rectangle (for example `320x180` and `top-left`) instead of the full output |
| `--transition <name\|none\|random>` | Shader transition used when switching wallpapers (`fade`, `mosaic`, ..., `boilover`, or `0`-`26`; default `fade`) |
| `--transition-duration <ms>` | Transition length in milliseconds (default `1000`) |
| `--transition-mode <freeze\|continue>` | What the outgoing wallpaper does once the transition ends. `freeze` (default) holds its last frame for the fade; `continue` keeps it animating through the fade (scene, video and web), then destroys it. Both modes crossfade the outgoing audio into the incoming audio |
| `--no-control` | Do not hand off to a running instance or own a control socket; always start a separate process |

Launching with a wallpaper for a display that already has a running instance hands the switch to that instance over a Unix control socket and exits, instead of starting a second process. The running instance crossfades from its current frame to the new wallpaper using Wallpaper Engine's own transition shaders (loaded from the install). Pass `--no-control` to opt out. The effect, duration and mode can also be set per switch on the second launch. Audio crossfades too: the outgoing wallpaper's sound and the incoming wallpaper's sound ramp against the transition progress (`--transition-mode freeze|continue`, default `freeze`).

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
