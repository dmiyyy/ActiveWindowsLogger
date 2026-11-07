# Active windows logger

A lightweight background app gathering information about active programs ( or rather active windows) opened by user and how much time user spent with specific app.
Works on **Windows** and **Linux** (X11 and KDE Plasma on Wayland).

The logs are stored in CSV files with 4 columns:

-Time stamp   
-Name of the program   
-Window title   
-Time in milliseconds   

![CSV_Screenshot.png](https://github.com/alexkmbk/ActiveWindowsLogger/blob/master/CSV_Screenshot.png)

![example.csv](https://github.com/alexkmbk/ActiveWindowsLogger/blob/master/example.csv)

For each day will be created separate log file:

![logs_screenshot.png](https://github.com/alexkmbk/ActiveWindowsLogger/blob/master/logs_screenshot.png)

The log format is the same on all platforms, so the logs can be viewed with the same tools (e.g. the 1C viewer in `Viewer/1C`).

Logging is suspended while the user is inactive (no keyboard / mouse input for `StopLoggingwhenInactiveInterval` minutes), while the screen is locked, while the computer sleeps and while logging is paused.

## Settings

`settings.ini`:

```ini
[Filters]
# Comma separated program names that should be excluded from monitoring (case-insensitive).
# On Linux the window class (WM_CLASS, e.g. org.telegram.desktop) can be used as well.
ProgramsFilter = chrome,foobar2000,firefox

[Tracking]
# Suspend monitoring after the interval of inactivity (in minutes). Default - 5 minutes, don't suspend - 0
StopLoggingwhenInactiveInterval = 5

[LogsFormat]
# CSV columns separator
Separator = ,
```

## Windows

The CSV files location is `%LOCALAPPDATA%\ActiveWindowsLogger\logs`, `settings.ini` is in the same folder.

The app lives in the system tray: open the logs folder, edit the settings, pause / continue, close.

Build: open `ActiveWindowsLogger.sln` in Visual Studio, or use CMake:

```
cmake -S . -B build
cmake --build build --config Release
```

The installer is built with WiX: `WinSetup\make.bat`.

## Linux

| Session | Active window | Idle detection |
|---|---|---|
| X11 (any EWMH window manager: KDE, GNOME, Xfce, ...) | Xlib (`_NET_ACTIVE_WINDOW`) | XScreenSaver extension |
| KDE Plasma, Wayland | KWin script + D-Bus | `ext-idle-notify-v1` (Plasma 6) |
| Other Wayland compositors | not supported (only XWayland windows are seen) | `ext-idle-notify-v1` |

Screen lock and sleep are detected via systemd-logind and `org.freedesktop.ScreenSaver`.

There is no tray icon: the app runs in the background and is controlled from the command line.

Files:

- logs: `~/.local/share/ActiveWindowsLogger/logs/YYYY-MM-DD.csv`
- settings: `~/.config/ActiveWindowsLogger/settings.ini` (created on the first start, re-read automatically when changed)

### Build

Dependencies (Debian / Ubuntu):

```
sudo apt install cmake g++ pkg-config libx11-dev libxss-dev libsystemd-dev libwayland-dev
```

Fedora: `sudo dnf install cmake gcc-c++ libX11-devel libXScrnSaver-devel systemd-devel wayland-devel`  
Arch: `sudo pacman -S cmake gcc libx11 libxss systemd-libs wayland`

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build
```

Each dependency is optional (`-DAWL_WITH_X11=OFF`, `-DAWL_WITH_SDBUS=OFF`, `-DAWL_WITH_WAYLAND=OFF`), but at least X11 or sd-bus is required. sd-bus (libsystemd or libelogind) is required for KDE Wayland.

### Autostart

As a systemd user service (recommended):

```
systemctl --user enable --now activewindowslogger.service
journalctl --user -u activewindowslogger -f   # diagnostics
```

Or via the desktop autostart: `cp /usr/local/share/applications/activewindowslogger.desktop ~/.config/autostart/`

### Usage

```
activewindowslogger            # start logging (in the foreground; -v prints diagnostics)
activewindowslogger status     # state, PID, logs and settings locations
activewindowslogger pause
activewindowslogger resume
activewindowslogger toggle     # e.g. bind to a global shortcut
activewindowslogger reload     # re-read settings.ini
activewindowslogger stop
```

`--backend=x11|kwin` forces the way the active window is detected (by default KWin is used on KDE Wayland and X11 otherwise).

### How it works on KDE Wayland

Wayland doesn't allow ordinary applications to see other windows. So on start the logger loads a small KWin script
(via `org.kde.KWin /Scripting`) that reports the active window and its title changes back to the logger over D-Bus
(`io.github.ActiveWindowsLogger`). The script is unloaded on exit and reloaded automatically if KWin restarts.
The program name is taken from `/proc/<pid>/exe`; for interpreters and wrappers (python, java, electron, ...) the window class is used instead.
