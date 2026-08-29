#include "journalwindow.hpp"

#include <limits>
#include <set>
#include <stack>
#include <string>
#include <utility>

#include <MyGUI_Button.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_TextBox.h>

#include <components/misc/strings/algorithm.hpp>
#include <components/widgets/imagebutton.hpp>
#include <components/widgets/list.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/journal.hpp"
#include "../mwbase/windowmanager.hpp"

#include "accessibility/screen.hpp"
#include "accessibility/speech.hpp"
#include "bookpage.hpp"
#include "journalbooks.hpp"
#include "journalviewmodel.hpp"
#include "windowbase.hpp"

namespace
{
    static constexpr std::string_view OptionsOverlay = "OptionsOverlay";
    static constexpr std::string_view OptionsBTN = "OptionsBTN";
    static constexpr std::string_view PrevPageBTN = "PrevPageBTN";
    static constexpr std::string_view NextPageBTN = "NextPageBTN";
    static constexpr std::string_view CloseBTN = "CloseBTN";
    static constexpr std::string_view JournalBTN = "JournalBTN";
    static constexpr std::string_view TopicsBTN = "TopicsBTN";
    static constexpr std::string_view QuestsBTN = "QuestsBTN";
    static constexpr std::string_view CancelBTN = "CancelBTN";
    static constexpr std::string_view ShowAllBTN = "ShowAllBTN";
    static constexpr std::string_view ShowActiveBTN = "ShowActiveBTN";
    static constexpr std::string_view PageOneNum = "PageOneNum";
    static constexpr std::string_view PageTwoNum = "PageTwoNum";
    static constexpr std::string_view TopicsList = "TopicsList";
    static constexpr std::string_view QuestsList = "QuestsList";
    static constexpr std::string_view LeftBookPage = "LeftBookPage";
    static constexpr std::string_view RightBookPage = "RightBookPage";
    static constexpr std::string_view LeftTopicIndex = "LeftTopicIndex";
    static constexpr std::string_view CenterTopicIndex = "CenterTopicIndex";
    static constexpr std::string_view RightTopicIndex = "RightTopicIndex";

    struct JournalWindowImpl : MWGui::JournalBooks, MWGui::JournalWindow
    {
        struct DisplayState
        {
            size_t mPage;
            std::shared_ptr<MWGui::TypesetBook> mBook;
        };

        std::stack<DisplayState> mStates;
        std::shared_ptr<MWGui::TypesetBook> mTopicIndexBook;
        bool mQuestMode;
        bool mOptionsMode;
        bool mTopicsMode;
        bool mAllQuests;

        // Screen reader: which of the two visible pages (the spread) is current
        // for reading. false = left, true = right. Up/Down turn the whole
        // spread (resetting this to the left page); Left/Right move between the
        // spread's two pages, reading each. See a11yReadCurrentPage.
        bool mA11yRightPage = false;
        // Screen reader: the entry (journal section) the reader last stepped to
        // with Ctrl+Up/Down, so that repeated presses move relative to it. An
        // entry is one dated update / topic response; it may be shorter or longer
        // than a page. npos = not yet placed on an entry this reading session
        // (the next Ctrl+Up/Down derives a starting entry from the visible page).
        size_t mA11yEntry = std::numeric_limits<size_t>::max();
        // Screen reader: cursor into the topic links of whatever is currently
        // being read, cycled with Page Down / Page Up. npos = nothing selected
        // yet, so the next Page Down starts at the first link. The list itself is
        // derived from the context on each press rather than cached, so this is
        // reset whenever that context changes (page turn, entry step, new book).
        size_t mA11yLink = std::numeric_limits<size_t>::max();
        // Virtual-focus accessibility screen. The journal's page text isn't a
        // list of navigable options, so this registers NO elements: the anchor
        // merely captures keys (so the engine's button-focus navigation can't
        // hijack the arrows) which we route through the extra-key handler. It
        // also gives us the framework's built-in R reread for free. Mirrors the
        // ScrollWindow / BookWindow pattern.
        MWGui::A11y::Screen mA11y;
        MyGUI::Widget* mA11yAnchor = nullptr;

        // Which screen-reader view is active. Reading = the entry pages (no
        // navigable options; arrows turn pages via the extra-key handler). The
        // others register one widget-less option per list item and let the
        // framework navigate them; Enter opens the selected item.
        enum class A11yView
        {
            Reading,    // journal/topic/quest entry pages
            TopicIndex, // the A-Z letter grid
            TopicList,  // topics starting with a chosen letter
            Quests,     // quest names (active or all, per mAllQuests)
        };
        A11yView mA11yView = A11yView::Reading;

        template <typename T>
        T* getWidget(std::string_view name)
        {
            T* widget;
            WindowBase::getWidget(widget, name);
            return widget;
        }

        template <typename value_type>
        void setText(std::string_view name, value_type const& value)
        {
            getWidget<MyGUI::TextBox>(name)->setCaption(MyGUI::utility::toString(value));
        }

        void setVisible(std::string_view name, bool visible) { getWidget<MyGUI::Widget>(name)->setVisible(visible); }

        void adviseButtonClick(std::string_view name, void (JournalWindowImpl::*handler)(MyGUI::Widget*))
        {
            getWidget<MyGUI::Widget>(name)->eventMouseButtonClick += newDelegate(this, handler);
        }

        void adviseKeyPress(
            std::string_view name, void (JournalWindowImpl::*handler)(MyGUI::Widget*, MyGUI::KeyCode, MyGUI::Char))
        {
            getWidget<MyGUI::Widget>(name)->eventKeyButtonPressed += newDelegate(this, handler);
        }

        MWGui::BookPage* getPage(std::string_view name) { return getWidget<MWGui::BookPage>(name); }

