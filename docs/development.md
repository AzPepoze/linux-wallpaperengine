# Development

Back to the [README](../README.md).

## Build

The Wayland protocol files come from the `lib/wlr-protocols` submodule. Clone with `--recurse-submodules`, or run `git submodule update --init` in an existing checkout. Without it, the Wayland feature is not built.

| Goal | Command |
| --- | --- |
| Build the default configuration | `xmake` |
| Debug build | `xmake f -m debug`, then `xmake` |
| Release build | `xmake f -m release`, then `xmake` |
| Run a wallpaper | `xmake run linux-wallpaperengine "/path/to/wallpaper"` |
| Run a video | `xmake run linux-wallpaperengine "/path/to/video.mp4"` |
| Clean build outputs | `xmake clean` |
| Static analysis (cppcheck) | `xmake check` |
| Unit tests | `xmake test` |
| Format source | `xmake format` |
| Debug effect sandbox | `xmake sandbox` |

Build outputs go to `bin/<mode>/`. The optional feature files (`libwayland.so`, `libx11.so`, `libmpris.so`) are built next to the binary.

## Extracting packages

| Goal | Command |
| --- | --- |
| Extract a package | `bin/<mode>/linux-wallpaperengine --extract-only "/path/to/scene.pkg"` |
| List GPUs | `bin/<mode>/linux-wallpaperengine --list-gpus` |
| Pick a GPU | `bin/<mode>/linux-wallpaperengine --gpu <index-or-name> "/path"` |

## Release and debug builds

- **Release:** reads `scene.pkg` in place from a memory map. Nothing is extracted.
- **Debug:** extracts to `extracted/<wallpaper-id>/` and keeps the `project.json` next to it.
- Compiled shaders are cached in `$XDG_CACHE_HOME/linux-wallpaperengine/` (default `~/.cache/linux-wallpaperengine/`).

## Render diagnostics (debug builds)

Diagnostics are off unless you ask for them:

```bash
bin/debug/linux-wallpaperengine --diagnose "/path/to/wallpaper"
```

They capture pass images, scene stages, render graphs, shader code and uniforms.

- `--diagnose-frame <n>` captures another frame (default 100). A window that is off screen advances frames slowly.
- `--diagnose-deterministic` uses a fixed time step and seeded random numbers. The same frame then renders the same way on each run. Window size and cursor position still affect the result.

All diagnostic options are listed in [Options](options.md#debug-builds-only).

## Feature status

See [features.md](features.md) for what is supported, partly supported and missing. [wallpaper-engine-assets.md](wallpaper-engine-assets.md) lists the content from a Wallpaper Engine install that the engine loads.

When you change behaviour, update both `docs/features.md` (the summary) and `docs/features-detail.md` (the item list) in the same commit.
