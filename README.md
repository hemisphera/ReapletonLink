# ReapletonLink

A headless REAPER extension that bridges REAPER's tempo, transport, and timeline position into an [Ableton Link](https://github.com/Ableton/link) network session. REAPER acts as the master authority for tempo and timeline; all connected Link peers follow. Start/stop can optionally be triggered by any Link peer (see *Remote start/stop*).

## Features

- **Zero-GUI** — no windows, docker tabs, or menus; runs entirely in the background
- **Opt-in connection** — the Link connection starts **disabled** by default and is controlled via registered Actions (see below)
- **Tempo sync** — propagates the effective tempo at the playhead, including tempo envelopes and markers
- **Transport sync** — start/stop state is shared with all Link peers; optionally, peers can start/stop REAPER
- **Phase sync** — REAPER's exact beat position is injected into the Link session, including seeks and timeline jumps
- **Efficient updates** — only emits to the network when tempo, transport state, or beat position actually changes

## Actions

The extension registers four actions in REAPER's Actions list (search for `ReapletonLink`):

| Action | Description |
|--------|-------------|
| `ReapletonLink: Enable Link connection` | Enables the Ableton Link network connection and starts the master sync loop |
| `ReapletonLink: Disable Link connection` | Disables the Ableton Link network connection and suspends sync |
| `ReapletonLink: Toggle Link connection` | Toggles the connection on/off — register this one as a **toolbar button** for visual feedback |
| `ReapletonLink: Toggle follow remote start/stop` | Lets Link peers start/stop REAPER's transport (off by default) — also usable as a toolbar button |

The Link connection and *follow remote start/stop* are both **disabled** when REAPER loads; neither setting is saved between sessions. Both *Toggle* actions are registered as toggle actions, so when added to a toolbar their buttons stay **pressed** while the option is on and release when it is off (the Actions list also shows a checkmark). Use the *Enable* / *Disable* actions for explicit, scriptable control of the connection.

## Threading

The sync loop runs on a dedicated thread at a steady ~60 Hz cadence — independent of REAPER's UI load. REAPER's main thread only samples transport state into a small snapshot (a Link-clock / beat-position *clock pair*) inside the control-surface tick and executes transport start/stop requested by Link peers, since REAPER API functions are not thread-safe. The sync thread extrapolates REAPER's beat position between snapshots using that clock pair, so phase corrections stay smooth even when REAPER's main thread stalls.

## Remote start/stop

By default REAPER is authoritative for transport too: if a Link peer presses start or stop, ReapletonLink immediately reverts the session to REAPER's play state.

With **Toggle follow remote start/stop** enabled, a start/stop from any peer drives REAPER's transport instead:

- **Start** — REAPER starts playback from its edit cursor. As soon as it is playing, REAPER's beat position is injected into the session, so peers align to REAPER's timeline (peers that started immediately may see a small phase jump, since REAPER reacts on its next ~30 Hz tick plus its audio start latency).
- **Stop** — triggers REAPER's Stop (cursor behaviour follows your REAPER preferences).
- **Recording is protected** — a remote stop is ignored while REAPER is recording, and the session is set back to playing.
- If REAPER and a peer change transport at the same moment, REAPER wins.

## Continuous Integration

A GitHub Actions workflow (`.github/workflows/build.yml`) builds the plugin for all three platforms on every push or pull request to `main`/`master`, and can be triggered manually via `workflow_dispatch`.

| Platform | Runner | Output |
|----------|--------|--------|
| Windows | `windows-latest` | `reaper_ReapletonLink.dll` |
| macOS | `macos-latest` | `reaper_ReapletonLink.dylib` (universal: arm64 + x86_64) |
| Linux | `ubuntu-latest` | `reaper_ReapletonLink.so` |

Artifacts are uploaded with the prefix `ReapletonLink-<OS>`.

## Building

### Prerequisites

- **CMake 3.15+** and a **C++17 compiler**
  - Windows: **Visual Studio 2022+** (MSVC, includes CMake)
  - macOS: Xcode command line tools
  - Linux: GCC or Clang
- **Git** and **internet access** — the Ableton Link C++ SDK and REAPER SDK are fetched via CMake's `FetchContent`; on macOS/Linux the [WDL](https://github.com/justinfrankel/WDL) repository is also cloned (REAPER's headers need SWELL)

### Quick build (Windows)

```powershell
.\build.ps1
```

The `build.ps1` script auto-discovers `cmake.exe` (either on `PATH` or bundled with Visual Studio via `vswhere`), configures the project if needed, and builds the `Release` binary.

Options:

| Flag | Default | Description |
|------|---------|-------------|
| `-Configuration` | `Release` | Build configuration (`Release`, `Debug`) |
| `-Platform` | `x64` | Target platform (`x64`, `Win32`) |
| `-Clean` | — | Deletes the `build` directory before configuring |

### Manual build

Windows:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

macOS (universal binary):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

Linux:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Output

| Platform | File | Location |
|----------|------|----------|
| Windows | `reaper_ReapletonLink.dll` | `build/Release/` |
| macOS | `reaper_ReapletonLink.dylib` | `build/` |
| Linux | `reaper_ReapletonLink.so` | `build/` |

## Installation

Copy the built binary into the `UserPlugins` folder of REAPER's resource path (*Options → Show REAPER resource path* in REAPER), then restart REAPER:

| Platform | Typical location |
|----------|------------------|
| Windows | `%APPDATA%\REAPER\UserPlugins\` |
| macOS | `~/Library/Application Support/REAPER/UserPlugins/` |
| Linux | `~/.config/REAPER/UserPlugins/` |

## Project structure

```
.github/workflows/
└── build.yml           — CI build for Windows, macOS, Linux
src/
├── main.cpp            — REAPER plugin entry point; registers the control surface and actions
├── LinkController.cpp  — Sync loop: pushes REAPER state into the Link session, handles remote start/stop
├── LinkController.h
└── ReaperAPI.h         — REAPER API function pointer resolution
CMakeLists.txt          — Build definition (fetches Ableton Link, REAPER SDK, WDL)
build.ps1               — Windows build helper
```