# Install

Back to the [README](../README.md).

This guide has three parts: install the libraries, set up mouse access (optional), and build the program.

## Step 1: Install the libraries

A **library** is a set of ready-made code that the program uses. Your package manager installs them for you.

### What each group is for

| Group | Libraries | What it is for |
| --- | --- | --- |
| Build tools | xmake, GCC or Clang, pkg-config, git | Turn the source code into a program |
| Graphics | Vulkan loader and a Vulkan driver | Draw the wallpaper on your graphics card |
| Video | FFmpeg, VA-API, libdrm | Play video wallpapers |
| Shaders | Slang | Compile the small programs that draw each effect |
| Windows | libX11, libXcursor, libXi | Open the window and read the keyboard and mouse |

A Vulkan driver for your graphics card is required. For example: `vulkan-radeon` for AMD, `vulkan-intel` for Intel, or the NVIDIA driver.

The program also needs a Wallpaper Engine install for its **assets** folder. Assets are the files that wallpapers use, such as fonts and images. You can point the program to that folder later (see [Options](options.md)).

### Optional features

Each feature needs its own library. Install the library only if you want that feature.

| Feature | Name in the program | Library |
| --- | --- | --- |
| `wayland` | Desktop background on Wayland | wayland, xkbcommon |
| `x11` | Desktop window on X11 | Xrandr |
| `mpris` | Current song info for wallpapers | systemd |
| web | Web wallpapers | Qt6 WebEngine |

If a library is missing, that feature is left out and the rest of the program still works.

## Which distro do you have?

Only **Arch Linux** has been tested. The other distros are listed, but their package names have not been checked.

| Distro | Status |
| --- | --- |
| Arch Linux | Tested |
| Debian / Ubuntu | Not tested |
| Fedora | Not tested |
| openSUSE | Not tested |

### Arch Linux

Pick one of the two options.

#### Option 1: Script (recommended)

This command downloads one build file, builds the program, installs it, and removes the temporary folder:

```bash
mkdir -p /tmp/linux-wallpaper-engine && cd /tmp/linux-wallpaper-engine
curl -L -O https://raw.githubusercontent.com/AzPepoze/linux-wallpaperengine/main/install/arch/PKGBUILD
makepkg -si
cd .. && rm -rf linux-wallpaper-engine
```

`makepkg -si` downloads the source, builds it, and installs any missing libraries and then the program. Then run the program with `linux-wallpaperengine`.

#### Option 2: Manual

Install the libraries yourself. `pacman` is the Arch package manager:

```bash
sudo pacman -S --needed \
    xmake gcc pkgconf git \
    vulkan-icd-loader shader-slang \
    libx11 libxcursor libxi \
    ffmpeg libva libdrm
```

Optional, one line per feature:

```bash
sudo pacman -S --needed qt6-webengine                            # web wallpapers
sudo pacman -S --needed wayland wayland-protocols libxkbcommon   # Wayland desktop background
sudo pacman -S --needed systemd                                  # current song info
sudo pacman -S --needed libxrandr                                # X11 desktop window
sudo pacman -S --needed libva-mesa-driver                        # hardware video decoding (AMD, Intel)
sudo pacman -S --needed libpulse                                 # sound output
```

You also need a graphics driver for your card, so install one of these (the program cannot draw without one):

```bash
sudo pacman -S --needed vulkan-radeon     # AMD
sudo pacman -S --needed vulkan-intel      # Intel
sudo pacman -S --needed nvidia-utils      # NVIDIA
```

For developers who check the code (`xmake check`, `xmake format`):

```bash
sudo pacman -S --needed clang-format cppcheck
```

With Option 2, continue with [Step 3: Build the program](#step-3-build-the-program) below.

### Debian / Ubuntu (not tested)

```bash
sudo apt install build-essential pkg-config git \
    libvulkan-dev libx11-dev libxcursor-dev libxi-dev \
    libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev \
    libva-dev libdrm-dev
```

Optional: `libwayland-dev wayland-protocols libxkbcommon-dev` (Wayland), `libsystemd-dev` (current song info), `libxrandr-dev` (X11 desktop window), `qt6-webengine-dev` (web wallpapers).

xmake and Slang are not in the default software list on most versions of Debian and Ubuntu. Install xmake from [xmake.io](https://xmake.io/) and Slang from its [GitHub releases](https://github.com/shader-slang/slang/releases).

### Fedora (not tested)

```bash
sudo dnf install gcc-c++ git pkgconf-pkg-config vulkan-loader-devel \
    libX11-devel libXcursor-devel libXi-devel \
    ffmpeg-devel libva-devel libdrm-devel
```

`ffmpeg-devel` comes from RPM Fusion, an extra software source you need to enable first. Optional: `wayland-devel wayland-protocols-devel libxkbcommon-devel systemd-devel libXrandr-devel qt6-qtwebengine-devel`.

xmake and Slang need the same manual install as on Debian.

### openSUSE (not tested)

```bash
sudo zypper install gcc-c++ git pkg-config vulkan-devel \
    libX11-devel libXcursor-devel libXi-devel \
    ffmpeg-devel libva-devel libdrm-devel
```

Optional: `wayland-devel wayland-protocols-devel libxkbcommon-devel libsystemd-devel libXrandr-devel`.

xmake and Slang need the same manual install as on Debian.

## Step 2 (optional): Mouse access

**Why:** Wallpapers can react to the mouse, even when the mouse is over another window. Linux blocks programs from reading the mouse this way by default. This step gives your user permission to read **mice only**, not keyboards.

**Do you need this step?**

| Your desktop | Needed? |
| --- | --- |
| X11 | **No** |
| Hyprland | **No** |
| KDE Plasma (Wayland) | **Yes**, if you want the mouse followed everywhere |
| GNOME (Wayland) | **Yes**, if you want the mouse followed everywhere |

On X11 and Hyprland the program reads the mouse position directly from the desktop. On KDE and GNOME, the mouse is followed only with this step. Without it, the mouse only works over the wallpaper itself.

GNOME does not support the desktop background mode (it has no layer-shell support), so use a normal window or the X11 desktop window there.

Run these two commands:

```bash
sudo tee /etc/udev/rules.d/70-lwe-mouse.rules <<< 'SUBSYSTEM=="input", KERNEL=="event*", ENV{ID_INPUT_MOUSE}=="1", TAG+="uaccess"'
sudo udevadm control --reload && sudo udevadm trigger --subsystem-match=input
```

- The first command saves a small rule file. The rule says: give the logged-in user access to devices that are mice.
- The second command tells the system to read the new rule now.

If the mouse still cannot be read, log out and back in.

**Other option:** add your user to the `input` group. Run `sudo usermod -aG input $USER`, then log in again. This also lets your user read every keyboard, so the rule above is safer.

## Step 3: Build the program

This step makes the program. For the two build modes (release and debug), see [Build](build.md).

1. Get the source code. The `--recurse-submodules` part also downloads a small folder the Wayland feature needs:

   ```bash
   git clone --recurse-submodules https://github.com/AzPepoze/linux-wallpaperengine.git
   cd linux-wallpaperengine
   ```

   If you already cloned without it, run `git submodule update --init`.

2. Build. This can take a few minutes:

   ```bash
   xmake
   ```

3. Run it on a wallpaper project:

   ```bash
   xmake run linux-wallpaperengine "/path/to/wallpaper"
   ```

The built program and its feature files are in `bin/release/`. The debug build, for developers, is in `bin/debug/`.
