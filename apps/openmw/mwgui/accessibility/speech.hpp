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

    /// Re-speak the last rereadable announcement, interrupting any current
    /// speech. No-op when nothing has been marked rereadable.
    void reread();

    /// Forget the last rereadable announcement, so a subsequent reread() does
    /// nothing. Called when leaving a screen so its content can't be re-read
    /// from an unrelated context later.
    void clearReread();
}

#endif