        JournalWindowImpl(std::shared_ptr<MWGui::JournalViewModel> model, bool questList, ToUTF8::FromType encoding)
            : JournalBooks(std::move(model), encoding)
            , JournalWindow()
        {
            center();

            adviseButtonClick(OptionsBTN, &JournalWindowImpl::notifyOptions);
            adviseButtonClick(PrevPageBTN, &JournalWindowImpl::notifyPrevPage);
            adviseButtonClick(NextPageBTN, &JournalWindowImpl::notifyNextPage);
            adviseButtonClick(CloseBTN, &JournalWindowImpl::notifyClose);
            adviseButtonClick(JournalBTN, &JournalWindowImpl::notifyJournal);

            adviseButtonClick(TopicsBTN, &JournalWindowImpl::notifyTopics);
            adviseButtonClick(QuestsBTN, &JournalWindowImpl::notifyQuests);
            adviseButtonClick(CancelBTN, &JournalWindowImpl::notifyCancel);

            adviseButtonClick(ShowAllBTN, &JournalWindowImpl::notifyShowAll);
            adviseButtonClick(ShowActiveBTN, &JournalWindowImpl::notifyShowActive);

            adviseKeyPress(OptionsBTN, &JournalWindowImpl::notifyKeyPress);
            adviseKeyPress(PrevPageBTN, &JournalWindowImpl::notifyKeyPress);
            adviseKeyPress(NextPageBTN, &JournalWindowImpl::notifyKeyPress);
            adviseKeyPress(CloseBTN, &JournalWindowImpl::notifyKeyPress);
            adviseKeyPress(JournalBTN, &JournalWindowImpl::notifyKeyPress);

            Gui::MWList* list = getWidget<Gui::MWList>(QuestsList);
            list->eventItemSelected += MyGUI::newDelegate(this, &JournalWindowImpl::notifyQuestClicked);

            Gui::MWList* topicsList = getWidget<Gui::MWList>(TopicsList);
            topicsList->eventItemSelected += MyGUI::newDelegate(this, &JournalWindowImpl::notifyTopicSelected);

            {
                MWGui::BookPage::ClickCallback callback = [this](MWGui::TypesetBook::InteractiveId linkId) {
                    const MWDialogue::Topic& topic = *reinterpret_cast<const MWDialogue::Topic*>(linkId);
                    notifyTopicClicked(topic);
                };

                getPage(LeftBookPage)->adviseLinkClicked(callback);
                getPage(RightBookPage)->adviseLinkClicked(std::move(callback));

                getPage(LeftBookPage)->eventMouseWheel
                    += MyGUI::newDelegate(this, &JournalWindowImpl::notifyMouseWheel);
                getPage(RightBookPage)->eventMouseWheel
                    += MyGUI::newDelegate(this, &JournalWindowImpl::notifyMouseWheel);
            }

            {
                MWGui::BookPage::ClickCallback callback = [this](MWGui::TypesetBook::InteractiveId index) {
                    notifyIndexLinkClicked(static_cast<Utf8Stream::UnicodeChar>(index));
                };

                getPage(LeftTopicIndex)->adviseLinkClicked(callback);
                getPage(CenterTopicIndex)->adviseLinkClicked(callback);
                getPage(RightTopicIndex)->adviseLinkClicked(std::move(callback));
            }

            adjustButton(PrevPageBTN);
            float nextButtonScale = adjustButton(NextPageBTN);
            adjustButton(CloseBTN);
            adjustButton(CancelBTN);
            adjustButton(JournalBTN);

            Gui::ImageButton* optionsButton = getWidget<Gui::ImageButton>(OptionsBTN);
            Gui::ImageButton* showActiveButton = getWidget<Gui::ImageButton>(ShowActiveBTN);
            Gui::ImageButton* showAllButton = getWidget<Gui::ImageButton>(ShowAllBTN);
            Gui::ImageButton* questsButton = getWidget<Gui::ImageButton>(QuestsBTN);

            Gui::ImageButton* nextButton = getWidget<Gui::ImageButton>(NextPageBTN);
            if (nextButton->getSize().width == 64)
            {
                // english button has a 7 pixel wide strip of garbage on its right edge
                nextButton->setSize(64 - 7, nextButton->getSize().height);
                nextButton->setImageCoord(MyGUI::IntCoord(0, 0, static_cast<int>((64 - 7) * nextButtonScale),
                    static_cast<int>(nextButton->getSize().height * nextButtonScale)));
            }

            if (!questList)
            {
                // If tribunal is not installed (-> no options button), we still want the Topics button available,
                // so place it where the options button would have been
                Gui::ImageButton* topicsButton = getWidget<Gui::ImageButton>(TopicsBTN);
                topicsButton->detachFromWidget();
                topicsButton->attachToWidget(optionsButton->getParent());
                topicsButton->setPosition(optionsButton->getPosition());
                topicsButton->eventMouseButtonClick.clear();
                topicsButton->eventMouseButtonClick += MyGUI::newDelegate(this, &JournalWindowImpl::notifyOptions);

                optionsButton->setVisible(false);
                showActiveButton->setVisible(false);
                showAllButton->setVisible(false);
                questsButton->setVisible(false);

                adjustButton(TopicsBTN);
            }
            else
            {
                optionsButton->setImage("textures/tx_menubook_options.dds");
                showActiveButton->setImage("textures/tx_menubook_quests_active.dds");
                showAllButton->setImage("textures/tx_menubook_quests_all.dds");
                questsButton->setImage("textures/tx_menubook_quests.dds");

                adjustButton(ShowAllBTN);
                adjustButton(ShowActiveBTN);
                adjustButton(OptionsBTN);
                adjustButton(QuestsBTN);
                adjustButton(TopicsBTN);
                int topicsWidth = getWidget<MyGUI::Widget>(TopicsBTN)->getSize().width;
                int cancelLeft = getWidget<MyGUI::Widget>(CancelBTN)->getPosition().left;
                int cancelRight = getWidget<MyGUI::Widget>(CancelBTN)->getPosition().left
                    + getWidget<MyGUI::Widget>(CancelBTN)->getSize().width;

                getWidget<MyGUI::Widget>(QuestsBTN)->setPosition(
                    cancelRight, getWidget<MyGUI::Widget>(QuestsBTN)->getPosition().top);

                // Usually Topics, Quests, and Cancel buttons have the 64px width, so we can place the Topics left-up
                // from the Cancel button, and the Quests right-up from the Cancel button. But in some installations,
                // e.g. German one, the Topics button has the 128px width, so we should place it exactly left from the
                // Quests button.
                if (topicsWidth == 64)
                {
                    getWidget<MyGUI::Widget>(TopicsBTN)->setPosition(
                        cancelLeft - topicsWidth, getWidget<MyGUI::Widget>(TopicsBTN)->getPosition().top);
                }
                else
                {
                    int questLeft = getWidget<MyGUI::Widget>(QuestsBTN)->getPosition().left;
                    getWidget<MyGUI::Widget>(TopicsBTN)->setPosition(
                        questLeft - topicsWidth, getWidget<MyGUI::Widget>(TopicsBTN)->getPosition().top);
                }
            }

            // Latin = 26 (13 + 13)
            mIndexRowCount = 13;
            bool isRussian = (mEncoding == ToUTF8::WINDOWS_1251);
            if (isRussian) // Cyrillic is either (10 + 10 + 10) or (15 + 15)
                mIndexRowCount = MWGui::getCyrillicIndexPageCount();

            mControllerButtons.mA = "#{Interface:Select}";
            mControllerButtons.mX = "#{OMWEngine:JournalQuests}";
            mControllerButtons.mY = "#{Interface:Topics}";

            mQuestMode = false;
            mAllQuests = false;
            mOptionsMode = false;
            mTopicsMode = false;

            // Screen-reader setup: an invisible, non-Button anchor holds key
            // focus so the engine's spatial button navigation can't steal the
            // arrow keys (which made Left/Right flaky). We register no options;
            // all page navigation is handled in the extra-key handler, and R
            // reread is provided by the framework.
            mA11yAnchor = mMainWidget->createWidget<MyGUI::Widget>(
                {}, MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
            mA11yAnchor->setNeedKeyFocus(true);
            mA11y.setVirtualFocus(mA11yAnchor);
            mA11y.setExtraKeyHandler([this](MyGUI::KeyCode key) { return a11yHandleKey(key); });
        }

        // Extra-key handler for the screen reader. Returns true if the key was
        // consumed (so the framework's own navigation doesn't also act on it).
        //
        // In the Reading view the page text isn't a list of options, so we
        // drive paging ourselves: Up/Down turn a two-page spread, Left/Right
        // step single pages. T opens the Topics browser, Q the Quests list.
        //
        // In the list views (TopicIndex / TopicList / Quests) navigation and
        // Enter are handled by the framework (the items are real options); here
        // we only add Tab/Shift+Tab to switch between the Topics and Quests
        // browsers (mirroring the stats/options menus) and Escape/Backspace to
        // step back out.
        bool a11yHandleKey(MyGUI::KeyCode key)
        {
            if (mA11yView == A11yView::Reading)
            {
                const bool ctrl = MyGUI::InputManager::getInstance().isControlPressed();
                switch (key.getValue())
                {
                    case MyGUI::KeyCode::ArrowUp:
                        // Ctrl+Up/Down step one entry (journal section) at a time,
                        // reading the whole entry and turning the page to follow;
                        // plain Up/Down turn a two-page spread.
                        if (ctrl)
                            a11yStepEntry(-1);
                        else
                            a11ySpreadPrev(mA11yAnchor);
                        return true;
                    case MyGUI::KeyCode::ArrowDown:
                        if (ctrl)
                            a11yStepEntry(1);
                        else
                            a11ySpreadNext(mA11yAnchor);
                        return true;
                    case MyGUI::KeyCode::ArrowLeft:
                        a11yPageLeft(mA11yAnchor);
                        return true;
                    case MyGUI::KeyCode::ArrowRight:
                        a11yPageRight(mA11yAnchor);
                        return true;
                    case MyGUI::KeyCode::PageDown:
                        // Cycle the topics referenced by what you are reading.
                        // Vanilla highlights these words so a sighted player can
                        // click straight through to a topic instead of hunting the
                        // A-Z browser; Page Up/Down is the same ring the scanner
                        // uses, and nothing else in the journal binds it.
                        a11yStepLink(1);
                        return true;
                    case MyGUI::KeyCode::PageUp:
                        a11yStepLink(-1);
                        return true;
                    case MyGUI::KeyCode::Return:
                    case MyGUI::KeyCode::NumpadEnter:
                        // Enter opens the highlighted link, exactly as clicking it
                        // does. Only consume the key when a link is actually
                        // selected, so Enter keeps its usual meaning otherwise.
                        return a11yOpenSelectedLink();
                    case MyGUI::KeyCode::Backspace:
                        // Return to what we were reading before a topic was
                        // opened. Only consume the key when there is somewhere to
                        // go back to, so at the top level Backspace keeps whatever
                        // meaning the window otherwise gives it.
                        if (mStates.size() < 2)
                            return false;
                        a11yBackOut();
                        return true;
                    case MyGUI::KeyCode::T:
                        a11yOpenTopics();
                        return true;
                    case MyGUI::KeyCode::Q:
                        a11yOpenQuests(/*allQuests=*/false);
                        return true;
                    default:
                        return false;
                }
            }

            // List views.
            switch (key.getValue())
            {
                case MyGUI::KeyCode::Tab:
                {
                    // Tab / Shift+Tab cycle between the two browsers (Topics and
                    // Quests), like switching tabs in the stats/options menus.
                    const bool shift = MyGUI::InputManager::getInstance().isShiftPressed();
                    a11yCycleBrowser(shift ? -1 : 1);
                    return true;
                }
                case MyGUI::KeyCode::Backspace:
                    // Step back: a chosen letter's topic list returns to the
                    // index; the index / quest list returns to reading.
                    a11yBackOut();
                    return true;
                case MyGUI::KeyCode::Escape:
                    // Let a top-level Escape close the window as usual, but from
                    // a list view first step back to reading.
                    a11yBackOut();
                    return true;
                default:
                    return false;
            }
        }

        void onOpen() override
        {
            mModel->load();

            setBookMode();

            std::shared_ptr<MWGui::TypesetBook> journalBook;
            if (mModel->isEmpty())
                journalBook = createEmptyJournalBook();
            else
                journalBook = createJournalBook();

            pushBook(journalBook);

            // fast forward to the last page
            if (!mStates.empty())
            {
                size_t& page = mStates.top().mPage;
                page = mStates.top().mBook->pageCount() - 1;
                if (page % 2)
                    --page;
            }
            updateShowingPages();

            if (Settings::gui().mControllerMenus)
                setControllerFocusedQuest(0);

            // Screen reader: take input via the virtual anchor (no options
            // registered) so arrows reach our extra-key handler instead of the
            // engine's button navigation, then read the page we opened on (the
            // most recent entries). Up/Down turn spreads, Left/Right step pages.
            mA11y.activate();
            mA11yView = A11yView::Reading;
            mA11yRightPage = false;
            a11yReadCurrentPage();
        }

        void onClose() override
        {
            mA11y.deactivate();
            MWGui::A11y::clearReread();
            mModel->unload();

            getPage(LeftBookPage)->showPage({}, 0);
            getPage(RightBookPage)->showPage({}, 0);

            while (!mStates.empty())
                mStates.pop();

            mTopicIndexBook.reset();
        }

        void onFrame(float dt) override { mA11y.onFrame(dt); }

        void setVisible(bool newValue) override { WindowBase::setVisible(newValue); }

        void setBookMode()
        {
            mOptionsMode = false;
            mTopicsMode = false;
            setVisible(OptionsBTN, true);
            setVisible(OptionsOverlay, false);

            updateShowingPages();
            updateCloseJournalButton();

            MWBase::Environment::get().getWindowManager()->updateControllerButtonsOverlay();
        }

        void setOptionsMode()
        {
            mOptionsMode = true;
            mTopicsMode = false;

            setVisible(OptionsBTN, false);
            setVisible(OptionsOverlay, true);

            setVisible(PrevPageBTN, false);
            setVisible(NextPageBTN, false);
            setVisible(CloseBTN, false);
            setVisible(JournalBTN, false);

            setVisible(TopicsList, false);
            setVisible(QuestsList, mQuestMode);
            setVisible(LeftTopicIndex, !mQuestMode);
            setVisible(CenterTopicIndex, !mQuestMode);
            setVisible(RightTopicIndex, !mQuestMode);
            setVisible(ShowAllBTN, mQuestMode && !mAllQuests);
            setVisible(ShowActiveBTN, mQuestMode && mAllQuests);

            // TODO: figure out how to make "options" page overlay book page
            //       correctly, so that text may show underneath
            getPage(RightBookPage)->showPage({}, 0);

            // If in quest mode, ensure the quest list is updated
            if (mQuestMode)
                notifyQuests(getWidget<MyGUI::Widget>(QuestsList));
            else
                notifyTopics(getWidget<MyGUI::Widget>(TopicsList));

            MWBase::Environment::get().getWindowManager()->updateControllerButtonsOverlay();
        }

        void pushBook(std::shared_ptr<MWGui::TypesetBook>& book)
        {
            DisplayState bs;
            bs.mPage = 0;
            bs.mBook = book;
            mStates.push(bs);
            updateShowingPages();
            updateCloseJournalButton();
        }

        void replaceBook(std::shared_ptr<MWGui::TypesetBook>& book)
        {
            assert(!mStates.empty());
            mStates.top().mBook = book;
            mStates.top().mPage = 0;
            updateShowingPages();
        }

        void popBook()
        {
            mStates.pop();
            updateShowingPages();
            updateCloseJournalButton();
        }

        void updateCloseJournalButton()
        {
            setVisible(CloseBTN, mStates.size() < 2);
            setVisible(JournalBTN, mStates.size() >= 2);
            MWBase::Environment::get().getWindowManager()->updateControllerButtonsOverlay();
        }

        void updateShowingPages()
        {
            std::shared_ptr<MWGui::TypesetBook> book;
            size_t page;
            size_t relPages;

            if (!mStates.empty())
            {
                book = mStates.top().mBook;
                page = mStates.top().mPage;
                relPages = book->pageCount() - page;
            }
            else
            {
                page = 0;
                relPages = 0;
            }

            MyGUI::Widget* nextPageBtn = getWidget<MyGUI::Widget>(NextPageBTN);
            MyGUI::Widget* prevPageBtn = getWidget<MyGUI::Widget>(PrevPageBTN);

            MyGUI::Widget* focus = MyGUI::InputManager::getInstance().getKeyFocusWidget();
            bool nextPageVisible = relPages > 2;
            nextPageBtn->setVisible(nextPageVisible);
            bool prevPageVisible = page > 0;
            prevPageBtn->setVisible(prevPageVisible);

            if (focus == nextPageBtn && !nextPageVisible && prevPageVisible)
                MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(prevPageBtn);
            else if (focus == prevPageBtn && !prevPageVisible && nextPageVisible)
                MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(nextPageBtn);

            setVisible(PageOneNum, relPages > 0);
            setVisible(PageTwoNum, relPages > 1);

            if (relPages > 0)
            {
                getPage(LeftBookPage)->showPage(book, page + 0);
                getPage(RightBookPage)->showPage(std::move(book), page + 1);
            }
            else
            {
                getPage(LeftBookPage)->showPage({}, page + 0);
                getPage(RightBookPage)->showPage({}, page + 1);
            }

            setText(PageOneNum, page + 1);
            setText(PageTwoNum, page + 2);

            MWBase::Environment::get().getWindowManager()->updateControllerButtonsOverlay();
        }

        // --- Screen-reader page reading ----------------------------------
        // Speak the page currently selected for reading (left or right of the
        // visible spread), as a rereadable announcement so R repeats it. The
        // page number is spoken last, per the positional-info-at-the-end
        // convention. No-op outside book mode (the options / topic-index
        // screens aren't read here).
        void a11yReadCurrentPage()
        {
            // Reading by page (open, page-turn, or return from a list view)
            // drops the Ctrl+Up/Down entry anchor, so the next entry step
            // resyncs to whatever page is now visible. a11yReadEntry sets the
            // anchor itself and does not route through here.
            mA11yEntry = std::numeric_limits<size_t>::max();
            // The link list is derived from the context, so a changed context
            // invalidates the cursor into it: Page Down after a page turn must
            // start again at that page's first topic, not resume an index into a
            // list that no longer exists.
            mA11yLink = std::numeric_limits<size_t>::max();

            if (mOptionsMode || mStates.empty())
                return;
            const std::shared_ptr<MWGui::TypesetBook>& book = mStates.top().mBook;
            if (!book)
                return;
            const size_t pageCount = book->pageCount();
            const size_t page = mStates.top().mPage + (mA11yRightPage ? 1 : 0);
            if (page >= pageCount)
                return;

            std::string text = book->getPageText(page);
            if (text.empty())
                text = "Blank page";
            text += "\nPage ";
            text += std::to_string(page + 1);
            text += " of ";
            text += std::to_string(pageCount);
            a11yAppendLinkCount(text, book->getPageLinks(page).size());
            text += '.';
            MWGui::A11y::sayRereadable(text, /*interrupt=*/true);
            mA11yLink = std::numeric_limits<size_t>::max();
        }

        // The topic links belonging to what is currently being read. Context
        // follows the reader: once Ctrl+Up/Down has placed a cursor on an entry
        // the links are that whole entry's (so an entry spanning a page break
        // still offers all of them); otherwise they are the visible page's.
        std::vector<MWGui::TypesetBook::Link> a11yCurrentLinks() const
        {
            if (mOptionsMode || mStates.empty())
                return {};
            const std::shared_ptr<MWGui::TypesetBook>& book = mStates.top().mBook;
            if (!book)
                return {};
            if (mA11yEntry != std::numeric_limits<size_t>::max())
                return book->getSectionLinks(mA11yEntry);
            return book->getPageLinks(mStates.top().mPage + (mA11yRightPage ? 1 : 0));
        }

        // Cycle the topics referenced by the current page or entry. These are the
        // words vanilla highlights for a mouse click; offering them as a list is
        // the equivalent affordance, and it saves walking the A-Z browser to reach
        // a topic the text in front of you already names.
        void a11yStepLink(int delta)
        {
            const std::vector<MWGui::TypesetBook::Link> links = a11yCurrentLinks();
            if (links.empty())
            {
                MWGui::A11y::say("No topics referenced.", /*interrupt=*/true);
                return;
            }

            const size_t count = links.size();
            if (mA11yLink == std::numeric_limits<size_t>::max())
                mA11yLink = (delta < 0) ? count - 1 : 0;
            else
                mA11yLink = (mA11yLink + static_cast<size_t>(delta < 0 ? count - 1 : 1)) % count;

            std::string text = links[mA11yLink].mText;
            if (text.empty())
                text = "Unnamed topic";
            text += ", ";
            text += std::to_string(mA11yLink + 1);
            text += " of ";
            text += std::to_string(count);
            text += '.';
            MWGui::A11y::sayRereadable(text, /*interrupt=*/true);
        }

        // Open the selected link, dispatching exactly what a mouse click would so
        // speech and pixels cannot diverge. Returns false when no link is
        // selected, leaving Enter its usual meaning.
        bool a11yOpenSelectedLink()
        {
            if (mA11yLink == std::numeric_limits<size_t>::max())
                return false;
            const std::vector<MWGui::TypesetBook::Link> links = a11yCurrentLinks();
            if (mA11yLink >= links.size())
                return false;

            const MWDialogue::Topic& topic
                = *reinterpret_cast<const MWDialogue::Topic*>(links[mA11yLink].mId);
            notifyTopicClicked(topic);
            // notifyTopicClicked only swaps the book in; the reading state has to
            // be resynced and the new page spoken, exactly as opening a topic from
            // the browser does (a11yActivateTopic). Without this the book-page
            // sound plays and nothing is read.
            mA11yRightPage = false;
            a11yBuildView(/*announceHeader=*/false);
            a11yReadCurrentPage();
            return true;
        }

        // --- Screen-reader entry stepping (Ctrl+Up/Down) -----------------
        // Move one journal entry (a dated update or topic response) at a time,
        // reading the WHOLE entry -- even one that visually spans a page break --
        // and turning the visible page to follow. An entry is one typeset
        // section; sectionCount()/getSectionText()/pageForSection() expose them
        // independent of pagination. delta is +1 (next/newer) or -1 (previous).
        void a11yStepEntry(int delta)
        {
            if (mOptionsMode || mStates.empty())
                return;
            const std::shared_ptr<MWGui::TypesetBook>& book = mStates.top().mBook;
            if (!book)
                return;
            const size_t count = book->sectionCount();
            if (count == 0)
            {
                MWGui::A11y::say("No entries.", /*interrupt=*/true);
                return;
            }

            size_t target;
            if (mA11yEntry == std::numeric_limits<size_t>::max())
            {
                // First step this reading session (or after a manual page turn):
                // anchor on the entry at the top of the visible page and read it,
                // rather than skipping past it.
                const size_t visiblePage = mStates.top().mPage + (mA11yRightPage ? 1 : 0);
                target = book->sectionForPage(visiblePage);
            }
            else if (delta < 0)
            {
                if (mA11yEntry == 0)
                {
                    MWGui::A11y::say("No previous entries.", /*interrupt=*/true);
                    return;
                }
                target = mA11yEntry - 1;
            }
            else
            {
                if (mA11yEntry + 1 >= count)
                {
                    MWGui::A11y::say("No more entries.", /*interrupt=*/true);
                    return;
                }
                target = mA11yEntry + 1;
            }

            a11yReadEntry(target);
        }

        // Read one entry in full and turn the visible spread to the page it
        // begins on, keeping the on-screen pages (and the R reread) in sync. The
        // entry number is spoken last, per the positional-info-at-the-end
        // convention.
        void a11yReadEntry(size_t entry)
        {
            const std::shared_ptr<MWGui::TypesetBook>& book = mStates.top().mBook;
            const size_t count = book->sectionCount();

            const size_t page = book->pageForSection(entry);
            const size_t spreadBase = page - (page % 2);
            if (mStates.top().mPage != spreadBase)
            {
                mStates.top().mPage = spreadBase;
                updateShowingPages();
            }
            mA11yRightPage = (page % 2) != 0;
            mA11yEntry = entry;

            std::string text = book->getSectionText(entry);
            if (text.empty())
                text = "Blank entry";
            text += "\nEntry ";
            text += std::to_string(entry + 1);
            text += " of ";
            text += std::to_string(count);
            a11yAppendLinkCount(text, book->getSectionLinks(entry).size());
            text += '.';
            MWGui::A11y::sayRereadable(text, /*interrupt=*/true);
            // Stepping to a new entry changes the link context, so the Page
            // Down/Up cursor starts again at this entry's first topic.
            mA11yLink = std::numeric_limits<size_t>::max();
        }

        // Tell the reader that what they just heard cross-references topics, so
        // they know Page Down has something to offer without having to try it.
        // Appended after the page/entry number, per the convention that positional
        // and summary info goes at the end. Silent when there are none: an absent
        // count is the common case and saying "0 topics" every time would be noise
        // on every page turn. Kept to the bare word "topics" because this rides on
        // the end of every page and entry announcement, where a longer phrase is
        // paid for on every single turn.
        static void a11yAppendLinkCount(std::string& text, size_t links)
        {
            if (links == 0)
                return;
            text += ", ";
            text += std::to_string(links);
            text += (links == 1) ? " topic" : " topics";
        }

        // Up/Down turn a whole two-page spread (the native behaviour), landing
        // on its left page; Left/Right step one page at a time within and
        // across spreads. Each reads the page it lands on. A manual page turn
        // drops the entry-step anchor so the next Ctrl+Up/Down resyncs to the
        // page the user turned to.
        void a11ySpreadNext(MyGUI::Widget* sender)
        {
            notifyNextPage(sender);
            mA11yRightPage = false;
            a11yReadCurrentPage();
        }

        void a11ySpreadPrev(MyGUI::Widget* sender)
        {
            notifyPrevPage(sender);
            mA11yRightPage = false;
            a11yReadCurrentPage();
        }

        void a11yPageRight(MyGUI::Widget* sender)
        {
            if (mOptionsMode || mStates.empty())
                return;
            const std::shared_ptr<MWGui::TypesetBook>& book = mStates.top().mBook;
            const size_t base = mStates.top().mPage;
            const size_t pageCount = book ? book->pageCount() : 0;
            if (!mA11yRightPage && base + 1 < pageCount)
                mA11yRightPage = true; // right page of the current spread
            else if (mA11yRightPage && base + 2 < pageCount)
            {
                notifyNextPage(sender); // first page of the next spread
                mA11yRightPage = false;
            }
            // else: already on the last page -- re-read it.
            a11yReadCurrentPage();
        }

        void a11yPageLeft(MyGUI::Widget* sender)
        {
            if (mOptionsMode || mStates.empty())
                return;
            const size_t base = mStates.top().mPage;
            if (mA11yRightPage)
                mA11yRightPage = false; // left page of the current spread
            else if (base >= 2)
            {
                notifyPrevPage(sender); // second page of the previous spread
                mA11yRightPage = true;
            }
            // else: already on the very first page -- re-read it.
            a11yReadCurrentPage();
        }

        void notifyKeyPress(MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char character)
        {
            if (key == MyGUI::KeyCode::ArrowUp)
                a11ySpreadPrev(sender);
            else if (key == MyGUI::KeyCode::ArrowDown)
                a11ySpreadNext(sender);
            else if (key == MyGUI::KeyCode::ArrowLeft)
                a11yPageLeft(sender);
            else if (key == MyGUI::KeyCode::ArrowRight)
                a11yPageRight(sender);
        }

        // --- Screen-reader list views (Topics / Quests) ------------------
        // Build the framework option list for the current mA11yView and
        // announce a short header so the user knows which browser they're in.
        // The Reading view registers no options (paging is handled separately).
        void a11yBuildView(bool announceHeader)
        {
            mA11y.clear();

            switch (mA11yView)
            {
                case A11yView::Reading:
                    return;

                case A11yView::TopicIndex:
                {
                    // The A-Z letter grid. Only offer letters that actually have
                    // topics, so the user isn't wading through dead entries.
                    const bool isRussian = (mEncoding == ToUTF8::WINDOWS_1251);
                    if (isRussian)
                    {
                        // Cyrillic capital A = U+0410; 32 letters, skipping the
                        // two (indices 26 and 28) that can't start a word.
                        for (int i = 0; i < 32; ++i)
                        {
                            if (i == 26 || i == 28)
                                continue;
                            a11yAddIndexLetter(static_cast<Utf8Stream::UnicodeChar>(0x0410 + i));
                        }
                    }
                    else
                    {
                        for (char c = 'A'; c <= 'Z'; ++c)
                            a11yAddIndexLetter(static_cast<Utf8Stream::UnicodeChar>(c));
                    }
                    if (announceHeader)
                        MWGui::A11y::say("Topics. Choose a letter.", /*interrupt=*/true);
                    break;
                }

                case A11yView::TopicList:
                {
                    Gui::MWList* list = getWidget<Gui::MWList>(TopicsList);
                    for (size_t i = 0; i < list->getItemCount(); ++i)
                    {
                        const std::string name = list->getItemNameAt(i);
                        if (name.empty())
                            continue;
                        mA11y.add({ .widget = nullptr, .label = name,
                            .activate = [this, name] { a11yActivateTopic(name); } });
                    }
                    if (announceHeader)
                        MWGui::A11y::say("Topics.", /*interrupt=*/true);
                    break;
                }

                case A11yView::Quests:
                {
                    // Collect the names of finished quests so we can mark them
                    // "completed" (the native UI greys them out instead). Only
                    // relevant in the "all quests" tab -- the active tab excludes
                    // finished quests entirely.
                    std::set<std::string, Misc::StringUtils::CiComp> finished;
                    if (mAllQuests)
                    {
                        mModel->visitQuestNames(/*activeOnly=*/false,
                            [&](std::string_view name, bool isFinished) {
                                if (isFinished)
                                    finished.emplace(name);
                            });
                    }

                    Gui::MWList* list = getWidget<Gui::MWList>(QuestsList);
                    for (size_t i = 0; i < list->getItemCount(); ++i)
                    {
                        const std::string name = list->getItemNameAt(i);
                        if (name.empty())
                            continue;
                        std::string label = name;
                        if (finished.find(name) != finished.end())
                            label += ", completed";
                        mA11y.add({ .widget = nullptr, .label = std::move(label),
                            .activate = [this, name] { a11yActivateQuest(name); } });
                    }
                    if (announceHeader)
                        MWGui::A11y::say(mAllQuests ? "All quests." : "Active quests.", /*interrupt=*/true);
                    break;
                }
            }

            // Land on the first item, announcing it after the header.
            mA11y.focusFirst(/*announce=*/true);
        }

        // Register one letter of the topic index, but only if at least one
        // topic starts with it. The label is just the letter; activation drills
        // into that letter's topic list.
        void a11yAddIndexLetter(Utf8Stream::UnicodeChar ch)
        {
            bool any = false;
            mModel->visitTopicNamesStartingWith(ch, [&](std::string_view) { any = true; });
            if (!any)
                return;

            // Build the spoken label for the letter (UTF-8 encode the codepoint).
            std::string label;
            if (ch < 0x80)
                label = std::string(1, static_cast<char>(ch));
            else if (ch < 0x800)
            {
                label += static_cast<char>(0xC0 | (ch >> 6));
                label += static_cast<char>(0x80 | (ch & 0x3F));
            }
            mA11y.add({ .widget = nullptr, .label = label,
                .activate = [this, ch] { a11yChooseIndexLetter(ch); } });
        }

        // Open the Topics browser (the A-Z index) from the reading view.
        void a11yOpenTopics()
        {
            mQuestMode = false;
            setOptionsMode();
            if (!mTopicIndexBook)
                mTopicIndexBook = createTopicIndexBook();
            mA11yView = A11yView::TopicIndex;
            a11yBuildView(/*announceHeader=*/true);
        }

        // Open the Quests list from the reading view. \p allQuests selects the
        // "all quests" tab (including completed) vs the "active quests" tab.
        void a11yOpenQuests(bool allQuests)
        {
            mQuestMode = true;
            mAllQuests = allQuests;
            setOptionsMode(); // populates the QuestsList per mAllQuests
            mA11yView = A11yView::Quests;
            a11yBuildView(/*announceHeader=*/true);
        }

        // Drill from the index into the topics that start with the chosen
        // letter (mirrors the native notifyIndexLinkClicked path).
        void a11yChooseIndexLetter(Utf8Stream::UnicodeChar ch)
        {
            notifyIndexLinkClicked(ch);
            mA11yView = A11yView::TopicList;
            a11yBuildView(/*announceHeader=*/true);
        }

        void a11yActivateTopic(const std::string& name)
        {
            notifyTopicSelected(name, 0);
            // notifyTopicClicked pushed/replaced the topic book and left options
            // mode; return to reading so the topic's entries can be read.
            mA11yView = A11yView::Reading;
            mA11yRightPage = false;
            a11yBuildView(/*announceHeader=*/false);
            a11yReadCurrentPage();
        }

        void a11yActivateQuest(const std::string& name)
        {
            notifyQuestClicked(name, 0);
            mA11yView = A11yView::Reading;
            mA11yRightPage = false;
            a11yBuildView(/*announceHeader=*/false);
            a11yReadCurrentPage();
        }

        // Tab / Shift+Tab cycle between three tabs, like the stats/options
        // menus: Topics -> Active Quests -> All Quests -> (wrap) Topics.
        // delta>0 = next, <0 = previous. From a drilled-in topic list the
        // current tab is treated as Topics (Tab leaves the sublist).
        void a11yCycleBrowser(int delta)
        {
            // Identify the current tab index: 0 Topics, 1 Active Quests, 2 All.
            int tab;
            if (mA11yView == A11yView::Quests)
                tab = mAllQuests ? 2 : 1;
            else
                tab = 0; // TopicIndex or TopicList

            tab = (tab + (delta > 0 ? 1 : 2)) % 3; // +1 or -1 (mod 3)

            switch (tab)
            {
                case 0:
                    a11yOpenTopics();
                    break;
                case 1:
                    a11yOpenQuests(/*allQuests=*/false);
                    break;
                case 2:
                    a11yOpenQuests(/*allQuests=*/true);
                    break;
            }
        }

        // Step back one level: a letter's topic list returns to the index;
        // the index or the quest list returns to the reading view.
        void a11yBackOut()
        {
            switch (mA11yView)
            {
                case A11yView::TopicList:
                    // Back to the letter index.
                    notifyTopics(getWidget<MyGUI::Widget>(TopicsList));
                    mA11yView = A11yView::TopicIndex;
                    a11yBuildView(/*announceHeader=*/true);
                    break;
                case A11yView::TopicIndex:
                case A11yView::Quests:
                    // Leave the options overlay and return to the entry pages.
                    setBookMode();
                    mA11yView = A11yView::Reading;
                    a11yReadCurrentPage();
                    break;
                case A11yView::Reading:
                    // A topic opened from a link (or the browser) was PUSHED onto
                    // the book stack, so reading is not necessarily the bottom
                    // level: step back to whatever we were reading before. Only
                    // when the stack is already at its base is there nothing to
                    // go back to, and Backspace should do nothing rather than
                    // close the journal out from under the reader.
                    if (mStates.size() > 1)
                    {
                        popBook();
                        mA11yRightPage = false;
                        a11yReadCurrentPage();
                    }
                    break;
            }
        }

        void notifyTopicClicked(const MWDialogue::Topic& topic)
        {
            std::shared_ptr<MWGui::TypesetBook> topicBook = createTopicBook(topic);

            if (mStates.size() > 1)
                replaceBook(topicBook);
            else
                pushBook(topicBook);

            setVisible(OptionsOverlay, false);
            setVisible(OptionsBTN, true);
            setVisible(JournalBTN, true);

            mOptionsMode = false;
            mTopicsMode = false;

            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("book page"));
            MWBase::Environment::get().getWindowManager()->updateControllerButtonsOverlay();
        }

