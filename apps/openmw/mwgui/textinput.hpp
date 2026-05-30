#ifndef MWGUI_TEXT_INPUT_H
#define MWGUI_TEXT_INPUT_H

#include "windowbase.hpp"

namespace MWGui
{
    class TextInputDialog : public WindowModal
    {
    public:
        TextInputDialog();

        std::string getTextInput() const;
        void setTextInput(const std::string& text);

        void setNextButtonShow(bool shown);
        void setTextLabel(std::string_view label);
        void onOpen() override;

        bool exit() override { return false; }

        /** Event : Dialog finished, OK button clicked.\n
            signature : void method()\n
        */
        EventHandle_WindowBase eventDone;

    protected:
        void onOkClicked(MyGUI::Widget* sender);
        void onTextAccepted(MyGUI::EditBox* sender);
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;

    private:
        MyGUI::EditBox* mTextEdit;
        // Cached prompt label shown above the edit box. Captured by
        // setTextLabel() so we can read it aloud through the screen reader
        // when the dialog opens.
        std::string mPromptLabel;
    };
}
#endif
