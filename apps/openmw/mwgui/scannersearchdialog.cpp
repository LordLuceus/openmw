#include "scannersearchdialog.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_UString.h>

namespace MWGui
{
    ScannerSearchDialog::ScannerSearchDialog()
        : WindowModal("openmw_text_input.layout")
    {
        center();

        getWidget(mTextEdit, "TextEdit");
        mTextEdit->eventEditSelectAccept += newDelegate(this, &ScannerSearchDialog::onTextAccepted);
        mEditField.attach(mTextEdit);

        MyGUI::Button* okButton;
        getWidget(okButton, "OKButton");
        okButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ScannerSearchDialog::onOkClicked);

        // The prompt is a fixed accessibility string (not a vanilla GMST, since
        // this is a mod-only feature). Kept short for quick narration.
        mPromptLabel = "Search";
        setText("LabelT", mPromptLabel);

        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mTextEdit);

        mControllerButtons.mA = "#{Interface:OK}";
    }

    std::string ScannerSearchDialog::getSearchText() const
    {
        return mTextEdit->getCaption();
    }

    void ScannerSearchDialog::setSearchText(const std::string& text)
    {
        mTextEdit->setCaption(text);
        mEditField.sync();
    }

    void ScannerSearchDialog::onOpen()
    {
        WindowModal::onOpen();
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mTextEdit);
        // Announce the prompt and current contents, and baseline the edit field
        // so subsequent keystrokes give correct spoken feedback.
        mEditField.announceContents(mPromptLabel);
    }

    void ScannerSearchDialog::onFrame(float /*duration*/)
    {
        mEditField.onFrame();
    }

    bool ScannerSearchDialog::exit()
    {
        // Escape cancels: notify the scanner (which leaves the existing filter
        // untouched) and let the GUI mode pop.
        eventCancelled(this);
        return true;
    }

    void ScannerSearchDialog::onOkClicked(MyGUI::Widget* /*sender*/)
    {
        // Unlike the chargen name dialog, an empty submission is allowed -- the
        // scanner reads it as "clear the filter".
        eventAccepted(this);
    }

    void ScannerSearchDialog::onTextAccepted(MyGUI::EditBox* sender)
    {
        onOkClicked(sender);
        // Avoid re-firing onTextAccepted repeatedly.
        MWBase::Environment::get().getWindowManager()->injectKeyRelease(MyGUI::KeyCode::None);
    }
}
