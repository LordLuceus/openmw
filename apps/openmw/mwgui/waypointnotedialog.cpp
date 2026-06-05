#include "waypointnotedialog.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_UString.h>

namespace MWGui
{
    WaypointNoteDialog::WaypointNoteDialog()
        : WindowModal("openmw_text_input.layout")
    {
        center();

        getWidget(mTextEdit, "TextEdit");
        mTextEdit->eventEditSelectAccept += newDelegate(this, &WaypointNoteDialog::onTextAccepted);
        mEditField.attach(mTextEdit);

        MyGUI::Button* okButton;
        getWidget(okButton, "OKButton");
        okButton->eventMouseButtonClick += MyGUI::newDelegate(this, &WaypointNoteDialog::onOkClicked);

        // Fixed accessibility prompt string (not a vanilla GMST, since this is a
        // mod-only feature). Kept short for quick narration.
        mPromptLabel = "Note";
        setText("LabelT", mPromptLabel);

        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mTextEdit);

        mControllerButtons.mA = "#{Interface:OK}";
    }

    std::string WaypointNoteDialog::getNoteText() const
    {
        return mTextEdit->getCaption();
    }

    void WaypointNoteDialog::setNoteText(const std::string& text)
    {
        mTextEdit->setCaption(text);
        mEditField.sync();
    }

    void WaypointNoteDialog::onOpen()
    {
        WindowModal::onOpen();
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mTextEdit);
        // Announce the prompt and current contents, and baseline the edit field
        // so subsequent keystrokes give correct spoken feedback.
        mEditField.announceContents(mPromptLabel);
    }

    void WaypointNoteDialog::onFrame(float /*duration*/)
    {
        mEditField.onFrame();
    }

    bool WaypointNoteDialog::exit()
    {
        // Escape cancels: no note dropped.
        eventCancelled(this);
        return true;
    }

    void WaypointNoteDialog::onOkClicked(MyGUI::Widget* /*sender*/)
    {
        // An empty submission is treated as cancel (a blank note is useless),
        // so route it to the cancel path; the scanner only drops a note when
        // there's text.
        if (getNoteText().empty())
            eventCancelled(this);
        else
            eventAccepted(this);
    }

    void WaypointNoteDialog::onTextAccepted(MyGUI::EditBox* sender)
    {
        onOkClicked(sender);
        // Avoid re-firing onTextAccepted repeatedly.
        MWBase::Environment::get().getWindowManager()->injectKeyRelease(MyGUI::KeyCode::None);
    }
}
