<h1 align="center">
  ✦ LINUX WALLPAPER ENGINE ✦
</h1>

<p align="center">
  <strong>◈ A high-performance wallpaper engine implementation for Linux ◈</strong>
  <br>
  <strong>◈ Powered by Go and Ebitengine ◈</strong>
</p>

<p align="center">
     <a href="LICENSE">
          <img src="https://img.shields.io/github/license/AzPepoze/linux-wallpaperengine?style=for-the-badge&label=%E2%97%88%20LICENSE%20%E2%97%88&labelColor=%23222222&color=%2300bfae" alt="License">
     </a>
     <a href="https://github.com/AzPepoze/linux-wallpaperengine/stargazers">
          <img src="https://img.shields.io/github/stars/AzPepoze/linux-wallpaperengine?style=for-the-badge&label=%E2%97%88%20STARS%20%E2%97%88&labelColor=%23222222&color=%2300bfae" alt="Stars">
     </a>
</p>

<p align="center">
  <strong>⚠️ EARLY DEVELOPMENT - BUT YOU CAN TRY IT OUT! ⚠️</strong>
</p>

## CONTENTS

-    [CONTENTS](#contents)
-    [ABOUT](#about)
-    [CURRENT STATUS](#current-status)
-    [PREREQUISITES](#prerequisites)
-    [INSTALLATION](#installation)
-    [USAGE](#usage)
     -    [Basic Usage](#basic-usage)
     -    [Debug Mode](#debug-mode)
-    [BUILD FROM SOURCE](#build-from-source)
-    [DEVELOPMENT](#development)
-    [ACKNOWLEDGMENTS](#acknowledgments)
-    [CONTRIBUTING](#contributing)
-    [STONKS!](#stonks)

## ABOUT

Linux Wallpaper Engine is a native reimplementation of Wallpaper Engine for Linux systems. Inspired by [Almamu's linux-wallpaperengine](https://github.com/Almamu/linux-wallpaperengine), this project aims to provide a high-performance, memory-efficient solution for running Wallpaper Engine content on Linux.

**Why This Project?**

While developing a [GUI](https://github.com/AzPepoze/linux-wallpaperengine-gui) for [Almamu's linux-wallpaperengine](https://github.com/Almamu/linux-wallpaperengine), I decided to create a native Go implementation focusing on performance and compatibility. This implementation maintains argument compatibility with the original project for seamless integration.

> [!Important]
> You must own and have installed Wallpaper Engine from Steam to use this project. This tool does not provide any wallpapers or assets by itself; you need to supply your own content from the official Wallpaper Engine.

> [!WARNING]
> This project is in **very early development** stage. Most features are incomplete or unusable. Expect bugs, crashes, and missing functionality. Not recommended for daily use yet.

## CURRENT STATUS

(Maybe already completed I just forgot to update this)

-    [x] **Core & I/O**

     -    [x] Unpacker & Parallel Texture Conversion
     -    [x] Basic Rendering (Linear Filtering, Scaling)
     -    [x] Project Structure (utils, convert, wallpaper, feature)

-    [x] **Audio System**

     -    [x] Malgo Implementation (PipeWire Support)
     -    [x] Multiple Sound Streams & Loop Support

-    [x] **Visual Features (Basic)**

     -    [x] Real-time Clock (Hour, Minute, Second)
     -    [x] Global Mouse Parallax (X11 Integration)
     -    [x] Smooth Shake Effect (Sine Wave logic)

-    [ ] **Visual Features (Intermediate)**

     -    [ ] **Text Rendering Enhancement**:
          -    [ ] Support custom `.otf`/`.ttf` fonts from `fonts/` folder
          -    [ ] Implement text alignment (Horizontal/Vertical)
          -    [ ] Support complex date/time formats
     -    [ ] **Property Binding**:
          -    [ ] Link Object properties (Alpha, Scale, Color) to JSON values or scripts

-    [ ] **Visual Features (Advanced - Shaders)**

     -    [x] **Color & Transparency**:
          -    [x] `opacity` effect pass
          -    [x] `tint` effect (Color mixing)
     -    [ ] **Dynamic Distortions**:
          -    [ ] `waterflow` (Water surface movement)
          -    [ ] `foliagesway` (Wind swaying effect)
          -    [ ] `perspective` (Perspective warping)
     -    [ ] **Post-Processing**:
          -    [ ] `vhs` (Scanlines, noise, chromatic aberration)
          -    [ ] `edgedetection` (Edge highlighting)
          -    [ ] `pulse` (Beating/scaling effects)

-    [ ] **Particle System (High Priority)**

     -    [x] Particle emitter logic (Basic)
     -    [ ] Support for: `Rain`, `Stars`, `Smoke`, `Fireworks`, `Dust motes`

-    [ ] **System Integration**
     -    [ ] **MPRIS Support**: Show current playing song/artist from system players (Spotify, etc.)
     -    [ ] **User Options**: Read `project.json` for user-defined properties (intensity, toggles)

## PREREQUISITES

-    **Go** 1.21 or higher
-    **OpenGL** support (Mesa, proprietary drivers)
-    **Audio libraries** (ALSA/PulseAudio)
-    **Wallpaper Engine content** (from Steam Workshop)

## INSTALLATION

Clone the repository and build from source:

```bash
git clone https://github.com/AzPepoze/linux-wallpaperengine.git
cd linux-wallpaperengine
make build
```

And you can see the built an executable in the `bin` folder.

## USAGE

### Basic Usage

Point the executable to your wallpaper's scene.json file:

```bash
./linux-wallpaperengine /path/to/wallpaper/folder
```

Or specify the scene file directly:

```bash
./linux-wallpaperengine --pkg /path/to/wallpaper/scene.json
```

### Debug Mode

Enable the debug overlay with `--debug` flag:

```bash
./linux-wallpaperengine /path/to/wallpaper/folder --debug
```

**Debug Features:**

-    **Hierarchy Tab** – Browse scene objects with full property inspector
-    **Particle Tab** – Monitor active particle systems and counts
-    **Performance Tab** – Real-time FPS, memory usage, and system metrics
-    **Object Inspector** – View and toggle properties (Visible, Effects, etc.)
-    **Bounding Boxes** – Visualize object bounds and particle positions
-    **Scrollable Panels** – Navigate through large object lists and properties

**Controls:**

-    `F1` – Toggle debug overlay
-    `Mouse Wheel` – Scroll object list (left) or inspector (right)
-    `Click` – Select objects, toggle boolean properties

## BUILD FROM SOURCE

**Requirements:**

-    [Go](https://golang.org/) 1.21+
-    Build essentials (gcc, make)
-    OpenGL development libraries
-    Audio development libraries (libasound2-dev/pulseaudio-dev)

**On Debian/Ubuntu:**

```bash
sudo apt install golang-go build-essential libgl1-mesa-dev libasound2-dev
```

**On Arch Linux:**

```bash
sudo pacman -S go base-devel mesa alsa-lib
```

**Build Steps:**

1. **Clone the repository:**

     ```bash
     git clone https://github.com/AzPepoze/linux-wallpaperengine
     cd linux-wallpaperengine
     ```

2. **Download dependencies:**

     ```bash
     go mod download
     ```

3. **Build:**

     ```bash
     make build
     ```

## DEVELOPMENT

**Run in development mode:**

```bash
make dev
```

You can pass custom arguments using the ARGS variable, for example:

```bash
make dev ARGS="/path/to/wallpaper/folder --debug"
```

## ACKNOWLEDGMENTS

This project is inspired by and maintains compatibility with:

-    **[Almamu/linux-wallpaperengine](https://github.com/Almamu/linux-wallpaperengine)** – The original C++ implementation that pioneered Wallpaper Engine support on Linux. This project aims to provide similar functionality while being written from scratch in Go for better memory safety and easier maintenance.

Special thanks to the Wallpaper Engine community and all contributors who helped document the file formats and rendering techniques.

## CONTRIBUTING

Feel free to open a PR or issue if you want to contribute or report a bug!

**Areas needing help:**

-    Shader implementation for advanced effects
-    3D model rendering support
-    Video background support
-    Performance optimizations
-    Documentation improvements

## STONKS!

<div align="center">
  <a href="https://www.star-history.com/#AzPepoze/linux-wallpaperengine&type=date&legend=top-left">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/svg?repos=AzPepoze/linux-wallpaperengine&type=date&theme=dark&legend=top-left" />
      <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/svg?repos=AzPepoze/linux-wallpaperengine&type=date&legend=top-left" />
      <img alt="Star History Chart" src="https://api.star-history.com/svg?repos=AzPepoze/linux-wallpaperengine&type=date&legend=top-left" width="600" />
    </picture>
  </a>
  <br>
  <br>
  <strong>✦ Made with ♥︎ by AzPepoze ✦</strong>
</div>
