# ReapletonLink

A headless REAPER extension that bridges REAPER's tempo, transport, and timeline position into an [Ableton Link](https://github.com/Ableton/link) network session. REAPER acts as the absolute master authority; all connected Link peers follow.

## Features

- **Zero-GUI** — no windows, docker tabs, or menus; runs entirely in the background
- **Tempo sync** — propagates the effective tempo at the playhead, including tempo envelopes and markers
- **Transport sync** — start/stop state is shared with all Link peers
- **Phase sync** — REAPER's exact beat position is injected into the Link session, including seeks and timeline jumps
- **Efficient updates** — only emits to the network when tempo, transport state, or beat position actually changes

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