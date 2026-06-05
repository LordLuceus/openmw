#ifndef MWGUI_WAYPOINT_NOTE_DIALOG_H
#define MWGUI_WAYPOINT_NOTE_DIALOG_H

#include "accessibility/editfield.hpp"
#include "windowbase.hpp"

namespace MWGui
{
    /// A small text-input prompt for the accessibility "drop note" feature: the
    /// player presses N during gameplay to place a map note (custom marker) at
    /// their current position, and types its text here. Modelled on
    /// ScannerSearchDialog -- shown via the GM_WaypointNote GUI mode so the
    /// world pauses and keystrokes route to the edit box, with the screen reader
    /// narrating editing via A11y::EditField.
    ///
    /// Unlike the search prompt an empty submission is treated as "cancel" (a
    /// blank note is useless), so only Enter/OK with text actually drops a note.
    class WaypointNoteDialog : public WindowModal
    {
    public:
        WaypointNoteDialog();

        std::string getNoteText() const;
        void setNoteText(const std::string& text);

        void onOpen() override;
        void onFrame(float duration) override;

        // Escape cancels the prompt (drops no note).
        bool exit() override;

        /// Fired when the user confirms (Enter / OK). The scanner reads
        /// getNoteText() and, if non-empty, places the marker.
        EventHandle_WindowBase eventAccepted;
        /// Fired when the user cancels (Escape / empty submit).
        EventHandle_WindowBase eventCancelled;

    private:
        void onOkClicked(MyGUI::Widget* sender);
        void onTextAccepted(MyGUI::EditBox* sender);

        MyGUI::EditBox* mTextEdit;
        std::string mPromptLabel;
        A11y::EditField mEditField;
    };
}

#endif
