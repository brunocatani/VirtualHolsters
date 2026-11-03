# Changelog

## Unreleased: FO4VR modernization fork

Everything below changed since CylonSurfer's last upstream commit, "Added additions to API" (`31f98f2`).

### Requirements

- Fallout 4 VR 1.2.72 with F4SEVR.
- FO4VRTools is no longer required. The mod reads the controllers directly from the OpenVR session the game already runs.
- ROCK 1.0 (provider API 2) is optional. Without ROCK the mod works as before with the single vanilla weapon in the configured weapon hand. With ROCK, each hand can hold its own weapon (akimbo) and draws go into a chosen hand.

### Holstering and drawing

- Each hand works its own holster. Both hands can holster and draw at the same time.
- The holster button defaults to the grip (`HolsterButtonID` in `[Button Config]`). With ROCK loaded, ROCK does not grab with that button while the hand is working a holster.
- **Draw:** with a free hand inside a holster that holds a weapon, press the holster button. The weapon comes out on the press.
- **Holster:** with a weapon in the hand, release the holster button inside an empty holster.
- **Swap:** release inside a holster that already holds a weapon. The hand's weapon goes in and the holstered one comes out.
- The weapon shown in the holster moves into the hand at once and stands in for the real weapon until the game has equipped it, so the weapon never disappears during a draw. With ROCK, the stand-in sits at ROCK's firing grip position for that hand.
- A weapon held in both hands is never holstered. Letting go of either hand inside a holster only lets go and never draws. A two-handed weapon held by one hand behaves like a one-handed weapon.
- A drawn weapon fires while the grip that drew it is still held.
- With ROCK, every holstered gun's animations stay loaded, so a draw does not wait on the game's first-use load.
- In the shoulder holsters, ROCK's own shoulder stash is turned off for that hand.
- Each holster slot can be switched off on its own: `[Holster Slots]` with `LeftShoulder`, `RightShoulder`, `LeftHip`, `RightHip`, `LowerBack`, `LeftChest`, `RightChest`.
- Config mode keeps ROCK and the game off the buttons it uses.

### Configuration files

- All runtime settings live in `Documents\My Games\Fallout4VR\Mods_Config\VirtualHolsters`: `VirtualHolsters.ini` and the `VRH_weapon_offsets` (offsets and the draw sound list), `VRH_WeaponBlackList` and `VRH_MagazineBlackList` folders. They are no longer read from the game's `Data` folder, so installing or updating the mod never overwrites a user's settings.
- A missing `VirtualHolsters.ini` is created on first run with the defaults. The files shipped in those three plugin folders are copied into `Mods_Config` once, and a user's own file is never overwritten.

### Inventory

- A holster stores only its weapon's display name. The exact inventory stack (form and instance data) is looked up again each time it is used, so two copies of the same weapon are told apart.
- A weapon name is never tagged in more holsters than there are unworn copies of it. Tags whose weapon was equipped elsewhere, for example from the Pip-Boy, are cleared.

### Stability

- The frame hook patches one verified call site in the game's main loop. Its bytes are checked before patching, and a call another mod already redirected is chained, not replaced.
- Holster and weapon nodes are attached through FO4VR's own `NiNode` virtual functions. FO4VR's vtable differs from flat Fallout 4, so the old F4SEVR SDK declarations called the wrong functions.
- Every entry from the plugin into the original code catches exceptions, so none reaches the engine.

### Build

- CMake and vcpkg build against CommonLibF4VR, with presets. This replaces the Visual Studio solution and project files.
- The ROCK headers come from ROCK's public `include/` folder through the `ROCK_INCLUDE_DIR` cache variable. The mod never links ROCK.
- Unit tests for the button gesture, zone selection and slot masks (`tests/`) build through the `build_tests` target, outside the plugin build.

### Logging

`VirtualHolsters.log` now records:

- each draw, holster and swap, and why a press did nothing;
- each holster-button press and release, with the zone, the nearest holster and its distance;
- changes to the input gates (config mode, menus, controller and hand availability).

### Files

Added:

| File | Purpose |
| --- | --- |
| `RockProvider.cpp/.h` | Optional ROCK 1.0 binding; the only place that calls ROCK. |
| `HolsterInteraction.h` | Holster button gesture and zone selection, without engine types. |
| `HolsterSlotPolicy.h` | Which holster slots are enabled, and their INI keys. |
| `NativeEquipment.cpp/.h` | Read-only views of the player's equipped weapons and their models. |
| `VRSceneGraph.cpp/.h` | `NiNode` AttachChild and RemoveChild through the FO4VR vtable slots. |
| `LegacyBridge.cpp/.h` | Boundary between the plugin entry and the original F4SEVR SDK code. |
| `ConfigPaths.h` | The `Mods_Config` folder and first-run copies of the shipped defaults. |
| `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json` | Build. |
| `openvr/` | OpenVR import library and its license. |
| `tests/HolsterInteractionTests.cpp` | Unit tests. |

Removed: `VR.cpp/.h`, `VRHookAPI.h`, `VRManagerAPI.h`, `PapyrusVRAPI.h`, `PapyrusVRTypes.h`, `OpenVRTypes.h` (FO4VRTools), and `VirtualHolsters.sln`, `.vcxproj`, `.vcxproj.filters`, `.vscode` (old build).

`VirtualHolstersAPI.h` changed in its description comment only; the API itself is unchanged.

### Where the code is

The fork's per-hand holster logic is one block near the top of `Holsters.cpp`. It starts with an overview comment and is split into sections: state, transform math, zones and activation, hand facts, holstering, draw stand-ins, warm weapons, drawing, blending, and ROCK input. The original code resumes at `// Main Functions`. Elsewhere in `Holsters.cpp` the fork's changes are inside the original functions, mainly `MainLoop`, `VRButtonsMain`, `detectHolsterSphere`, `loadConfigFiles` and the weapon display functions.

### History

The fork's history is squashed into these commits, oldest first:

1. `feature/runtime`: standalone FO4VR build and native runtime foundation.
2. `feature/holsters`: per-hand holstering, held pull-out and slot controls.
3. `build/repository`: vcpkg from `VCPKG_ROOT`, tests outside the default build.
4. `fix/config`: runtime settings in `Mods_Config`.
5. `feature/holsters`: per-hand akimbo holstering through ROCK 1.0.
6. `fix/config`: copy the shipped default files into `Mods_Config`.
7. `fix/holsters`: holster button through ROCK, config-mode input.
8. `feature/holsters`: draw the holstered weapon straight into the hand.
9. `fix/holsters`: two-handed weapons and firing with the draw grip held.
10. `feature/holsters`: trace holster input gates, presses and releases.
11. `cleanup/holsters`: organize, comment and indent the fork's code.
12. `docs/repository`: this changelog and the project instructions (`AGENTS.md`).
