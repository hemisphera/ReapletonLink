# ReapletonLink

A headless REAPER extension that bridges REAPER's tempo, transport, and timeline position into an [Ableton Link](https://github.com/Ableton/link) network session. REAPER acts as the master authority for tempo and timeline; all connected Link peers follow. Start/stop can optionally be triggered by any Link peer (see *Remote start/stop*).

## Features

- **Zero-GUI** — no windows, docker tabs, or menus; runs entirely in the background
- **Opt-in connection** — the Link connection starts **disabled** by default and is toggled via two registered Actions (see below)
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

The Link connection is **disabled** when REAPER loads. The *Toggle* action is registered as a toggle action, so when added to a toolbar its button stays **pressed** while the connection is enabled and releases when disabled (the Actions list also shows a checkmark). Use the *Enable* / *Disable* actions for explicit, scriptable control.

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

- **Visual Studio 2022+** (MSVC with C++17 support, includes CMake)
- **Internet access** — the Ableton Link C++ SDK and REAPER SDK are fetched via CMake's `FetchContent`

### Quick build

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

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

### Output

The build produces `reaper_ReapletonLink.dll` in `build/Release/`.

## Installation

Copy `reaper_ReapletonLink.dll` into REAPER's `Plugins` directory (found in the REAPER resource path — typically `%APPDATA%/REAPER/Plugins/`).

## Project structure

```
src/
├── main.cpp            — REAPER plugin entry point
├── LinkController.cpp  — Master sync loop: captures REAPER state, forces Link session
├── LinkController.h
└── ReaperAPI.h         — REAPER API function pointer resolution
```