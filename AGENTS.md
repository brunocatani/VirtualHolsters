# AGENTS.md — VirtualHolsters

VirtualHolsters for Fallout 4 VR: weapons are holstered and drawn at body zones with the VR controllers. This repository modernizes CylonSurfer's VirtualHolsters; `CHANGELOG.md` lists everything changed since the upstream version.

## Layout

- `Holsters.cpp`: the runtime. The per-hand holster interaction is the block near the top of the file, with an overview comment and sections; the original code follows from `// Main Functions`.
- `RockProvider.cpp/.h`: the only code that calls ROCK.
- `HolsterInteraction.h`, `HolsterSlotPolicy.h`: the holster button gesture, zone selection and slot rules, free of engine types and unit tested.
- `NativeEquipment`, `VRSceneGraph`, `VRHook`, `hook`, `LegacyBridge`, `ConfigPaths.h`: FO4VR, OpenVR and plugin-entry plumbing.
- `InvSorting`, `ConfigModeMenu`, `weaponOffset`, `MenuChecker`, `VRHGameEvents`: inventory tags, config mode, weapon offsets, menus and game events.
- `PapyrusSource/`: the mod's Papyrus scripts. `tests/`: unit tests.

## Build and tests

- CMake with vcpkg (`VCPKG_ROOT`) against CommonLibF4VR and the F4SEVR SDK: `cmake --preset fast`, then `cmake --build build-fast --config Release --target VirtualHolsters`.
- Cache variables, set in an ignored `CMakeUserPresets.json`: `ROCK_INCLUDE_DIR` (required; ROCK's public `include/` folder, which holds `ROCK/Client.h`), `COMMON_LIB_F4VR_PATH` and `LEGACY_F4SEVR_PATH` when the defaults do not match the checkout layout, and `POST_BUILD_COPY_PLUGIN` with `COPY_PLUGIN_BASE_PATH` to copy the DLL into a mod folder's `F4SE/Plugins`.
- Tests build only through the `build_tests` target: `cmake --build build-fast --config Release --target build_tests`, then `ctest --test-dir build-fast -C Release --output-on-failure`.

## Runtime files

- Settings live in the Documents folder under `My Games/Fallout4VR/Mods_Config/VirtualHolsters`: `VirtualHolsters.ini` and the `VRH_weapon_offsets`, `VRH_WeaponBlackList` and `VRH_MagazineBlackList` folders. A missing INI is created with the defaults; shipped default files are copied there once and a user's file is never overwritten.
- Log: `My Games/Fallout4VR/F4SE/VirtualHolsters.log`.

## Invariants

- VirtualHolsters works without ROCK. ROCK is an optional runtime provider bound through its public provider API major 2: the build reads only ROCK's public headers, never links ROCK and never includes its private sources. With ROCK loaded, hand weapon facts, hand-directed equip and unequip, input reservation and shoulder routing come from ROCK; without it the vanilla single-weapon path runs. ROCK has no VirtualHolsters-specific interface; this mod is only a ROCK consumer.
- The Holsters runtime owns its OpenVR access, frame hook, zone detection, haptics, input filtering, inventory transitions, persistence and stored-weapon presentation.
- A new hardcoded FO4VR address or vtable slot is checked against the live bytes before use and fails closed, as `hook.cpp` and `VRSceneGraph.cpp` do.
- Tracked source and build configuration stay portable; machine-local dependency, SDK and deploy paths live only in ignored local configuration.

Remote: `origin` at `https://github.com/brunocatani/VirtualHolsters`.
