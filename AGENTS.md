# AGENTS.md — VirtualHolsters

The user's independent FO4VR modernization of VirtualHolsters; this checkout is its source authority and implementation target.

- VirtualHolsters works with ROCK absent. ROCK is an optional runtime provider bound through its public provider API major 2: the build reads only ROCK's public headers through the `ROCK_INCLUDE_DIR` cache variable (set in the ignored `CMakeUserPresets.json`), never links ROCK and never includes its private sources. With ROCK loaded, hand weapon facts, hand-directed equip and unequip, input reservation and shoulder routing come from ROCK; without it, the vanilla single-weapon path runs. Do not restore the retired ROCK-side VirtualHolsters ABI or input-deferral bridge.
- The Holsters runtime owns its OpenVR client, update integration, detector, haptics, input filtering, inventory transitions, persistence and stored-weapon presentation.
- Design only the narrow compatibility and event surface current Holsters work needs. All ROCK calls live in `RockProvider.cpp`; PAPER and PAPER_Toolkit accommodation needs its own explicitly started phase.
- Tracked source and build configuration stay portable; machine-local dependency, SDK and deploy paths live only in ignored local configuration.
- Remote: `origin` at `https://github.com/brunocatani/VirtualHolsters`.
