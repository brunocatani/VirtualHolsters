# Building VirtualHolsters

The project builds with CMake. The old `VirtualHolsters.sln` / `.vcxproj` are gone. You can build from VS Code, from Visual Studio or from a terminal; all three use the same files.

## Why CMake

- Parts of the plugin now use CommonLibF4VR next to the F4SEVR SDK (the frame hook, attaching nodes through the correct FO4VR `NiNode` functions, reading the equipped weapons). CommonLibF4VR builds with CMake and vcpkg, so the plugin does too.
- No path of your PC is stored in the project. The few folders that differ per machine go into one file that Git ignores (`CMakeUserPresets.json`).
- vcpkg downloads and builds the small libraries CommonLibF4VR needs. There is nothing to install by hand.

## What you need

| What | Notes |
| --- | --- |
| Visual Studio 2022 or its Build Tools | With the "Desktop development with C++" workload. Only the compiler is used; you can keep editing in VS Code. |
| CMake 3.24 or newer | The one that ships with Visual Studio works. |
| vcpkg | Clone https://github.com/microsoft/vcpkg, run `bootstrap-vcpkg.bat`, and set the Windows environment variable `VCPKG_ROOT` to that folder. |
| CommonLibF4VR | Clone https://github.com/brunocatani/CommonLibF4VR. The build needs the `CommonLibF4` folder inside it. |
| F4SEVR source | The `f4sevr` source folder you already build against: the one that contains `f4se/PluginAPI.h`, with the `common` folder beside it. |
| ROCK headers | Clone https://github.com/brunocatani/ROCK. The build only reads its `include` folder. ROCK is never linked, and the plugin runs without ROCK installed. |

The F4SEVR `.lib` files and the OpenVR library are already in this repository (`lib/` and `openvr/`).

## One-time setup

Create a file named `CMakeUserPresets.json` next to `CMakeLists.txt` and put your own folders in it. Use forward slashes.

```json
{
  "version": 6,
  "include": ["CMakePresets.json"],
  "configurePresets": [
    {
      "name": "local",
      "inherits": "fast",
      "cacheVariables": {
        "COMMON_LIB_F4VR_PATH": "<your folder>/CommonLibF4VR/CommonLibF4",
        "LEGACY_F4SEVR_PATH": "<your folder>/f4sevr_0_6_21/src/f4sevr",
        "ROCK_INCLUDE_DIR": "<your folder>/ROCK/include",
        "POST_BUILD_COPY_PLUGIN": true,
        "COPY_PLUGIN_BASE_PATH": "<your mod folder>/Virtual Holsters"
      }
    }
  ],
  "buildPresets": [
    { "name": "local", "configurePreset": "local", "configuration": "Release" }
  ]
}
```

`POST_BUILD_COPY_PLUGIN` and `COPY_PLUGIN_BASE_PATH` are optional. With them, every build copies `VirtualHolsters.dll` into `F4SE/Plugins` of that mod folder. Leave both lines out if you prefer to copy the DLL yourself.

## Building in VS Code

1. Install the extensions "C/C++" and "CMake Tools" (both from Microsoft).
2. Open the repository folder.
3. CMake Tools asks for a configure preset. Pick `local`. You can change it later with "CMake: Select Configure Preset" from the command palette.
4. Press F7, or run "CMake: Build".

The first configure takes several minutes, because vcpkg builds its libraries and CMake sets up CommonLibF4VR. Later builds only compile what changed.

The DLL is written to `build-fast/Release/VirtualHolsters.dll`.

## Building from a terminal

```
cmake --preset local
cmake --build --preset local --target VirtualHolsters
```

## Building in Visual Studio

Use "File > Open > Folder" on the repository folder. Visual Studio reads the same presets; pick `local` in the configuration box and build.

## Adding a source file

Add the new `.cpp` to the `LEGACY_GAMEPLAY_SOURCES` list in `CMakeLists.txt`. That is the list for code that uses the F4SEVR headers (`f4se/...`), which is all of the gameplay code. Headers do not need to be listed.

`COMMONLIB_SOURCES` is the second list. Files in it are compiled against CommonLibF4VR (`RE::`, `REL::`) and cannot include `f4se/` headers. The two SDKs define the same engine classes in different ways, so one `.cpp` can only use one of them. `LegacyBridge.h`, `NativeEquipment.h`, `VRSceneGraph.h` and `RockProvider.h` are the headers that both sides can include.

## Tests (optional)

`tests/HolsterInteractionTests.cpp` tests the button and zone rules in `HolsterInteraction.h` and `HolsterSlotPolicy.h`. It is not part of the normal build.

```
cmake --build build-fast --config Release --target build_tests
ctest --test-dir build-fast -C Release --output-on-failure
```

## Common problems

| Message | Cause |
| --- | --- |
| `VCPKG_ROOT is not set` | The environment variable is missing, or VS Code was open while you set it. Restart VS Code. |
| `CommonLibF4VR was not found at ...` | `COMMON_LIB_F4VR_PATH` must point at the `CommonLibF4` folder inside the checkout, not at the checkout itself. |
| `The transitional F4SEVR source ABI was not found` | `LEGACY_F4SEVR_PATH` must be the folder that contains `f4se/PluginAPI.h`. |
| `ROCK_INCLUDE_DIR must name ROCK's include directory` | Point it at the `include` folder of the ROCK checkout. |
| The build ends with a copy error | The game is running and holds the DLL. Close the game and build again. |
