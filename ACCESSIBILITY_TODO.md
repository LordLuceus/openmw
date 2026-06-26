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
- [x] **Spell making / creation** (`GM_SpellCreation`) — effect builder. Virtual-
      focus screen over name/available-effects/used-effects/stats/Buy/Cancel;
      shared list logic in EffectEditorBase (initEffectListA11y /
      addEffectListElements / a11yEffectTooltip / a11yUsedEffectWidget) so
      enchanting can reuse it. Modal EditEffectDialog (ownModal) with Range as a
      cycle value + magnitude/duration/area sliders (Left/Right +/-1, Ctrl +/-10,
      Home/End min/max). Main screen reclaims the single active-screen slot in
      onFrame when the child modal closes, landing on the edited effect.
- [ ] **Enchanting** (`GM_Enchanting`) — item + effects + soul gem
- [x] **Alchemy / potion-making** (`GM_Alchemy`) — ingredients grid → effects
- [x] **Recharge** (`GM_Recharge`) — recharge enchanted items with soul gems.
      Mirrors Repair: virtual-focus A11y::Screen over the soul gem (Enter opens
      the charged-soulstone picker), then each rechargeable item (label = name +
      Charges current/max, T-key tooltip = full enchant detail; Enter recharges it
      with the current gem), then Cancel. updateView() activates on first run and
      rebuilds keeping cursor afterwards. Native Enchant Success/Fail sounds cue
      the roll outcome; a successful recharge re-reads the item's new charge.
- [x] **Repair** (`GM_Repair`) — player's own repair hammers
- [x] **Merchant repair** (`GM_MerchantRepair`) — paying a smith (separate window)

### Progress / confirmation dialogs
- [x] **Level up** (`GM_Levelup`) — attribute picks
- [x] **Rest / wait** (`GM_Rest`, WaitDialog) — hour slider + progress bar
- [x] **Jail** (`GM_Jail`) — non-interactive progress screen. Announces the
      sentence up front ("Prison. Serving N days.") since the screen is otherwise
      silent for a blind player; the end-of-sentence skill-change summary is
      already spoken via the engine's interactive message box.

### Player-action UIs
- [x] **Store items into containers** (`GM_Container`) — the loot window and the
      player's inventory are now enrolled as a two-pane PaneGroup (container = 0,
      inventory = 1), so Tab/Shift+Tab switch between taking and storing, just
      like barter. In the inventory pane, **S** stores the selected item into the
      open container (count picker for a stack; Shift+S = whole stack) via the
      existing `transferItem`/`ItemTransfer::apply` path. `a11yStoreItem` +
      `onA11yCountStored` mirror the drop flow (unequip-if-needed, then follow the
      item). Closes the take/drop/store gap (could take + drop, not store).
- [x] **Companion** (`GM_Companion`) — share/transfer items. The companion window
      has NO accessibility yet (no `mA11y`), so it needs its own item list +
      buildAccessibility first, then enrol as pane 0 (mirroring the container
      work above) so the inventory's S-store path can target it too. Inventory
      enrolment + S-store currently gated to `GM_Container`; extend to
      `GM_Companion` once its pane exists.
- [x] **Quick keys menu** (`GM_QuickKeysMenu`) — 10 slots navigable as an
      A11y::Screen (virtual focus); each reads "Quick key N, <name>/None/Hand To
      Hand", live via describe(). Enter opens the assign chooser (Item/Magic/
      Unassign/Cancel, modal, suspends parent). The item picker
      (ItemSelectionDialog) was already accessible; the magic picker
      (MagicSelectionDialog) now enumerates the SpellModel like SpellWindow
      (label + section + T-key tooltip). Chooser hides itself BEFORE opening a
      picker to keep the suspend/resume chain LIFO. Gameplay activation (keys
      1–9) speaks the result via A11y::say; the engine's "missing" messageBox is
      already auto-spoken, so only success paths were instrumented.
- [x] **Console** (`console.cpp`)
- [x] **HUD** — accessible HUD (AHUD). H toggles it; pauses the world (via a
      time-manager tag, not a GuiMode) so the scanner + quick-info keys still
      work while frozen, giving a blind player time to assess an ambush. Quick
      info works in gameplay too: Alt+H/M/F read player health/magicka/fatigue
      ("current of max"); Shift+Alt+H reads the current enemy's health (% only).

