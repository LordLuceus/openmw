# Project Hortator — Audio Cues Reference

This lists every **non-speech audio cue** the accessibility layer plays, what
each one means, and when it fires. These are short sound effects that convey
status at a glance without waiting for (or talking over) speech. They are
distinct from spoken output.

All cues play on their own dedicated sound channel, so you can set their volume
independently of game audio: it is the **Accessibility Cues** slider on the
**Audio** tab of the settings window. Turn it down if the cues talk over your
screen reader, or up if you want to rely on them more than on speech. The sound
files live in `files/data/sounds/a11y/` (copied into the VFS at build time). If a
file is missing, the cue simply plays nothing and the game continues normally.

Cues are one of two kinds:

- **2D (non-positional):** a HUD-style notification about *your own* state. It
  has no direction — it is not coming from a place in the world.
- **3D (positional):** emitted from a location in the world, so its direction
  and distance are meaningful.

---

## Navigation — the audio beacon

The beacon is opt-in. Toggle it with **Ctrl + Enter** on the selected target
("Audio beacon on." / "Audio beacon off."). It guides you toward whatever the
scanner currently has selected — an object, an actor, or a reachable waypoint. A
single sound pair is reused for every category, so every scanner category gets a
working beacon automatically.

| Cue | File | Kind | Meaning / when it fires |
|-----|------|------|--------------------------|
| **Approach** | `approach.wav` | 3D, looping | Plays continuously from the target while the beacon is on and you are **outside activation range**. Follow it by ear to home in. Stops the moment you arrive. |
| **Arrival** | `arrival.wav` | 3D, one-shot | Plays once from the target when you cross **into activation range** — you are close enough to interact. A small hysteresis band (1.25× activation distance) means you must wander back out past that wider ring before the approach loop re-arms, so standing on the boundary won't make it chatter. |

Notes:
- The beacon only guides in the horizontal plane (direction + distance), not
  vertical.
- For a waypoint, the beacon only homes on a **reachable** one; a waypoint in
  another worldspace is skipped (pointing a direction across coordinate systems
  would be nonsense).

---

## Combat

These fire only while you are **locked onto a live actor**. They are about your
own combat readiness against that target.

| Cue | File | Kind | Meaning / when it fires |
|-----|------|------|--------------------------|
| **Enemy in range** | `enemy_in_range.wav` | 2D | Your locked target has just become hittable with what you have readied — a clear shot for a ranged/target spell, or within melee/touch reach. Fires on the **out-of-range → in-range** transition (and on the initial lock if already in range). |
| **Enemy out of range** | `enemy_out_of_range.wav` | 2D | Your locked target has just moved out of hittable range. Fires on the **in-range → out-of-range** transition. |
| **Enemy died** | `enemy_died.wav` | 2D | Your locked target has died. Reinforces the spoken "*Name* is dead." with a cue, because a kill is exactly when combat chatter is loudest and speech is easiest to miss. The lock is then released. |

Notes:
- The in/out-of-range cue only sounds on an actual transition, never repeatedly.
  If nothing relevant is readied it stays silent, but it remembers the last
  in/out state so re-drawing a weapon won't re-announce a state you already know.
- Range meaning depends on what's readied: line-of-sight clear shot for a
  ranged/target spell or bow; `fCombatDistance` reach for melee/touch. A
  self-only spell has no enemy-range concept and produces no cue.

---

## Status notifications

2D notifications about your own character state.

| Cue | File | Kind | Meaning / when it fires |
|-----|------|------|--------------------------|
| **Quest update** | `quest_update.wav` | 2D | A journal entry was added (quest advanced). Multiple entries added by one dialogue line collapse into a single cue for that frame. |
| **Quest complete** | `quest_complete.wav` | 2D | A journal entry that **finishes** a quest was added. Outranks a plain update, so if a single dialogue line both advances and completes, you hear the completion cue. |
| **Magic expiring** | `magic_expiring.wav` | 2D | A survival- or navigation-critical magic effect on you is about to run out (**5 seconds left**). One cue even if several such effects lapse together. |

**Which effects warn on expiry** (deliberately limited to effects whose sudden
loss can strand, drown, drop, or expose a blind player — routine stat buffs like
Fortify/Restore/Shield are excluded as noise):

- Levitate
- Water Walking
- Water Breathing
- Slow Fall
- Invisibility
- Chameleon
- Sanctuary

Notes:
- Permanent effects (constant-effect items, abilities) never warn — they don't
  expire.
- An effect whose *entire* duration is 5 seconds or less never warns, since the
  cue would fire the instant it's applied.
- Re-casting an effect re-arms its warning.

---

## Stealth

2D notifications about whether you are currently seen while sneaking.

| Cue | File | Kind | Meaning / when it fires |
|-----|------|------|--------------------------|
| **Spotted** | `sneak_detected.wav` | 2D | Someone nearby has noticed you while you are sneaking — your cover is blown. Fires on any transition into being detected, whether you were hidden a moment ago or started sneaking while already seen. |
| **Hidden again** | `sneak_hidden.wav` | 2D | You have slipped back out of sight after having been spotted — safe to move again. A calmer sound than the spotted cue. |

Notes:
- Simply crouching when nobody has noticed you is **silent**. The "hidden again"
  cue fires only when you were previously detected, so starting a sneak doesn't
  beep every time.
- Neither cue fires while a menu or dialogue is up, and opening a menu mid-sneak
  won't re-fire them when you return to the game.

---

## Quick reference

| File | Kind | One-line meaning |
|------|------|------------------|
| `approach.wav` | 3D loop | Beacon: target is this way, keep moving. |
| `arrival.wav` | 3D one-shot | Beacon: you're now in range to interact. |
| `enemy_in_range.wav` | 2D | Locked target is now hittable. |
| `enemy_out_of_range.wav` | 2D | Locked target is no longer hittable. |
| `enemy_died.wav` | 2D | Locked target died. |
| `quest_update.wav` | 2D | Quest advanced. |
| `quest_complete.wav` | 2D | Quest finished. |
| `magic_expiring.wav` | 2D | A critical effect has ~5 seconds left. |
| `sneak_detected.wav` | 2D | You've been spotted while sneaking. |
| `sneak_hidden.wav` | 2D | You're out of sight again after being spotted. |
