# Project Hortator Changelog

A running log of screen-reader accessibility features added to this OpenMW fork.
Newest changes are listed first. (OpenMW's own engine changelog lives in
`CHANGELOG.md`.)

## 2026-06-06

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
