#include "confirmationdialog.hpp"

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>

#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

#include "accessibility/speech.hpp"
#include "accessibility/uimanager.hpp"

namespace MWGui
{
    ConfirmationDialog::ConfirmationDialog()
        : WindowModal("openmw_confirmation_dialog.layout")
    {
        getWidget(mMessage, "Message");
        getWidget(mOkButton, "OkButton");
        getWidget(mCancelButton, "CancelButton");

        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ConfirmationDialog::onCancelButtonClicked);
        mOkButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ConfirmationDialog::onOkButtonClicked);

        if (Settings::gui().mControllerMenus)
        {
            mDisableGamepadCursor = true;
            mControllerButtons.mA = "#{Interface:OK}";
            mControllerButtons.mB = "#{Interface:Cancel}";
        }
    }

    void ConfirmationDialog::askForConfirmation(const std::string& message)
    {
        setVisible(true);

        mMessage->setCaptionWithReplacing(message);

        int height = mMessage->getTextSize().height + 60;

        int width = mMessage->getTextSize().width + 24;

        mMainWidget->setSize(width, height);

        mMessage->setSize(mMessage->getWidth(), mMessage->getTextSize().height + 24);

        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mOkButton);

        if (Settings::gui().mControllerMenus)
        {
            mOkButtonFocus = true;
            mOkButton->setStateSelected(true);
            mCancelButton->setStateSelected(false);
        }

        center();

        // Screen reader: suspend whatever screen was active underneath (e.g. the
        // Magic pane that triggered a spell delete) so it stops handling keys
        // while the dialog is up, then take input ourselves. Announce the
        // question first, then land on Yes; the user arrows between Yes/No and
        // presses Enter. We resume the previous screen on close.
        // Re-entrancy guard: this is a shared singleton with a single mA11yPrev.
        // If a prior askForConfirmation didn't resume yet (mA11yPrev still set),
        // keep that original covered screen rather than overwriting it -- which
        // would orphan it (suspended forever) and could even capture our own
        // screen. Only capture when we have nothing pending.
        if (!mA11yPrev)
        {
            mA11yPrev = A11y::UiManager::instance().active();
            if (mA11yPrev)
                mA11yPrev->suspend();
        }
        // Rebuild the Yes/No options each open: deactivate() clears the element
        // list (and unbinds the per-widget key delegates), so re-adding here is
        // both necessary and safe. Real-focus mode -- the dialog is itself
        // modal, as in the persuasion dialog.
        mA11y.clear();
        mA11y.add({ .widget = mOkButton, .label = mOkButton->getCaption().asUTF8(),
            .activate = [this] { onOkButtonClicked(mOkButton); } });
        mA11y.add({ .widget = mCancelButton, .label = mCancelButton->getCaption().asUTF8(),
            .activate = [this] { onCancelButtonClicked(mCancelButton); } });
        A11y::say(message, /*interrupt=*/true);
        mA11y.activate(mOkButton);
    }

    void ConfirmationDialog::onFrame(float dt)
    {
        mA11y.onFrame(dt);
    }

    void ConfirmationDialog::a11yRestorePrevious(bool announce)
    {
        mA11y.deactivate();
        // Resume the screen we covered (if it's still active in its window) so
        // it handles keys again. Re-announce where it left off unless the
        // caller's own callback will announce (avoids a stale-then-fresh pair).
        if (mA11yPrev)
        {
            mA11yPrev->resume();
            if (announce)
                mA11yPrev->announceCurrent();
            mA11yPrev = nullptr;
        }
    }

    void ConfirmationDialog::onClose()
    {
        // Safety net: setVisible(false) always routes here, so even a dismissal
        // path that bypasses exit()/onOkButtonClicked() (or a future one) can
        // never leave the covered screen suspended with no active screen --
        // that would be a total, silent input lockout for a screen-reader user
        // with no visual way to recover. The normal paths restore BEFORE hiding
        // (so their chosen announce policy wins) and null mA11yPrev, making this
        // a no-op there; this only fires for an unexpected close, where
        // re-reading where the covered screen left off is the safe default.
        a11yRestorePrevious(/*announce=*/true);
        WindowModal::onClose();
    }

    bool ConfirmationDialog::exit()
    {
        // Resume the covered screen before hiding (and before firing the
        // callback) so any spoken feedback lands on the now-active screen, and
        // so the onClose() that setVisible(false) triggers finds nothing left to
        // do. Cancel usually has no announcing callback, so re-read the restored
        // screen's row.
        a11yRestorePrevious(/*announce=*/true);
        setVisible(false);
        eventCancelClicked();
        return true;
    }

    void ConfirmationDialog::onCancelButtonClicked(MyGUI::Widget* /*sender*/)
    {
        exit();
    }

    void ConfirmationDialog::onOkButtonClicked(MyGUI::Widget* /*sender*/)
    {
        // Resume the covered screen first: the OK callback may rebuild and
        // announce on it (e.g. the Magic pane's onDeleteSpellAccept), which
        // requires that screen to be the active one again. Don't re-announce
        // here -- the callback speaks the result (the new row after a delete);
        // re-reading the stale row first would just stutter. Restoring before
        // setVisible(false) also means the onClose() it fires is a no-op (so it
        // won't override this announce=false policy with a stale re-read).
        a11yRestorePrevious(/*announce=*/false);
        setVisible(false);
        eventOkClicked();
    }

    bool ConfirmationDialog::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_A)
        {
            if (mOkButtonFocus)
                onOkButtonClicked(mOkButton);
            else
                onCancelButtonClicked(mCancelButton);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_B)
        {
            onCancelButtonClicked(mCancelButton);
        }
        else if ((arg.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT && !mOkButtonFocus)
            || (arg.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT && mOkButtonFocus))
        {
            mOkButtonFocus = !mOkButtonFocus;
            mOkButton->setStateSelected(mOkButtonFocus);
            mCancelButton->setStateSelected(!mOkButtonFocus);
        }

        return true;
    }
}
