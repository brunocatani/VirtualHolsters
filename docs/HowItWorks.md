# How the per-hand holster code works

This page follows one press of the holster button through the code. It covers what this fork added on top of the original `Holsters.cpp`.

## The files

| File | What is in it |
| --- | --- |
| `Holsters.cpp` | The original runtime: main loop, config mode, holster spheres, the weapons shown in the holsters, the API. |
| `HandHolsters.cpp/.h` | Reads the holster button for each hand and decides what a press does. Holstering, loose weapons and grenades are here. |
| `HolsterDraw.cpp` | A draw or swap, which takes several frames, and the weapon that moves from the holster into the hand. |
| `HolsterInteraction.h` | The button rules (when is it a press, when a release) and which zone a hand is in. No game types, so it has unit tests. |
| `HolsterMath.cpp/.h` | Transform helpers. |
| `HolsterTrace.cpp/.h` | Log output only. Nothing in it changes what a holster does. |
| `RockProvider.cpp/.h` | Every call into ROCK. No other file talks to ROCK. |
| `InvSorting.cpp/.h` | Inventory: finding a weapon's stack, the holster tags, equip and unequip without ROCK. |

## Words used in the code

- **Slot**: a holster, numbered 1 to 7 (1 and 2 are the shoulders).
- **Tag**: the weapon name stored for a slot (`holsteredWeapNames[slot]`, `"Empty"` when free). A slot only stores the name. The real inventory item is looked up again each time.
- **Stack**: one inventory entry (`WeaponStack`), found by name with `FindStoredStack`.
- **Holster copy**: the cloned weapon model shown in the holster (`MyWeapon`, `WeaponRegisteredObjects`).
- **Stand-in**: the holster copy after it moved into the hand at the start of a draw. It is shown until the real weapon appears, so the hand is never empty.
- **Facts**: what a hand holds right now (`HandWeaponFacts`).

## Each frame

`MainLoop` in `Holsters.cpp` calls, in this order:

1. `ServiceTransfers`, `ServiceThrowableDraws`, `ServiceDeposits`: continue work started in an earlier frame.
2. `ServiceHandoffs`, `ServiceWarmWeapons`, `UpdateWeaponBlends`: remove stand-ins that are no longer needed and move the holster copies that are sliding.
3. `detectHolsterSphere`: finds the holster zone each hand is in and stores it in `gLeftHand.zone` and `gRightHand.zone`.
4. `VRButtonsMain`: reads the controllers and calls `UpdateHandGestures`, then `RunHandActions`.

Everything a hand needs is in one `HandState` (`gLeftHand`, `gRightHand`), declared in `HandHolsters.h`.

## One press, step by step

1. **What does the hand hold?** `GetHandWeaponFacts` answers this. With ROCK loaded it asks ROCK, because with ROCK each hand can hold its own weapon. Without ROCK it looks at the one vanilla weapon.
2. **Press or release?** `UpdateHandGestures` gives the button state to `hand.gesture` (`HolsterInteraction.h`). The result is one of two things:
   - *press-draw*: the button went down inside a holster the hand can draw from. Drawing happens on the press.
   - *release-in-zone*: the button came up inside a holster. Holstering and swapping happen on the release.
3. **What should happen?** `RunHandActions` calls `DecideAction`, which returns one of:
   - `HolsterAction_Holster`: the hand has a weapon and the slot is empty.
   - `HolsterAction_Draw`: the slot has a weapon and the hand is free.
   - `HolsterAction_Swap`: both have a weapon.
   - `HolsterAction_None`.
4. **Do it.**
   - Holster: `HolsterHandWeapon` shows the holster copy, unequips the weapon and sets the tag. It is finished in the same frame.
   - Draw or swap: `BeginDraw` (next section).

## A draw takes several frames

Equipping is not instant, so a draw is stored in `hand.transfer` (`HandTransfer`) and continued each frame.

1. `BeginDraw` asks for the equip (`RequestDraw`) and moves the holster copy into the hand (`TakeDisplayIntoHand`). The slot keeps its tag.
2. `ServiceTransfers` asks each frame how the equip is going.
3. When it succeeded, `CompleteTransfer` clears the slot's tag (or, for a swap, gives the slot the weapon that left the hand) and passes the stand-in to `hand.handoff`.
4. `ServiceHandoffs` removes the stand-in as soon as the real weapon is visible.
5. If the equip fails, `AbortTransfer` puts the copy back. The slot never lost its tag, so nothing is lost.

## Loose weapons (ROCK only)

A loose weapon is one picked up from the world with ROCK. It is not equipped.

1. `UpdateDepositClaim` runs every frame. While the hand holds a loose weapon inside an empty holster, it asks ROCK to reserve that weapon (`hand.deposit.lease`).
2. On release in that slot, `StartDeposit` sets `committing`.
3. `ServiceDeposits` waits until ROCK has let go, then `DepositReference` picks the weapon up into the inventory and sets the slot's tag.

`DepositReference` is also what the API function `AddHolster` calls.

## Grenades and mines (ROCK only)

A grenade slot is never emptied by a draw. It keeps its tag while the inventory has that grenade.

1. `BeginThrowableDraw` asks ROCK to put one grenade from the inventory into the hand as a grabbed object (`hand.throwableDraw`).
2. `ServiceThrowableDraws` waits for ROCK's answer.
3. ROCK keeps the button for that press, so letting go throws the grenade.
4. `ServiceThrowableHold`: keeping the button held inside the holster for 1.5 seconds empties the slot.

## ROCK and the holster button

The holster button is the grip by default, which is also ROCK's grab button. `ReserveRockInput` tells ROCK each frame which hand is working a holster, so ROCK does not grab or drop with that press. `Rock::BeginFrame` and `Rock::EndFrame` in `MainLoop` open and close ROCK's frame.

Without ROCK, `Rock::IsBound()` is false and the code takes the vanilla path: one weapon, in the weapon hand.

## Where to look

| To change | Look at |
| --- | --- |
| When a press counts, or zone selection | `HolsterInteraction.h` |
| What a press does | `DecideAction`, `RunHandActions` in `HandHolsters.cpp` |
| How a weapon goes into a holster | `HolsterHandWeapon` in `HandHolsters.cpp` |
| How a weapon comes out | `BeginDraw`, `ServiceTransfers` in `HolsterDraw.cpp` |
| The slide between holster and hand | `AdvanceBlend`, `AdvanceStandIn` in `HolsterDraw.cpp` |
| Timings | The constants at the top of `HandHolsters.h` |
| A call into ROCK | `RockProvider.cpp` |
| A log line | `HolsterTrace.cpp` |
