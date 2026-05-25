# Target Linux GTK Wayland with C and CMake

The first version targets Linux desktop environments through GTK4 on Wayland, implemented in C and built with CMake. X11, Windows, and macOS are out of scope for now; this keeps the application focused on the user's current desktop target while making the Wayland global-shortcut constraints explicit instead of hiding them behind a generic "Linux" label.

The Wake Shortcut is implemented as a system-configured shortcut that runs a toggle command, such as `mini-dict --toggle`. The application does not register global shortcuts itself in the first version; users configure the actual key binding in their desktop environment, and the command signals the running instance to show or hide the Lookup Window.

The C application uses GTK4 for the interface, libsoup 3 for HTTP, json-glib for JSON parsing, SQLite for the local cache, GStreamer for pronunciation audio playback, and CMake with pkg-config for builds. These dependencies keep the project within the Linux/GTK C ecosystem while avoiding hand-written HTTP, JSON, cache-file handling, or media playback.
