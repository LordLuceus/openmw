#ifndef MWGUI_MESSAGE_BOX_H
#define MWGUI_MESSAGE_BOX_H

#include <memory>

#include <MyGUI_KeyCode.h>
#include <MyGUI_Types.h>

#include "windowbase.hpp"

namespace MyGUI
{
    class Widget;
    class Button;
    class EditBox;
}

namespace MWGui
{
    class InteractiveMessageBox;
    class MessageBoxManager;
    class MessageBox;
    class MessageBoxManager
    {
    public:
        MessageBoxManager(float timePerChar);
        ~MessageBoxManager();
        void onFrame(float frameDuration);
        // isSubtitle: this message originated from the dialogue /
        // sound-subtitles system. When true, screen-reader speech is
        // gated on the Settings::gui().mReadSubtitlesAloud flag so
        // sighted players who only want the visual subtitles do not
        // get them narrated as well.
        void createMessageBox(std::string_view message, bool stat = false, bool isSubtitle = false);
        void removeStaticMessageBox();
        bool createInteractiveMessageBox(std::string_view message, const std::vector<std::string>& buttons,
            bool immediate = false, int defaultFocus = -1);
        bool isInteractiveMessageBox();

        std::size_t getMessagesCount();

        const InteractiveMessageBox* getInteractiveMessageBox() const { return mInterMessageBoxe.get(); }

        /// Remove all message boxes
        void clear();

        bool removeMessageBox(MessageBox* msgbox);

        /// @param reset Reset the pressed button to -1 after reading it.
        int readPressedButton(bool reset = true);

        void resetInteractiveMessageBox();

        void setLastButtonPressed(int index);

        typedef MyGUI::delegates::MultiDelegate<int> EventHandle_Int;

        // Note: this delegate unassigns itself after it was fired, i.e. works once.
        EventHandle_Int eventButtonPressed;

        void onButtonPressed(int button)
        {
            eventButtonPressed(button);
            eventButtonPressed.clear();
        }

        void setVisible(bool value);

        const std::vector<std::unique_ptr<MessageBox>>& getActiveMessageBoxes() const;

    private:
        std::vector<std::unique_ptr<MessageBox>> mMessageBoxes;
        std::unique_ptr<InteractiveMessageBox> mInterMessageBoxe;
        MessageBox* mStaticMessageBox;
        float mMessageBoxSpeed;
        int mLastButtonPressed;
        bool mVisible = true;
    };

    class MessageBox : public Layout
    {
    public:
        MessageBox(MessageBoxManager& parMessageBoxManager, std::string_view message);
        const std::string& getMessage() { return mMessage; }
        int getHeight();
        void update(int height);
        void setVisible(bool value);

        float mCurrentTime;
        float mMaxTime;

    protected:
        MessageBoxManager& mMessageBoxManager;
        std::string mMessage;
        MyGUI::EditBox* mMessageWidget;
        int mBottomPadding;
        int mNextBoxPadding;
    };

    class InteractiveMessageBox : public WindowModal
    {
    public:
        InteractiveMessageBox(MessageBoxManager& parMessageBoxManager, const std::string& message,
            const std::vector<std::string>& buttons, bool immediate, size_t defaultFocus);
        void mousePressed(MyGUI::Widget* widget);
        int readPressedButton();

        MyGUI::Widget* getDefaultKeyFocus() override;

        bool exit() override { return false; }

        bool mMarkedToDelete;

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;

    private:
        void buttonActivated(MyGUI::Widget* widget);

        // Accessibility: announce a button as "<label>, button. N of M" when it
        // receives keyboard focus, so a screen-reader user can arrow/Tab through
        // the choices one at a time (the engine already moves focus + activates
        // on Enter; only the speech was missing). Interrupts prior speech since
        // the move is deliberate. Bound to each button's eventKeySetFocus.
        void onButtonKeyFocus(MyGUI::Widget* sender, MyGUI::Widget* oldFocus);
        // Accessibility: R re-reads the prompt (the per-option labels are
        // re-read simply by arrowing back onto them). Bound to each button's
        // eventKeyButtonPressed; all other keys are left for the engine's
        // keyboard navigation. Speaks the prompt and consumes only R.
        void onButtonKeyPressed(MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char ch);

        MessageBoxManager& mMessageBoxManager;
        MyGUI::EditBox* mMessageWidget;
        MyGUI::Widget* mButtonsWidget;
        std::vector<MyGUI::Button*> mButtons;

        int mButtonPressed;
        size_t mDefaultFocus;
        bool mImmediate;
        size_t mControllerFocus = 0;

        // Returns the spoken form of button \p i: "<label>, button".
        std::string buttonAnnouncement(size_t i) const;

        // The button focused when the box opened. We announce it explicitly in
        // the constructor (right after the prompt) because the engine does NOT
        // reliably fire a focus event on open for every box -- multi-button
        // pickers do, but two-button yes/no boxes do not. To avoid then
        // DOUBLE-announcing it if the focus event does arrive, the focus handler
        // suppresses exactly one event for this widget; once focus lands on any
        // other button (the user navigated) the guard is cleared so returning to
        // this option later still announces normally. Null after it's consumed.
        MyGUI::Widget* mInitialFocusWidget = nullptr;
    };

}

#endif
