#ifndef MWGUI_SCANNER_SEARCH_DIALOG_H
#define MWGUI_SCANNER_SEARCH_DIALOG_H

#include "accessibility/editfield.hpp"
#include "windowbase.hpp"

namespace MWGui
{
    /// A small text-input prompt used by the accessibility object scanner to
    /// filter the current category by name. Modelled on TextInputDialog, but:
    ///  - it allows an empty submission (which the scanner treats as "clear the
    ///    filter"), and
    ///  - Escape cancels (leaving the existing filter untouched) instead of
    ///    being swallowed.
    ///
    /// The dialog is shown via the GM_ScannerSearch GUI mode so the world
    /// pauses and keystrokes route to the edit box; the screen reader narrates
    /// editing via A11y::EditField.
    class ScannerSearchDialog : public WindowModal
    {
    public:
        ScannerSearchDialog();

        std::string getSearchText() const;
        void setSearchText(const std::string& text);

        void onOpen() override;
        void onFrame(float duration) override;

        // Escape cancels the prompt (pops the mode without applying a change).
        bool exit() override;

        /// Fired when the user confirms (Enter / OK). The scanner reads
        /// getSearchText() and applies it as the active filter.
        EventHandle_WindowBase eventAccepted;
        /// Fired when the user cancels (Escape). The scanner leaves the current
        /// filter unchanged.
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
