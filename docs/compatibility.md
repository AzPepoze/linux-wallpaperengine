# Compatibility

Back to the [README](../README.md).

This project uses the same command line as [Almamu/linux-wallpaperengine](https://github.com/Almamu/linux-wallpaperengine). Scripts written for that launcher should start here. Some options work the same way. Others are accepted and ignored, so the wallpaper still starts.

## Upstream options

| Option | Status here | Notes |
| --- | --- | --- |
| `--silent` | Works | Mutes audio. `-s`, `--mute` and `--no-audio` also work |
| `--volume <n>` | Works | 0 to 100 |
| `--noautomute` | Accepted, ignored | Audio is not muted when other apps play |
| `--no-audio-processing` | Works | Turns off the audio spectrum capture |
| `--fps <n>` | Works | `-f` also works. `0` follows the display |
| `--window <XxYxWxH>` | Works, size only | Opens a windowed run. The X,Y position is logged and not applied |
| `--screen-root <screen>` | Works | `-r` also works |
| `--screen-span <a,b,...>` | Accepted, ignored | Stretching one wallpaper over several screens is not supported |
| `--bg <id/path>` | Accepted, ignored | Per-screen background assignment is not supported |
| `--scaling <mode>` | Works | `default`, `fit`, `fill` or `stretch` |
| `--clamping <mode>` | Accepted, ignored | Also accepted as `--clamp` |
| `--assets-dir <path>` | Works | Install root or its `assets/` folder |
| `--screenshot <file>` | Accepted, ignored | No screenshot is saved |
| `--list-properties` | Accepted, ignored | Properties are not printed. The wallpaper still starts |
| `--set-property name=value` | Works | Repeat the option for more properties |
| `--disable-mouse` | Accepted, ignored | The mouse position is still read |
| `--disable-parallax` | Accepted, ignored | Parallax stays on |
| `--no-fullscreen-pause` | Accepted, ignored | Fullscreen pausing is not implemented |
| `--fullscreen-pause-only-active` | Accepted, ignored | Fullscreen pausing is not implemented |
| `--fullscreen-pause-ignore-appid <id>` | Accepted, ignored | Fullscreen pausing is not implemented |

> [!NOTE]
> Options that are accepted and ignored are logged as `ignoring unsupported option`. They never become the wallpaper path.

## Options only in this project

| Option | What it does | Details |
| --- | --- | --- |
| `-r`, `--layer` | Draws as a layer-shell surface on Wayland | [Options](options.md#display) |
| `--resolution` | Sets the render size of layers | [Options](options.md#graphics) |
| `--transition`, `--transition-duration`, `--transition-mode` | Switches between wallpapers with a transition | [Options](options.md#transition) |
| `--audio-device`, `--no-audio` | Chooses the output device, or mutes it | [Options](options.md#audio) |
| `--pointer` | Chooses where the mouse position comes from | [Options](options.md#pointer) |
| `--no-control` | Starts a separate instance instead of handing off to a running one | [Options](options.md#control) |
| `--performance-profile`, `--script-profile` | Logs frame and script timing | [Options](options.md#graphics) |
