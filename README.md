# RyCenter

A little more centered. A free, MIT-licensed, native Windows utility with one job: center the active window on its current monitor, without resizing it.

## Use

Windows 10 1703 or later / Windows 11, x64. Run **RyCenter.exe**, choose your settings, then press **Ctrl+Alt+C**. Click the tray icon for Settings, or right-click it for Settings / Quit. Running the executable again opens Settings instead of starting another copy.

- Click the shortcut in Settings, then press Ctrl, Alt, or Win with another key. Release the key to finish recording; Escape cancels. The saved global shortcut is temporarily suspended only while recording, so it can be recorded again without firing. Most Win-key combinations and F12 are reserved by Windows; conflicting/reserved shortcuts are rejected without losing the previous shortcut.
- **Reset** restores the shortcut to **Ctrl+Alt+C** without changing automatic centering or launch at login. **Save** applies the draft; **Cancel** discards it, including a reset.
- Centers the visible window frame inside the monitor's work area, excluding the taskbar. Handles negative monitor coordinates and mixed DPI scaling.
- Does not resize, activate, or change the stacking order of the target window. Minimized, maximized, fullscreen, cloaked, and utility windows are skipped. Windows larger than the work area align to its top/left on the overflowing axis so the title bar stays reachable.
- **Automatic centering is off by default.** Opt in to center newly created app windows once, after a 250 ms settling delay. Existing windows, restored windows, owned dialogs, and windows moved/resized during that delay are left alone. Apps that reposition themselves later can override centering; RyCenter does not keep fighting them.
- Launch at login is opt-in and per-user. Keep the executable in a permanent location before enabling it. If you move it, save Settings again to update the startup path. Windows Startup Apps can separately disable the entry.
- No installation, admin privileges, accounts, ads, telemetry, network requests, or automatic updater. The executable is not code signed; Windows may show a SmartScreen warning. Elevated apps may not accept moves from a normal-privilege process.

## Download

Grab `RyCenter_<version>_x64_portable.exe` from [Releases](https://github.com/ryanpolasky/RyCenter/releases/latest). It's a single portable executable with no installer or runtime download. Put it somewhere permanent before turning on Start with Windows.

## Testing

Windows CI runs real Win32 integration checks, including the Settings dialog's shortcut capture, save, cancel, reset, conflict, toggle, Tab navigation, and DPI-layout paths, in addition to portable geometry and shortcut-state tests. The `RyCenter-native-ui` artifact contains native renders of Settings, including recording/error states and larger DPI layouts. Settings tests redirect registry writes to a temporary isolated key. They do not replace real-keyboard checks or manual checks with multiple physical monitors, mixed scaling, different apps, and Explorer restarts. Memory/CPU numbers are deliberately not advertised until measured on real Windows.

## Overhead

C++17 and Win32, one process, one message-loop thread. The default mode uses `RegisterHotKey` and blocks in `GetMessage`: **no polling, keyboard hook, timers, window scanning, rendering loop, or background network work**. The Studio Settings window uses Windows GDI+ for event-driven painting only while open; its graphics resources are released when it closes. There is no webview or bundled UI runtime. High Contrast uses standard Windows controls and system colors.

Opt-in automatic mode registers out-of-process accessibility event hooks for window creation/show/destruction and drag-start, with no DLL injection. It uses a one-shot settling timer per newly shown app window and unregisters hooks/timers when disabled. The default mode registers none of those hooks. The C/C++ runtime is statically linked; only Windows system DLLs are imported.

Settings: `HKCU\Software\RyCenter`. Optional startup entry: `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\RyCenter`. To remove: uncheck Start with Windows, save, quit, and delete the executable. Delete the settings key too if you want to remove your preferences.

## Build

On Windows, install Visual Studio 2022 Build Tools (Desktop development with C++) and CMake. From this directory:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Output: `build/Release/RyCenter.exe`. `/W4 /WX`, static runtime, size optimization, and link-time optimization are enabled for release builds.

Linux/macOS geometry tests:

```sh
cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Linux x64 cross-build with MinGW-w64 (Ubuntu: `sudo apt-get install g++-mingw-w64-x86-64 cmake`):

```sh
cmake -S . -B build-mingw -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-mingw
```

Formatting: `clang-format --dry-run --Werror src/*.cpp src/*.h tests/*.cpp`.

The native icon is generated from [RyApps](https://github.com/ryanpolasky/RyApps)' `public/rycenter.svg` using ImageMagick: `convert -background none rycenter.svg -define icon:auto-resize=256,64,48,32,24,16 resources/app.ico`.
