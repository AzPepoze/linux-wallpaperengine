# Capture

Back to the [README](../README.md).

Capture saves pictures of a wallpaper at the times you choose. It runs without a window, so you can make screenshots from a script or over SSH.

> [!NOTE]
> A capture run has no window, no sound, no UI and no control socket. It also ignores the global mouse. It exits by itself when it is done.

## Quick start

Take three shots at 0.5, 2 and 10 seconds of scene time:

```bash
linux-wallpaperengine /path/to/wallpaper --capture 0.5,2,10
```

Take one shot every 5 seconds, up to 60 seconds:

```bash
linux-wallpaperengine /path/to/wallpaper --capture-every 5 --capture-until 60
```

Take one shot to a file:

```bash
linux-wallpaperengine /path/to/wallpaper --screenshot ./wallpaper.png --screenshot-delay 3
```

## Flags

| Flag | Value | Default | What it does |
| --- | --- | --- | --- |
| `--capture` | `<times>` | | Shots at these scene times, in seconds, separated by commas. Example: `0.2,1,3` |
| `--capture-every` | `<s\|frame>` | | Repeat a shot every `s` seconds, or on every frame |
| `--capture-from` | `<s>` | 0 | Start time of the repeat. Needs `--capture` or `--capture-every` |
| `--capture-until` | `<s>` | | Stop the repeat at this scene time. The time itself is included |
| `--capture-limit` | `<n>` | 100 | Most shots to take. The run exits when it is reached |
| `--capture-dir` | `<dir>` | `./captures/<wallpaper>` | Folder for the output |
| `--capture-size` | `<WxH>` | 1920x1080 | Size of each picture |
| `--capture-fps` | `<n>` | 60 | Frames per second of scene time. See [How timing works](#how-timing-works) |
| `--capture-dump` | | off | Also save a diagnostics folder for each shot. Debug builds only |

> [!IMPORTANT]
> `--capture-from`, `--capture-until`, `--capture-limit`, `--capture-dir` and `--capture-dump` need `--capture` or `--capture-every`.

## Output

Files go to the output folder. The default is `./captures/<wallpaper>`.

```
captures/<wallpaper>/
├── shot-0001_t0.500s.png
├── shot-0002_t2.000s.png
├── shot-0003_t10.000s.png
└── manifest.json
```

- The name is `shot-<number>_t<time>s.png`. The time is the requested scene time, with 3 decimals.
- `manifest.json` lists every shot and the settings of the run.

### manifest.json

Top level:

| Field | What it holds |
| --- | --- |
| `wallpaper` | Name of the wallpaper |
| `width`, `height` | Picture size in pixels |
| `fps` | Frames per second of scene time |
| `shots` | List of shots (see below) |

Each shot in the list:

| Field | What it holds |
| --- | --- |
| `index` | Shot number, starting at 1 |
| `requested_time` | Scene time you asked for, in seconds |
| `scene_time` | Scene time of the frame that was saved, in seconds |
| `file` | File name of the picture |

## Screenshot mode

`--screenshot` takes one shot and writes it to the exact path you give.

| Flag | Value | Default | What it does |
| --- | --- | --- | --- |
| `--screenshot` | `<path>` | | Path of the PNG file to write |
| `--screenshot-delay` | `<s>` | 0 | Scene time of the shot, in seconds |

- It writes no `manifest.json`.
- It does not take the `--capture-*` flags. Mixing them is an error.

## Diagnostics bundle (debug builds)

`--capture-dump` saves a folder for each shot, for example `shot-0001/`. The folder holds:

- the render graph (`rendergraph.json`, `rendergraph.md`)
- the shader sources (`shaders/`)
- the uniform values and shader combos (`uniforms.json`, `combos.json`)
- a PNG per render pass, when the wallpaper has effects

> [!NOTE]
> `--capture-dump` is only in debug builds. See [Build](build.md).

## How timing works

Scene time moves in fixed steps of `1 / fps`. With the default 60 fps, a step is about 0.017 seconds.

```
scene time   0.000   0.017   0.033   ...   0.500   0.517   ...
request 0.51                                    ^ shot is taken here (0.517)
```

- A shot is taken on the first frame whose scene time is at or after the requested time.
- So `scene_time` in the manifest can be up to one step later than `requested_time`.
- Only one shot is taken per frame. If several shots are due at once, the rest wait for the next frame.

## Same result each run

- The random numbers use a fixed seed.
- Each frame uses the fixed step.

So the same wallpaper gives the same picture on each run. Widgets that show a live clock or FPS counter still show the real value, so those pixels change.

## Limits

> [!WARNING]
> Video and web wallpapers may not move exactly with scene time. Web wallpapers run in a separate process.

- Capture runs with no window, so the output size comes from `--capture-size`, not from a screen.
- Wallpapers with a live clock or FPS counter do not give the same pixels each run.
