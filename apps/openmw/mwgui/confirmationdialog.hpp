#ifndef MWGUI_CONFIRMATIONDIALOG_H
#define MWGUI_CONFIRMATIONDIALOG_H

#include "windowbase.hpp"

#include "accessibility/screen.hpp"

namespace MWGui
{
    class ConfirmationDialog : public WindowModal
    {
    public:
        ConfirmationDialog();
        void askForConfirmation(const std::string& message);
        bool exit() override;

        void onFrame(float dt) override;
        // Guaranteed teardown hook: setVisible(false) always fires this, so it's
        // the safe place to ensure the suspended underlying screen is resumed no
        // matter how the dialog is dismissed (prevents a silent input lockout).
        void onClose() override;

        typedef MyGUI::delegates::MultiDelegate<> EventHandle_Void;

        /** Event : Ok button was clicked.\n
            signature : void method()\n
        */
        EventHandle_Void eventOkClicked;
        EventHandle_Void eventCancelClicked;

    private:
        MyGUI::EditBox* mMessage;
        MyGUI::Button* mOkButton;
        MyGUI::Button* mCancelButton;

        void onCancelButtonClicked(MyGUI::Widget* sender);
        void onOkButtonClicked(MyGUI::Widget* sender);

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        bool mOkButtonFocus = true;

        // --- Screen-reader accessibility ---------------------------------
        // Real-focus screen with the Yes/No buttons as navigable options, so
        // the user can arrow between them and activate with Enter (like the
        // persuasion dialog). The dialog is modal over whichever screen was
        // active when it opened (e.g. the Magic pane during a spell delete);
        // that screen is suspended on open and resumed on close.
        A11y::Screen mA11y;
        A11y::Screen* mA11yPrev = nullptr;
        // Hand input back to the screen we covered. \p announce re-reads where
        // that screen left off; pass false on the OK path when the OK callback
        // itself announces (e.g. the Magic pane rebuilding after a spell
        // delete), to avoid speaking the stale row then the new one.
        void a11yRestorePrevious(bool announce);
    };

}

#endif
