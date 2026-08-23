# Project Hortator — Complete Key Reference

Every key the accessibility layer adds or changes, grouped by where you use it.
The readme covers the essentials; this is the full list.

Two things to know before the tables:

- **Keys mean different things in different places.** `Delete` drops an item in
  your inventory but deletes a spell in the magic window. Each section below says
  where its keys apply, and **Keys that mean different things** at the end lists
  the overlaps in one place.
- **While you are typing in a text box, almost all of these stop working.** Text
  boxes take every key so you can type freely. Press **Escape** to finish
  editing, and the keys below come back.

---

## Contents

- [Anywhere in the game world](#anywhere-in-the-game-world)
- [Scanning and targets](#scanning-and-targets)
- [Acting on the target](#acting-on-the-target)
- [Information and orientation](#information-and-orientation)
- [The accessible HUD](#the-accessible-hud)
- [Menus, lists and dialogs](#menus-lists-and-dialogs)
- [Typing in a text box](#typing-in-a-text-box)
- [Conversations](#conversations)
- [Inventory, containers and companions](#inventory-containers-and-companions)
- [Buying and selling](#buying-and-selling)
- [Magic and spellmaking](#magic-and-spellmaking)
- [Alchemy](#alchemy)
- [Books and scrolls](#books-and-scrolls)
- [The journal](#the-journal)
- [Saving and loading](#saving-and-loading)
- [Settings](#settings)
- [The console](#the-console)
- [Keys that mean different things](#keys-that-mean-different-things)

---

## Anywhere in the game world

| Key | Action |
| --- | --- |
| **H** | Open / close the accessible HUD (pauses the world) |
| **Alt + H** | Read your health |
| **Alt + M** | Read your magicka |
| **Alt + F** | Read your fatigue |
| **Shift + Alt + H** | Read the current target's health |

The quick health / magicka / fatigue keys work while the HUD is open too.

---

## Scanning and targets

The world is grouped into categories (actors, doors, containers and so on). You
pick a category, then cycle through what's in it, nearest first. Whatever is
selected is your **target**.

| Key | Action |
| --- | --- |
| **Page Down / Page Up** | Next / previous target in the current category. Hold to run through a long list |
| **Ctrl + Page Down / Page Up** | Next / previous category |
| **Shift + Page Down / Page Up** | Cycle the subcategory filter (e.g. Plants / Storage within Containers) |
| **Ctrl + 1 … 9** | Jump straight to a category: 1 Actors, 2 Doors, 3 Containers, 4 Items, 5 Activators, 6 Detected, 7 Waypoints, 8 Locations, 9 Terrain |
| **Home** | Announce the current target again |
| **Backspace** | Back to the first (nearest) target in the category |
| **End** | Clear the selection |
| **/** | Search: filter the current category by name, a door's destination, or any note you've attached with Ctrl + K |
| **Ctrl + /** | Clear the search filter |
| **Ctrl + Up** | Direction filter: show only what lies the way you're facing, and keep following you as you turn. Press again to switch off |

---

## Acting on the target

| Key | Action |
| --- | --- |
| **Enter** | Face the target |
| **Shift + Enter** | Auto-walk to the target |
| **Ctrl + Enter** | Toggle the audio beacon on the target |
| **Ctrl + Shift + Enter** | Teleport to the target — a last resort, not fast travel. Warns you to explore properly first and asks you to confirm (you can turn the warning off once you've read it). Limited to about 58 metres, and unavailable during character creation. Followers in range come with you |
| **Space** | Activate the target (open, take, talk). With nothing selected, works as normal Activate |
| **X** | Toggle lock-on to the target, so you stay aimed at it |
| **Shift + X** | Engage: jump to the nearest hostile and lock on, in one press |
| **K** | Mark / unmark the target as "already looked at" |
| **Ctrl + K** | Attach or edit a note on the target (e.g. labelling a caravaner). Notes are searchable with **/** |
| **Shift + K** | Cycle which objects you see: all → unmarked only → marked only → all |
| **I** | Inspect the target's hidden state — says whether a one-shot mechanism has "Already used" or is "Not used yet", then reads out script values such as whether a lever is on or off. Says "has no readable state" if there's nothing to report |

Any movement key cancels auto-walk.

---

## Information and orientation

| Key | Action |
| --- | --- |
| **L** | Announce your location (cell name) |
| **Ctrl + L** | Announce which way you're facing |
| **Shift + L** | Announce your height above the ground, or depth underwater |
| **Alt + L** | Announce the time of day and date |
| **Ctrl + Left / Right** | Snap your facing to the previous / next compass point |
| **Ctrl + Down** | Turn around 180 degrees |
| **Shift + Up / Down** | Aim your view up / down a step, through five fixed stops (straight up, up, level, down, straight down). Use it to fly with Levitation, or surface and dive while swimming |
| **Shift + Home** | Snap your view back to level |
| **N** | Drop a named map note at your position |

---

## The accessible HUD

| Key | Action |
| --- | --- |
| **H** | Open / close |
| **Up / Down** | Move through your stats and status |
| **Enter** | On the active-effects row, open the list of individual effects |
| **Left / Escape** | Back out of the effects list, or close the HUD |
| **Home** | Re-read the current row |

The scanner and quick-info keys keep working while the HUD is open.

---

## Menus, lists and dialogs

These work in every accessible menu and window unless that window says otherwise.

| Key | Action |
| --- | --- |
| **Up / Down** | Previous / next option |
| **Ctrl + Up / Down** | Jump to the previous / next section |
| **Left / Right** | Change the focused option's value |
| **Enter** or **Space** | Activate the option — open a submenu, run the action, or start editing a text box |
| **T** | Cycle the extra detail lines for the focused option (an item's weight, value and so on) |
| **Shift + T** | Cycle those detail lines backwards |
| **R** | Repeat the last announcement |
| **Escape** | Close the window |
| **Tab / Shift + Tab** | Switch between panes shown side by side (Stats, Inventory, Magic, Map; a container and your inventory; a merchant and your inventory) |

When a submenu is open:

| Key | Action |
| --- | --- |
| **Up / Down** | Move between the submenu's items |
| **Home / End** | Jump to the first / last item |
| **Escape** or **Left** | Close the submenu and go back to the option it came from |
| **Enter / Space** | Activate the item, if it does anything |

---

## Typing in a text box

Press **Enter** on a text option to start editing; you'll hear "Editing. Press
Escape when done." **Escape** finishes. While editing, every key goes to the text
box — other shortcuts are inert until you press Escape.

| Key | What you hear |
| --- | --- |
| **Left / Right** | The character you moved onto or over |
| **Home / End** | The character at the start / end |
| **Up / Down** | The line you moved to (or the whole contents, in a single-line box) |
| **Backspace / Delete** | The character removed |
| Typing or pasting | The text that was inserted |

Empty positions read as "blank"; spaces, tabs and line breaks are spoken by name.

---

## Conversations

| Key | Action |
| --- | --- |
| **Up / Down** | Move between topics and options |
| **Enter** | Choose the focused topic or option |
| **Ctrl + Down / Up** | Jump to the next / previous topic you haven't exhausted yet. Says "No more topics." when there are none left |
| **D** | Announce the character's disposition toward you |
| **R** | Re-read what was just said |

---

## Inventory, containers and companions

| Key | Action |
| --- | --- |
| **Up / Down** | Move through the items |
| **Enter** | Take, store, equip or use the focused item — whole stack |
| **Shift + Enter** | The same, but ask how many first |
| **T** | Cycle the item's details (weight, value, condition, effects) |
| **Ctrl + Left / Right** | Cycle the category filter: All, Weapon, Apparel, Magic, Misc |
| **Delete** | Drop the focused item (asks how many for a stack). Bound and conjured items can't be dropped |
| **E** | Announce your encumbrance and armour rating |
| **Tab / Shift + Tab** | Switch between your inventory and the container, companion or merchant |

In a companion's inventory, **E** reads *their* encumbrance instead, and their
running profit or loss if they work for a fee.

---

## Buying and selling

These work on both sides of a barter — your inventory and the merchant's.

| Key | Action |
| --- | --- |
| **Enter** | Add the focused item to the deal |
| **B** | Announce the running balance — what you'd pay, or what you'd receive |
| **G** | Announce your gold |
| **Shift + G** | Announce the merchant's gold |
| **=** or **numpad +** | Raise your offer by 1 |
| **Shift + =** or **Shift + numpad +** | Raise your offer by 100 |
| **-** or **numpad -** | Lower your offer by 1 |
| **Shift + -** or **Shift + numpad -** | Lower your offer by 100 |
| **C** | Type an exact offer amount |
| **O** | Make the offer |

---

## Magic and spellmaking

In the magic window:

| Key | Action |
| --- | --- |
| **Up / Down** | Move through your spells and enchanted items |
| **Enter** | Select the focused spell or item |
| **T** | Cycle the spell's details (effects, cost, chance) |
| **Delete** | Delete the focused spell. Powers, racial and birthsign spells can't be deleted; enchanted items say "You cannot delete this." |
| **Enter** on **Active effects** | Open the list of what's currently affecting you |

When editing an effect while making a spell or enchantment, on a slider
(magnitude, duration, area):

| Key | Action |
| --- | --- |
| **Left / Right** | Adjust by 1 |
| **Ctrl + Left / Right** | Adjust by 10 |
| **Home / End** | Jump to the lowest / highest value |

---

## Alchemy

| Key | Action |
| --- | --- |
| **Up / Down** | Move through the name, quantity, filters, apparatus, ingredients and Create |
| **Enter** | Fill or empty an apparatus slot, add an ingredient, or remove one you've chosen |
| **Left / Right** | Change the quantity or filter |
| **E** | Re-read the effects of the potion you're about to make |
| **T** | Cycle an ingredient's details |

---

## Books and scrolls

| Key | Action |
| --- | --- |
| **Up / Down** | Previous / next paragraph |
| **Enter** | Take the book, or close it, when on those options |
| **R** | Re-read the current paragraph |
| **Escape** | Close |

A book with no readable text says so — either that it contains only pictures, or
that it's blank.

---

## The journal

**While reading:**

| Key | Action |
| --- | --- |
| **Up / Down** | Previous / next two-page spread |
| **Ctrl + Up / Down** | Previous / next journal entry, read in full even if it runs across a page break |
| **Left / Right** | Back / forward one page |
| **T** | Open the topics browser |
| **Q** | Open the quest list |

**In the topic and quest lists:**

| Key | Action |
| --- | --- |
| **Up / Down** | Move through the letters, topics or quests |
| **Enter** | Open the focused entry |
| **Tab / Shift + Tab** | Switch between Topics, Active Quests and All Quests |
| **Backspace** or **Escape** | Go back a level |

In the All Quests list, finished quests are read out as "completed".

Note that **T** opens the topics browser only while *reading*. Once you're in a
list, T goes back to its usual job of reading out extra detail.

---

## Saving and loading

| Key | Action |
| --- | --- |
| **Up / Down** | Move through your saves |
| **Enter** | Load, or save to the focused slot |
| **Delete** | Delete the focused save. You'll be asked to confirm, and the save is named |

---

## Settings

| Key | Action |
| --- | --- |
| **Tab / Shift + Tab** | Next / previous settings tab (the tab name is announced) |
| **Up / Down** | Move through the settings |
| **Ctrl + Up / Down** | Jump between sections |
| **Left / Right** | Change the focused setting |
| **Enter** | Activate a button, or start editing a value |

---

## The console

Open the console with the **`** or **~** key. What you type and what it prints
are both spoken. The console takes the keyboard even when another window is
already open, and puts you back where you were when you close it.

| Key | Action |
| --- | --- |
| **Up / Down** | Recall previous commands |
| **Ctrl + Up / Down** | Re-hear previous output, line by line |
| **Ctrl + T** | Make the scanner's selected object the console target — this replaces clicking on an object. Press with nothing selected to clear it |

---

## Keys that mean different things

Most keys do one job everywhere. These don't, so they're worth knowing:

| Key | Depends where you are |
| --- | --- |
| **T** | Journal, while reading: open the topics browser. **Everywhere else** (including the journal's own lists): read out extra detail for the focused thing |
| **Delete** | Inventory: drop the item. Magic window: delete the spell. Save/Load: delete the save. While typing: delete a character |
| **Tab** | Settings: next tab. Journal lists: switch between Topics and Quests. Elsewhere: switch between side-by-side panes |
| **E** | Your inventory: your encumbrance. A companion's: theirs. Alchemy: re-read the potion's effects |
| **Ctrl + Left / Right** | Inventory: change category filter. Editing a spell effect: adjust a slider by 10. In the world: turn to the next compass point |
| **Ctrl + Up / Down** | Journal, while reading: move by journal entry. Conversations: jump to an unexhausted topic. In the world, Ctrl+Up is the direction filter and Ctrl+Down turns you around. In menus: jump between sections |
| **Up / Down** | Journal, while reading: turn a page spread. Books: move by paragraph. Elsewhere: move between options |
| **Home** | In the world: repeat the target. Submenus: jump to the first item. On a slider: lowest value. While typing: start of the line |
| **Escape** | While typing: stop editing. In a submenu: close the submenu. Journal lists: go back a level. Otherwise: close the window |

---

## A note on conflicts with Morrowind's own controls

The accessibility keys mostly use keys Morrowind leaves free — the number row
with Ctrl, Page Up and Page Down, the arrow keys with Ctrl, and so on. A few are
plain letters (**H**, **K**, **X**, **I**, **L**, **N**). If one of these clashes
with something you've rebound, you can change your own binding in the game's
controls settings; the accessibility keys themselves are fixed for now.

Note that **R** appears only in menus, conversations and books — in the world it
stays Morrowind's own "ready / put away magic". To hear the last thing spoken
again while you're playing, use **Home**.
