# Project Hortator — Accessible OpenMW (Beta)

A screen-reader-accessible build of OpenMW, the open-source engine for *The Elder
Scrolls III: Morrowind*. This build adds spoken output, a target scanner,
auto-walk, an accessible HUD, and combat assistance so that blind and
low-vision players can play Morrowind by ear.

This is a **beta**. Things may be rough or incomplete. Please report problems —
see **Reporting problems** at the bottom.

### The other documents in this package

- **`ACCESSIBILITY_KEYS.html`** — every keystroke, including the menus, journal,
  barter, alchemy and spellmaking.
- **`ACCESSIBILITY_AUDIO_CUES.html`** — what each sound the mod plays means.
- **`ACCESSIBILITY_MODDING.html`** — optional. How to install Morrowind mods with
  this build, plus a recommended mod list. Everything here works without mods.
- **`ACCESSIBILITY_CHANGELOG.html`** — what changed in each release.

---

## What you need

1. **A copy of Morrowind's game data.** You must own Morrowind (Steam, GOG, or
   disc). Project Hortator does **not** include the game itself — only the engine.
2. **A screen reader running.** NVDA is recommended and tested. The build also
   supports SAPI, Windows Narrator (OneCore), and JAWS.

---

## Installing

1. **Unzip this folder anywhere you have write access** — for example your Desktop
   or Documents. Do **not** put it in "Program Files" or "Program Files (x86)";
   Windows blocks writes there and the game will fail to save settings.

2. **Run `openmw-launcher.exe`.** The first time it runs, it will offer to run the
   Installation Wizard. Let it. The wizard finds your Morrowind data and sets
   everything up automatically. This is the easy path — use it if you can.

3. If the wizard can't find your data, point it at your Morrowind folder (the one
   that contains a "Data Files" folder).

### Manual setup (only if the wizard didn't work)

In the launcher:

- On the **Data Files** tab, tick `Morrowind.esm` (and any expansions:
  `Tribunal.esm`, `Bloodmoon.esm`).
- **Also** open the **Archives** list and tick the `.bsa` files (`Morrowind.bsa`,
  etc.). This step is easy to miss. If the game starts but crashes with a
  `Resource 'meshes/base_anim.nif' not found` error, it means the archives are not
  ticked — come back here and tick them.

---

## Playing

Launch the game from the launcher's **Play** button, or run `openmw.exe` directly.

Speech is on by default; you should hear menus and the world announced as you
play. Press **H** in gameplay to open the accessible HUD and confirm speech is
working. If you hear nothing, check that your screen reader or the system speech
output is running, then see **Troubleshooting** below.

Your saved games live in your Documents folder, under `My Games\OpenMW`. They are
kept **separately** from any normal OpenMW install, so testing this beta will not
touch or overwrite your existing OpenMW progress.

---

## Core concepts

**The scanner.** The world around you is grouped into categories (actors, doors,
containers, items, and so on). You pick a category, then cycle through the
objects in it nearest-first. Inside a multi-storey building the list is grouped
by floor — everything on your current level first, then the next nearest level —
so you can sweep one storey before moving on instead of being sent up and down
stairs. (Outdoors it stays plain nearest-first.) The currently selected object
is your *target*.

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

Menus and dialogs are navigated with the **arrow keys**; **Enter** activates,
**Escape** backs out. **R** re-reads the last spoken line (dialogue, book page,
etc.) — in menus, conversations and books. Out in the world, **Home** repeats the
last announcement instead, since R is Morrowind's own "ready / put away magic".

The keys below are in addition to OpenMW's normal controls. The accessibility
keys use keys that Morrowind itself leaves free (the number row with Ctrl, Page
Up/Down, the arrow keys with Ctrl, etc.), so they don't clash with movement or
combat.

**This section covers the keys you need for getting around and interacting with
the world.** For the complete list — including every menu, the journal, barter,
alchemy, spellmaking and text editing — open **`ACCESSIBILITY_KEYS.html`**, which
came with this package.

### Scanning and targets

