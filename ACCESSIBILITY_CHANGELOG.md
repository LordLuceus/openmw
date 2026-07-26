# Project Hortator Changelog

A running log of screen-reader accessibility features added to this OpenMW fork.
Newest changes are listed first. (OpenMW's own engine changelog lives in
`CHANGELOG.md`.)

## 2026-07-26

- **Auto-walk can now descend to targets below you while levitating, instead of
  giving up on the floor above.** Previously, flying to something on a lower
  level (a person at the bottom of a Telvanni tower's levitation shaft, say)
  would wander around the upper platform for a bit and then stop with "target is
  N metres below" -- because the walk was standing on the very floor separating
  you from the target, with nowhere to sink. Auto-walk now does what a sighted
  player does: it looks around for a genuine opening (the levitation shaft, a
  stairwell void, a balcony edge), flies level to it, and drops down through it,
  easing off as it nears the target's height. This mirrors the existing climb
  behaviour for targets high overhead. If there really is no opening nearby,
  auto-walk carries on as before and still reports honestly rather than grinding
  into the floor.

## 2026-07-21

- **Reading a skill now tells you how close it is to going up.** Sighted
  players see a progress bar in each skill's tooltip; the accessible skills list
  now speaks the same thing. Inspecting a skill appends its progress toward the
  next increase (for example "Long Blade 42. Governing attribute: Strength.
  Skill progress 63/100. ..."), or "Skill increases at maximum" once the skill
  has reached 100. The percentage matches the bar sighted players see.

## 2026-07-20

- **You now hear when you're spotted (or slip back out of sight) while
  sneaking.** Sighted players get a HUD "sneak eye" that shows while you're
  crouched and unnoticed and vanishes the moment someone sees you; blind players
  had no equivalent. There's now an audio cue when a nearby character detects
  you while sneaking (your cover is blown), and a distinct, calmer cue when you
  become unnoticed again after having been seen -- so you know when it's safe to
  move. Simply crouching when nobody's looking stays silent. (The bundled sounds
  are placeholder tones for now and can be replaced with nicer ones later.)

- **Spell-cast announcements no longer wrongly say "at you" for a
  self-only spell.** When another actor cast a spell that only affects
  themselves -- most noticeably a companion levitating or water-walking herself
  so she can follow you -- the announcement sometimes appended "at you" (for
  example "Erisa casts Levitate at you"), because the engine still resolves a
  stray aim target for such casts and it was often the player standing right in
  front. A self-only spell can never be aimed at anyone, so it's now always
  announced plainly ("Erisa casts Levitate"). Spells that genuinely reach you
  still correctly say "at you."

## 2026-07-18

- **Auto-walk no longer chases an unreachable wandering creature in place
  forever.** If you targeted a moving NPC or creature that auto-walk couldn't
  actually reach -- for example one wandering on a ledge or across a gap the
  route can't bridge -- it would inch back and forth without ever getting
  closer, and keep doing so until you manually cancelled. It now recognises when
  it's stuck far from a moving target with no progress, stops after about fifteen
  seconds, says "Can't reach [name]," and offers the teleport shortcut
  (Ctrl+Shift+Enter) in case the target is within range. Chasing a creature you
  can genuinely catch up to is unaffected -- the timeout only counts while you're
  stuck far away.

## 2026-07-16

- **The stats screen now tells you when an attribute or skill is permanently
  damaged.** Some enemies (a bonewalker's curse is the classic example) inflict
  permanent Damage Attribute/Skill, which lowers the stat until you cure it with
  Restore magic -- and, unlike a temporary Drain, it never appears in your
  active-effects list. Sighted players see the affected number turn red; blind
  players previously had no way to tell. Now, reading your attributes or skills
  in the stats window appends ", damaged" to any stat with uncured permanent
  damage (for example "Strength 34, damaged"). Temporary Drain and Absorb
  effects, which already show in the active-effects list, are correctly not
  flagged.

- **Shift+K (marked-object view) is now a three-way toggle.** It used to just
  hide marked objects and show them again. It now cycles through three modes:
  show all objects (the default), show unmarked only (hides what you've already
  checked, so you cycle only what's left), and show marked only (focus on just
  the objects you've flagged), then back to all. Each press announces the new
  mode. Toggling an object's mark (K) while a filtered view is active still
  advances the cursor to the next visible object, in whichever direction the
  view hides.

- **"Lock onto nearest hostile" (Shift+X) now always finds an attacker,
  regardless of your active scanner filters.** Previously, if you had the
  direction filter engaged (Ctrl+Up), Shift+X would only consider hostiles in
  the compass direction you happened to be facing -- so an enemy attacking from
  behind or the side made it wrongly say "No hostiles nearby." It now clears the
  direction filter, any name search, and the hide-marked view before searching,
  so it reliably locks onto the closest attacker anywhere around you.

- **Picture-only books and scrolls are no longer announced as "blank."**
  Some books consist entirely of an illustration with no body text (for
  example *The Egg of Time* and *Divine Metaphysics*). Opening one used to say
  "This book is blank," which was misleading. It now says "This book contains
  only images, which cannot be read aloud." (and the equivalent for scrolls),
  so you know there is a picture there rather than nothing at all. Genuinely
  empty books and scrolls still report as blank.

## 2026-07-14

- **Arrow keys can now reach every option in a choice dialog, even when the
  labels vary a lot in length.** In pop-up choice boxes with a vertical list of
  options (the kind used by shrines, soul-gem prompts, yes/no questions, and mod
  menus such as Uvirith's Legacy teleport platforms), an option whose text was
  much longer or shorter than its neighbours could become a dead spot for the Up
  and Down arrows -- you could not arrow onto it, and once you landed on it with
  Tab the arrows stopped responding. Such options are now always reachable with
  the arrow keys. (Tab still works everywhere too.)

## 2026-07-12

- **Auto-walk no longer stops short when a door leading to another area is in
  the way.** Previously, walking toward (or past) a door that leads elsewhere
  would halt immediately with "that door leads elsewhere, stopping". Such doors
  are solid and never open on their own during a walk, so auto-walk now simply
  keeps going; if the door genuinely blocks the route, it falls back to the
  normal "stuck, cannot reach" handling with the teleport option. Auto-walk
  still never opens a door that would move you to another area on its own.

## 2026-07-11

- **You can now attach a custom note to a marked object.** Press Ctrl+K on the
  selected object to type a short label; the scanner then speaks it as part of
  the object's identity, e.g. \"Gjalund, Khuul shipmaster, marked\". Handy for
  labelling things whose names are hard to remember -- silt-strider caravaners,
  shipmasters, or any object you want to recognise later. Plain K still toggles a
  quick mark with no note, and Shift+K still hides marked objects. Notes persist
  with your save alongside the marks, and Ctrl+K on an already-noted object edits
  its note (submit an empty note to clear it, keeping the mark).

- **Fixed auto-walk getting stuck in an endless run-up on a slope.** The run-up
  maneuver that mounts steep steps could mistake a long, steep hillside for a
  step and charge it over and over, gaining a little height, sliding back, and
  trying again forever without ever giving up -- you had to cancel the walk by
  hand. It now engages only on genuine step-height obstacles after the walker is
  truly wedged (not merely climbing a slope slowly), and each run-up counts
  against a fixed budget that refills only when you actually get closer to the
  goal. A real flight of steps still gets taken one run-up at a time, but a spot
  that cannot be climbed now gives up promptly and falls back to the usual
  \"stuck, cannot reach\" with the teleport option, instead of looping.

## 2026-07-10

- **Fixed the signpost feature hijacking activators that share a name with a
  place.** Some interactive objects are named after the cell they sit in -- the
  Sanctus Shrine you activate to complete that pilgrimage is named \"Sanctus
  Shrine\", the same as its location. The new signpost handling saw the matching
  name and spoke a direction instead of letting the object do its job, so the
  shrine could not be used. Signpost handling is now limited to signs that
  genuinely do nothing on their own (no attached script); anything that actually
  reacts when you activate it -- like the shrine -- works normally again.

## 2026-07-09

- **Road signposts now tell you which way a place is, and how far.** Signposts
  showed up in the scanner's Activators list but only spoke their bare name
  (\"Balmora\", \"Vos\"), and activating one did nothing -- so they were useless,
  even though a signpost's whole point is pointing the way. Activating a signpost
  now speaks the compass direction and distance to the place it names, e.g. \"Vos,
  northwest, 1800 metres\". This works for places you have never visited (the case
  a signpost actually helps with), reading the direction from wherever you stand.
  A sign pointing to a landmark that is not a town on the map, or read from
  indoors, just speaks the name as before rather than guess a direction.

- **Auto-walk now climbs steep steps up to a raised door instead of stalling at
  the bottom.** Some interior doors sit at the top of a short, steep flight (a
  raised threshold in Wolverine Hall, for example). Walking to one from close by
  used to jam the walker against the bottom step and never get up it, even though
  walking back a few metres and trying again worked. Auto-walk now does that for
  you automatically: when it meets a step too tall to walk straight up, it backs
  off a couple of metres for a run-up and charges up onto it. Flights of several
  steps are taken one run-up at a time until you reach the door.

- **Auto-walk correctly announces arrival at people standing on a rise.** Walking
  to someone perched a little above you -- a Telvanni Mouth on their raised dais,
  for instance -- used to end with a misleading "so-and-so is 2 metres ahead and
  2 metres above" as if it had fallen short, even though you were parked right in
  front of them and could talk to them straight away. It now checks whether you
  can actually reach the person (the same test that decides whether you can speak
  to them) and announces "Arrived" properly. Someone genuinely out of reach up on
  a balcony still reports the honest remaining distance.

## 2026-07-08

- **Auto-walk now reaches Ashlander yurt doors.** Walking to a yurt's entrance
  (and some other oddly-built doors) used to stop a few metres short and never
  arrive -- the walker aimed at the door's hidden anchor point buried in the
  middle of the tent instead of the actual doorway you step through, so it kept
  wedging against the wrong side of the shell. It now walks to the real door
  geometry, so you arrive right at the entrance where you can open it.

- **Auto-walk no longer bails out at doors that lead somewhere.** After the fix
  above, walking to a yurt door would announce "leads elsewhere, stopping" and
  quit the moment you got close -- the walker mistook the door you were heading
  for for one blocking your way. Walking to a door that leads to another place is
  exactly what you asked for, so it now arrives normally and lets you step through
  yourself. (Doors that genuinely block the route to a different target are still
  handled as before.)

- **Auto-walk can now fly up to targets high overhead.** While levitating, walking
  to something far above you -- Divayth Fyr atop his tower in Tel Fyr, a shrine on
  a raised ledge -- used to skim you along the floor to the base and then stall
  underneath it, unable to rise. Auto-walk now climbs straight up first, in open
  space where there's room, then flies in level once it's high enough, so it
  reaches targets set well above the ground. If you're genuinely boxed in beneath
  something solid with no way up, it still stops honestly and offers to teleport
  you the rest of the way.

## 2026-07-06

- **Read the journal one entry at a time.** Reading the journal used to speak a
  whole page at once, which could be several entries run together or a fragment
  of a long one. Now, while reading, press **Ctrl+Up** and **Ctrl+Down** to move
  through the journal one entry at a time -- each dated update (or, in a topic,
  each response) is read on its own, from first word to last, even if it happens
  to spill across a page break. The page you see turns to follow along, and the
  reread key (R) repeats the current entry. It tells you where you are ("Entry 4
  of 12") and stops politely at the ends ("No more entries"). Plain Up/Down still
  turn a two-page spread and Left/Right still step single pages, so you can read
  either way.

- **Object marks now stick.** Marking an object as "already looked at" (the K
  key) used to be a scratchpad for the room you were standing in: it wiped the
  moment you left the cell, and reloading a save forgot everything. Now marks are
  permanent -- they stay put when you leave and come back, and they survive
  saving and reloading. So you can mark the crates you've emptied, go clear the
  rest of a dungeon, reload after a bad fight, and still know exactly what you'd
  already looted. Marks are per-save (each save remembers its own), and you
  remove one the same way you always did, by pressing K on it again. This does
  not change the save file itself -- the marks live in a small companion file
  next to each save, so old saves keep working normally.

## 2026-07-03

- **New: inspect an object's hidden state with the I key.** Some objects have a
  state a sighted player reads from how they look -- a lever thrown up or down, a
  switch on or off -- that was completely invisible before. This is what made the
  six-lever Dwemer door puzzle in the AFFresh mod's Grinding Hall impossible:
  pulling a lever gave no feedback, so you had no way to know which were on. Now,
  with an object selected on the scanner, press **I** to hear its state read out
  (for the levers, "on" or "off"). It works generally, not just for that puzzle:
  whatever internal state a scripted object tracks, you can hear it. Objects with
  nothing to report say so ("has no readable state").

- **The teleport escape hatch now works when auto-walk stops right above or below
  a target it can't reach on foot.** If you tried to walk to something that sits
  in a vertical gap the pathfinding can't cross -- for example a hidden lever or
  hatch a few metres below the floor you're standing on (as with some modded
  dungeon entrances) -- auto-walk would say "X is N metres below" and then leave
  you with no way to get there: Ctrl+Shift+Enter did nothing. The teleport is
  only meant to arm after a walk has genuinely failed, and this particular
  "stopped short" case was mistakenly not arming it. Now it does, so you can blink
  the short gap and reach the lever (still capped to short hops, so it never
  becomes fast travel).

## 2026-07-01

- **New: mark scanner objects you've already dealt with (K), and hide them
  (Shift+K).** Looting a room full of identical "Crate" or "Urn" containers used
  to be a memory test -- cycle in order and you're fine, but the moment you switch
  to another category and come back, the list re-sorts and you've lost track of
  which ones you already emptied. Now press **K** on the selected object to mark
  it as already-looked-at; it then reads back with a "marked" tag so you can tell
  at a glance. Press **Shift+K** to hide every marked object from the scanner
  entirely, so you cycle only through the ones you still need to check -- press it
  again to bring them back. Marks last until you leave the area (they clear when
  you change cells, just like the A/B/C letters that tell identical objects
  apart). Works in every object category (containers, items, doors, people, and
  so on), but not on map-note waypoints or map locations.

## 2026-06-30

- **Auto-walk's fall-catch no longer mistakes a steep walkable slope for a deadly
  fall.** It was still stopping you on ordinary hillsides (e.g. around Caldera)
  where you can simply walk down and take no damage at all. The check had been
  looking straight down for distant ground, but a slope has just as much "ground
  far below" as a cliff does, so it couldn't tell them apart. It now reads the
  game's own falling measurement -- the height you've actually dropped through the
  air, which the game itself uses to decide fall damage. On a slope that stays at
  zero (you're never really airborne), so the catch leaves you alone; on a true
  plunge it climbs fast and the catch still saves you before you hit. As before,
  it weighs the predicted damage against your character's health and only steps in
  for a genuinely dangerous fall.

- **(Superseded, same day) Earlier attempt: judge a drop by predicted damage
  instead of a fixed distance.** This was the right idea but used a straight-down
  ground probe to estimate the fall, which still misread steep slopes as cliffs.
  The fix above replaces the probe with the game's real airborne-fall measurement.

- **Auto-walk's fall-catch no longer interrupts normal walking with false
  alarms.** The safety net that catches you before a fatal drop was sometimes
  triggering on ordinary routes -- snapping you back and stopping the walk when
  you weren't actually going to fall. It turns out a normal route (the game's
  proper walkable map) is built so it never runs along a deadly drop in the
  first place, so the catch had nothing real to do there and only misfired. The
  fall-catch now stays armed only on the rougher kinds of route where a real
  plunge is actually possible: long cross-country walks, the older hand-drawn
  node paths used inside some multi-level ruins, and last-resort straight-line
  beelines. On everyday routes it simply stays out of your way. If you do get
  steered onto one of the riskier route types and head for a real drop, it still
  catches you exactly as before.

## 2026-06-29

- **New: teleport to an auto-walk target it couldn't reach (Ctrl+Shift+Enter).**
  Sometimes a place is obviously reachable but the game's pathfinding just can't
  work out a route -- classically after you levitate up to a ledge and there's
  no walkable path back down, so auto-walk gets within a few metres and gives up.
  When that happens, auto-walk now quietly arms an escape hatch: press
  Ctrl+Shift+Enter on that same target to blink straight to it. Any followers
  standing near you come along, so you won't strand your companions. There are
  deliberate guardrails so this can't become fast travel: it only works right
  after auto-walk has actually tried and failed to reach the target, and only
  over a short hop (roughly 58 metres) -- you can't select somewhere across the
  map and warp there. If nothing is armed it says so; if the target is too far
  it refuses.

- **Auto-walk now stops you before it walks you into lava (or any damage).**
  Some places hide treasure behind a hazard you're meant to fly over -- e.g.
  Shushishi has a lava pit below a levitation-only room. Pointing auto-walk at
  something past it used to march you straight into the lava and kill you, with
  no way to stop in time. Now, if auto-walk ever leads you onto a surface that
  starts hurting you (lava, a fire field, damaging water) and you aren't in a
  fight, the game pulls you back to the last safe ground you crossed, stops, and
  says the path crosses a hazard. As with the fall-catch, you end up alive and
  standing instead of dead, and you know that spot needs levitation or another
  way around.

- **Dialogue topics now say "disabled" when you can't talk right now.**
  Sometimes an NPC cuts the conversation short -- often a quest-gated brush-off
  like "Now isn't the time, let's clear the ship of undead and then we'll talk"
  -- after which every topic in the list stops working and only Goodbye does
  anything. Sighted players see the topic list greyed out, but with a screen
  reader there was no hint: topics still read normally and selecting one just did
  nothing. Now each topic is announced with "disabled" while the list is locked,
  so you know straight away that you have to say Goodbye and come back later
  rather than clicking topics that won't respond.

## 2026-06-28

- **Auto-walk now catches you instead of letting you fall to your death.** If
  auto-walk ever steers you off a ledge or crest and you pitch into a real fall,
  the game now grabs you: it snaps you back to the last safe ground you crossed,
  stops, and says *"Path drops off. Cannot reach … safely."* — so you end up alive
  and standing rather than dead at the bottom. This mainly matters with mods that
  shrink the player's collision box (see below), where a few tricky spots used to
  mean a fatal fall, but it's a general safety net for any auto-walk drop. If you
  ever see this catch fire on ground that *wasn't* actually a dangerous fall (a
  normal step-down or a gentle slope), please report it with your `openmw.log` —
  there's now an always-on `fall-arrest` line in the log that records each catch.

- **Reversed earlier advice: collision-box-shrinking mods ("Jammings off") are now
  fine to keep.** An earlier note had advised removing these mods because they
  could get you killed during auto-walk. With the fall-catch above, that danger is
  gone, and a slimmer body genuinely helps — it fits through tight tomb chokepoints
  a normal body wedges on. The remaining limitation is that in complex multi-level Dwemer
  ruins, auto-walk sometimes can't get a shrunk body up a crest and will stop short
  of the target rather than reach it. The advice now: **keep the mod if you like
  it, but carry a levitation option (spell, potion, or scroll) when exploring
  Dwemer ruins** — if auto-walk says it can't reach somewhere safely, a short
  levitate gets you over, then walk on. See the "Mods that shrink the player's
  collision box" section in the README.

## 2026-06-27

- **Added: the enchanting window is now fully usable with a screen reader.**
  Whether you're enchanting an item yourself or paying an enchanter, you can now
  arrow through every part of the window: the name field, the item slot and the
  soul-gem slot (press Enter to choose one from the pop-up list, or to clear a
  slot you've already filled), the cast type (Left/Right to cycle Cast Once,
  When Strikes, When Used, or Constant Effect), the list of magic effects you can
  add, the effects you've already added (Enter to adjust or remove one), and the
  read-out results — enchantment points, magicka cost, charge, success chance,
  and price. The soul-gem slot also reads which soul is trapped and its strength.
  This works just like the spell-making window, which was made accessible
  earlier.

## 2026-06-26

- **Added: the jail screen now tells you your sentence.** When you're sent to
  prison, the screen reader announces it (for example "Prison. Serving 5 days.")
  instead of leaving you with a silent, sightless wait. The list of skills that
  went up or down while you were locked up is already read out at the end as
  usual.

- **Fixed: a crash to desktop when closing an accessible menu** (seen on Cancel in
  the Recharge window, but the bug was general to every accessible menu). When a
  menu closed, the screen reader tried to hand keyboard focus back to whatever
  had it before the menu opened — but if that thing had since disappeared (for
  example a temporary picker shown while you were in the menu), the game followed
  a stale reference into freed memory and crashed. The reader now notices the
  moment such a widget goes away and simply skips the hand-back, so closing a
  menu is safe.

- **Added: the Recharge window is now screen-reader accessible.** Recharging an
  enchanted item with a filled soul gem was previously unusable without sight.
  The window now reads as a simple list you arrow through: first the soul gem
  (with its soul and value; press Enter to pick a different filled gem), then each
  of your rechargeable enchanted items — each announced with its name and current
  / maximum charge, with the full enchantment detail on the T key — and finally
  Cancel. Press Enter on an item to recharge it with the chosen gem; on success
  you hear the item's new charge read back, so you know it took.

- **Added: hear your height above ground or depth underwater (Shift + L), and a
  tidier orientation layout.** When you're flying with Levitation or diving while
  swimming, you previously had no way to tell how high up or how deep you were.
  **Shift + L** now says it: "12 metres above ground" while airborne, "3 metres
  underwater" while submerged, or "On the ground" when you've landed. To make room
  (and to keep things memorable), the orientation keys now follow one rule —
  **Ctrl is horizontal, Shift is vertical** — on both the arrow keys and L. So
  "which way am I facing?" moves from Shift + L to **Ctrl + L**, leaving Shift + L
  for the new up/down readout.

- **Added: aim your view up and down without a mouse (Shift + Up / Down).** Until
  now there was no way to look up or down, because that normally needs the mouse —
  so flying straight up with Levitation, or diving and surfacing while swimming,
  wasn't really possible. **Shift + Up** aims higher and **Shift + Down** aims
  lower, snapping through five fixed stops — straight up, up, level, down, straight
  down — and announcing where you're now pointed each time. With Levitation, aim up
  and walk forward to rise (down to descend); in water, the same aims you up to
  surface or down to dive. **Shift + Home** snaps you straight back to level.

- **Changed: auto-walk now opens only safe doors, and stops instead of grinding
  against ones it can't.** Auto-walk opens an ordinary closed door in your way,
  but it used to be unaware of three kinds it should not just shove open. Now if
  the door across your path is **trapped**, it stops and says so rather than
  springing the trap on you (which could be deadly). If it's **locked**, it stops
  and tells you, instead of spinning in place trying to wiggle through a door that
  will never open. And if it's a **door to another area** (a load door), it stops
  and says it leads elsewhere rather than potentially carrying you somewhere you
  didn't choose to go. In every case you get a clear spoken reason; deal with the
  door (disarm, unlock, or step through yourself) and start auto-walk again.

- **Fixed: auto-walk no longer gives up partway to far-off outdoor targets.**
  Walking to something a long way off across open country (say a hut a hundred-
  plus metres away), auto-walk would sometimes stop after about ten seconds and
  announce "stuck" or how far you still had to go — even though you were running
  straight at it the whole time. Re-triggering it just continued, often needing
  two or three tries to actually arrive. The cause: on a long trip only the
  ground near you is loaded, so the planned route only reaches partway and its far
  end keeps moving forward as new terrain loads in — which the "are we still
  making progress?" check misread as standing still. It now also watches whether
  you're getting closer to the target as the crow flies, so steady progress
  toward a distant goal counts and the walk carries through in one go. (Genuine
  dead-ends still stop as before — a truly stuck body or one going in circles is
  caught by separate checks.)

## 2026-06-24

- **Changed: the scanner now groups things by floor inside multi-level
  buildings.** In an interior with several storeys, cycling the scanner used to
  mix floors together by raw distance, so walking down the list could send you up
  and down stairs over and over. Indoors the scanner now keeps each level
  together: it lists everything on your current floor first (nearest first), then
  the next nearest floor, and so on — so you can sweep one storey, then move to
  the next, instead of bouncing between them. This applies to all the in-world
  categories (people, doors, containers, items, activators) and to your map-note
  waypoints. Distances within a floor are unchanged, and announcements still tell
  you how far up or down something is when it's on a different level. Outdoors the
  list stays plain nearest-first, since open terrain has no real storeys to group
  by and grouping there would only scramble the order. (Global map Locations are
  unaffected, since those are whole towns rather than spots in one building.)

- **Fixed: auto-walk no longer falsely says "Arrived" beneath an overhead
  trapdoor.** Walking to a trapdoor or hatch in the ceiling above you (for
  example in an Imperial Legion fort) could announce "Arrived" while the door was
  still several metres straight up, too high to actually open. A related case was
  fixed before, but this one slipped through a second arrival path that only
  checked how far you were horizontally — so standing directly underneath counted
  as "there". Auto-walk now also checks you can really reach the trapdoor before
  declaring arrival; when it's genuinely too high to get to, it tells you how far
  short you stopped instead of pretending you made it.

- **New: a direction filter for the scanner — show only what lies the way you're
  facing.** When an NPC tells you something is "to the north" or "east of here",
  you can now declutter the scanner to just that direction. Face the way you want
  (turn, or use the compass-snap keys), then press Ctrl+Up to engage the filter:
  every scanner category — doors, people, containers, items, places, everything —
  is narrowed to only the things in that compass direction, so cycling through
  the list no longer wades past everything behind and beside you. The filter
  follows you as you turn: rotate to a new heading and what's listed updates to
  match, so you can sweep around and sample each direction in turn. Press Ctrl+Up
  again to switch it off and see everything again ("Direction filter off"). Like
  the name search and the indoor/outdoor list, it clears automatically when you
  cross between inside and outside. It pairs with the existing facing keys
  (Ctrl+Left/Right to snap to a compass point, Ctrl+Down to turn around).

- **Fixed: auto-walk now works in multi-level interiors like Dwemer ruins.**
  Auto-walking to something on a different floor of a complex interior — the
  classic case being the steep staircases of a Dwemer ruin such as Arkngthand —
  used to fail badly: it would get you partway, then either stop short at the
  edge of a level it couldn't cross, or, going up, get caught walking up a few
  steps and back down again over and over without ever reaching the top, never
  giving up on its own. Getting back *up* and out was the worst case. This is now
  fixed, and you can auto-walk down to a lower floor and all the way back up and
  out in a single go, no manual nudging or levitation needed. Three things were
  behind the fix: auto-walk now falls back to the area's hand-authored path
  network when the automatic one leaves floors disconnected (steep stairs exceed
  what it will connect on its own); it follows that route steadily instead of
  re-planning every second, which was what made it turn around on the stairs; and
  if it ever genuinely can't get through, it now recognises it's circling and
  tells you honestly rather than walking in place forever.

## 2026-06-23

- **New: a Lua scripting API for speech output (`openmw.accessibility`).** Mods
  can now speak text through the same screen-reader backend the engine uses, via
  `accessibility.say(text)` (plus `sayRereadable` and `reread`). This is the
  foundation for making custom mod menus — which the engine's built-in screen
  reader can't see into — accessible: a small standalone "accessibility plugin"
  mod can narrate another mod's UI without that mod bundling its own
  text-to-speech and without any changes to the engine. Available to player and
  menu scripts; documented in the Lua API reference.

## 2026-06-22

- **Fixed: filled soul gems now say which soul they hold.** A charged soul gem's
  item tooltip (the T key) now reads a "Soul: <creature>" line — e.g. "Soul:
  Golden Saint" — so you can tell a filled gem from an empty one, and tell your
  gems apart, without having to compare their gold values. This shows up
  anywhere item tooltips do: inventory, containers, barter, and the alchemy and
  enchanting ingredient pickers.

- **New: the alchemy (potion brewing) screen is now fully accessible.** You can
  mix and brew potions entirely by ear. Open an alchemy session and use Up/Down
  to move through the screen: the potion-name field (press Enter to type a name,
  or keep the suggested one), the brew quantity, the ingredient filter, the four
  apparatus slots (mortar and pestle, alembic, calcinator, retort), the
  ingredients you've added so far, the resulting potion effects, your full
  ingredient list, and the Create button. On an ingredient in the list, press
  Enter to add it to the mix and press T to hear its weight, value and known
  effects; each time you add or remove one, the potion's combined effects are
  read back so you can tell when two ingredients share an effect (the
  requirement for a working potion). The brew quantity announces how many you can
  actually make from what you have (e.g. "3 of 5"), and Left/Right adjusts it
  within that limit. To make a specific kind of potion, use the filter: set its
  type to Magic Effects, open the filter values, and choose an effect such as
  Restore Magicka to narrow the ingredient list to just the ones that have it;
  the active filter is spoken and can be toggled back off. The apparatus slots
  read what's installed (or "empty") and let you add a tool via a picker or
  remove the one that's there.

## 2026-06-20

- **New: the spell-creation (spellmaking) screen is now fully accessible.** You
  can design and buy custom spells entirely by ear. Open the spellmaking service
  and use Up/Down to move through the screen: the spell-name field (press Enter
  to type a name), the list of magic effects you can add, the effects already in
  your spell, the running magicka cost, success chance and price, your gold, and
  the Buy / Cancel buttons. On a magic effect in the left-hand list, press T to
  hear its school and description; press Enter to add it. Each effect you add
  opens an editor where Left/Right adjust magnitude, duration and area (hold Ctrl
  for steps of ten, or Home/End to jump to the minimum/maximum), and Left/Right
  on Range cycles Self / Touch / Target. Press Enter on OK to confirm, Delete to
  remove an effect you're editing. Every effect in your spell reads back as a
  full line (e.g. "Fire Damage 1 to 1 points for 1 second on Touch"), so you
  always know exactly what you're building before you pay for it.

- **New: the Quick Keys menu is now fully accessible.** You can assign weapons,
  items, spells and enchanted items to the number keys 1–9 entirely by ear. Open
  the menu (default F1, or your bound key), then use Up/Down to move
  between the ten slots — each reads its number and what's in it (e.g. "Quick key
  3, Fireball" or "Quick key 5, None"). Press Enter on a slot to choose Item,
  Magic, Unassign or Cancel; the item and spell pickers that open are read like
  the inventory and magic lists (name, with weight/value/effect detail on the T
  key). Assignments are spoken back as soon as you make them.

- **New: pressing a number key in gameplay now speaks what it did.** When you use
  a quick key out in the world, you hear the result — the weapon or item name,
  "<spell> ready" for a readied spell or enchanted item, "Hand To Hand", or
  "None" for an empty slot. Slots whose item or spell is no longer available
  still give the game's usual "missing" message. No more pressing a number and
  wondering whether anything happened.

## 2026-06-19

- **New: a separate volume control for accessibility cues.** All the screen-
  reader audio cues (auto-walk approach/arrival, combat in/out-of-range, the new
  magic-expiry and journal/quest sounds) now play on their own dedicated audio
  channel instead of the shared Effects channel. There's a new **Accessibility
  Cues** slider under Options → Audio, so you can turn these cues up or down
  without affecting the game's normal sound effects (and vice versa). Defaults to
  full volume.

- **New: audio cue when a key timed effect is about to wear off.** A short sound
  now plays once when a tracked effect has about five seconds left, so a sudden
  loss doesn't catch you out — for example Levitation dropping you, or Water
  Breathing running out mid-swim. The warning covers survival- and movement-
  critical effects (Levitation, Water Walking, Water Breathing, Slow Fall,
  Invisibility, Chameleon, Sanctuary); routine stat buffs are left alone to keep
  it quiet. Effects that only last five seconds or less in the first place don't
  warn (the cue would fire the instant they're cast).

- **New: audio cues for journal updates and quest completions.** Your journal
  advancing now plays a short sound, with a distinct one when an entry actually
  *completes* a quest — so you can tell "the journal updated" from "you finished
  that quest" without opening it. If several entries are added at once, you hear a
  single cue, and a completion in the batch takes priority.

- **Change: scanner filters now clear when you move between indoors and
  outdoors.** A name search or subcategory filter (e.g. "Storage" containers, the
  "Hostile" actors view) set in one place rarely makes sense in the other, and a
  forgotten filter is a common "why can't I see this door / person / item?" trap.
  Crossing an interior/exterior threshold now drops all scanner filters and says
  "Scanner filters cleared." Walking around outdoors (crossing the cell grid)
  leaves your filter intact as before.

- **Fix: auto-walk no longer falsely says "Arrived" at an out-of-reach object
  directly above or below you.** Walking to an item on a high ledge (e.g. a Dwemer
  coin 5 m overhead) used to announce arrival as soon as you were horizontally
  underneath it, even though you couldn't possibly pick it up from there. Arrival
  at a non-actor object now also requires that the object is within activation
  (interaction) reach. A ceiling hatch you open from directly below is within
  reach and still registers as arrived, so the start-of-game hatch is unaffected;
  only genuinely unreachable overhead/underfoot objects are now correctly rejected.

## 2026-06-18

- **Change: distance announcements now use horizontal distance plus a separate
  elevation, and "stopped short" gives no advice.** The scanner read a 3D
  straight-line distance while auto-walk's "stopped short" used a horizontal one,
  so a target almost directly overhead was described as "21 metres away / 21
  metres up" by one and "4 metres short" by the other. Both now report the
  horizontal (ground) distance as the headline, with the height carried by the
  separate "N metres above/below" phrase. The "stopped short" line is also
  stripped of unhelpful suggestions ("use the beacon", "find stairs") and now
  just states the facts, e.g. "Dwemer Door is 4 metres ahead and 21 metres
  above."

- **Fix: spellcast announcements only say "at you" when the spell really is aimed
  at you.** Previously an enemy casting anything while fighting you was reported
  as cast "at you", even a self-buff or a spell aimed at your companion or summon.
  The cue now uses the engine's actual resolved cast target, so "at you" is
  reliable; spells aimed at anyone (or anything) else are announced plainly as
  "<Caster> casts <spell>." (Note: ranged bolt spells resolve their target only
  on impact, so those are announced plainly too rather than guessing.)

- **Fix: auto-walk no longer falsely says "Arrived" below a target on an upper
  floor.** Walking to a person (or creature) standing on a balcony or upper
  storey could stop on the floor directly beneath them and announce arrival,
  even though they were several metres up and out of reach. Arrival at an actor
  now requires being on roughly the same level, not just horizontally beneath
  them. When the route genuinely can't get closer, the "stopped short" message
  now also tells you how far above or below the target is, so you know to look
  for stairs or a ramp.

## 2026-06-16

- **Fix: spellcast announcements no longer drop the spell name.** Some casts --
  notably scripted helper spells fired by companions and other mods (e.g. a
  cure-disease spell that has no authored name) -- were announced as
  "<Caster> casts ." with the effect missing. The announcement now falls back to
  a name built from the spell's effects ("Cure Common Disease", "Restore Health
  and Fortify Strength", etc.) when the spell has no name of its own, and stays
  silent rather than say a bare "casts" if nothing can be resolved.

- **New: the scanner now reads a container's or door's lock and trap state.**
  When you cycle to a container or door, its spoken identity now includes the
  lock level if it is locked, "Unlocked" if it has a lock that is currently open,
  and "Trapped" if a trap is armed -- e.g. "Chest, Lock Level: 50, Trapped". This
  is exactly the information a sighted player gets from the hover tooltip (the
  trap's type stays hidden, just as it does on screen), so you get the same
  forewarning before you pick a lock or spring a trap. State is read live, so it
  updates as you unlock or disarm.

- **New: the Containers scanner category now has Plants and Storage
  subcategories.** As with Items and Actors, cycle them with Shift+PageUp /
  Shift+PageDown: **All**, **Plants** (harvestable flora -- plants, mushrooms and
  the like that you pick from), and **Storage** (everything else: chests,
  barrels, sacks, urns...). The split uses the game's own organic-container flag,
  so it stays correct for mod-added flora. (Morrowind has no data telling a chest
  from a barrel, so storage is kept as one group rather than guessing kinds from
  names.)

- **Fixed: typing in an item window's name search no longer triggers stray
  shortcuts.** In the barter and companion windows, typing a letter into the Name
  search box could leak through to the window's own shortcuts -- e.g. typing "o"
  while filtering would fire the merchant's Submit Offer. The act of filtering
  rebuilt the spoken item list, which silently knocked the reader out of
  text-entry mode mid-word, so the next keystroke was treated as a command. The
  list rebuild is now held until you finish typing, so every letter goes to the
  search box as intended.

- **Changed: target lock-on moved from K to X.** Lock onto the scanner selection
  is now **X** (and **Shift+X** still engages the nearest hostile in one press).
  This keeps the lock key under the left hand so your right hand can stay on the
  mouse to swing and aim while you switch targets.

## 2026-06-15

- **New: the scanner now tells you whether a door is open or closed.** When you
  cycle to a door it is announced as part of its identity, e.g. "Wooden Door, to
  Seyda Neen, closed" or "Wooden Door, open". The state is read live each time, so
  a door you just walked through reads "open" the next time you select it.

- **New: auto-walk now flies in 3D when you are levitating.** While airborne
  (Levitation, or a flying creature form), auto-walk used to steer only left and
  right - it flew dead level into archways and could never gain height to reach a
  raised target, and a straight line could not get through tight interior spaces
  like the Vivec Puzzle Canal. It now follows the same maze-solving path as
  walking, but aims up and down as well, so it climbs and descends along the
  route and onto raised platforms. (Note: some targets are genuinely unreachable
  by flight, e.g. a shrine sealed behind a puzzle - auto-walk will get you as
  close as the path allows and then report that it is stuck.)

- **Fixed: auto-walk now opens closed doors in your way instead of wedging
  against them.** Pathfinding routes you straight through doorways on the
  assumption the door will open, but auto-walk never actually opened them - so a
  shut door looked just like a wall and the walk would stall and eventually give
  up. Auto-walk now detects a closed door directly ahead, opens it (with the
  normal sound), announces "<name>. Opening.", and carries on through. Only
  ordinary in-cell doors are opened automatically; a door that would teleport you
  to another area is left shut (so you are never silently yanked somewhere you
  did not choose to go), and locked doors stay locked.

## 2026-06-14

- **Changed: route hazard warnings now only fire during a long auto-walk.** The
  "route crosses deep water" / "route has a drop" warnings are now limited to the
  long-distance progressive auto-walk (the cross-area bee-line where they're
  actually actionable). During ordinary short auto-walks they were either
  useless or misleading - normal pathfinding already routes around cliffs, and a
  short water crossing was being announced at the very moment you heard yourself
  splash in, too late to do anything about it.

- **Fixed: Telekinesis now extends interaction reach as it should.** Activating,
  opening, or lockpicking a target while a Telekinesis effect is active now
  works from a distance - for example opening an interior door across the room -
  matching what a sighted player gets by aiming with the spell up. Previously
  the screen-reader interaction always used your bare arm's-length reach and
  ignored Telekinesis entirely, so it would just say the target was too far
  away. The bonus only applies to things the game actually lets you manipulate
  by telekinesis (items and most doors, but not people, and not plain teleport
  doors).

- **Fixed: lockpicking and Open-spelling a locked target now respect reach.**
  Because the screen-reader lets you act on a locked-on target directly (rather
  than aiming a crosshair), it was possible to pick a lock or cast Open on a
  chest clear across the room - the distance check the game normally enforces
  was being skipped. All three interaction paths (activate, lockpick/probe, and
  Open-type spells on objects) now share one reach rule, including the
  Telekinesis bonus above. If a locked target is out of reach, lockpicking now
  says "<name> is too far away." instead of silently doing nothing, so you know
  to close the distance rather than wondering why the pick didn't fire.

- **New: choice dialogs are now navigable instead of read all at once.** Pop-up
  dialogs with a row of buttons - shrine blessing pickers, the death screen
  ("load last save?"), shrine donation prompts, companion warnings, soul gem
  use, and any yes/no confirmation - previously read the prompt followed by
  every option in one breath, with no way to pick by keyboard. They now announce
  the prompt and then the first option, and you arrow (or Tab) between options -
  each spoken as "<label>, button" - and press Enter to choose. Press R to
  re-read the prompt. This is one shared dialog type, so the improvement applies
  everywhere these pop-ups appear.

- **Fixed: auto-walk no longer gives up while chasing a moving NPC.** Previously,
  walking to a wandering NPC would often report "Stuck. Cannot reach" the moment
  they strolled away after you'd gotten close, even though you were chasing
  correctly. Auto-walk now recognises a moving target, announces "<name> is
  moving." once, and keeps following until you reach them or cancel.

- **Improved: auto-walk is better at getting around tight spots.** When something
  wedges you (furniture, a doorway, a step), the recovery now probes both sides
  and sidesteps toward whichever has more open space, instead of guessing - so it
  squeezes through narrow gaps far more reliably.

- **New: auto-walk gets you past NPCs blocking the way.** When a person is
  standing in a doorway or corridor with no room to pass (a classic Morrowind
  annoyance - they aren't part of the walkable map, so you simply can't push
  through), auto-walk now announces "<name> is blocking the way. Moving past."
  and slips past them, restoring things to normal once you are clear. If you
  genuinely cannot get through, it tells you exactly who is in the way.

- **New: auto-walk warns you about dangerous routes.** When you start walking to
  a target, the route is now checked for two hazards that could get you killed
  without warning: deep water you would have to swim across (drowning, or
  slaughterfish), and steep drops the path would walk you off. If either is
  found, you hear a single warning up front - for example "Warning: route
  crosses deep water. Consider Water Walking or Levitation." or "...has a 7 metre
  drop. Consider Levitation or Slow Fall." - so you can cast a spell or cancel
  before setting off. Auto-walk still proceeds (the warning is advisory, not a
  stop), and a route that simply stays near water will not nag you repeatedly.
  Solid bridges and jetties over deep water are not flagged.

## 2026-06-13

- **New: spell tooltips now tell you the school.** When you read a spell's
  detail in your spellbook or at a spell merchant, it now announces the magic
  school (for example "School: Destruction"), matching what the visual tooltip
  shows. As in the base game, this appears only for ordinary castable spells,
  not powers, abilities, or diseases.

- **New: the console is now usable with a screen reader.** Opening the console
  announces itself (and its current target, if any). Typing is spoken as you go,
  arrow keys read the command line character by character, and Up/Down through
  your command history reads each recalled command aloud. Command OUTPUT is now
  spoken too - results like a position readout, script errors, and tab-
  completion suggestions - so you are no longer typing blind. Ctrl+Up and
  Ctrl+Down step back through previous output line by line to re-hear it.

- **New: you can clear the console target.** Pressing Ctrl+T with nothing
  selected in the scanner now clears the console's current target (it says
  "Target cleared"), giving you a deliberate way to drop a target you grabbed
  earlier. Grabbing a target takes a snapshot, so simply clearing your scanner
  selection does not change the console target on its own.

- **Fixed: taking or storing a whole stack no longer jumps the cursor to the
  search box.** When you moved an entire item stack out of your inventory (into
  a container or companion, or by dropping it), focus jumped all the way up to
  the name-search field instead of staying near the item you just moved. It now
  lands on the neighbouring item, matching the other pane. Partial moves were
  unaffected.

- **New: you can target an object in the console without clicking it.** Many
  console commands act on a selected object, which a sighted player picks by
  clicking it in the world - impossible without aiming. Now you select the
  object in the scanner during normal play (or lock onto it), then open the
  console and press Ctrl+T to adopt that selection as the console target; it
  announces the object name (e.g. "Target: Calvus Horatius"), or tells you if
  nothing is selected. This makes target-scoped commands - setting a variable
  on an NPC, resurrecting, unlocking, and so on - usable from the keyboard.

- **New: the companion-share window is now usable.** Talking to a companion and
  choosing "Companion share" opens a window that lets you move items between
  your inventory and theirs (to manage what they carry, or to keep a contract
  companion in profit). It was previously unreadable. You can now arrow through
  the companion's items - each reads as its name and count, with the full detail
  on the tooltip key - and Tab switches between their inventory and yours. Enter
  moves the whole stack across; Shift+Enter opens the count picker for part of a
  stack. The same Enter / Shift+Enter now works for ordinary containers and
  loot too (replacing the old separate "store" key), so taking and storing share
  one key in both directions. Press E at any time to hear the companion's carry
  weight, and their profit if they are a contract companion.

## 2026-06-11

- **New: the Scripts tab in Settings (mod-defined settings) is now navigable.**
  The per-mod settings pages added by Lua mods - things like footstep volumes,
  camera options, or combat tweaks - were previously unreadable. You can now
  arrow through them: each setting reads as its name plus current value (e.g.
  "Always Run: Yes"), with its longer help text available on the tooltip key.
  Checkboxes toggle with Enter or Left/Right; multiple-choice settings step with
  Left/Right; and numeric fields open for typing with Enter (type a value, then
  Escape to confirm). Group headings are announced as you cross into them, and
  each group's "Reset to defaults" button is offered at its end. Settings that
  apply through the game's mod system (which take a moment to come back) are now
  spoken only once they have actually changed, so you never hear a stale value.

- **Fix: equipping an item no longer talks over a popup it triggers.** Equipping
  an item could trigger a game message (most visibly the early-game tutorial
  popup after you equip your first weapon), but the "equipped" announcement cut
  that popup's text off mid-sentence. You were then left with a modal popup you
  hadn't heard and arrow keys that seemed dead (they were - the popup had to be
  dismissed first, but you had no way to know it was there). The equip
  announcement now waits its turn instead of interrupting, so you hear the popup
  and then the equip result.

- **Fix: interior doors no longer announce a bogus destination.** Ordinary doors
  inside buildings and dungeons - the kind that just open to another part of the
  same area rather than loading a new cell - were being read as leading somewhere
  (often "to Ashlands Region", the default exterior region) because of leftover
  data on the door. The scanner now only announces a destination for doors that
  actually teleport you elsewhere, matching the game's own door tooltip. Real
  doors between locations (e.g. "Door, to Balmora, Guild of Mages") are unchanged.

- **Fix: combat range cues (and spell names) now work with readied spells.** The
  new in/out-of-range cues did nothing while a spell was readied (any spell,
  touch or target), because the player's selected spell was read from the wrong
  place - for the player, the spell chosen in the UI is not stored where it is
  for NPCs. The same underlying bug also meant the readied-spell announcement
  said only a generic "Magic ready." instead of the spell's name, and the HUD /
  quick-info "Spell:" line was blank for spells. All three now read the player's
  actual readied spell, so the range cues fire for spells and the spell is named.

- **New: audio cues for combat range, based on what you have readied.** While
  locked onto an enemy, a short sound now tells you whether you can actually hit
  it from where you stand - and it is contextual to your readied weapon or spell:
  with a melee weapon (or hand to hand, or a touch spell) it tracks melee reach;
  with a bow, crossbow, thrown weapon, or a ranged "target" spell it tracks
  whether you have a clear line of fire to the target. You hear an "in range"
  cue the moment you can hit and an "out of range" cue the moment you cannot,
  including right when you lock on, so you know your situation immediately. For
  ranged spells this means you learn you have no clear shot *before* you spend
  the magicka, instead of only finding out after the cast fizzles. These cues
  reinforce the existing spoken "Out of range" / "No clear shot" feedback, which
  can be hard to catch over the noise of a fight. (Cues require a lock-on and
  only fire when the situation changes, so they never nag.)

- **New: audio cue when your locked target dies.** In addition to the spoken
  "<Name> is dead.", a cue now plays the moment your locked-on enemy falls -
  reinforcement for the loudest moment of combat, when the speech is easiest to
  miss.

## 2026-06-10

- **New: "engage nearest enemy" in one key (Shift+K).** When a fight starts you
  no longer have to open the HUD, cycle the scanner to Actors, find the Hostile
  filter, pick the attacker, and lock on. **Shift+K** does all of it at once:
  it jumps to the nearest actor that is actually attacking you and locks on, so
  you can immediately ready a weapon and fight. If nothing is attacking you it
  says "No hostiles nearby." and does nothing. (Plain **K** still toggles lock-on
  for the current selection as before.)

- **New: jump straight to a scanner category (Ctrl+number).** Instead of stepping
  through every category with Ctrl+PageUp/PageDown, you can jump directly:
  **Ctrl+1** Actors, **Ctrl+2** Doors, **Ctrl+3** Containers, **Ctrl+4** Items,
  **Ctrl+5** Activators, **Ctrl+6** Detected, **Ctrl+7** Waypoints, **Ctrl+8**
  Locations. Each announces the category and how many are in range, then selects
  the nearest one. The last three only have anything when relevant (a Detect
  effect is active, you've dropped a waypoint, you've discovered map locations);
  jumping to an empty one just says "0 in range" rather than refusing. (Plain
  number keys are unchanged - they're still the game's item/spell quick slots.)

- **Fix: editing a text field no longer goes silent when you overwrite a
  selection.** Typing or pasting over selected text (e.g. replacing a highlighted
  save name) now speaks the new text instead of saying nothing or reading the
  wrong characters. Plain typing, backspace/delete, and paste are all handled by
  one consistent diff now.

- **Fix: auto-walk now cancels with your own movement keys.** Pressing a movement
  key to cancel a runaway auto-walk previously only worked if you used the
  default WASD layout. It now reads your actual key bindings, so remapped and
  non-QWERTY (AZERTY, Dvorak, Colemak) players can cancel too — and a key that's
  no longer movement for you won't cancel by surprise.

- **Fix: confirmation dialogs could (in theory) leave you locked out.** The
  yes/no confirmation box (used by spell delete, save overwrite, settings resets,
  etc.) suspends whatever screen is underneath it and resumes it on close. That
  resume is now anchored to the dialog's guaranteed close hook, so no dismissal
  path can leave you with a suspended screen and no input — and reopening it back
  to back can no longer strand the original screen.

- **Reliability: the accessibility layer no longer fails silently.** Added a
  single `[a11y]`-prefixed diagnostic log used wherever the speech layer gives up
  with nothing to say — a broken localization tag, or a spell/enchant/active
  effect whose data is missing from the game files. Previously these produced
  silence with no trace; now they leave a locatable line in `openmw.log` so
  problems are diagnosable instead of invisible.

- **Fix: Save screen was silent with no existing saves.** On a fresh game with
  no saved games yet (e.g. saving for the very first time), the Save screen came
  up completely silent and unusable for screen-reader users. It now announces
  itself and lets you type a name and save as normal.

## 2026-06-09

- **Save and Load screens are now accessible.** Opening Save or Load announces
  which one it is, then lets you arrow through the controls. When loading, a
  character selector (Left/Right cycles characters) precedes the list of saves;
  each save is read with its name and details (level, day, health, in-game time,
  time played, location) as you arrow onto it. Press Enter to load. When saving,
  arrow to the name field and press Enter to type a name (Escape leaves the
  field), or arrow onto an existing save to overwrite it, then arrow to OK. The
  Delete button (and overwrite/load confirmations) are spoken too; deleting a
  save announces the result. If there are no saves to load, it says so.

- **Level-up screen is now accessible.** When you gain a level, the screen
  announces your new level and its flavour text, then lets you arrow through the
  eight attributes. Each attribute is spoken with its current value and the bonus
  it would gain if chosen (e.g. "Strength: 40, +5 if chosen"; maxed attributes
  say "maxed"). Press Enter to pick or unpick an attribute (it re-reads the
  updated state), press T for the attribute's description, then arrow to OK and
  press Enter to confirm.

- **Rest / Wait screen is now accessible.** Opening Rest or Wait announces the
  current date and time and whether you're resting or waiting. Arrow to the hour
  selector and use Left/Right to set how many hours; arrow to "Rest until healed"
  (when available), the Rest/Wait button, or Cancel and press Enter to choose.

- **Snap-to-direction and turn-around keys.** New facing controls for orienting
  without a mouse: **Ctrl+Left** snaps your facing to the previous compass point
  and **Ctrl+Right** to the next (the eight points: north, northeast, east,
  southeast, south, southwest, west, northwest), so repeated presses step all the
  way around the compass. **Ctrl+Down** turns you 180 degrees to face the
  opposite way. Each announces your new heading.

- **Weapon and spell quick-cycling now speaks the selection.** Cycling your
  readied spell with the - and = keys, or your weapon with the [ and ] keys,
  now announces the spell or weapon you've just switched to. Previously this
  cycling gave no spoken feedback (the selection only showed on the closed HUD),
  so you couldn't tell what was selected without opening a menu.

- **Missing stats-pane tooltips added.** Several tooltips you can see by hovering
  the visual stats sheet were not exposed to the accessible menu; press T on
  these items to hear them now:
  - **Level** -- your progress toward the next level (e.g. "Level Progress: 6 /
    10") and which attributes will get a level-up multiplier and how big (e.g.
    "Strength x5"). This is the info the wiki describes hovering the level for.
  - **Health / Magicka / Fatigue** -- the description of what each represents.
  - **Race** -- the racial description.
  - **Class** -- specialisation and description.
  - **Faction** -- per faction, your current rank plus the next rank and what's
    needed for it (required attributes, favoured skills).
  - **Birthsign** -- its description and the powers, abilities, and spells it
    grants, each with its full effect breakdown.
  - **Reputation / Bounty** -- the in-game help text explaining each.

- **Scanner actor list no longer shifts under you while browsing.** The live
  refresh of the Actors list now happens only on the "Hostile" subcategory,
  where it's useful for tracking attackers in a fight. On the other views (All,
  NPCs, Creatures) the list stays put as you read it, so wandering townsfolk no
  longer re-sort the list under your cursor and make you lose your place or skip
  people. Distances are still accurate -- they're measured fresh each time you
  read an item, so they were never dependent on the live refresh.

## 2026-06-08

- **Enchanted items now say how they're triggered, and their charges.** An
  enchanted item's accessible tooltip previously read only the effect (e.g. the
  Demon Tanto's "Bound Dagger for 60 secs on Self") with no indication of when it
  fires. It now appends the cast type the way the visual tooltip does -- "Cast
  When Used", "Cast When Strikes", "Cast Once", or "Constant Effect" -- so you can
  tell a use-activated item from one that triggers on hit or is always on. For
  the rechargeable cast types (When Used / When Strikes) it also reads the
  remaining and maximum enchantment charge, e.g. "Charges: 60 / 80". Previously
  charges were only visible in the magic pane, which excludes on-strike weapons
  like the Firebite Dagger, so their charge was unreadable entirely.

- **Discovered locations in the scanner.** The scanner has a new "Locations"
  category listing the named places you've found on the world map -- towns you've
  visited and anywhere an NPC has marked for you ("I've marked it on your map").
  Each town is one entry (its sub-cells are merged to the town centre), and like
  waypoints you can range them, hear the bearing, and auto-walk to them. While
  you're outdoors they give a real distance and direction; from inside a building
  they're listed as "on the map" with no bearing (the coordinates aren't
  comparable indoors). The category is hidden until you've discovered at least
  one place. This fills the gap where our map pane only ever exposed your own
  custom notes, never the game's own visited/marked locations.

- **Global waypoint list + long-distance auto-walk.** The scanner's Waypoints
  category now lists ALL your map notes and your Mark across the whole world, not
  just the ones in the cell you're standing in -- so distant towns and quest
  dungeons are discoverable. Notes in the same area as you (the outdoor world if
  you're outside, or the specific interior you're currently in) announce a real
  distance and bearing and can be auto-walked to; notes in another area (other
  buildings, dungeons, other worldspaces) are still listed with their location
  name (e.g. "Balmora, different area") so you know they exist, but have no
  bearing and can't be walked to, since there's no continuous path across a door.
  Auto-walk can now cross open country cell by cell: when a reachable waypoint is
  far away, it steers toward the farthest walkable point along the route and
  keeps extending as new terrain loads, calling out the remaining distance every
  so often. If the land genuinely blocks the way (a mountain or bay), it stops
  and tells you how far short it got and suggests the audio beacon, rather than
  blindly walking you into the scenery.

- **Put items into containers.** While looting a container or corpse you can now
  reach your own inventory and store items into it -- closing the gap where you
  could take things out and drop items in the world, but not put them away. Press
  Tab (or Shift+Tab) to switch between the container's contents and your
  inventory, just like in barter. In your inventory, press S to store the
  selected item into the open container; for a stack you'll get the usual count
  picker (Shift+S stores the whole stack at once). Equipped items are unequipped
  automatically, and the container refuses anything it can't hold.

- **No clear shot warning for ranged spells.** When you cast a ranged spell like
  Fireball at a locked-on enemy but a wall, pillar, or other obstacle is between
  you and them, you'll now hear "No clear shot" -- the ranged counterpart to the
  "Out of range" warning for melee and touch spells. Ranged spell bolts fly in a
  straight line until they hit something, so an obstruction you can't see makes
  the spell harmlessly strike scenery instead of your target; this tells you to
  reposition for a clear line. It only speaks when you're locked on (which is
  what aims the bolt), and it stays silent when the path is clear.

