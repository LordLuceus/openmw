# Project Hortator — Accessible OpenMW

A screen-reader-accessible build of OpenMW, the open-source engine for *The Elder
Scrolls III: Morrowind*. This build adds spoken output, a target scanner,
auto-walk, an accessible HUD, and combat assistance so that blind and
low-vision players can play Morrowind by ear.

This README covers the accessibility features only. For general OpenMW setup
(pointing the engine at your Morrowind data files, installing mods, etc.) see the
standard OpenMW documentation at https://openmw.readthedocs.io.

---

## Getting started

1. Install and configure OpenMW as normal so it can find your Morrowind data
   (run the launcher / wizard once if you haven't).
2. Launch the game. Speech is on by default; you should hear menus and the world
   announced as you play.
3. Press **H** in gameplay to open the accessible HUD and confirm speech is
   working.

If you hear nothing, check that your screen reader or the system speech output is
running, then see **Troubleshooting** below.

---

## Core concepts

**The scanner.** The world around you is grouped into categories (people, doors,
containers, items, and so on). You pick a category, then cycle through the
objects in it nearest-first. The currently selected object is your *target*.

**The target.** Almost everything you do — facing it, walking to it, activating
it, attacking it — acts on the currently selected target, so you never need to
aim a crosshair you can't see.

**Auto-walk.** Once you have a target, you can have the game walk you to it
automatically, routing around obstacles. Press any movement key to cancel.

**The audio beacon.** A looping directional sound placed on your target so you
can find the way by ear, useful when auto-walk can't reach somewhere and you need
to navigate the last stretch yourself.

---

## Key bindings

These are in addition to OpenMW's normal controls. The accessibility keys use
keys that Morrowind itself leaves free (the number row with Ctrl, Page Up/Down,
the arrow keys with Ctrl, etc.), so they don't clash with movement or combat.

### Scanning and targets

| Key | Action |
| --- | --- |
| **Page Down / Page Up** | Cycle to the next / previous target in the current category |
| **Ctrl + Page Down / Page Up** | Switch to the next / previous category |
| **Shift + Page Down / Page Up** | Cycle the subcategory filter (e.g. Plants / Storage within Containers) |
| **Ctrl + 1 … 8** | Jump straight to a category: 1 People, 2 Doors, 3 Containers, 4 Items, 5 Activators, 6 Detected, 7 Waypoints, 8 Locations |
| **Home** | Repeat the last announcement |
| **Backspace** | Jump back to the first (nearest) target in the category |
| **End** | Clear the current selection |
| **/** (slash) | Search: filter the current category by name |
| **Ctrl + /** | Clear an active search filter |

### Acting on the target

| Key | Action |
| --- | --- |
| **Enter** | Face the target (turn to look directly at it) |
| **Shift + Enter** | Auto-walk to the target |
| **Ctrl + Enter** | Toggle the audio beacon on the target |
| **Space** | Activate the target (open, take, talk, etc.) — falls back to normal Activate if nothing is selected |
| **X** | Toggle combat / interaction lock-on to the target (keeps you aimed at it) |
| **Shift + X** | Engage: jump to the nearest hostile and lock onto it in one press |

### Information and orientation

| Key | Action |
| --- | --- |
| **Alt + H** | Read your health |
| **Alt + M** | Read your magicka |
| **Alt + F** | Read your fatigue |
| **Shift + Alt + H** | Read the current enemy's health |
| **L** | Announce your location (cell name) |
| **Shift + L** | Announce which way you're facing |
| **Ctrl + Left / Right** | Snap your facing to the previous / next compass point |
| **Ctrl + Down** | Turn around 180 degrees |
| **N** | Drop a named map note (waypoint) at your current position |

### The accessible HUD

| Key | Action |
| --- | --- |
| **H** | Open / close the accessible HUD (pauses the world) |
| **Up / Down** | Move through the list of stats / status |
| **Enter** | On the active-effects row, drill into the list of individual effects |
| **Left / Escape** | Back out of the effects sub-list (or close the HUD from the top level) |
| **Home** | Re-read the current row |

While the HUD is open, the scanner and quick-info keys keep working, so you can
still cycle targets, check health, and so on.

---

## Auto-walk in depth

Auto-walk steers you to your selected target, routing around walls and other
obstacles, opening ordinary closed doors in your way, and warning you up front
about hazards (deep water, steep drops) on long cross-country routes.

- **Start it** with **Shift + Enter** on a target.
- **Cancel it** by pressing any of your movement keys, or by selecting a new
  target and starting again.
- On arrival it announces "Arrived at *name*." If it can't get all the way there,
  it stops, turns to face the target, and tells you how far short it is (and
  whether the target is above or below you) so you can finish on foot — the audio
  beacon (**Ctrl + Enter**) is the easiest way to find the last stretch by ear.

Auto-walk follows the game's navigation mesh — an invisible map of walkable
ground that the engine builds in the background as you explore. This is reliable
the vast majority of the time, but it is not perfect, and occasionally it can get
stuck. The next section is what to do when that happens.

---

## Troubleshooting

### Auto-walk gets stuck, stops short, or won't reach something

This is the most common rough edge. Auto-walk depends on the navigation mesh,
which the engine generates in the background — and on a busy or slower machine it
can lag behind, leaving a temporary gap in the route. Try these in order; the
first one fixes it most of the time:

1. **Just start auto-walk again.** Re-issuing it (**Shift + Enter**) forces a
   fresh route against the now more-complete navigation mesh. This alone resolves
   most stuck situations.
2. **Wait a few seconds, then try again.** If you've only just entered an area,
   the navigation mesh may still be building. Standing still for a moment lets it
   catch up, then a fresh auto-walk routes cleanly.
3. **Take a few manual steps, then try again.** Walking forward a little (which
   also cancels auto-walk) moves you to a spot with a clearer route. Nudging
   forward and slightly to one side past the stuck point, then re-issuing
   auto-walk, very often works.
4. **Use the audio beacon.** Turn it on with **Ctrl + Enter** and follow the
   sound. When the path is something auto-walk can't model (a tricky doorway,
   stairs up to a balcony), your ears can thread it where the router can't.
5. **Walk to a closer target first.** Pick a nearer object on the way (cycle to
   it with Page Down, or jump to a category with Ctrl + a number) and auto-walk to
   that, then continue from there. Breaking a long trip into legs sidesteps a
   single bad spot.
6. **As a last resort, reload.** Reloading your save rebuilds the area fresh and
   clears a one-off bad state.

A few things that are **not** the cause, so you don't waste time on them: where
your mouse or camera is pointing has no effect on auto-walk (it controls your
facing itself), and it isn't tied to a specific save being broken.

### Auto-walk is more likely to struggle when…

- You've **just entered** a cell or a large exterior area (the mesh is still
  building).
- Your **computer is under heavy load** — recording software, voice chat, a
  digital audio workstation, lots of browser tabs, etc. The mesh is built on
  spare CPU, so heavy background load makes it lag. Closing some of that, or
  pausing a moment before auto-walking, helps.

### A target says "Arrived" but you can't interact with it

If this happens, you're likely separated from it by a level the router treated as
"close enough" (for example, directly below something on an upper floor). Step a
little to the side and auto-walk again, or use the beacon to approach from the
right direction. (Arrival onto people and creatures was tightened to require
being on the same level — if you still see this, it's worth reporting.)

### I hear nothing / speech stopped

- Make sure your screen reader or the system speech output is actually running.
- Toggle the accessible HUD with **H** to force a fresh announcement.
- If speech died after a long session, save and reload, or restart the game.

---

## Reporting problems

When something goes wrong, the most useful report includes:

- **What you were doing** and **where** (the cell or area name — press **L**).
- Whether it's **reproducible**, and if so, the exact steps.
- For auto-walk problems: whether your machine was **busy** at the time, and
  whether **retrying** auto-walk eventually worked.
- If you can, the OpenMW log file (`openmw.log` in your OpenMW user folder), which
  records diagnostic detail that helps pin down navigation issues.

Thank you for testing, and for helping make Morrowind playable for everyone.
