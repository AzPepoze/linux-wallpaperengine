# Compatibility

Back to the [README](../README.md).

Upstream: [Almamu/linux-wallpaperengine](https://github.com/Almamu/linux-wallpaperengine). This table lists its command-line options and whether this project has them.

| Option | This project has it |
| --- | --- |
| `--silent` | Yes |
| `--volume <n>` | Yes |
| `--noautomute` | No |
| `--no-audio-processing` | Yes |
| `--fps <n>` | Yes |
| `--window <XxYxWxH>` | Partly (size only) |
| `--screen-root <screen>` | Yes |
| `--screen-span <a,b,...>` | No |
| `--bg <id/path>` | No |
| `--scaling <mode>` | Yes |
| `--clamping <mode>` | No |
| `--assets-dir <path>` | Yes |
| `--screenshot <file>` | No |
| `--list-properties` | Yes |
| `--set-property name=value` | Yes |
| `--disable-mouse` | Yes |
| `--disable-parallax` | Yes |
| `--no-fullscreen-pause` | No |
| `--fullscreen-pause-only-active` | No |
| `--fullscreen-pause-ignore-appid <id>` | No |

"No" options are accepted and logged as `ignoring unsupported option`. They do nothing and never become the wallpaper path.
