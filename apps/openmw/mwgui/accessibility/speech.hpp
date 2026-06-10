#ifndef OPENMW_MWGUI_ACCESSIBILITY_SPEECH_H
#define OPENMW_MWGUI_ACCESSIBILITY_SPEECH_H

#include <string_view>

namespace MWGui::A11y
{
    /// Speak \p text through the active screen-reader backend.
    ///
    /// Resolves MyGUI \c #{Group:Key} localization tags first, so callers can
    /// pass either raw text or GMST / L10n tags interchangeably. This is the
    /// single entry point for spoken output from the GUI layer -- there is no
    /// other copy of the tag-resolution logic.
    ///
    /// \param interrupt when true, cancel any in-progress speech first. The
    ///        default is false so that focus + value announcements queue
    ///        cleanly instead of clobbering each other.
    void say(std::string_view text, bool interrupt = false);

    /// Speak \p text and remember it as the "last primary announcement" that
    /// the user can repeat with the reread key (R). Use for contextual content
    /// the user can't otherwise re-navigate to -- e.g. a class-quiz question or
    /// a line of dialogue -- NOT for per-option focus announcements (those are
    /// re-read by simply arrowing back onto the option).
    void sayRereadable(std::string_view text, bool interrupt = false);

    /// Speak \p spoken now, but remember a DIFFERENT \p rereadable string as the
    /// text the reread key (R) will repeat. Use when the on-the-fly
    /// announcement should stay terse but the reread should carry extra context
    /// -- e.g. a dialogue line is spoken without the speaker's name (it would be
    /// repetitive on every line), yet R repeats it WITH the speaker prefixed so
    /// the user can recall who said it.
    void sayRereadable(std::string_view spoken, std::string_view rereadable, bool interrupt = false);

    /// Re-speak the last rereadable announcement, interrupting any current
    /// speech. No-op when nothing has been marked rereadable.
    void reread();

    /// Forget the last rereadable announcement, so a subsequent reread() does
    /// nothing. Called when leaving a screen so its content can't be re-read
    /// from an unrelated context later.
    void clearReread();

    /// Log a single diagnostic warning from the accessibility framework, with a
    /// consistent \c [a11y] prefix so failures are greppable in openmw.log.
    ///
    /// A speech-only interface has no visual fallback, so a silent give-up
    /// (an unresolved localization tag, a missing effect/enchant lookup, an
    /// unexpected-null widget) is invisible to BOTH the player and the
    /// developer. Route every such "I have nothing to say and that's not
    /// normal" branch through here so a future engine/data change surfaces as a
    /// locatable log line rather than as silence or plausible-but-wrong speech.
    /// Keep messages terse and contextual (what was being announced, what was
    /// missing). This is intentionally NOT spoken -- it's for diagnosis.
    void logWarn(std::string_view message);
}

#endif
