# Options

Back to the [README](../README.md).

Command-line flags override `config.json`. The wallpaper path can come first or last. `-h` / `--help` prints the same list from the binary.

## Config file

The engine reads `config.json` from the working directory or one of its parents. `config.example.json` is a template. Every key is optional.

| Key | Value | Default | What it does |
| --- | --- | --- | --- |
| `engine_path` | `<path>` | auto | Wallpaper Engine install root |
| `default_wallpaper` | `<path>` | — | Wallpaper to open when none is given (`wallpaper_path` also works) |
| `scaling_mode` | `default\|fit\|fill\|stretch` | fit | Same as `--scaling` |
| `parallax_smoothing` | `<seconds>` | 0.1 | Parallax response time |
| `parallax_scale` | `<factor>` | 50.0 | Particle parallax multiplier |
| `transition` | `<name\|none\|random>` | fade | Transition shader (also `0`-`26`) |
| `transition_duration_ms` | `<ms>` | 1000 | Transition length |
| `transition_mode` | `freeze\|continue` | freeze | What the old wallpaper does during the transition |
| `web_transport` | `auto\|dma-buf\|off-screen\|snapshot` | auto | Web frame transport |
| `web_devtools_port` | `<port>` | 9222 | DevTools port |
| `web_devtools_browser` | `<cmd>` | xdg-open | Browser for DevTools |
| `audio_device` | `<name>` | system output | Same as `--audio-device` |
| `pointer` | `auto\|x11\|hyprland\|surface\|evdev` | auto | Same as `--pointer` |
| `resolution` | `auto\|native\|WxH` | auto | Same as `--resolution` |

## Environment variables

| Variable | What it does |
| --- | --- |
| `WALLPAPER_ENGINE_PATH` | Wallpaper Engine install root |
| `DEFAULT_WALLPAPER_PATH` or `WALLPAPER_PATH` | Wallpaper to open when none is given |
| `LWE_PLUGIN_DIR` | Extra folder to look for optional feature files in |

## Command-line options

### Wallpaper

| Option | Value | Default | What it does |
| --- | --- | --- | --- |
| `--assets-dir` | `<path>` | auto | Wallpaper Engine install root or its `assets/` folder |
| `--pkg` | `<path>` | — | Treat the path as a package |
| `--extract-only` | | off | Extract the package and exit |
| `--extract-dir` | `<path>` | — | Where to extract, with `--extract-only` |
| `--set-property` | `<name=value>` | — | Set a wallpaper property; repeatable |

### Graphics

| Option | Value | Default | What it does |
| --- | --- | --- | --- |
| `--gpu` | `<id>` | auto | Pick a GPU by index or name |
| `--list-gpus` | | off | List GPUs and exit |
| `-f`, `--fps` | `<n>` | display rate | Frame-rate cap; `0` follows the display |
| `--scaling` | `default\|fit\|fill\|stretch` | fit | `fill` crops, `fit` letterboxes, `stretch` ignores aspect ratio |
| `--intro-zoom` | `<factor>` | 1.0 | Startup zoom. The view eases from this zoom to 1.0. `1.0` is off |
| `--intro-duration` | `<seconds>` | 4 | Length of the startup zoom |
| `--cover` | | off | Force cover scaling |
| `--clamp`, `--clamping` | `<mode>` | ignored | Accepted, does nothing |
| `--video-ram` | | off | Load video into RAM instead of streaming |
| `--performance-profile` | | off | Log frame timing after a 5-second warmup |
| `--resolution` | `auto\|native\|WxH` | auto | Render size of effect chains, puppets and models. `auto` follows the output size. `native` uses the authored size. `WxH` sizes them as if the output were that size (for example `3840x2160`). `--effect-resolution` still works |
| `--script-profile` | | off | Log the slowest scripts every 10 seconds |

### Display

| Option | Value | Default | What it does |
| --- | --- | --- | --- |
| `-r`, `--screen-root` | `<output>` | — | Draw on this output (for example `DP-4`) |
| `--window` | `<XxYxWxH>` | 1280x720 | Windowed mode at this size. The X,Y position is not applied. Ignored with `-r` or `--layer` |
| `--layer` | `background\|bottom\|top\|overlay` | background | Which layer to draw on |

On Wayland, `-r` uses the layer-shell feature. On X11, it uses the X11 desktop feature. If neither works, the wallpaper runs in a normal window.

### Pointer

| Option | Value | Default | What it does |
| --- | --- | --- | --- |
| `--pointer` | `auto\|x11\|hyprland\|surface\|evdev` | auto | Where the mouse position comes from |

