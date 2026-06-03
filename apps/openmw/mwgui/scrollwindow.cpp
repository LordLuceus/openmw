#include "scrollwindow.hpp"

#include <MyGUI_ScrollView.h>

#include <components/esm3/loadbook.hpp>
#include <components/esm4/loadbook.hpp>
#include <components/widgets/imagebutton.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwmechanics/actorutil.hpp"

#include "../mwworld/actiontake.hpp"
#include "../mwworld/class.hpp"

#include "accessibility/booktext.hpp"
#include "accessibility/speech.hpp"

#include "formatting.hpp"

namespace MWGui
{

    ScrollWindow::ScrollWindow()
        : BookWindowBase("openmw_scroll.layout")
        , mTakeButtonShow(true)
        , mTakeButtonAllowed(true)
    {
        getWidget(mTextView, "TextView");

        getWidget(mCloseButton, "CloseButton");
        mCloseButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ScrollWindow::onCloseButtonClicked);

        getWidget(mTakeButton, "TakeButton");
        mTakeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ScrollWindow::onTakeButtonClicked);

        adjustButton("CloseButton");
        adjustButton("TakeButton");

        mCloseButton->eventKeyButtonPressed += MyGUI::newDelegate(this, &ScrollWindow::onKeyButtonPressed);
        mTakeButton->eventKeyButtonPressed += MyGUI::newDelegate(this, &ScrollWindow::onKeyButtonPressed);

        mControllerScrollWidget = mTextView;
        mControllerButtons.mB = "#{Interface:Close}";
        mControllerButtons.mDpad = "#{Interface:ScrollDown}";

        // Screen-reader setup: invisible anchor holds key focus while the
        // A11y::Screen tracks the current line/button internally. See
        // BookWindow for the rationale.
        mA11yAnchor = mMainWidget->createWidget<MyGUI::Widget>(
            {}, MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
        mA11yAnchor->setNeedKeyFocus(true);
        mA11y.setVirtualFocus(mA11yAnchor);

        center();
    }

    void ScrollWindow::setPtr(const MWWorld::Ptr& scroll)
    {
        if (scroll.isEmpty() || (scroll.getType() != ESM::REC_BOOK && scroll.getType() != ESM::REC_BOOK4))
            throw std::runtime_error("Invalid argument in ScrollWindow::setPtr");
        mScroll = scroll;

        MWWorld::Ptr player = MWMechanics::getPlayer();
        bool showTakeButton = scroll.getContainerStore() != &player.getClass().getContainerStore(player);

        const std::string* text;
        if (scroll.getType() == ESM::REC_BOOK)
            text = &scroll.get<ESM::Book>()->mBase->mText;
        else
            text = &scroll.get<ESM4::Book>()->mBase->mText;
        bool shrinkTextAtLastTag = scroll.getType() == ESM::REC_BOOK;

        Formatting::BookFormatter formatter;
        formatter.markupToWidget(mTextView, *text, 390, mTextView->getHeight(), shrinkTextAtLastTag);
        MyGUI::IntSize size = mTextView->getChildAt(0)->getSize();

        // Canvas size must be expressed with VScroll disabled, otherwise MyGUI would expand the scroll area when the
        // scrollbar is hidden
        mTextView->setVisibleVScroll(false);
        if (size.height > mTextView->getSize().height)
            mTextView->setCanvasSize(mTextView->getWidth(), size.height);
        else
            mTextView->setCanvasSize(mTextView->getWidth(), mTextView->getSize().height);
        mTextView->setVisibleVScroll(true);

        mTextView->setViewOffset(MyGUI::IntPoint(0, 0));

        setTakeButtonShow(showTakeButton);

        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCloseButton);

        buildAccessibility();
        mA11y.activate();
    }

    void ScrollWindow::onKeyButtonPressed(MyGUI::Widget* /*sender*/, MyGUI::KeyCode key, MyGUI::Char character)
    {
        int scroll = 0;
        if (key == MyGUI::KeyCode::ArrowUp)
            scroll = 40;
        else if (key == MyGUI::KeyCode::ArrowDown)
            scroll = -40;

        if (scroll != 0)
            mTextView->setViewOffset(mTextView->getViewOffset() + MyGUI::IntPoint(0, scroll));
    }

    void ScrollWindow::setTakeButtonShow(bool show)
    {
        mTakeButtonShow = show;
        mTakeButton->setVisible(mTakeButtonShow && mTakeButtonAllowed);
    }

    void ScrollWindow::setInventoryAllowed(bool allowed)
    {
        mTakeButtonAllowed = allowed;
        mTakeButton->setVisible(mTakeButtonShow && mTakeButtonAllowed);
    }

    void ScrollWindow::onCloseButtonClicked(MyGUI::Widget* /*sender*/)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Scroll);
    }

    void ScrollWindow::onTakeButtonClicked(MyGUI::Widget* /*sender*/)
    {
        MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Item Book Up"));

        MWWorld::ActionTake take(mScroll);
        take.execute(MWMechanics::getPlayer());

        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Scroll);
    }

    void ScrollWindow::onClose()
    {
        mA11y.deactivate();
        if (Settings::gui().mControllerMenus)
            MWBase::Environment::get().getInputManager()->setGamepadGuiCursorEnabled(true);
        BookWindowBase::onClose();
    }

    void ScrollWindow::onFrame(float duration)
    {
        mA11y.onFrame(duration);
    }

    void ScrollWindow::buildAccessibility()
    {
        mA11y.clear();

        // Extract the body text via the engine's own parser and register one
        // navigable (widget-less) option per paragraph. See BookWindow.
        const std::string* text;
        if (mScroll.getType() == ESM::REC_BOOK)
            text = &mScroll.get<ESM::Book>()->mBase->mText;
        else
            text = &mScroll.get<ESM4::Book>()->mBase->mText;
        const bool shrinkTextAtLastTag = mScroll.getType() == ESM::REC_BOOK;

        std::vector<std::string> paragraphs = A11y::bookMarkupToParagraphs(*text, shrinkTextAtLastTag);

        if (paragraphs.empty())
            mA11y.add({ .widget = nullptr, .label = "This scroll is blank." });
        else
            for (std::string& para : paragraphs)
                mA11y.add({ .widget = nullptr, .label = std::move(para) });

        if (mTakeButton->getVisible())
            mA11y.add({ .widget = mTakeButton, .label = "#{sTake}",
                .activate = [this] { onTakeButtonClicked(mTakeButton); } });
        mA11y.add({ .widget = mCloseButton, .label = "#{sClose}",
            .activate = [this] { onCloseButtonClicked(mCloseButton); } });
    }

    ControllerButtons* ScrollWindow::getControllerButtons()
    {
        if (mTakeButton->getVisible())
            mControllerButtons.mA = "#{Interface:Take}";
        else
            mControllerButtons.mA.clear();
        return &mControllerButtons;
    }

    bool ScrollWindow::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_A)
        {
            if (mTakeButton->getVisible())
                onTakeButtonClicked(mTakeButton);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_B)
            onCloseButtonClicked(mCloseButton);
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_UP || arg.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
            return false; // Fall through to keyboard

        return true;
    }
}
