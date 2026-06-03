#include "bookwindow.hpp"

#include <MyGUI_InputManager.h>
#include <MyGUI_TextBox.h>

#include <components/esm3/loadbook.hpp>
#include <components/esm4/loadbook.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwmechanics/actorutil.hpp"

#include "../mwworld/actiontake.hpp"
#include "../mwworld/class.hpp"

#include "accessibility/booktext.hpp"
#include "accessibility/speech.hpp"

#include "formatting.hpp"

namespace MWGui
{

    BookWindow::BookWindow()
        : BookWindowBase("openmw_book.layout")
        , mCurrentPage(0)
        , mTakeButtonShow(true)
        , mTakeButtonAllowed(true)
    {
        getWidget(mCloseButton, "CloseButton");
        mCloseButton->eventMouseButtonClick += MyGUI::newDelegate(this, &BookWindow::onCloseButtonClicked);

        getWidget(mTakeButton, "TakeButton");
        mTakeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &BookWindow::onTakeButtonClicked);

        getWidget(mNextPageButton, "NextPageBTN");
        mNextPageButton->eventMouseButtonClick += MyGUI::newDelegate(this, &BookWindow::onNextPageButtonClicked);

        getWidget(mPrevPageButton, "PrevPageBTN");
        mPrevPageButton->eventMouseButtonClick += MyGUI::newDelegate(this, &BookWindow::onPrevPageButtonClicked);

        getWidget(mLeftPageNumber, "LeftPageNumber");
        getWidget(mRightPageNumber, "RightPageNumber");

        getWidget(mLeftPage, "LeftPage");
        getWidget(mRightPage, "RightPage");

        adjustButton("CloseButton");
        adjustButton("TakeButton");
        adjustButton("PrevPageBTN");
        float scale = adjustButton("NextPageBTN");

        mLeftPage->setNeedMouseFocus(true);
        mLeftPage->eventMouseWheel += MyGUI::newDelegate(this, &BookWindow::onMouseWheel);
        mRightPage->setNeedMouseFocus(true);
        mRightPage->eventMouseWheel += MyGUI::newDelegate(this, &BookWindow::onMouseWheel);

        mNextPageButton->eventKeyButtonPressed += MyGUI::newDelegate(this, &BookWindow::onKeyButtonPressed);
        mPrevPageButton->eventKeyButtonPressed += MyGUI::newDelegate(this, &BookWindow::onKeyButtonPressed);
        mTakeButton->eventKeyButtonPressed += MyGUI::newDelegate(this, &BookWindow::onKeyButtonPressed);
        mCloseButton->eventKeyButtonPressed += MyGUI::newDelegate(this, &BookWindow::onKeyButtonPressed);

        if (mNextPageButton->getSize().width == 64)
        {
            // english button has a 7 pixel wide strip of garbage on its right edge
            mNextPageButton->setSize(64 - 7, mNextPageButton->getSize().height);
            mNextPageButton->setImageCoord(MyGUI::IntCoord(
                0, 0, static_cast<int>((64 - 7) * scale), static_cast<int>(mNextPageButton->getSize().height * scale)));
        }

        mControllerButtons.mL1 = "#{Interface:Prev}";
        mControllerButtons.mR1 = "#{Interface:Next}";
        mControllerButtons.mB = "#{Interface:Close}";

        // Screen-reader setup: an invisible anchor holds key focus while the
        // A11y::Screen tracks the current line/button internally, so the
        // book's page widgets and image buttons never eat our arrow keys.
        mA11yAnchor = mMainWidget->createWidget<MyGUI::Widget>(
            {}, MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
        mA11yAnchor->setNeedKeyFocus(true);
        mA11y.setVirtualFocus(mA11yAnchor);

        center();
    }

    void BookWindow::onMouseWheel(MyGUI::Widget* /*sender*/, int rel)
    {
        if (rel < 0)
            nextPage();
        else
            prevPage();
    }

    void BookWindow::clearPages()
    {
        mPages.clear();
    }

    void BookWindow::setPtr(const MWWorld::Ptr& book)
    {
        if (book.isEmpty() || (book.getType() != ESM::REC_BOOK && book.getType() != ESM::REC_BOOK4))
            throw std::runtime_error("Invalid argument in BookWindow::setPtr");
        mBook = book;

        MWWorld::Ptr player = MWMechanics::getPlayer();
        bool showTakeButton = book.getContainerStore() != &player.getClass().getContainerStore(player);

        clearPages();
        mCurrentPage = 0;

        const std::string* text;
        if (book.getType() == ESM::REC_BOOK)
            text = &book.get<ESM::Book>()->mBase->mText;
        else
            text = &book.get<ESM4::Book>()->mBase->mText;
        bool shrinkTextAtLastTag = book.getType() == ESM::REC_BOOK;

        Formatting::BookFormatter formatter;
        mPages = formatter.markupToWidget(mLeftPage, *text, shrinkTextAtLastTag);
        formatter.markupToWidget(mRightPage, *text, shrinkTextAtLastTag);

        updatePages();

        setTakeButtonShow(showTakeButton);

        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCloseButton);