        void notifyTopicSelected(const std::string& topicIdString, int id)
        {
            ESM::RefId topic = ESM::RefId::stringRefId(topicIdString);
            const MWBase::Journal* journal = MWBase::Environment::get().getJournal();
            const auto it = journal->getTopics().find(topic);
            if (it != journal->getTopics().end())
                notifyTopicClicked(it->second);
        }

        void notifyQuestClicked(const std::string& name, int id)
        {
            std::shared_ptr<MWGui::TypesetBook> book = createQuestBook(name);

            if (mStates.size() > 1)
                replaceBook(book);
            else
                pushBook(book);

            setVisible(OptionsOverlay, false);
            setVisible(OptionsBTN, true);
            setVisible(JournalBTN, true);

            mOptionsMode = false;

            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("book page"));
            MWBase::Environment::get().getWindowManager()->updateControllerButtonsOverlay();
        }

        void notifyOptions(MyGUI::Widget* /*sender*/)
        {
            setOptionsMode();

            if (!mTopicIndexBook)
                mTopicIndexBook = createTopicIndexBook();

            if (mIndexPagesCount == 3)
            {
                getPage(LeftTopicIndex)->showPage(mTopicIndexBook, 0);
                getPage(CenterTopicIndex)->showPage(mTopicIndexBook, 1);
                getPage(RightTopicIndex)->showPage(mTopicIndexBook, 2);
            }
            else
            {
                getPage(LeftTopicIndex)->showPage(mTopicIndexBook, 0);
                getPage(RightTopicIndex)->showPage(mTopicIndexBook, 1);
            }

            if (Settings::gui().mControllerMenus)
                setIndexControllerFocus(true);
        }

