# Project Hortator Changelog

A running log of screen-reader accessibility features added to this OpenMW fork.
Newest changes are listed first. (OpenMW's own engine changelog lives in
`CHANGELOG.md`.)

## 2026-06-16

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