        // (Re)build the screen-reader option list for this book and take over
        // input. setPtr() runs after onOpen(), so this is where the text is
        // available; activate() pins focus to our anchor.
        buildAccessibility();
        mA11y.activate();
    }

    void BookWindow::setTakeButtonShow(bool show)
    {
        mTakeButtonShow = show;
        mTakeButton->setVisible(mTakeButtonShow && mTakeButtonAllowed);
    }

    void BookWindow::onKeyButtonPressed(MyGUI::Widget* /*sender*/, MyGUI::KeyCode key, MyGUI::Char character)
    {
        if (key == MyGUI::KeyCode::ArrowUp)
            prevPage();
        else if (key == MyGUI::KeyCode::ArrowDown)
            nextPage();
    }

    void BookWindow::setInventoryAllowed(bool allowed)
    {
        mTakeButtonAllowed = allowed;
        mTakeButton->setVisible(mTakeButtonShow && mTakeButtonAllowed);
    }

    void BookWindow::onCloseButtonClicked(MyGUI::Widget* /*sender*/)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Book);
    }

    void BookWindow::onTakeButtonClicked(MyGUI::Widget* /*sender*/)
    {
        MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Item Book Up"));

        MWWorld::ActionTake take(mBook);
        take.execute(MWMechanics::getPlayer());

        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Book);
    }

    void BookWindow::onNextPageButtonClicked(MyGUI::Widget* /*sender*/)
    {
        nextPage();
    }

    void BookWindow::onPrevPageButtonClicked(MyGUI::Widget* /*sender*/)
    {
        prevPage();
    }

    void BookWindow::updatePages()
    {
        mLeftPageNumber->setCaption(MyGUI::utility::toString(mCurrentPage * 2 + 1));
        mRightPageNumber->setCaption(MyGUI::utility::toString(mCurrentPage * 2 + 2));

        MyGUI::Widget* focus = MyGUI::InputManager::getInstance().getKeyFocusWidget();
        bool nextPageVisible = (mCurrentPage + 1) * 2 < mPages.size();
        mNextPageButton->setVisible(nextPageVisible);
        bool prevPageVisible = mCurrentPage != 0;
        mPrevPageButton->setVisible(prevPageVisible);

        if (focus == mNextPageButton && !nextPageVisible && prevPageVisible)
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mPrevPageButton);
        else if (focus == mPrevPageButton && !prevPageVisible && nextPageVisible)
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mNextPageButton);

        if (mPages.empty())
            return;

        MyGUI::Widget* paper;

        paper = mLeftPage->getChildAt(0);
        paper->setCoord(paper->getPosition().left, -mPages[mCurrentPage * 2].first, paper->getWidth(),
            mPages[mCurrentPage * 2].second);

        paper = mRightPage->getChildAt(0);
        if ((mCurrentPage + 1) * 2 <= mPages.size())
        {
            paper->setCoord(paper->getPosition().left, -mPages[mCurrentPage * 2 + 1].first, paper->getWidth(),
                mPages[mCurrentPage * 2 + 1].second);
            paper->setVisible(true);
        }
        else
        {
            paper->setVisible(false);
        }
    }

    void BookWindow::nextPage()
    {
        if ((mCurrentPage + 1) * 2 < mPages.size())
        {
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("book page2"));

            ++mCurrentPage;

            updatePages();
        }
    }
    void BookWindow::prevPage()
    {
        if (mCurrentPage > 0)
        {
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("book page"));

            --mCurrentPage;

            updatePages();
        }
    }

    ControllerButtons* BookWindow::getControllerButtons()
    {
        if (mTakeButton->getVisible())
            mControllerButtons.mA = "#{Interface:Take}";
        else
            mControllerButtons.mA.clear();
        return &mControllerButtons;
    }

    bool BookWindow::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_A)
        {
            if (mTakeButton->getVisible())
                onTakeButtonClicked(mTakeButton);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_B)
            onCloseButtonClicked(mCloseButton);
        else if (arg.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
            prevPage();
        else if (arg.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
            nextPage();

        return true;
    }

    void BookWindow::buildAccessibility()
    {
        mA11y.clear();

        // Extract the body text as plain paragraphs via the engine's own
        // parser, and register one navigable (widget-less) option per
        // paragraph. Up/Down read consecutive paragraphs; arrowing back
        // re-reads one. This mirrors how a sighted reader scans the page.
        const std::string* text;
        if (mBook.getType() == ESM::REC_BOOK)
            text = &mBook.get<ESM::Book>()->mBase->mText;
        else
            text = &mBook.get<ESM4::Book>()->mBase->mText;
        const bool shrinkTextAtLastTag = mBook.getType() == ESM::REC_BOOK;

        std::vector<std::string> paragraphs = A11y::bookMarkupToParagraphs(*text, shrinkTextAtLastTag);

        if (paragraphs.empty())
            mA11y.add({ .widget = nullptr, .label = "This book is blank." });
        else
            for (std::string& para : paragraphs)
                mA11y.add({ .widget = nullptr, .label = std::move(para) });

        // Always register the Take button. Its visibility is dynamic -- the
        // engine shows/hides it via setInventoryAllowed() as game state
        // changes (e.g. inventory becomes allowed only after character
        // creation), and that can happen *after* setPtr() built this list. We
        // therefore register it unconditionally and let the framework's
        // isUsable() check (which gates on getInheritedVisible()) include or
        // skip it live, so a Take button that appears while the window is open
        // becomes navigable without rebuilding. Close is always available.
        mA11y.add({ .widget = mTakeButton, .label = "#{sTake}",
            .activate = [this] { onTakeButtonClicked(mTakeButton); } });
        mA11y.add({ .widget = mCloseButton, .label = "#{sClose}",
            .activate = [this] { onCloseButtonClicked(mCloseButton); } });
    }

    void BookWindow::onClose()
    {
        mA11y.deactivate();
        BookWindowBase::onClose();
    }

    void BookWindow::onFrame(float duration)
    {
        mA11y.onFrame(duration);
    }
}