- **Crash fix.** Fixed a crash that could happen if you loaded a save while
  auto-walking to something (or while the proximity beacon was guiding you to a
  target). The mod now cleanly stops auto-walk and the beacon when a game is
  loaded or ended, so the leftover target can't cause a crash on the next frame.

- **Accessible HUD.** Press H to open a spoken version of the on-screen HUD. The
  world pauses while it's open -- so if you're suddenly attacked you have time to
  get your bearings -- but the scanner keys and quick-info keys keep working, so
  you can find who's attacking and check your stats without the fight moving on.
  Use the Up and Down arrows to move through the items: your location, health,
  magicka, fatigue, breath (only when you're underwater), whether you're
  sneaking, your readied weapon and spell, active magic effects, and the actor
  you currently have targeted. Press Home to re-read the current item. On the
  "Active effects" item, press Enter to step into the full list of what's
  affecting you (with its strength and time remaining), and Escape or Left to
  come back out. The target item follows your scanner selection live, so as you
  cycle targets while in the HUD, it updates to show each one's health. Press H
  again or Escape to close.
- **Quick stat checks.** Even during normal play (and in the HUD), you can read a
  single stat instantly: Alt+H for health, Alt+M for magicka, Alt+F for fatigue,
  and Shift+Alt+H for the health of whatever you have targeted.

- **Enemy spellcasting announcements.** When another actor casts a spell or uses
  a scroll, you'll now hear it -- for example "Dagoth Gares casts Fireball at
  you." The "at you" is added when the caster is fighting you and the spell
  reaches outward, so you can tell an attack aimed your way from a buff someone
  cast on themselves. Casts are announced when they're nearby, or at any
  distance when the caster is targeting you; your own casts aren't announced
  (you already hear those when you ready them).

- **Reaching things on the ground.** Fixed not being able to activate objects
  whose base is sunk into the floor or terrain -- most notably Fargoth's hollow
  tree stump. Auto-walk would bring you right on top of it and say you'd arrived,
  but trying to use it claimed you were too far away with no way to get closer.
  Activation now measures to the object's surface (as the game does when you look
  at something) instead of its buried centre point.