| Key | Action |
| --- | --- |
| **Page Down / Page Up** | Cycle to the next / previous target in the current category |
| **Ctrl + Page Down / Page Up** | Switch to the next / previous category |
| **Shift + Page Down / Page Up** | Cycle the subcategory filter (e.g. Plants / Storage within Containers) |
| **Ctrl + 1 … 9** | Jump straight to a category: 1 Actors, 2 Doors, 3 Containers, 4 Items, 5 Activators, 6 Detected, 7 Waypoints, 8 Locations, 9 Terrain |
| **Home** | Announce current target |
| **Backspace** | Jump back to the first (nearest) target in the category |
| **End** | Clear the current selection |
| **/** (slash) | Search: filter the current category by name, a door's destination, or any note you've attached with Ctrl + K |
| **Ctrl + /** | Clear an active search filter |
| **Ctrl + Up** | Direction filter: show only things lying the way you're facing. Affects every category at once and follows you as you turn; press again to switch off. Handy when an NPC says something is "to the north" |

### Acting on the target

| Key | Action |
| --- | --- |
| **Enter** | Face the target (turn to look directly at it) |
| **Shift + Enter** | Auto-walk to the target |
| **Ctrl + Enter** | Toggle the audio beacon on the target |
| **Ctrl + Shift + Enter** | Teleport to the target — a last resort for somewhere auto-walk genuinely can't reach. Warns before it acts, and is unavailable during character creation (see **When auto-walk can't get there** below) |
| **Space** | Activate the target (open, take, talk, etc.) — falls back to normal Activate if nothing is selected |
| **X** | Toggle combat / interaction lock-on to the target (keeps you aimed at it) |
| **Shift + X** | Engage: jump to the nearest hostile and lock onto it in one press |
| **K** | Mark / unmark the target as "already looked at" (it reads back as "marked"). Handy for keeping track when looting a room full of identical crates. Marks are permanent: they persist when you leave and return, and survive saving and reloading. Each save keeps its own marks (stored in a small companion file beside the save). Press K again on an object to unmark it. Marks also survive installing, removing or reordering mods; if you uninstall the mod an object came from, its mark is dropped, since the object itself is gone (see **Marks and your mod list** below) |
| **Ctrl + K** | Attach or edit a note on the target — a label of your own that reads back with it (e.g. naming a silt-strider caravaner). Notes are searchable with **/**, so labelling someone "silt strider" lets you find them by typing "silt". Marks the object if it wasn't already |
| **Shift + K** | Cycle the marked-object view: show all objects (the default), then unmarked only (hides what you've checked, so you cycle only what's left), then marked only (focus on just the objects you've flagged), then back to all |

### Information and orientation

| Key | Action |
| --- | --- |
| **I** | Inspect the selected object's hidden state — says whether a one-shot mechanism has "Already used" or is "Not used yet", then reads out its script variables (e.g. whether a puzzle lever is "on" or "off"). Says "has no readable state" for objects with nothing to report |
| **Alt + H** | Read your health |
| **Alt + M** | Read your magicka |
| **Alt + F** | Read your fatigue |
| **Shift + Alt + H** | Read the current target's health |
| **L** | Announce your location (cell name) |
| **Ctrl + L** | Announce which way you're facing (compass point) |
| **Shift + L** | Announce your height above the ground, or depth underwater |
| **Alt + L** | Announce the time of day and date |
| **Ctrl + Left / Right** | Snap your facing to the previous / next compass point |
| **Ctrl + Down** | Turn around 180 degrees |
| **Ctrl + Up** | Direction filter: narrow the scanner to only what lies the way you're facing (see Scanning and targets) |
| **Shift + Up / Down** | Aim your view up / down a step. Cycles five fixed stops — straight up, up, level, down, straight down — and says where you're aimed. Use it to fly up or down with Levitation, or to surface / dive while swimming |
| **Shift + Home** | Snap your view back to level (horizontal) |
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

### The console

Open the console with the default `` ` `` or `~` key. Typing and output are spoken.
The console takes the keyboard for as long as it is open, even if another window
is already up; the window underneath goes quiet and puts you back where you were
when you close the console again. That matters because the console is the only
way out of a few situations the game can't otherwise recover from — see
**Getting unstuck with the console** under Troubleshooting.

| Key | Action |
| --- | --- |
| **Up / Down** | Recall previous commands |
| **Ctrl + Up / Down** | Re-hear previous output, line by line |
| **Ctrl + T** | Make the scanner's selected object the console target (the keyboard replacement for clicking an object). Press with nothing selected to clear the target |

---

## Terrain: hazards and shafts

Some things that matter are features of the room itself rather than objects in
it — a lava pool, or the open shaft that is the only way between floors of a
Telvanni tower. These live in the scanner's **Terrain** category
(**Ctrl + 9**), with subcategories **All**, **Hazards** and **Shafts** you can
move between using the usual subcategory keys.

Putting them in the scanner means they behave like every other target: cycle
through them nearest first, hear what each one is with its distance and bearing,
face one, put the audio beacon on it, or **auto-walk to it**. That last one
matters for shafts in particular — walking into the shaft is now just
auto-walking to the selected entry, the same keystroke you'd use to walk to a
door, and it puts you in the column ready to levitate up or down.

The category is skipped when you cycle past it in a room that has neither, so it
only shows up where there is something to report.

### Damaging terrain

Some places try to kill you just for standing in them. Lava is the worst offender
— it deals heavy damage every second, so a wrong step in a Dwemer ruin can end a
character in a few seconds, and it burns your followers too. A sighted player
simply sees the glowing pool and walks around it.

Two things now give you the same information.

**You are warned as you approach.** Walk within about four metres of damaging
terrain and you'll hear what it is and where, for example *"Lava, 3.4 metres
north."* You get one warning per pool as you come near it rather than a
repeating alarm, and you'll be warned again if you leave and come back. Terrain
more than a storey above or below you is ignored, so a lava cave under a walkway
won't nag you while you cross it.

**You can find it with the scanner**, under Terrain's **Hazards** subcategory,
as described above. Where a room holds several, they're lettered — "Lava A",
"Lava B" — so you can tell which one is being announced as you cycle.

A few details worth knowing:

- **Large pools are announced as one thing.** A big lava lake is built out of
  many separate pieces in the game data; they're merged and reported by their
  nearest edge, which is the part that can actually burn you.
- **It works in mods, not just the original game.** Nothing here looks for
  "lava" by name. It looks for terrain whose script does damage to whoever
  stands on or touches it, which is how the game itself makes lava dangerous.
  Any mod's acid pit, fire trap or poison pool is picked up the same way,
  including in Tamriel Rebuilt and other landmass mods.
- **Decorative lava is correctly ignored.** Lava-textured rock and cave walls
  are scenery and can't hurt you, so they are not announced. In one vanilla cave
  only 8 of its 136 lava-named objects are actually dangerous.
- **Unnamed hazards get a sensible name.** Most damaging terrain has no name in
  the game data at all. Where the substance can be recognised you'll hear
  "Lava", "Acid", "Fire" and so on; where it can't, you'll hear "Hazard" rather
  than a meaningless internal id.

This is a warning system, not a barrier: nothing stops you walking into lava if
you choose to. Auto-walk doesn't plan around damaging terrain either, but it does
notice when it has walked you into some — it stops, pulls you back to safe
ground, and says *"Path crosses a hazard"* — so the warnings above are your
advance notice, and that is the safety net behind them.

---

## Auto-walk in depth

Auto-walk steers you to your selected target, routing around walls and other
obstacles, opening ordinary closed doors in your way, and warning you up front
about hazards (deep water, steep drops) on long cross-country routes. It opens
only safe doors: a **locked** or **trapped** door is never forced, so auto-walk
can never spring a trap on you. Passing such a door — a locked house as you
cross a town, say — doesn't interrupt the walk. But if one genuinely stands
across your route, you'll come up against it and auto-walk stops and tells you
which it is. Unlock or disarm it and walk on.

A **door leading to another area** is never opened for you either, so you can't
be teleported somewhere you didn't choose. Such a door is solid, so if one stands
across your route auto-walk simply stops against it and reports that it can't
reach the target — step through the door yourself and carry on from the other
side.

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

## Tips and tricks

A few things that aren't strictly part of the mod, but make the game far smoother
to play blind.

- **Get reliable Levitation as early as you can — it is the single most useful
  thing you can carry.** Vertical movement is where blind play is hardest:
  navigation routes you around the ground floor well, but it can't fly you up to a
  ledge, across a gap, or out of a pit, and some places (deep Dwemer ruins,
  shafts, broken stairways) can leave you genuinely stuck on foot even when a
  sighted player would just hop up. A Levitation spell or a stack of Levitation
  potions turns every one of those into a non-problem: rise straight up, drift
  over, done. It is worth far more to you than to a sighted player. Buy potions
  whenever you see them, and pick up the spell as soon as you can afford it.
- **Why not just teleport out?** Recall, Almsivi Intervention and Divine
  Intervention will get *you* out of a bad spot, but they leave any follower or
  companion behind, stranded where you were — which usually creates a bigger
  problem than the one you escaped. Levitation keeps you and your companion
  together.
- **Mark and Recall your home base.** Cast Mark somewhere central (your house, a
  guild hall) and you can Recall back to it to sell, store loot, and resupply
  without a long manual trek. It's a fast-travel point you control — just
  remember the follower caveat above if anyone is travelling with you.

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

### When auto-walk can't get there at all

Some places simply cannot be walked to — a ledge you levitated up to, the far
side of a gap, a spot the engine's route map doesn't connect. Press
**Ctrl + Shift + Enter** to teleport straight to the selected target.

**Treat this as a genuine last resort.** It ignores whatever stands between you
and the target: locked doors, walls, and routes the game expects you to find for
yourself. Used too early it can drop you somewhere you were never meant to reach
yet, which can break quests or scripted events — sometimes with no obvious sign
that anything has gone wrong. Explore properly and let auto-walk try first.

Because of that, the first time you use it you get a warning asking you to
confirm, with three choices: cancel, teleport this once, or teleport and stop
warning you. Cancel is selected when the warning opens, so pressing Enter out of
habit does nothing. If you pick *"Teleport and stop warning me"*, the warning
stays off from then on (including after restarting); set `accessibility teleport
warning acknowledged` back to `false` in `settings.cfg` to bring it back.

It is not fast travel, and two limits are not negotiable:

- It only reaches about **58 metres**. Anything further says *"… is too far to
  teleport to."*
- It is **unavailable during character creation**, where jumping ahead would
  break the game's own scripted opening. It says so if you try.

Followers within range come with you, so you won't strand a companion.

### Getting unstuck with the console

Rarely, the game can put you somewhere it has no way out of. The console is the
escape hatch, and it now works even with another window already open.

The known case is a **conversation topic that loops forever** — every option
returns you to the same line and none of them ends it. Open the console and type
`TM` (then Enter) to hide the menus, which breaks the loop; `TM` again puts them
back. Saving and reloading does *not* help here, so without this you would have
to abandon your progress.

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
- You're running a **mod that shrinks the player's collision size** (see the next
  section) — these help in tight spaces but can struggle in complex multi-level
  Dwemer ruins.

### Mods that shrink the player's collision box ("Jammings off")

**Short version: these mods are fine to use and genuinely help in tight spaces.
Auto-walk will no longer let them kill you — but in complex multi-level Dwemer
ruins they can fail to *reach* a target, so go in with levitation available.**

A few mods shrink the player's physical collision box, usually to stop you
snagging on doorways and narrow gaps. The best-known is **"Jammings off"**
(it replaces the base animation/skeleton meshes with smaller collision boxes).
A slimmer body really does help: it fits through tight tomb chokepoints that a
normal body wedges on, so auto-walk reaches some targets it otherwise couldn't.

The trade-off is in **complex multi-level interiors, especially Dwemer ruins**.
Auto-walk normally follows the engine's navigation mesh, which is computed for
your exact body size. Where the mesh doesn't fully connect a tricky multi-level
route (steep Dwemer stairs and crests are the worst), auto-walk falls back to the
cell's hand-authored waypoint path — and that path is the **same for every body
size**. A normal-width body can ride those crests; a shrunk one sometimes can't
make the step up and **slides off the edge instead**. The classic case is the
climb out of **Arkngthand**: an unmodded character walks it, a shrunk one slides
off a ledge inside.

**What auto-walk now does about it.** When the shrunk body pitches into a fall
during auto-walk, the game **catches you** — it snaps you back to the last safe
ground you crossed, stops, and says *"Path drops off. Cannot reach … safely."*
You stay alive and standing instead of dying. So these mods are **no longer
dangerous** to use.

What the catch *can't* do is get you up a crest your body physically can't climb.
So in those spots auto-walk will stop short rather than reach the target. Practical
advice:

- **Keep the mod if you like it** — the tight-space benefit is real, and falls are
  now caught rather than fatal.
- **Carry a levitation option** (spell, potion, or scroll) when exploring Dwemer
  ruins and other complex multi-level interiors. If auto-walk reports it can't
  reach somewhere safely, a short levitate gets you over the crest, then walk on.
- **"Jammings off" is a data-files-only mod** (no plugin/ESP/ESM, just replacement
  meshes), so it's safe to toggle on and off whenever you like — you can leave it
  on for general play and switch it off before a Dwemer ruin if you'd rather have
  stock routing there, with no effect on your save.
- If you'd rather auto-walk just route everything the normal way, **removing the
  collision-box mod** restores stock routing (you lose the tight-space benefit but
  every navmesh route is one your body can walk).

### A target says "Arrived" but you can't interact with it

Auto-walk now only declares arrival when the target is genuinely within reach, so
this should be rare. If it still happens, you're likely separated from the target
by a level the router treated as "close enough" — step a little to the side and
auto-walk again, or use the beacon to approach from the right direction. (Arrival
onto people and creatures requires being on the same level, and arrival onto an
object now requires being close enough to actually interact with it — so a coin on
a high ledge directly above you no longer reads as "arrived". If you still see a
false arrival, it's worth reporting.)

### Marks and your mod list

Marks you put on objects with **K** are stored per save and survive changing your
mods: install, remove or reorder plugins and your marks stay on the objects they
were put on.

Two things are worth knowing:

- If you **uninstall the mod an object came from**, its marks are dropped. The
  object no longer exists, and keeping the mark would mean reading your label out
  on some unrelated object instead.
- Marks made with **builds before 9 August 2026** can't be protected
  retrospectively — those older files didn't record the information needed to
  identify the objects. They keep working as long as your load order stays as it
  is, but if you change your mods they may disappear and have to be redone. Any
  mark you make from this build onward is safe.

### The game crashes on startup with a "Resource not found" error

The `.bsa` archives aren't ticked in the launcher. See **Manual setup** above.

### I hear nothing / speech stopped

- Make sure your screen reader or the system speech output is actually running.
- Toggle the accessible HUD with **H** to force a fresh announcement.
- If speech died after a long session, save and reload, or restart the game.

---

## Updating (patches between full releases)

Most updates fix or add accessibility features in the game engine itself. For
those, instead of re-downloading this whole package, you may get a small **patch**
zip — named something like `Project-Hortator-Patch-2026-06-13.zip` — that contains
just `openmw.exe` (plus this readme and the changelog).

To apply a patch:

1. Open the folder where you installed Project Hortator (the folder that contains
   `openmw.exe` and this readme).
2. Copy the `openmw.exe` from the patch zip into that folder, replacing
   ("overwriting") the existing `openmw.exe`. Say yes when asked to replace it.
3. That's it — launch the game as usual. Your settings and saves are untouched.

Notes:

- A patch only works **on top of** an existing install. If this is your first
  time, you need the full package, not a patch.
- Once in a while an update changes more than just the engine (for example a menu
  layout, a new sound, or a new setting). When that happens you will be given a
  full package again, not a patch — just unzip it fresh.
- Not sure which you have? If the zip contains lots of files and folders, it's a
  full release; if it contains essentially just `openmw.exe`, it's a patch.

---

## Reporting problems

When something goes wrong, the most useful report includes:

- **What you were doing** and **where** (the cell or area name — press **L**).
- What you **expected** to hear, and what happened instead.
- Whether it's **reproducible**, and if so, the exact steps.
- For auto-walk problems: whether your machine was **busy** at the time, and
  whether **retrying** auto-walk eventually worked.
- The OpenMW log file, which records diagnostic detail that helps pin down
  problems: `Documents\My Games\OpenMW\openmw.log`.

Thank you for testing, and for helping make Morrowind playable for everyone.