        void notifyJournal(MyGUI::Widget* /*sender*/)
        {
            assert(mStates.size() > 1);
            popBook();

            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("book page"));
            MWBase::Environment::get().getWindowManager()->updateControllerButtonsOverlay();
        }

        void addControllerButtons(Gui::MWList* list, size_t selectedIndex)
        {
            mButtons.clear();
            for (size_t i = 0; i < list->getItemCount(); i++)
            {
                MyGUI::Button* listItem = list->getItemWidget(list->getItemNameAt(i));
                if (listItem)
                {
                    listItem->setTextColour(
                        mButtons.size() == selectedIndex ? MWGui::journalHeaderColour : MyGUI::Colour::Black);
                    mButtons.push_back(listItem);
                }
            }
        }

        void notifyIndexLinkClicked(Utf8Stream::UnicodeChar index)
        {
            setVisible(LeftTopicIndex, false);
            setVisible(CenterTopicIndex, false);
            setVisible(RightTopicIndex, false);
            setVisible(TopicsList, true);

            mTopicsMode = true;

            Gui::MWList* list = getWidget<Gui::MWList>(TopicsList);
            list->clear();

            AddNamesToList add(list);

            mModel->visitTopicNamesStartingWith(index, add);

            list->adjustSize();

            if (Settings::gui().mControllerMenus)
            {
                setControllerFocusedQuest(0);
                addControllerButtons(list, mSelectedQuest);
            }

            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("book page"));
            MWBase::Environment::get().getWindowManager()->updateControllerButtonsOverlay();
        }

        void notifyTopics(MyGUI::Widget* /*sender*/)
        {
            mQuestMode = false;
            mTopicsMode = false;
            setVisible(LeftTopicIndex, true);
            setVisible(CenterTopicIndex, true);
            setVisible(RightTopicIndex, true);
            setVisible(TopicsList, false);
            setVisible(QuestsList, false);
            setVisible(ShowAllBTN, false);
            setVisible(ShowActiveBTN, false);

            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("book page"));
            MWBase::Environment::get().getWindowManager()->updateControllerButtonsOverlay();
        }

        struct AddNamesToList
        {
            AddNamesToList(Gui::MWList* list)
                : mList(list)
            {
            }

            Gui::MWList* mList;
            void operator()(std::string_view name, bool finished = false) { mList->addItem(name); }
        };
        struct SetNamesInactive
        {
            SetNamesInactive(Gui::MWList* list)
                : mList(list)
            {
            }

            Gui::MWList* mList;
            void operator()(std::string_view name, bool finished)
            {
                if (finished)
                {
                    mList->getItemWidget(name)->setStateSelected(true);
                }
            }
        };

        void notifyQuests(MyGUI::Widget* /*sender*/)
        {
            mQuestMode = true;

            setVisible(LeftTopicIndex, false);
            setVisible(CenterTopicIndex, false);
            setVisible(RightTopicIndex, false);
            setVisible(TopicsList, false);
            setVisible(QuestsList, true);
            setVisible(ShowAllBTN, !mAllQuests);
            setVisible(ShowActiveBTN, mAllQuests);

            Gui::MWList* list = getWidget<Gui::MWList>(QuestsList);
            list->clear();

            AddNamesToList add(list);

            mModel->visitQuestNames(!mAllQuests, add);

            list->sort();
            list->adjustSize();

            if (Settings::gui().mControllerMenus)
            {
                addControllerButtons(list, mSelectedQuest);
                setControllerFocusedQuest(std::min(mSelectedQuest, mButtons.size()));
            }

            if (mAllQuests)
            {
                SetNamesInactive setInactive(list);
                mModel->visitQuestNames(false, setInactive);
            }

            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("book page"));
            MWBase::Environment::get().getWindowManager()->updateControllerButtonsOverlay();
        }

        void notifyShowAll(MyGUI::Widget* sender)
        {
            mAllQuests = true;
            notifyQuests(sender);
        }

        void notifyShowActive(MyGUI::Widget* sender)
        {
            mAllQuests = false;
            notifyQuests(sender);
        }

        void notifyCancel(MyGUI::Widget* sender)
        {
            if (mTopicsMode)
            {
                notifyTopics(sender);
            }
            else
            {
                setBookMode();
                MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("book page"));
            }
        }

        void notifyClose(MyGUI::Widget* /*sender*/)
        {
            MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
            winMgr->playSound(ESM::RefId::stringRefId("book close"));
            winMgr->popGuiMode();
        }

        void notifyMouseWheel(MyGUI::Widget* sender, int rel)
        {
            if (rel < 0)
                notifyNextPage(sender);
            else
                notifyPrevPage(sender);
        }

        void notifyNextPage(MyGUI::Widget* /*sender*/)
        {
            if (mOptionsMode)
                return;
            if (!mStates.empty())
            {
                size_t& page = mStates.top().mPage;
                std::shared_ptr<MWGui::TypesetBook> book = mStates.top().mBook;

                if (page + 2 < book->pageCount())
                {
                    MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("book page"));

                    page += 2;
                    updateShowingPages();
                }
            }
        }

        void notifyPrevPage(MyGUI::Widget* /*sender*/)
        {
            if (mOptionsMode)
                return;
            if (!mStates.empty())
            {
                size_t& page = mStates.top().mPage;

                if (page >= 2)
                {
                    MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("book page"));

                    page -= 2;
                    updateShowingPages();
                }
            }
        }

        MWGui::ControllerButtons* getControllerButtons() override
        {
            if (mOptionsMode || mStates.size() > 1)
                mControllerButtons.mB = "#{Interface:Back}";
            else
                mControllerButtons.mB = "#{Interface:Close}";

            mControllerButtons.mL1.clear();
            mControllerButtons.mR1.clear();
            mControllerButtons.mR3.clear();
            if (!mOptionsMode)
            {
                mControllerButtons.mL1 = "#{Interface:Prev}";
                mControllerButtons.mR1 = "#{Interface:Next}";
            }
            else if (mQuestMode)
            {
                mControllerButtons.mR3 = "#{OMWEngine:JournalShowAll}";
            }
            return &mControllerButtons;
        }

        void setIndexControllerFocus(bool focused)
        {
            size_t col = mSelectedIndex / mIndexRowCount;
            size_t row = mSelectedIndex % mIndexRowCount;
            mTopicIndexBook->setColour(col, row, 0, focused ? MWGui::journalHeaderColour : MyGUI::Colour::Black);
        }

        void moveSelectedIndex(int offset)
        {
            setIndexControllerFocus(false);

            int numChars = mEncoding == ToUTF8::WINDOWS_1251 ? 30 : 26;
            size_t col = mSelectedIndex / mIndexRowCount;

            if (offset == -1) // Up
            {
                if (mSelectedIndex % mIndexRowCount == 0)
                    mSelectedIndex = (col * mIndexRowCount) + mIndexRowCount - 1;
                else
                    mSelectedIndex--;
            }
            else if (offset == 1) // Down
            {
                if (mSelectedIndex % mIndexRowCount == mIndexRowCount - 1)
                    mSelectedIndex = col * mIndexRowCount;
                else
                    mSelectedIndex++;
            }
            else
            {
                // mSelectedIndex is unsigned, so we have to be careful with our math.
                if (offset < 0)
                    offset += numChars;

                mSelectedIndex = (mSelectedIndex + offset) % numChars;
            }

            setIndexControllerFocus(true);
            setText(PageOneNum, 1); // Redraw the list
        }

        bool optionsModeButtonHandler(const SDL_ControllerButtonEvent& arg)
        {
            if (arg.button == SDL_CONTROLLER_BUTTON_A) // A: Mouse click or Select
            {
                if (mQuestMode)
                {
                    // Choose a quest
                    Gui::MWList* list = getWidget<Gui::MWList>(QuestsList);
                    if (mSelectedQuest < list->getItemCount())
                        notifyQuestClicked(list->getItemNameAt(mSelectedQuest), 0);
                }
                else if (mTopicsMode)
                {
                    // Choose a topic
                    Gui::MWList* list = getWidget<Gui::MWList>(TopicsList);
                    if (mSelectedQuest < list->getItemCount())
                        notifyTopicSelected(list->getItemNameAt(mSelectedQuest), 0);
                }
                else
                {
                    // Choose an index. Cyrillic capital A is a 0xd090 in UTF-8.
                    // Words can not be started with characters 26 or 28.
                    int russianOffset = 0xd090;
                    if (mSelectedIndex >= 26)
                        russianOffset++;
                    if (mSelectedIndex >= 27)
                        russianOffset++; // 27, not 28, because of skipping char 26
                    bool isRussian = (mEncoding == ToUTF8::WINDOWS_1251);
                    size_t ch = isRussian ? mSelectedIndex + russianOffset : mSelectedIndex + 'A';
                    notifyIndexLinkClicked(static_cast<Utf8Stream::UnicodeChar>(ch));
                }
            }
            else if (arg.button == SDL_CONTROLLER_BUTTON_B) // B: Back
            {
                // Hide the options overlay
                notifyCancel(getWidget<MyGUI::Widget>(CancelBTN));
                mQuestMode = false;
            }
            else if (arg.button == SDL_CONTROLLER_BUTTON_X) // X: Quests
            {
                if (mQuestMode)
                {
                    // Hide the quest overlay if visible
                    notifyCancel(getWidget<MyGUI::Widget>(CancelBTN));
                    mQuestMode = false;
                }
                else
                {
                    // Show the quest overlay if viewing the topics list
                    notifyQuests(getWidget<MyGUI::Widget>(QuestsBTN));
                }
            }
            else if (arg.button == SDL_CONTROLLER_BUTTON_Y) // Y: Topics
            {
                if (!mQuestMode)
                {
                    // Hide the topics overlay if visible
                    notifyCancel(getWidget<MyGUI::Widget>(CancelBTN));
                }
                else
                {
                    // Show the topics overlay if viewing the quest list
                    notifyTopics(getWidget<MyGUI::Widget>(TopicsBTN));
                }
            }
            else if (arg.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK && mQuestMode) // R3: Show All/Some
            {
                if (mAllQuests)
                    notifyShowActive(getWidget<MyGUI::Widget>(ShowActiveBTN));
                else
                    notifyShowAll(getWidget<MyGUI::Widget>(ShowAllBTN));
            }
            else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_UP)
            {
                if (mQuestMode || mTopicsMode)
                {
                    if (mButtons.size() <= 1)
                        return true;

                    // Scroll through the list of quests or topics
                    setControllerFocusedQuest(MWGui::wrap(mSelectedQuest, mButtons.size(), -1));
                }
                else
                    moveSelectedIndex(-1);
            }
            else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
            {
                if (mQuestMode || mTopicsMode)
                {
                    if (mButtons.size() <= 1)
                        return true;

                    // Scroll through the list of quests or topics
                    setControllerFocusedQuest(MWGui::wrap(mSelectedQuest, mButtons.size(), 1));
                }
                else
                    moveSelectedIndex(1);
            }
            else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT && !mQuestMode && !mTopicsMode)
                moveSelectedIndex(-static_cast<int>(mIndexRowCount));
            else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT && !mQuestMode && !mTopicsMode)
                moveSelectedIndex(static_cast<int>(mIndexRowCount));
            else if (arg.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER && (mQuestMode || mTopicsMode))
            {
                // Scroll up 5 items in the list of quests or topics
                setControllerFocusedQuest(mSelectedQuest >= 5 ? mSelectedQuest - 5 : 0);
            }
            else if (arg.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER && (mQuestMode || mTopicsMode))
            {
                // Scroll down 5 items in the list of quests or topics
                setControllerFocusedQuest(std::min(mSelectedQuest + 5, mButtons.size() - 1));
            }

            return true;
        }

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override
        {
            // If the topics or quest list is open, it should handle the buttons.
            if (mOptionsMode)
                return optionsModeButtonHandler(arg);

            if (arg.button == SDL_CONTROLLER_BUTTON_A)
                return false;
            else if (arg.button == SDL_CONTROLLER_BUTTON_B) // B: Back
            {
                if (mStates.size() > 1)
                {
                    // Pop the current book. If in quest mode, reopen the quest list.
                    notifyJournal(getWidget<MyGUI::Widget>(JournalBTN));
                    if (mQuestMode)
                    {
                        notifyOptions(getWidget<MyGUI::Widget>(OptionsBTN));
                        notifyQuests(getWidget<MyGUI::Widget>(QuestsBTN));
                    }
                }
                else
                {
                    // Close the journal window
                    notifyClose(getWidget<MyGUI::Widget>(CloseBTN));
                }
            }
            else if (arg.button == SDL_CONTROLLER_BUTTON_X) // X: Quests
            {
                // Show the quest overlay
                notifyOptions(getWidget<MyGUI::Widget>(OptionsBTN));
                if (!mQuestMode)
                    notifyQuests(getWidget<MyGUI::Widget>(QuestsBTN));
            }
            else if (arg.button == SDL_CONTROLLER_BUTTON_Y) // Y: Topics
            {
                // Show the topics overlay
                notifyOptions(getWidget<MyGUI::Widget>(OptionsBTN));
                if (mQuestMode)
                    notifyTopics(getWidget<MyGUI::Widget>(TopicsBTN));
            }
            else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT || arg.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
                notifyPrevPage(getWidget<MyGUI::Widget>(PrevPageBTN));
            else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT
                || arg.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
                notifyNextPage(getWidget<MyGUI::Widget>(NextPageBTN));

            return true;
        }

        void setControllerFocusedQuest(size_t index)
        {
            size_t listSize = mButtons.size();
            if (mSelectedQuest < listSize)
                mButtons[mSelectedQuest]->setTextColour(MyGUI::Colour::Black);

            mSelectedQuest = index;
            if (mSelectedQuest < listSize)
            {
                mButtons[mSelectedQuest]->setTextColour(MWGui::journalHeaderColour);

                // Scroll the list to keep the active item in view
                Gui::MWList* list = getWidget<Gui::MWList>(mQuestMode ? QuestsList : TopicsList);
                int offset = 0;
                for (int i = 4; i < static_cast<int>(mSelectedQuest); i++)
                    offset += mButtons[i]->getHeight();
                list->setViewOffset(-offset);
            }
        }
    };
}

// glue the implementation to the interface
std::unique_ptr<MWGui::JournalWindow> MWGui::JournalWindow::create(
    std::shared_ptr<JournalViewModel> model, bool questList, ToUTF8::FromType encoding)
{
    return std::make_unique<JournalWindowImpl>(model, questList, encoding);
}

MWGui::JournalWindow::JournalWindow()
    : BookWindowBase("openmw_journal.layout")
{
}
