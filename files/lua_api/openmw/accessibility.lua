---
-- Screen-reader / text-to-speech output for accessibility.
-- Lets scripts speak text through the same backend the engine's built-in
-- screen reader uses, so mods (including standalone accessibility "plugin" mods
-- written for other mods) can provide spoken output without bundling their own
-- text-to-speech. Speech is only audible when a screen-reader backend is active;
-- calls are harmless no-ops otherwise.
-- @context menu|player
-- @module accessibility
-- @usage local accessibility = require('openmw.accessibility')

---
-- Speak a line of text through the screen reader.
-- @function [parent=#accessibility] say
-- @param #string text The text to speak. Empty text is ignored. MyGUI
--   localization tags (`#{Group:Key}`) are resolved before speaking.
-- @param #boolean interrupt Optional. When true (the default), cancel any
--   in-progress speech first; pass false to queue this line after whatever is
--   currently being spoken.
-- @usage accessibility.say("Balmora, 3 of 9")
-- @usage -- queue a follow-up line instead of clobbering the first
-- accessibility.say("Loading complete", false)

---
-- Speak a line and remember it as the last "rereadable" announcement, so the
-- player can repeat it with the engine's reread key. Use for contextual prose
-- the player cannot otherwise navigate back to (for example a line of dialogue
-- or a one-off narrated event), not for routine focus/selection announcements.
-- @function [parent=#accessibility] sayRereadable
-- @param #string text The text to speak and store as rereadable.
-- @param #boolean interrupt Optional, defaults to true. See `say`.
-- @usage accessibility.sayRereadable("You have entered the Mages Guild.")

---
-- Re-speak the last rereadable announcement (interrupting any current speech),
-- mirroring the engine's reread key. Does nothing if nothing has been marked
-- rereadable.
-- @function [parent=#accessibility] reread
-- @usage accessibility.reread()

return nil