### System screens (no GuiMode — separate path)
- [x] **Save / Load** (`savegamedialog.cpp`)
- [x] **Death screen** — bespoke yes/no dialogue (differs from the usual one)
- [x] **Scripts tab in options = Lua-mod settings pages** (commit `ef9f6c9d9e`).
      Generic walker over `mScriptAdapter`'s deterministic page scaffold (groups
      flex → group flex → settings flex → named row) emits one option per setting
      (name + live value, help text on tooltip key). Checkbox = Enter/Left/Right;
      select = Left/Right; number/text field = Enter to type, Escape to confirm;
      each group's Reset offered last; group titles announced as section prefix.
      Widgets re-resolved live by stable name (never cached — Lua destroys/recreates
      rows on change); global settings apply async so the new value is announced
      only once it settles. Mod switching (`ScriptList`) + filter (`mScriptFilter`)
      announced. Validated against real mod pages (Maxar Dynamic Footsteps, OpenMW
      camera/combat). Added `LuaUi::WidgetExtension::hasEventCallback`.

### Open-world navigation
- [x] **Global waypoint list** — Scanner Waypoints category lists ALL map notes +
      Mark across the world (via `getAllPlayerMapNotes`), not just the current
      cell. A note is "reachable" (real distance/bearing, auto-walkable) when its
      worldspace matches the player's CURRENT worldspace -- the overworld if
      outside, or the specific interior you're in. Other-worldspace notes are
      listed with a crude area label, no bearing, auto-walk/beacon refused (XY not
      comparable across coordinate systems). Reachability: a note's worldspace is
      `sDefaultWorldspaceId` if `note.mCell.getIf<ESM3ExteriorCellRefId>()` is set,
      else `note.mCell` itself; compare to `player.getCell()->getWorldSpace()`.
      (Do NOT gate on the player being outdoors -- that wrongly excluded a note in
      the very interior you're standing in.)
- [x] **Progressive (cross-cell) auto-walk** — for a far reachable target,
      AutoWalker steers toward a "carrot" (`DetourNavigator::raycast` finds the
      farthest walkable navmesh point along the bearing) and re-paths each second
      so the carrot advances as cells stream in. Crosses open same-worldspace
      terrain cell by cell. Periodic distance callouts; honest "stopped short, use
      the beacon" when terrain truly blocks the straight bearing.
- [x] **Discovered locations ("Locations" category)** — new scanner category
      listing global-map places (visited named exterior cells + NPC `ShowMap`/
      `FillMap` marks), aggregated one entry per town at the barycentre. Source:
      `WindowManager::getDiscoveredLocations()` → `MapWindow::getDiscoveredLocations()`
      which aggregates `MapWindow::mMarkers` (the authoritative discovered-cell
      set) by `ESM::Cell::mName` before any comma. Reuses the position-based
      Waypoint/AutoWalker/ProximityCue machinery (isWaypointCategory() now covers
      both Waypoints and Locations). Reachable only while the player is in the
      default exterior worldspace; listed as "on the map" (no bearing) indoors.
- [ ] **Auto-walk switchback/winding-route stall (deferred).** On a long route
      with tight switchbacks (observed walking to Llarara Omayn), the walk can
      stall: distance pins, ~30 navmesh waypoints never get consumed, and no
      physical wedge is detected (so neither the wiggle nor the blocker/phase
      logic engages). Suspected path-following/steering issue in tiered geometry
      where the next waypoint sits behind/above us and the heading controller
      can't make progress. Needs its own investigation (likely a look-ahead /
      waypoint-advance fix in the steering in onFrame).

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
      Throttled; uses the engine's own reach math.
- [x] **No-clear-shot feedback for ranged spells** — magic "target" bolts (e.g.
      Fireball) fly in a straight line with no gravity and NO distance cap, so
      the real failure a blind player can't see is an obstructed path, not range.
      `Scanner::announceNoClearShot()` raycasts torso->target-centre (the exact
      trajectory updateLockOn aims) using the projectile collision mask; if the
      first thing hit isn't the locked target, speak "No clear shot." Only when
      locked onto a live actor; throttled via the shared reach cooldown. Wired in
      World::castSpell for the player's RT_Target spells/enchantments
      (spellHasTargetEffect).
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

## Design-principles review (2026-06-10)

Full audit of the a11y layer against the 18 accessibility-mod design principles.
Avg ~6.4/10. Strong where it's hardest (honesty of output, cache validity,
authoritative state); weak on diagnosability, tests, and localization. Scores:
P1 8, P2 3, P3 8, P4 8, P5 9, P6 9, P7 5, P8 8, P9 4, P10 5, P11 7, P12 8,
P13 6, P14 7, P15 10, P16 1, P17 6, P18 4.

