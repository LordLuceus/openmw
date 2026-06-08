# Project Hortator — Accessibility To-Do

Tracks which game screens and mechanics still need screen-reader accessibility
work. Companion doc to `ACCESSIBILITY_CHANGELOG.md` (which records what's done).

Each GUI screen is identified by its `GuiMode` (see
`apps/openmw/mwgui/mode.hpp`) where applicable.

## Done

Inventory, Spells/Magic, Stats, Map (+ waypoints & map notes), Dialogue
(+ topic jumping), Journal (page reading + topics/quests browsing), Container,
Book, Scroll, Barter, Spell buying, Repair (own + smith), Travel, Training, Persuasion, Settings window, Main menu, and the
character-creation screens (Name, Race, Birth, Class, Review). Object scanner,
auto-walk, location announcements, and audio beacon (gameplay, not a screen).

## To-Do

### Merchant / services windows (model-backed lists, barter-like)
- [x] **Spell buying** (`GM_SpellBuying`) — list of purchasable spells + cost
- [ ] **Spell making / creation** (`GM_SpellCreation`) — effect builder (complex)
- [ ] **Enchanting** (`GM_Enchanting`) — item + effects + soul gem
- [ ] **Alchemy / potion-making** (`GM_Alchemy`) — ingredients grid → effects
- [ ] **Recharge** (`GM_Recharge`) — recharge enchanted items with soul gems
- [x] **Repair** (`GM_Repair`) — player's own repair hammers
- [x] **Merchant repair** (`GM_MerchantRepair`) — paying a smith (separate window)

### Progress / confirmation dialogs
- [ ] **Level up** (`GM_Levelup`) — attribute picks
- [ ] **Rest / wait** (`GM_Rest`, WaitDialog) — hour slider + progress bar
- [ ] **Jail** (`GM_Jail`) — sentence / skill-loss screen

### Player-action UIs
- [ ] **Companion** (`GM_Companion`) — share/transfer items. Reuses the inventory
      pane; needs the same two-pane PaneGroup treatment barter got. Likely the
      cheapest remaining win.
- [ ] **Quick keys menu** (`GM_QuickKeysMenu`)
- [ ] **Console** (`console.cpp`)
- [x] **HUD** — accessible HUD (AHUD). H toggles it; pauses the world (via a
      time-manager tag, not a GuiMode) so the scanner + quick-info keys still
      work while frozen, giving a blind player time to assess an ambush. Quick
      info works in gameplay too: Alt+H/M/F read player health/magicka/fatigue
      ("current of max"); Shift+Alt+H reads the current enemy's health (% only).

### System screens (no GuiMode — separate path)
- [ ] **Save / Load** (`savegamedialog.cpp`)
- [ ] **Death screen** — bespoke yes/no dialogue (differs from the usual one)
- [ ] **Scripts tab** in options — within the otherwise-accessible settings window

### Targeting / combat (lock-on backbone landed)
- [x] **Lock-on targeting** — press K to lock the scanner selection; player is
      re-aimed (yaw + pitch, eye-to-centre) every frame so melee/spells/tools
      connect. Releases on death, walk-away, or K again.
- [x] **Lockpicking / probes** — now use the locked target directly, so they work
      even when the camera ray is blocked by furniture in front of the container.
- [x] **Touch-on-object spells** (e.g. Open) — same locked-target bypass.
- [x] **Hostile actor list** — "Actors" category (renamed from NPCs) gained a
      "Hostile" subcategory; actor list refreshes live so attackers appear at once
      and stay distance-ordered. Fixed hostiles vanishing from the scanner the
      moment they entered combat (hasToolTip() exception for actors).
- [x] **Weapon/spell ready announcements** — draw state polled and announced.
- [x] **Out-of-range feedback** — when the player swings melee or casts a touch
      spell at a locked target that's unreachable, announce "Out of range" (too
      far) or "Target too high"/"Target too low" (beyond vertical reach).
      Throttled; uses the engine's own reach math. Ranged "target" spells are
      excluded so distance casting isn't nagged.
- [x] **Spell-cast announcements** — announce when another actor casts a spell or
      uses a scroll/magic item. Hooked at the two CastSpell::cast success points
      (spell + item/scroll); excludes on-strike/projectile enchantments. Spoken
      as "<Caster> casts <spell>" with " at you" appended when the caster is in
      combat with the player AND the spell reaches outward (touch/target range,
      not a self-buff). Announced when nearby (~28 m) OR when targeting the
      player at any distance; the player's own casts are excluded.
- [x] **Enemy health readout** — Shift+Alt+H reads the locked target's (else the
      scanner selection's) health as a percentage only, matching the native
      enemy health bar; magicka/fatigue not shown to sighted players so not
      exposed. Part of the AHUD quick-info keys.

### Tech debt
- [x] **Memory-safety audit (post-combat)** — swept the a11y code for stale
      `MWWorld::Ptr` handling across world teardown. Findings + fixes:
      - **BUG (UAF) — auto-walker / proximity cue survived `clear()`.** Both
        cache a Ptr target and deref it every frame (`getCellRef`, `getRefData`).
        `Scanner::clear()` reset the lock target + object lists but did NOT
        cancel `mAutoWalker` / stop `mProximityCue`, so after a quickload while
        auto-walking (or with a beacon homing), the next `onFrame` would deref a
        freed target — same crash class as the original lock-on UAF. FIXED:
        `clear()` now calls `mAutoWalker.cancel()` + `mProximityCue.stop()`.
      - **Hardening — `lockTarget()` had no `State_Running` guard** (unlike its
        sibling announce* helpers). External combat consumers
        (`World::castSpell` worldimp.cpp:2962, `CharacterController` character.cpp:1761)
        check `isEmpty()`, which canNOT catch a dangling-but-non-null Ptr. FIXED:
        `lockTarget()` now returns empty unless `State_Running`, so it can never
        hand out a freed Ptr regardless of call ordering. Both consumers
        confirmed safe (isEmpty-checked, immediate use, not cached).
      - **Verified safe:** `pruneDeadObjects` (only runs after onFrame's
        `isGameplayActive()` guard; `clear()` empties `mObjects` on teardown
        first); per-frame helper `onFrame`s run after that same guard.
      - Note: `isEmpty()` is a null check only; the real teardown protection is
        the synchronous `Scanner::clear()` from `StateManager::cleanup` clearing
        every cached Ptr before the next frame. Future code holding a Ptr across
        frames MUST be released in `clear()`.

## Notes
- There are TWO repair windows: `GM_Repair` (own hammer) vs `GM_MerchantRepair`.
- `ItemSelectionDialog` (the item picker modal) is now accessible — this is the
  same picker used by Alchemy, Enchanting, Recharge, and the quick-keys menu, so
  those screens get the item-choosing half for free.
- Companion is the same two-pane inventory pattern as barter (`A11y::PaneGroup`),
  so it should be a quick follow-up.
