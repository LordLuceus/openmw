# Project Hortator Changelog

A running log of screen-reader accessibility features added to this OpenMW fork.
Newest changes are listed first. (OpenMW's own engine changelog lives in
`CHANGELOG.md`.)

## 2026-06-08

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
  in combat with you. Cycle to it with Shift+PageUp/PageDown. The actor list now
  also refreshes while you're in it, so a new attacker appears right away and
  distances stay accurate as everyone moves around during a fight.
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
