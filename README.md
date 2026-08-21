# Usage

## Build from source

### Requirements

- [xmake](https://xmake.io/)
- GCC or Clang with C++ support
- Vulkan development libraries
- [Slang](https://github.com/shader-slang/slang)
- X11 development libraries (`libX11`, `libXcursor`, `libXi`)
- A working Vulkan driver for your GPU

On Arch / CachyOS:

```bash
sudo pacman -S xmake vulkan-icd-loader shader-slang libx11 libxcursor libxi
```

Build the project:

```bash
xmake
```

The executable is generated in the `bin` directory.

## Run

```bash
# Run a wallpaper project folder or standalone .pkg file.
xmake run linux-wallpaperengine "/path/to/wallpaper"

# Configure and run the release build.
xmake f -m release
xmake run linux-wallpaperengine

# Configure and run the debug build.
xmake f -m debug
xmake run linux-wallpaperengine
```

## Development

```bash
# Clean and rebuild.
xmake clean
xmake

# Rebuild and run.
xmake run

# Validate formatting and static analysis.
xmake check

# Format source files.
xmake format

# Validate, build, and open the debug effect sandbox.
xmake sandbox
```
