# Project Hortator Changelog

A running log of screen-reader accessibility features added to this OpenMW fork.
Newest changes are listed first. (OpenMW's own engine changelog lives in
`CHANGELOG.md`.)

## 2026-06-09

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
