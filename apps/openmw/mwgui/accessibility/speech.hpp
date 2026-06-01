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
}

#endif
