# Accessibility UI Framework

Reusable screen-reader support for OpenMW's MyGUI windows. The goal is that
making a new window accessible means *describing its options declaratively*,
not re-implementing navigation, announcements, and tooltips by hand.

## Why this exists

The first three accessible screens (main menu, options, race) were each
hand-written. That produced three copies of the same logic (tag-resolving
speech, Up/Down navigation, value announcements, slider stepping) and one
real bug: the options screen drove Up/Down by *injecting Tab* and letting the
engine's global `KeyboardNavigation` decide where focus landed. After a tab
switch that fallback walked the entire GUI and landed on the main-menu buttons
behind the settings window, so the screen reader read the wrong screen.

This framework fixes that class of bug structurally and removes the
duplication.

## Layering / where code lives

- `components/accessibility/` — low-level TTS facade (`AccessibilityManager`)
  over Prism. **No MyGUI dependency.** Knows nothing about widgets or layouts.
- `apps/openmw/mwgui/accessibility/` — this framework. MyGUI-aware. Resolves
  MyGUI `#{Group:Key}` localization tags and owns the interaction model.
- Individual windows (`race.cpp`, `settingswindow.cpp`, ...) — own an
  `A11y::Screen` member and register their options with it. No navigation or
  speech logic of their own.

We keep the framework in `apps/openmw/mwgui/` (not `components/`) because it is
tightly coupled to MyGUI widgets and to `WindowManager`. Putting it in new
files means upstream merges never conflict with it; existing engine files get
only minimal, stable hook calls.

## Components

### `speech.hpp` — `A11y::say(text, interrupt=false)`
Single entry point for spoken output from the GUI. Resolves MyGUI
`#{Group:Key}` tags via `LanguageManager::replaceTags`, then forwards to
`AccessibilityManager::speak`. Every screen speaks through this; there is no
other copy of the tag-resolution logic.

### `element.hpp` — `A11y::Element`
A declarative description of one navigable option. All behaviour is supplied as
optional callbacks, so the framework never needs to know what a given option
*is*:

| Field      | Type                                  | Meaning |
|------------|---------------------------------------|---------|
| `widget`   | `MyGUI::Widget*`                      | The widget that receives keyboard focus. May be a *proxy* (e.g. a header TextBox) when the real control eats arrow keys. |
| `label`    | `std::string`                         | Spoken name. May contain `#{tags}`. |
| `value`    | `std::function<std::string()>`        | Current value text, spoken on focus and after a change. Optional. |
| `change`   | `std::function<void(bool next)>`      | Left/Right handler. Optional (omit for buttons). |
| `tooltips` | `std::function<std::vector<std::string>()>` | Lines cycled by T / Shift+T. Optional. |
| `activate` | `std::function<void()>`               | Enter/Space handler. Optional. |

### `screen.hpp` — `A11y::Screen`
The per-window controller. A window owns one as a member. Responsibilities:

- **Registration**: `add(Element)` stores the element, forces its widget
  focusable, and hooks `eventKeySetFocus` + `eventKeyButtonPressed`.
- **Navigation**: owns an explicit ordered list of elements. Up/Down moves
  between them by calling `setKeyFocusWidget` *itself* — it never injects Tab
  and never delegates to engine `KeyboardNavigation`. This is the core fix for
  the options-tab bug.
- **Value change**: Left/Right → `element.change(next)`, then re-announces the
  value and invalidates the tooltip cache.
- **Activation**: Enter / NumpadEnter / Space → `element.activate()`.
- **Tooltips**: T cycles `element.tooltips()` forward, Shift+T backward, with an
  "N of M" position indicator appended at the **end** (project convention for
  all positional info).
- **Delayed hint**: via `onFrame(dt)`, once focus has rested on an element with
  tooltips for `sHintDelay` (2s), announces "Has N tooltips. Press T to read."
  once per focus.
- **Custom keys**: `setExtraKeyHandler(fn)` lets a screen handle keys the
  framework doesn't (e.g. settings' Ctrl+Left/Right tab cycling).

### `uimanager.hpp` — `A11y::UiManager` (singleton)
Tracks **which screen is currently active**. `Screen::activate()` registers
itself here; `deactivate()` clears it. Every `Screen` key/frame handler first
checks `UiManager::instance().active() == this` and bails otherwise. This is
the explicit state machine: even if a stale event fires from a hidden window,
it cannot reach a non-active screen. Focus ownership makes routing correct in
practice; the UiManager makes it correct *by construction* and gives one place
to inspect UI state when debugging.

## How a window becomes accessible

```cpp
// In the window's constructor, after getWidget() calls:
mA11y.add({ .widget = mRaceProxy, .label = "#{sRaceMenu5}",
            .value = [this]{ return currentRaceName(); },
            .change = [this](bool next){ selectRace(next); },
            .tooltips = [this]{ return raceTooltips(); } });
mA11y.add({ .widget = mOkButton, .label = "#{sOK}",
            .activate = [this]{ onOkClicked(mOkButton); } });

// On open / close:
void onOpen() override  { /* ...existing... */ mA11y.activate(); }
void onClose() override { mA11y.deactivate(); }

// Tick (WindowBase already receives this):
void onFrame(float dt) override { mA11y.onFrame(dt); }
```

That is the entire per-screen surface. No speech, navigation, or tooltip code
lives in the window.

## Conventions baked in

- Positional info ("N of M") goes at the **end** of an announcement.
- Speech is non-interrupting by default so focus + value events queue cleanly.
- The framework disables engine `KeyboardNavigation`
  (`WindowManager::setKeyboardNavigationEnabled(false)`) while a screen is
  active, because MyGUI's spatial nav and list widgets fight custom schemes.
- Look up widgets via stored member pointers from `getWidget()`, never
  `mMainWidget->findWidget("Name")` — the Layout system name-prefixes widgets
  so find-by-name returns nullptr.
