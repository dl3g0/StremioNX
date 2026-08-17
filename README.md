# StremioNX

A **Stremio client for the Nintendo Switch**, built on top of [borealis](https://github.com/xfangfang/borealis). Browse catalogs, pick a movie or series episode, choose a source from your installed add-ons and stream it directly on your console using `mpv` for playback.

> This is a homebrew project for educational and personal use. It is not affiliated with Stremio or Nintendo.

---

## Features

- Browse catalog-based add-ons (Cinemeta, etc.) with poster grids
- Movie and **series** support:
  - Season picker (defaults to Season 1) that can be reopened from the sidebar
  - Episode list with summaries
  - Streams are loaded only after selecting an episode
- Sources sidebar with per-add-on filtering ("Fuentes" button)
- Playback via `libmpv` with subtitle/audio language preferences
- Add-on management over the local web server (`http://<ip>:8080`)
- Catalog reordering / hiding from the settings menu

## Building

### Requirements

- [devkitPro](https://devkitpro.org/) with the Switch toolchain
- [MSYS2](https://www.msys2.org/) (or a Linux environment)
- `libmpv`, `curl`, `libwebp` and `libnx` installed in the devkitPro portlibs
- CMake and `pkg-config`

### Build

```bash
# from the project root, inside MSYS2
./build.sh                # full build
./build.sh StremioNX.nro  # rebuild only the .nro package
```

The resulting NRO is written to `build/StremioNX.nro`.

> **Windows note:** the `build.sh` wrapper works around a CMake generator bug where absolute Windows paths (e.g. `C:/Users/...`) get mangled in `compiler_depend.make`. It fixes the paths and retries the build automatically.

### Install

Copy `build/StremioNX.nro` into the `switch/` folder of your SD card and launch it from the Homebrew Menu.

## Controls

| Input | Action |
| ----- | ------ |
| **A** | Confirm / open stream / select episode |
| **B** | Back (from streams → back to episode list in series) |
| **Left / Right** | Navigate catalogs and sidebars |
| **X** | Open settings |

## Project layout

```
CMakeLists.txt       Build configuration
build.sh             Build wrapper for MSYS2/devkitPro
source/              Application source (core + UI)
library/borealis     Vendored borealis UI framework (Apache-2.0)
resources/           App resources (XML views + icons)
```

## Libraries used (open source)

This project complies with the open-source licenses of the libraries it uses:

| Library | Purpose | License |
| ------- | ------- | ------- |
| [borealis](https://github.com/xfangfang/borealis) | UI framework | Apache-2.0 |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON parsing | MIT |
| [curl / libcurl](https://github.com/curl/curl) | HTTP requests | curl / MIT |
| [mpv](https://github.com/mpv-player/mpv) | Video playback engine | GPL-2.0+ |
| [libwebp](https://chromium.googlesource.com/webm/libwebp/) | WebP image decoding | BSD-3-Clause |
| [libnx](https://github.com/switchbrew/libnx) | Switch homebrew SDK | devkitPro |
| tinyxml2 | XML parsing (bundled in borealis) | zlib |
| yoga | Flexbox layout (bundled in borealis) | MIT |
| fmt | String formatting (bundled in borealis) | MIT |
| tweeny | Animations (bundled in borealis) | MIT |
| nanovg | 2D vector rendering (bundled in borealis) | zlib |

Each library is distributed under its own license terms.

## Author

Made by **DL3G0** — developer of StremioNX.

## Disclaimer

StremioNX is a homebrew application for Nintendo Switch and is provided "as is", without warranty of any kind. Streaming content may be subject to the terms of the add-ons you install and local copyright laws.