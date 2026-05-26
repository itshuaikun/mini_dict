# Mini Dict

A small GTK4 dictionary lookup window for Linux Wayland desktops.

## Scope

- English word and common short phrase lookup
- One-line wake/input window
- Full scrollable dictionary result after pressing Enter
- British and American phonetic text when available
- Pronunciation audio playback when the dictionary source provides audio URLs
- SQLite cache for successful lookup results
- Wayland-friendly shortcut model: configure a system shortcut to run `mini-dict --toggle`

The first version does not support sentence translation, selected-text capture, X11 global shortcut registration, or Chinese meanings.

## Interface Rules

- Use a lightly translucent GTK window so text behind the lookup surface remains faintly visible.
- Keep the interface plain, fast, and keyboard-friendly.
- Do not add blur, animation, decorative gradients, floating effects, or other visual effects that do not directly improve lookup speed or readability.

## Dependencies

- C compiler
- CMake
- pkg-config
- GTK4
- gtk4-layer-shell
- libsoup 3
- json-glib
- SQLite
- GStreamer
- GStreamer runtime plugins for HTTPS MP3 playback

On Arch Linux:

```sh
sudo pacman -S base-devel cmake pkgconf gtk4 gtk4-layer-shell libsoup3 json-glib sqlite gstreamer gst-plugins-base gst-plugins-good gst-plugins-ugly gst-libav
```

On Debian/Ubuntu:

```sh
sudo apt install build-essential cmake pkg-config libgtk-4-dev libgtk4-layer-shell-dev libsoup-3.0-dev libjson-glib-dev libsqlite3-dev libgstreamer1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-ugly gstreamer1.0-libav
```

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMINI_DICT_NATIVE_OPTIMIZE=ON -DMINI_DICT_ENABLE_LTO=ON
cmake --build build
```

Run:

```sh
./build/mini-dict
```

Install for the current user:

```sh
mkdir -p ~/.local/bin
install -m 0755 ./build/mini-dict ~/.local/bin/mini-dict
```

Toggle an existing instance:

```sh
~/.local/bin/mini-dict --toggle
```

Force a specific output:

```sh
~/.local/bin/mini-dict --toggle --monitor eDP-1
```

Clear cached lookup results:

```sh
~/.local/bin/mini-dict --clear-cache
```

## Wayland Shortcut

Wayland applications generally cannot register arbitrary global shortcuts themselves. Configure your desktop environment's keyboard shortcuts to run:

```sh
~/.local/bin/mini-dict --toggle
```

The command is single-instance aware: if the app is already running, it toggles the lookup window; otherwise it starts the app and shows the input window. On KDE Wayland, Mini Dict asks KWin for the active output before showing the window. Other compositors can pass `--monitor OUTPUT` or set `MINI_DICT_MONITOR=OUTPUT`.

Mini Dict uses gtk4-layer-shell on Wayland. The lookup window is horizontally centered on the selected output with its top edge placed at 25% of that output's height.