### Beta-blockers (fix before test release)
- [x] **(P14, lockout risk) `ConfirmationDialog` resume not on guaranteed hook.**
      DONE 2026-06-10: added `onClose()` that restores via `a11yRestorePrevious`
      (idempotent on the normal paths, which now restore before hiding) + a
      re-entrancy guard so a back-to-back open can't orphan the covered screen.
      `confirmationdialog.cpp:64-66` suspends the underlying a11y screen but only
      resumes in `exit()`/`onOkButtonClicked()` — NO `onClose()` override, unlike
      siblings `ItemSelectionDialog` (itemselection.cpp:74-87) and `EditNoteDialog`
      (mapwindow.cpp:1844-1849). It's a shared singleton used by 8 call sites
      (mainmenu, savegame, settings, spellwindow, mapwindow…). Any dismissal that
      bypasses `exit()` → suspended screen + no active screen = total silent input
      lockout for a blind player. Fix: add `onClose()` calling
      `a11yRestorePrevious()` with a double-resume guard; also guard re-entrancy on
      the singleton's single `mA11yPrev`.
- [~] **(P2/P18) No diagnostic logging anywhere in the a11y layer.** PARTIALLY
      DONE 2026-06-10: added `A11y::logWarn()` (single `[a11y]`-prefixed sink in
      speech.cpp) and wired the highest-value silent-drop sites — failed tag
      resolution in `say`/`sayRereadable`, and unresolved effect/enchant IDs in
      `spelltext.cpp`/`itemtext.cpp`/`activeeffects.cpp`. STILL TODO: route the
      MyGUI coupling points (delegate rebind / focus restore in `screen.cpp`,
      null-anchor / unexpected-widget-state) through it too.

### Correctness bugs surfaced
- [x] **(P7) `EditField` diff mishandles selection-replace / paste.**
      DONE 2026-06-10: replaced the separate insert-only / delete-only branches
      with a single-span diff (`diffSpan` -> common prefix/suffix stripped) feeding
      a shared `announceSpan`. Now handles insertion, deletion AND replacement
      (typing or pasting over a selection) uniformly, and never goes silent on a
      real change. Backspace/Delete still speak what was removed; replacement
      speaks the new text. Unit tests for diffSpan still pending (see P16 epic).
- [ ] **(P3) Omitted spell effects should be audible.** `formatSpellEffectLine`
      one overload says "Unknown effect" (spelltext.cpp:117-118) but the other
      `return {}`s silently (28-29), so the two overloads are inconsistent and an
      enchant/spell can read as complete while missing an effect.
- [x] **(P12/P8) Autowalk-cancel hardcodes physical W/A/S/D** (scanner.cpp:705-715)
      DONE 2026-06-10: added `MWBase::InputManager::getActionKeyBinding(action)`
      (delegates to `BindingsManager::getKeyBinding`, returns the bound
      SDL_Scancode) and a scanner `isMovementKey()` helper that checks the live
      A_MoveForward/Backward/Left/Right bindings. Remapped / AZERTY / Dvorak /
      Colemak players can now cancel with their own movement keys, and an unbound
      action (UNKNOWN) never spuriously matches.

### Localization (P9=4, P10=5) — post-beta epic (does NOT affect English testers)
- [ ] **~45 hardcoded English fragments across 15 files.** Highest priority is the
      framework chrome (heard on every screen): `screen.cpp` ("No description
      available.", "Empty.", "Editing. Press Escape when done.", tooltip hints,
      the `" of "` position indicator), `editfield.cpp` ("space/tab/newline/blank"),
      `spelltext.cpp` ("Unknown effect"), `activeeffects.cpp` (", permanent").
      Then screen-specific: savegamedialog (~9), mapwindow (5), race (4), dialogue,
      journal, levelup, book/scroll, spellwindow, tradewindow, countdialog.
- [ ] **(P10) Language-specific assembly:** binary plural "tooltip/tooltips"
      (screen.cpp:977,996 — use ICU `formatMessage`), punctuation-sniffing on
      localized text in `withPosition` (screen.cpp:23-25), English word-order/
      separators in the save-timestamp assembly (savegamedialog.cpp:447-453).
- [ ] Build a shared **localized position-indicator + plural helper** to replace
      `withPosition` + the scattered `" of "` joins in one change.

### Testability + tests (P16=1, P13=6) — post-beta epic
- [~] **Add unit tests** (harness already exists: `apps/openmw_tests` GTests link
      `openmw-lib` and already test `mwgui/tooltips.cpp` — additive). STARTED
      2026-06-10: first a11y unit tests landed — 20 green GTests across
      `apps/openmw_tests/mwaccessibility/spokenformat.cpp` (formatDistance/
      formatElevation/letterForIndex/compassLabel) and `.../itembucket.cpp`
      (classifyItemType). Enabling them flips `BUILD_OPENMW_TESTS=ON` (was OFF);
      build the `openmw-tests` target and run `openmw-tests.exe
      --gtest_filter=MWAccessibility*`. Remaining top units:
      `bookMarkupToParagraphs` (booktext.cpp — the unmatched-`<` P7 case),
      `editfield` diffSpan + hand-rolled UTF-8 encoder, `withPosition`
      (screen.cpp:19-28), `formatSpellEffectLine`, `itemTooltipLines` dedup,
      screen nav math (moveSelection/jumpSection).