- **Out-of-range feedback.** When you swing a melee weapon or cast a touch spell
  at a locked target that's too far to reach, you'll now hear "Out of range" --
  or "Target too high" / "Target too low" when the target is within horizontal
  reach but above or below your weapon's vertical reach (like a cliff racer
  overhead). Ranged spells you cast from a distance, such as Fireball, won't
  trigger it. The message is rate-limited so a flurry of swings won't spam it.

## 2026-06-07

- **Combat lock-on.** Press K to lock onto whatever the scanner has selected.
  Your character keeps facing and aiming at that target every moment, so melee
  swings, spells, and even lockpicks and probes connect without you having to
  line up a crosshair you can't see. It also fixes aiming at things above or
  below you, like a cliff racer overhead or a chest on the floor. Lock-on
  releases automatically when the target dies or you walk away, or press K again
  to release it yourself.
- **Picking locks and casting on things you can't see directly.** With a target
  locked on, lockpicks, probes, and touch spells like Open now work on it even
  when furniture or clutter sits between you and it -- the action uses your
  locked target instead of relying on a clear line of sight.
- **Find what's attacking you.** The scanner's actor list (renamed from "NPCs"
  to "Actors") has a new "Hostile" subcategory listing only the actors currently
  in combat with you. Cycle to it with Shift+PageUp/PageDown. While you're on
  this Hostile view the list refreshes live, so a new attacker appears right
  away as everyone moves around during a fight.
