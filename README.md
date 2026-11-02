# Archicad Add-in Manager

Load, reload and run Archicad add-ons **without restarting Archicad**.

Normally an add-on (`.apx`) is loaded when Archicad starts and stays locked until you quit. Every build-test cycle costs
a restart. The Add-in Manager is a small add-on that loads a *temporary copy* of the add-on you are developing, runs one
of its commands, and unloads it. The original file stays free, so you can rebuild it while Archicad is running.

> **Archicad 27 only for now.** The code relies on the memory layout of the Archicad 27 DevKit (see
> [How it works](#how-it-works)). Other versions are refused at compile time.

## What it does

- Runs the **menu commands of any add-on** (`.apx`) from a fresh copy: its `CheckEnvironment`, `RegisterInterface` and
  `Initialize` run under the manager's identity, and its menu handlers are kept by the manager instead of being
  installed in Archicad.
- Runs the commands of a **Commands DLL**: a plain DLL that exports `RunCommand` (and optionally `ListCommands`) and does
  its Archicad calls through a function table the manager provides. See
  [`Shared/CommandsApi.hpp`](ArchiCAD_AddonManager/Shared/CommandsApi.hpp) for the contract.
- Copies the add-on's folder next to the temporary copy (DLLs, data files, subfolders; not other add-ons, at most 300 MB),
  so add-ons that load files from their own folder keep working.
- Remembers the last add-on and command, for the faceless mode.

## Menu

The manager adds one tab to Archicad's menu bar with two items:

| Item | What it does |
| --- | --- |
| **Add-in Manager (Manual Mode)...** | Opens a window: pick an `.apx` or a Commands DLL, see its commands, choose one, **Run**. Double-click a command to run it. |
| **Add-in Manager (Faceless: run last command)** | Runs the last command again from a fresh copy, with no window. Useful for a quick rebuild-and-run loop. |

The last selection is stored in `%APPDATA%\AddonManager\AddonManager.ini`.

## Typical workflow

1. Build your add-on (or Commands DLL).
2. In Archicad: **Add-in Manager (Manual Mode)...**, select your `.apx` / `.dll`, pick a command, **Run**.
3. Change your code, rebuild, then use **Faceless: run last command**. No restart.

## Build

Requirements:

- Windows, **Archicad 27** and the matching **Archicad API DevKit 27** (download it from Graphisoft's developer site and
  install it yourself; it is not part of this repository).
- The **MSVC v142** toolset (VS 2019 C++ build tools, MSVC 14.29). The DevKit headers refuse any other compiler.
- CMake 3.20+ and Python 3 (used by the DevKit's `CompileResources.py`).

The DevKit is discovered in `C:\Program Files\GRAPHISOFT\API Development Kit <ver>.*`. Use `-DAC_DEVKITS="27=<dir>"` for
another location.

With Visual Studio:

```
cmake --preset ac27-vs
```

This writes `ArchiCAD_AddonManager.slnx` at the repository root. Open it and build `Release-AC27`.

With Ninja: `CMakePresets.json` only holds the shared base presets (`ac27-base`). Create your own
`CMakeUserPresets.json` with a preset named `ac27` that inherits `ac27-base` and sets your compiler, the Python path
(`Python_EXE`) and, for a portable MSVC, the `INCLUDE`, `LIB` and `PATH` environment. A `release-ac27` build preset
(configure preset `ac27`, configuration `Release-AC27`) goes in the same file. Then:

```
cmake --preset ac27
cmake --build --preset release-ac27
```

The output is `ArchiCAD_AddonManager_AC27.apx`, in `out/Release-AC27` next to the repository folder (set `-DAC_OUT_DIR=<dir>`
to change it).

## Install

Copy `ArchiCAD_AddonManager_AC27.apx` to a folder Archicad loads add-ons from (add one in **Options > Add-On Manager**,
or use Archicad's own `Add-Ons` folder), then restart Archicad once.

## How it works

Every add-on is linked with `ACAP_STAT.lib`. Archicad calls two exported functions, `GetExportedFuncAddrs` and
`SetImportedFuncAddrs`, and gives the add-on its table of API functions and a client pointer. The client pointer *is*
the add-on's identity: every `ACAPI_*` call passes it to Archicad.

The manager keeps both of them. A module loaded later with `LoadLibrary` then gets the same table and client pointer,
so its `ACAPI_*` calls are accepted as if they came from a registered add-on. The details are in
[`Lib/Execute/ApiIdentity.hpp`](ArchiCAD_AddonManager/Lib/Execute/ApiIdentity.hpp).

## Limitations and warnings

- **This is not a supported Graphisoft technique.** It depends on the internal layout of `ACAP_STAT.lib`, measured on the
  Archicad 27 DevKit. A different DevKit or Archicad build can break it, in the worst case by crashing Archicad. Save your
  work before testing.
- Only Archicad 27 (Windows). Porting to another version means re-measuring `kImpListSize` and the function slots.
- A loaded module stays loaded until the next load. Windows or callbacks it leaves behind stay valid, but a module that
  keeps static state across runs sees a fresh copy each time.
- Add-ons that register file types, palettes or other persistent integrations are not a good fit: they are meant to be
  installed by Archicad at startup.
- **Developer ID.** `Resources/RFIX/ArchiCAD_AddonManagerFix.grc` uses the DevKit's *test* IDs. Request your own Developer
  ID and Local ID from Graphisoft before you distribute a built `.apx`.

## Disclaimer

This is an independent, unofficial project. It is **not affiliated with, endorsed by or supported by GRAPHISOFT SE**.
Archicad and GRAPHISOFT are trademarks of their owners. This repository does not contain any part of the Archicad API
DevKit; you need your own copy, under Graphisoft's terms.

## License

_Not chosen yet._ Add a `LICENSE` file before publishing.