- [~] **(P13) Extract a pure, injectable-seam layer** (speech sink, clock, state
      reads) so the above become testable without standing up MyGUI + the engine.
      STARTED 2026-06-10: carved two engine-free TUs out of scanner.cpp —
      `mwaccessibility/spokenformat.{hpp,cpp}` (formatDistance/formatElevation/
      letterForIndex/compassLabel + shared kPi/kUnitsPerMetre) and
      `mwaccessibility/itembucket.{hpp,cpp}` (classifyItemType, the Items
      subcategory bucketing incl. the Misc catch-all). Both fully unit-tested.
      The scanner `is*` item predicates now delegate to classifyItemType.
      Decision logic is organizationally separated (good `Element`/`Screen`
      abstraction) but still physically fused to engine singletons; keep carving
      pure logic out incrementally. Engine-coupled predicates that resisted this
      slice (need a live Ptr/world, not just a type id): `matchesCategory`
      (Items/Activators call isItem/isActivator/getModel), `isHostileActor`
      (combat state), `matchesSubcategory` (mixes pure + Hostile).

### Maintainability (P17=6) — not urgent
- [~] **God-class decomposition.** `scanner.cpp` was 2545 lines / ~70 methods /
      ~8 responsibilities (the bigger offender); `screen.cpp` 1182 lines, 52
      methods, 28 members. STARTED 2026-06-10 on scanner, incremental, one
      reviewable commit per cluster. Done so far (scanner now 2282 lines):
      `spokenformat` (pure distance/elevation/letter/compass helpers + tests),
      `itembucket` (pure item classification + tests), and the **Accessible HUD**
      lifted into its own `Hud` class (`hud.{hpp,cpp}`, 241 lines) talking back
      via a narrow `HudHost` interface — a structural slice, NOT unit-tested
      (engine-coupled), so needs an in-game smoke test (open/close, effects
      drill-in, target-row follow). Remaining scanner clusters to carve (rough
      sizes): category list-mgmt (`rebuildCurrentList` 180 lines), lock-on combat
      (~280), quick-info text builders (~150), waypoints/locations (~220). These
      are engine-coupled (need a Ptr/world seam to be testable) — bigger design
      step than the drop-in slices so far. `screen.cpp`: tooltip-cache
      invalidation (`mTooltipElement`, `mSubTooltip*`) is scattered across 5+
      reset sites = silent-desync risk; extract `TooltipCycler` + `Submenu`
      sub-objects before next features push it past ~1500 lines.
- [ ] **(P17) De-dup `formatSpellEffectLine` overloads** (spelltext.cpp:35-43 vs
      127-135, and the magnitude switch 52-76 vs 142-170) — copy-pasted verbatim.

### Distribution hygiene (P15)
- [ ] Add a `THIRD-PARTY-LICENSES` note for Prism's **MPL-2.0** to the beta ZIP
      (currently ships GPLv3 `LICENSE.txt` only). MPL §3.2 wants the notice kept.

## Notes
- There are TWO repair windows: `GM_Repair` (own hammer) vs `GM_MerchantRepair`.
- `ItemSelectionDialog` (the item picker modal) is now accessible — this is the
  same picker used by Alchemy, Enchanting, Recharge, and the quick-keys menu, so
  those screens get the item-choosing half for free.
- Navigation hard limits (why cross-map auto-walk can't "just work"): only a 3x3
  cell grid is loaded around the player (`Constants::CellGridRadius = 1`), and the
  Detour navmesh exists ONLY for loaded cells. So you can't path to anything more
  than ~1 cell away directly — hence the carrot/raycast progressive approach,
  which only extends as the player physically moves and new cells stream in.
  Doors/interiors are separate worldspaces (disjoint navmesh + coordinate
  systems), so continuous pathing across them is impossible; that's the
  door-to-door routing TODO above. `findPath` returns `EndPolygonNotFound` when
  the destination isn't near loaded navmesh — the trigger for the straight-line
  fallback we replaced with the carrot.
- Companion is the same two-pane inventory pattern as barter/container
  (`A11y::PaneGroup`). The container store work (S key, inventory enrols as pane 1
  in `GM_Container`) is the template; companion just needs its own accessible
  pane 0 built first, then to be added to the gating in InventoryWindow::onOpen
  and the S-key handler.