- **Weapon and spell readiness.** Drawing a weapon or readying a spell now
  announces what you've readied -- for example "Iron Dagger ready", "Hand to
  hand ready", or "Fireball ready" -- and you'll hear "Weapon sheathed" or
  "Magic put away" when you lower it again.

- **Smoother auto-walk.** Auto-walk no longer hops around for no reason on a clear
  path -- it now only does its little jump-and-sidestep recovery when the body is
  genuinely wedged against something, not when the route simply curves around a
  wall or corner. Walking to a person also works properly now: it announces you've
  arrived as soon as you're standing beside them, instead of jostling into them
  and then claiming it was stuck.

- **Repairing items.** Both repair screens are now screen-reader accessible. At a
  smith's Repair service, Up and Down move through your damaged items (each read
  with its repair price) and Enter repairs the selected one. Using a repair tool
  (hammer or prong) from your inventory opens the repair screen: the first option
  is your repair tool, which reads its remaining uses and quality; press Enter on
  it to open a picker and choose a different tool. Below the tool are your damaged
  items, each read with its condition; press T for full details and Enter to
  repair it with the current tool.

## 2026-06-06

- **Buying spells.** The spell merchant screen is now screen-reader accessible.
  Up and Down move through the spells for sale, each read with its price; press T
  to hear a spell's cost/chance and its magic effects, and Enter to buy it.