- `auto` (default): tries these in order and uses the first that works: `x11`, `hyprland`, `surface`, `evdev`.
- `x11`: the real cursor position on an X11 session.
- `hyprland`: the real cursor position on Hyprland.
- `surface`: the real position while the mouse is over the wallpaper.
- `evdev`: mouse movement from `/dev/input`. An estimate, and it can drift.

If the source you pick does not work, the program moves down the same list and logs a warning.

### Transition

| Option | Value | Default | What it does |
| --- | --- | --- | --- |
| `--transition` | `<name\|none\|random>` | fade | Transition shader |
| `--transition-duration` | `<ms>` | 1000 | Transition length |
| `--transition-mode` | `freeze\|continue` | freeze | What the old wallpaper does during the transition |

### Control

| Option | Value | Default | What it does |
| --- | --- | --- | --- |
| `--no-control` | | off | Don't hand off to a running wallpaper, and don't open a control socket |

### Audio

| Option | Value | Default | What it does |
| --- | --- | --- | --- |
| `--no-audio` | | off | Mute the output. The visualizer keeps working |
| `-s`, `--silent`, `--mute` | | off | Same as `--no-audio` |
| `--volume` | `<n>` | 100 | Volume, 0-100 |
| `--audio-device` | `<name>` | system output | Output to play on. Part of the name is enough. `default` means system output |
| `--no-audio-processing` | | off | Stop the spectrum capture. The visualizer stays at zero and uses less CPU |
| `--list-audio-devices` | | | List output devices and exit |

> [!NOTE]
> `--no-audio-processing` only affects the visualizer. Sound still plays.

> [!TIP]
> Set `audio_device` in `config.json` to pick an output every time.

### Web

| Option | Value | Default | What it does |
| --- | --- | --- | --- |
| `--web-transport` | `auto\|dma-buf\|off-screen\|snapshot` | auto | Frame transport |
| `--no-web-devtools` | | off | Turn off the local debug server |
| `--web-devtools-port` | `<port>` | 9222 | DevTools port |
| `--web-devtools-browser` | `<cmd>` | xdg-open | Browser for DevTools |

### Info

| Option | Value | Default | What it does |
| --- | --- | --- | --- |
| `--whoareyou` | | — | Print the engine identity as one JSON line, then exit |

### Particles

| Option | Value | Default | What it does |
| --- | --- | --- | --- |
| `--particle-debug`, `--particle-debug-bounds` | | off | Draw particle bounds |
| `--particle-debug-velocity` | | off | Draw particle velocity |
| `--particle-debug-velocity-scale` | `<f>` | 0.05 | Velocity arrow length |
| `--particle-debug-max-particles` | `<n>` | 128 | Max particles drawn |

### Debug builds only

| Option | Value | Default | What it does |
| --- | --- | --- | --- |
| `--layer-size` | `<WxH>` | — | Small test rectangle, e.g. `320x180` |
| `--layer-anchor` | `<edges>` | — | Where the small rectangle sits, e.g. `top-left` |
| `--toggle-debug-ui` | | off | Show or hide the debug panel of the running wallpaper on this output |
| `--diagnose`, `--diagnostics` | | off | Capture render diagnostics |
| `--diagnose-frame` | `<n>` | 100 | Frame to capture |
| `--diagnose-final-only` | | off | Capture only the final output |
| `--diagnose-deterministic` | | off | Fixed time step and seeded random numbers |
| `--exit-after-diagnose` | | off | Quit after the capture |
| `--disable-effects` | `<pattern>` | — | Turn off effects matching a name (`*/all` = all) |
| `--disable-particles` | | off | Turn off particles |
| `--disable-bloom` | | off | Turn off bloom |
| `--sandbox` | | off | Run the effect sandbox |
| `--no-ui` | | off | Start without the debug UI |

## Ignored launcher options

Options from the upstream launcher that this build does not support are accepted and logged as `ignoring unsupported option`. They never become the wallpaper path. The full list with status is in [Compatibility](compatibility.md).

## Hand-off to a running wallpaper

If a wallpaper is already running on the same output, a new launch hands the change to that running instance and exits. The running instance then:

1. Switches to the new wallpaper with the transition from `--transition` (default `fade`, a crossfade). `none` cuts straight over, and `random` picks one effect per switch. Effects use the original transition shaders from your install, or the built-in fade if they are missing.
2. Applies new `--set-property` values live.
3. Updates volume, mute, frame cap, output device and visualizer on or off. A device change reloads the wallpaper so its sounds play on the new output.

Pass `--no-control` to start a separate instance instead.
