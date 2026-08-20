<h1 align="center">
  ✦ LINUX WALLPAPER ENGINE ✦
</h1>

<p align="center">
     <strong>◈ A wallpaper engine implementation for Linux ◈</strong>
     <br>
     <strong>◈ Written in C (Sokol) ◈</strong>
</p>

<p align="center">
     <a href="LICENSE">
          <img src="https://img.shields.io/github/license/AzPepoze/linux-wallpaperengine?style=for-the-badge&label=%E2%97%88%20LICENSE%20%E2%97%88&labelColor=%23181818&color=%23007bff" alt="License">
     </a>
     <a href="https://github.com/AzPepoze/linux-wallpaperengine/stargazers">
          <img src="https://img.shields.io/github/stars/AzPepoze/linux-wallpaperengine?style=for-the-badge&label=%E2%97%88%20STARS%20%E2%97%88&labelColor=%23181818&color=%23007bff" alt="Stars">
     </a>
</p>

## ABOUT

Linux Wallpaper Engine is a native reimplementation of Wallpaper Engine for Linux systems. This version is written in pure C using Sokol headers for maximum performance and portability.

## BUILD FROM SOURCE

**Requirements:**

- [xmake](https://xmake.io/)
- GCC or Clang
- OpenGL development libraries (libGL)
- X11 development libraries (libX11, libXcursor, libXi)

**On Arch / CachyOS:**
```bash
sudo pacman -S xmake libglvnd libx11 libxcursor libxi
```

**Build Steps:**

```bash
xmake
```

The executable will be generated in the `bin` folder.

## USAGE

```bash
# Run with custom wallpaper
xmake run linux-wallpaperengine "/path/to/wallpaper"

# Run in debug mode
xmake run -d linux-wallpaperengine
```

## DEVELOPMENT

The project uses `xmake` for a modern development workflow.

```bash
# Clean build
xmake clean
xmake

# Rebuild and run
xmake run
```