- **Bartering.** Trading with a merchant is now screen-reader accessible. Tab
  and Shift+Tab switch between the merchant's goods and your own inventory; each
  item announces its name, how many, and its barter price. Press Enter to add an
  item to the deal (buy from the merchant, or sell from your inventory) and Enter
  again on an item marked "on offer" to take it back off the table. Adjust the
  running deal with the balance keys: B reads the current total, plus and minus
  change it by one (hold Shift for 100), C types in an exact amount, and O makes
  the offer. Press G to hear your own gold, or Shift+G for the merchant's
  available gold (the most they can pay you for a sale).
- **Waypoints.** Drop a map note anywhere with the N key, then scan, face, and
  walk to your dropped notes, Marked locations, and existing map notes in the
  current area. The audio beacon works with waypoints too.
- **Smarter auto-walk.** Auto-walk now reaches far more destinations: it can
  approach doors set into walls and route up to targets on raised areas instead
  of getting stuck against a wall. When somewhere genuinely can't be reached on
  foot, it stops, faces the target, tells you how far short it stopped, and
  suggests using the beacon.
- **Elevation cues.** Scanner and waypoint announcements now tell you when a
  target is above or below you (for example, "3 metres up"), so multi-level
  areas are easier to navigate.
- **Edit and delete map notes.** In the map's Notes list, press Enter on a note
  to open it: change its text, or delete it (with a confirmation), all narrated
  by the screen reader.
