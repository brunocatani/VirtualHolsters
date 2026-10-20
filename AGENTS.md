# AGENTS.md — VirtualHolsters

The user's independent FO4VR modernization of VirtualHolsters; this checkout is its source authority and implementation target.

- VirtualHolsters works with ROCK absent: never add ROCK as a build or runtime dependency, and do not restore the retired ROCK-side VirtualHolsters ABI or input-deferral bridge.
- The Holsters runtime owns its OpenVR client, update integration, detector, haptics, input filtering, inventory transitions, persistence and stored-weapon presentation.
- Design only the narrow compatibility and event surface current Holsters work needs. ROCK, PAPER and PAPER_Toolkit accommodation belongs to a separate, explicitly started compatibility phase outside this repository.
- Tracked source and build configuration stay portable; machine-local dependency, SDK and deploy paths live only in ignored local configuration.
- Remote: `origin` at `https://github.com/brunocatani/VirtualHolsters`.
