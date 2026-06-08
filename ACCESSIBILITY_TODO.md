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
- [ ] **HUD** — persistent in-game bars / widgets

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
- [ ] **Spell-cast announcements** — announce when a (nearby) actor casts a spell.
- [ ] **Enemy health readout** — read the selected/locked actor's health (% only,
      matching the native enemy health bar; magicka/fatigue not shown to sighted
      players so not exposed).

### Tech debt
- [ ] **Memory-safety audit (post-combat)** — sweep the a11y code for stale
      `MWWorld::Ptr` handling across world teardown. The scanner caches Ptrs
      (lock-on target, per-category object lists, auto-walk target, proximity
      cue) that dangle when a save loads/ends or a cell unloads. Two quickload
      crashes were traced (via the minidump) to `updateLockOn` dereferencing a
      freed lock target; root-caused to the synchronous quickload completing
      within one input handler, so `onFrame` state-polling never fired. Fixed
      deterministically via `Scanner::clear()` from `StateManager::cleanup`.
      Audit every cached Ptr for the same hazard, confirm `clear()` covers all
      of them, and check the per-frame `pruneDeadObjects`/cell-change paths and
      `lockTarget()` consumers (CharacterController, World::castSpell) for
      use-after-free. Prefer storing stable `RefNum`s + re-resolving over
      holding raw Ptrs where practical.

## Notes
- There are TWO repair windows: `GM_Repair` (own hammer) vs `GM_MerchantRepair`.
- `ItemSelectionDialog` (the item picker modal) is now accessible — this is the
  same picker used by Alchemy, Enchanting, Recharge, and the quick-keys menu, so
  those screens get the item-choosing half for free.
- Companion is the same two-pane inventory pattern as barter (`A11y::PaneGroup`),
  so it should be a quick follow-up.