- **Location announcements.** The name of a new area is spoken automatically as
  you enter it. It won't repeat as you move between the cells that make up a
  single city (so you won't hear "Balmora" over and over), and it stays quiet
  when you first load a save.
- **Reading the journal.** Open the journal to hear your most recent entries
  read aloud. Use Up and Down to turn between two-page spreads, Left and Right
  to move one page at a time, and R to reread the current page.
- **Browsing topics and quests.** From the journal, press T to browse topics
  (pick a letter, then a topic) or Q to browse your active quests. Tab and
  Shift+Tab cycle between the Topics, Active Quests, and All Quests tabs. Press
  Enter on a topic or quest to read its entries, and Backspace or Escape to step
  back out. Completed quests are announced as "completed" in the All Quests tab.

## 2026-06-05

- **Spells (magic) window** is now screen-reader accessible, including reading
  out your active magic effects.
- **Inventory window** is now screen-reader accessible.
- **Dialogue topics.** Jump to the previous or next topic you haven't fully
  explored yet.
- **Object scanner** can now search by name to filter what it finds, and scans
  all nearby areas at once while keeping your selection as you cross between
  them.
- **Fix:** Faction and Birthsign lines now show reliably on the character sheet.

## 2026-06-04

- **Character stats window** is now screen-reader accessible.
- **Container window** is now screen-reader accessible.
- **Fix:** Keyboard key-rebinding now works in the options menu.
- **Fix:** Dialogue status notifications are queued instead of cutting each
  other off.

## 2026-06-03

- **Dialogue window** is now screen-reader accessible, including persuasion,
  training, and travel services.
- **Book and scroll windows** are now screen-reader accessible.
- **Object scanner improvements:**
  - New Items and Activators categories.
  - Subcategories via Shift+PageUp/PageDown.
  - Activate the selected target with Space.
  - Audio beacon to home in on a target by ear.
  - Navigation aids for orientation and telling apart similarly-named objects.
  - Non-interactable objects are filtered out.
  - Item ownership / "stolen" info is read out when full help is enabled.

## 2026-06-02

- **Character creation** is now screen-reader friendly throughout: pick-class,
  class quiz and result, custom class creation, birthsign, and the
  review/summary screen.
- **Reread support.** Press R to repeat the last announcement.
- **Text-editing feedback** for typing in text fields.
- Framework support for expandable submenus.

## 2026-06-01

- **Settings window** and **race selection screen** are now screen-reader
  friendly.
- Reusable screen-reader UI framework extracted to build the rest of the mod on.
- **Fix:** Auto-walk no longer cancels when pressing Space.

## 2026-05-30

- **Initial screen reader mod for OpenMW**, starting with the Options/Settings
  menu.
