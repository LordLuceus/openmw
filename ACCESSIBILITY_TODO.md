# Project Hortator — Accessibility To-Do

Tracks which game screens and mechanics still need screen-reader accessibility
work. Companion doc to `ACCESSIBILITY_CHANGELOG.md` (which records what's done).

Each GUI screen is identified by its `GuiMode` (see
`apps/openmw/mwgui/mode.hpp`) where applicable.

## Done

Inventory, Spells/Magic, Stats, Map (+ waypoints & map notes), Dialogue
(+ topic jumping), Journal (page reading + topics/quests browsing), Container,
Book, Scroll, Barter, Travel, Training, Persuasion, Settings window, Main menu, and the
character-creation screens (Name, Race, Birth, Class, Review). Object scanner,
auto-walk, location announcements, and audio beacon (gameplay, not a screen).

## To-Do

### Merchant / services windows (model-backed lists, barter-like)
- [ ] **Spell buying** (`GM_SpellBuying`) — list of purchasable spells + cost
- [ ] **Spell making / creation** (`GM_SpellCreation`) — effect builder (complex)
- [ ] **Enchanting** (`GM_Enchanting`) — item + effects + soul gem
- [ ] **Alchemy / potion-making** (`GM_Alchemy`) — ingredients grid → effects
- [ ] **Recharge** (`GM_Recharge`) — recharge enchanted items with soul gems
- [ ] **Repair** (`GM_Repair`) — player's own repair hammers
- [ ] **Merchant repair** (`GM_MerchantRepair`) — paying a smith (separate window)

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

### Targeting-dependent (deferred until combat/targeting work)
- [ ] **Combat** — needs target selection
- [ ] **Lockpicking / probes** — using the tool alone didn't work; needs a target,
      so slots in with combat/targeting

## Notes
- There are TWO repair windows: `GM_Repair` (own hammer) vs `GM_MerchantRepair`.
- Companion is the same two-pane inventory pattern as barter (`A11y::PaneGroup`),
  so it should be a quick follow-up.
