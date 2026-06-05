#include "dialogue.hpp"

#include <MyGUI_Button.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_ProgressBar.h>
#include <MyGUI_ScrollBar.h>
#include <MyGUI_UString.h>
#include <MyGUI_Window.h>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/settings/values.hpp>
#include <components/translation/translation.hpp>
#include <components/widgets/box.hpp>
#include <components/widgets/list.hpp>

#include "../mwbase/dialoguemanager.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwdialogue/keywordsearch.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/player.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/npcstats.hpp"

#include "bookpage.hpp"
#include "textcolours.hpp"

#include "accessibility/speech.hpp"

#include <algorithm>
#include <functional>

#include <MyGUI_InputManager.h>

namespace MWGui
{
    void ResponseCallback::addResponse(std::string_view title, std::string_view text)
    {
        mWindow->addResponse(title, text, mNeedMargin);
    }

    void ResponseCallback::updateTopics() const
    {
        mWindow->updateTopics();
    }

    PersuasionDialog::PersuasionDialog(std::unique_ptr<ResponseCallback> callback)
        : WindowModal("openmw_persuasion_dialog.layout")
        , mCallback(std::move(callback))
        , mInitialGoldLabelWidth(0)
        , mInitialMainWidgetWidth(0)
    {
        getWidget(mCancelButton, "CancelButton");
        getWidget(mAdmireButton, "AdmireButton");
        getWidget(mIntimidateButton, "IntimidateButton");
        getWidget(mTauntButton, "TauntButton");
        getWidget(mBribe10Button, "Bribe10Button");
        getWidget(mBribe100Button, "Bribe100Button");
        getWidget(mBribe1000Button, "Bribe1000Button");
        getWidget(mGoldLabel, "GoldLabel");
        getWidget(mActionsBox, "ActionsBox");

        int totalHeight = 3;
        adjustAction(mAdmireButton, totalHeight);
        adjustAction(mIntimidateButton, totalHeight);
        adjustAction(mTauntButton, totalHeight);
        adjustAction(mBribe10Button, totalHeight);
        adjustAction(mBribe100Button, totalHeight);
        adjustAction(mBribe1000Button, totalHeight);
        totalHeight += 3;

        int diff = totalHeight - mActionsBox->getSize().height;
        if (diff > 0)
        {
            auto mainWidgetSize = mMainWidget->getSize();
            mMainWidget->setSize(mainWidgetSize.width, mainWidgetSize.height + diff);
        }

        mInitialGoldLabelWidth = mActionsBox->getSize().width - mCancelButton->getSize().width - 8;
        mInitialMainWidgetWidth = mMainWidget->getSize().width;

        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &PersuasionDialog::onCancel);
        mAdmireButton->eventMouseButtonClick += MyGUI::newDelegate(this, &PersuasionDialog::onPersuade);
        mIntimidateButton->eventMouseButtonClick += MyGUI::newDelegate(this, &PersuasionDialog::onPersuade);
        mTauntButton->eventMouseButtonClick += MyGUI::newDelegate(this, &PersuasionDialog::onPersuade);
        mBribe10Button->eventMouseButtonClick += MyGUI::newDelegate(this, &PersuasionDialog::onPersuade);
        mBribe100Button->eventMouseButtonClick += MyGUI::newDelegate(this, &PersuasionDialog::onPersuade);
        mBribe1000Button->eventMouseButtonClick += MyGUI::newDelegate(this, &PersuasionDialog::onPersuade);

        mDisableGamepadCursor = Settings::gui().mControllerMenus;
        mControllerButtons.mA = "#{Interface:Select}";
        mControllerButtons.mB = "#{Interface:Cancel}";
    }

    void PersuasionDialog::buildAccessibility()
    {
        // Register the persuasion actions in visual order, then Cancel. The
        // bribe buttons may be disabled (insufficient gold); the framework's
        // isUsable() check skips disabled widgets during navigation, so we can
        // register them unconditionally and they self-hide. Real-focus mode:
        // each button is an ordinary focusable widget.
        mA11y.clear();
        mA11y.add({ .widget = mAdmireButton, .label = "#{sAdmire}",
            .activate = [this] { onPersuade(mAdmireButton); } });
        mA11y.add({ .widget = mIntimidateButton, .label = "#{sIntimidate}",
            .activate = [this] { onPersuade(mIntimidateButton); } });
        mA11y.add({ .widget = mTauntButton, .label = "#{sTaunt}",
            .activate = [this] { onPersuade(mTauntButton); } });
        mA11y.add({ .widget = mBribe10Button, .label = "#{sBribe 10 Gold}",
            .activate = [this] { onPersuade(mBribe10Button); } });
        mA11y.add({ .widget = mBribe100Button, .label = "#{sBribe 100 Gold}",
            .activate = [this] { onPersuade(mBribe100Button); } });
        mA11y.add({ .widget = mBribe1000Button, .label = "#{sBribe 1000 Gold}",
            .activate = [this] { onPersuade(mBribe1000Button); } });
        mA11y.add({ .widget = mCancelButton, .label = "#{Interface:Cancel}",
            .activate = [this] { onCancel(mCancelButton); } });
    }

    void PersuasionDialog::adjustAction(MyGUI::Widget* action, int& totalHeight)
    {
        const int lineHeight = Settings::gui().mFontSize + 2;
        auto currentCoords = action->getCoord();
        action->setCoord(currentCoords.left, totalHeight, currentCoords.width, lineHeight);
        totalHeight += lineHeight;
    }

    void PersuasionDialog::onCancel(MyGUI::Widget* /*sender*/)
    {
        setVisible(false);
    }

    void PersuasionDialog::onPersuade(MyGUI::Widget* sender)
    {
        MWBase::MechanicsManager::PersuasionType type;
        if (sender == mAdmireButton)
            type = MWBase::MechanicsManager::PT_Admire;
        else if (sender == mIntimidateButton)
            type = MWBase::MechanicsManager::PT_Intimidate;
        else if (sender == mTauntButton)
            type = MWBase::MechanicsManager::PT_Taunt;
        else if (sender == mBribe10Button)
            type = MWBase::MechanicsManager::PT_Bribe10;
        else if (sender == mBribe100Button)
            type = MWBase::MechanicsManager::PT_Bribe100;
        else /*if (sender == mBribe1000Button)*/
            type = MWBase::MechanicsManager::PT_Bribe1000;

        MWBase::Environment::get().getDialogueManager()->persuade(type, mCallback.get());
        mCallback->updateTopics();

        setVisible(false);
    }

    void PersuasionDialog::onOpen()
    {
        center();

        MWWorld::Ptr player = MWMechanics::getPlayer();
        int playerGold = player.getClass().getContainerStore(player).count(MWWorld::ContainerStore::sGoldId);

        mBribe10Button->setEnabled(playerGold >= 10);
        mBribe100Button->setEnabled(playerGold >= 100);
        mBribe1000Button->setEnabled(playerGold >= 1000);

        mGoldLabel->setCaptionWithReplacing("#{sGold}: " + MyGUI::utility::toString(playerGold));

        int diff = mGoldLabel->getRequestedSize().width - mInitialGoldLabelWidth;
        if (diff > 0)
            mMainWidget->setSize(mInitialMainWidgetWidth + diff, mMainWidget->getSize().height);
        else
            mMainWidget->setSize(mInitialMainWidgetWidth, mMainWidget->getSize().height);

        if (Settings::gui().mControllerMenus)
        {
            mControllerFocus = 0;
            mButtons.clear();
            mButtons.push_back(mAdmireButton);
            mButtons.push_back(mIntimidateButton);
            mButtons.push_back(mTauntButton);
            if (mBribe10Button->getEnabled())
                mButtons.push_back(mBribe10Button);
            if (mBribe100Button->getEnabled())
                mButtons.push_back(mBribe100Button);
            if (mBribe1000Button->getEnabled())
                mButtons.push_back(mBribe1000Button);

            for (size_t i = 0; i < mButtons.size(); i++)
                mButtons[i]->setStateSelected(i == 0);
        }

        WindowModal::onOpen();

        // Rebuild the option list and take screen-reader input. clear() (called
        // here and on deactivate) now unbinds the per-widget key delegates, so
        // re-adding them on each open is safe. Announce the player's gold first
        // (it gates which bribe options are available), then land on the first
        // action. isUsable() re-checks each button's live enabled state.
        buildAccessibility();
        A11y::say("#{sGold}: " + std::to_string(playerGold));
        mA11y.activate(mAdmireButton);
    }

    void PersuasionDialog::onClose()
    {
        WindowModal::onClose();
        mA11y.deactivate();
        // Hand screen-reader input back to the dialogue window underneath.
        if (mOnClosed)
            mOnClosed();
    }

    void PersuasionDialog::onFrame(float dt)
    {
        mA11y.onFrame(dt);
    }

    MyGUI::Widget* PersuasionDialog::getDefaultKeyFocus()
    {
        return mAdmireButton;
    }

    bool PersuasionDialog::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_A)
        {
            onPersuade(mButtons[mControllerFocus]);
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_B)
            onCancel(mCancelButton);
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_UP)
        {
            setControllerFocus(mButtons, mControllerFocus, false);
            mControllerFocus = wrap(mControllerFocus, mButtons.size(), -1);
            setControllerFocus(mButtons, mControllerFocus, true);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
        {
            setControllerFocus(mButtons, mControllerFocus, false);
            mControllerFocus = wrap(mControllerFocus, mButtons.size(), 1);
            setControllerFocus(mButtons, mControllerFocus, true);
        }

        return true;
    }

    // --------------------------------------------------------------------------------------------------

    Response::Response(std::string_view text, std::string_view title, bool needMargin)
        : mTitle(title)
        , mNeedMargin(needMargin)
    {
        mText = text;
    }

    void Response::write(std::shared_ptr<BookTypesetter> typesetter, const MWDialogue::KeywordSearch& keywordSearch,
        std::unordered_map<std::string, std::unique_ptr<Link>>& topicLinks) const
    {
        using namespace MWDialogue;

        MWBase::WindowManager& windowManager = *MWBase::Environment::get().getWindowManager();
        const Translation::Storage& translationStorage = windowManager.getTranslationDataStorage();
        const TextColours& colors = windowManager.getTextColours();

        typesetter->sectionBreak(mNeedMargin ? 9 : 0);

        if (!mTitle.empty())
        {
            BookTypesetter::Style* title = typesetter->createStyle({}, colors.header, false);
            typesetter->write(title, mTitle);
            typesetter->sectionBreak();
        }

        struct Token
        {
            size_t mStart;
            size_t mEnd;
            Link* mTopic;
        };

        std::vector<KeywordSearch::Match> matches = keywordSearch.parseHyperText(mText, translationStorage);
        std::vector<Token> tokens;
        tokens.reserve(matches.size());
        std::string text;
        text.reserve(mText.size());

        // Generate the displayed text and a more convenient token list.
        // The matches we got provide positions in the original text and must be recalculated.
        KeywordSearch::Point pos = mText.begin();
        for (const KeywordSearch::Match& token : matches)
        {
            const std::string_view displayName(token.getDisplayName());
            text.append(pos, token.mBeg);
            text.append(displayName);
            pos = token.mEnd;

            auto value = topicLinks.find(token.mTopicId);
            if (value != topicLinks.end())
                tokens.emplace_back(text.size() - displayName.size(), text.size(), value->second.get());
        }
        text.append(pos, mText.end());

        typesetter->addContent(text);

        BookTypesetter::Style* textStyle = typesetter->createStyle({}, colors.normal, false);

        size_t i = 0;
        for (const Token& token : tokens)
        {
            if (i < token.mStart)
                typesetter->write(textStyle, i, token.mStart);

            auto id = reinterpret_cast<TypesetBook::InteractiveId>(token.mTopic);
            BookTypesetter::Style* linkStyle
                = typesetter->createHotStyle(textStyle, colors.link, colors.linkOver, colors.linkPressed, id);
            typesetter->write(linkStyle, token.mStart, token.mEnd);
            i = token.mEnd;
        }

        if (i < text.size())
            typesetter->write(textStyle, i, text.size());
    }

    Message::Message(std::string_view text)
    {
        mText = text;
    }

    void Message::write(std::shared_ptr<BookTypesetter> typesetter, const MWDialogue::KeywordSearch&,
        std::unordered_map<std::string, std::unique_ptr<Link>>&) const
    {
        const MyGUI::Colour& textColour = MWBase::Environment::get().getWindowManager()->getTextColours().notify;
        BookTypesetter::Style* title = typesetter->createStyle({}, textColour, false);
        typesetter->sectionBreak(9);
        typesetter->write(title, mText);
    }

    // --------------------------------------------------------------------------------------------------

    void Choice::activated()
    {
        MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
        eventChoiceActivated(mChoiceId);
    }

    void Topic::activated()
    {
        MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
        eventTopicActivated(mTopicId);
    }

    void Goodbye::activated()
    {
        MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
        eventActivated();
    }

    // --------------------------------------------------------------------------------------------------

    // Morrowind uses 3 px invisible borders for padding topics
    static constexpr int sVerticalPadding = 3;

    DialogueWindow::DialogueWindow()
        : WindowBase("openmw_dialogue_window.layout")
        , mIsCompanion(false)
        , mGoodbye(false)
        , mPersuasionDialog(std::make_unique<ResponseCallback>(this))
        , mCallback(std::make_unique<ResponseCallback>(this))
        , mGreetingCallback(std::make_unique<ResponseCallback>(this, false))
    {
        // Centre dialog
        center();

        mPersuasionDialog.setVisible(false);

        // History view
        getWidget(mHistory, "History");

        // Topics list
        getWidget(mTopicsList, "TopicsList");
        mTopicsList->eventItemSelected += MyGUI::newDelegate(this, &DialogueWindow::onSelectListItem);

        getWidget(mGoodbyeButton, "ByeButton");
        mGoodbyeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &DialogueWindow::onByeClicked);

        getWidget(mDispositionBar, "Disposition");
        getWidget(mDispositionText, "DispositionText");
        getWidget(mScrollBar, "VScroll");

        mScrollBar->eventScrollChangePosition += MyGUI::newDelegate(this, &DialogueWindow::onScrollbarMoved);
        mHistory->eventMouseWheel += MyGUI::newDelegate(this, &DialogueWindow::onMouseWheel);

        BookPage::ClickCallback callback = [this](TypesetBook::InteractiveId link) { notifyLinkClicked(link); };
        mHistory->adviseLinkClicked(std::move(callback));

        mMainWidget->castType<MyGUI::Window>()->eventWindowChangeCoord
            += MyGUI::newDelegate(this, &DialogueWindow::onWindowResize);

        mControllerScrollWidget = mHistory->getParent();
        mControllerButtons.mA = "#{Interface:Ask}";
        mControllerButtons.mB = "#{Interface:Goodbye}";
        mControllerButtons.mRStick = "#{Interface:ScrollUp}";

        // Screen-reader setup: an invisible anchor holds key focus while the
        // A11y::Screen tracks the selected topic/choice internally, so the
        // native topics ListBox and history view never eat our arrow keys.
        // The topic and choice lists are widget-less (pure text), rebuilt by
        // buildAccessibility() from updateHistory().
        mA11yAnchor = mMainWidget->createWidget<MyGUI::Widget>(
            {}, MyGUI::IntCoord(0, 0, 1, 1), MyGUI::Align::Default);
        mA11yAnchor->setNeedKeyFocus(true);
        mA11y.setVirtualFocus(mA11yAnchor);
        // Extra keys for the topic list:
        //  - D announces the NPC's disposition on demand (see announceDisposition).
        //  - Ctrl+Up / Ctrl+Down jump to the previous / next un-exhausted topic.
        //    Long topic lists are tedious to scan, and freshly-learned topics
        //    often appear mid-list, so this skips straight to the topics that
        //    still have something new to say (un-exhausted keywords), skipping
        //    services, Goodbye, and already-exhausted topics.
        mA11y.setExtraKeyHandler([this](MyGUI::KeyCode key) {
            if (key == MyGUI::KeyCode::D)
            {
                announceDisposition();
                return true;
            }

            const bool ctrl = MyGUI::InputManager::getInstance().isControlPressed();
            if (ctrl && (key == MyGUI::KeyCode::ArrowDown || key == MyGUI::KeyCode::ArrowUp))
            {
                const int delta = (key == MyGUI::KeyCode::ArrowDown) ? 1 : -1;
                if (!mA11y.selectMatchingLabel(delta, [this](std::string_view label) {
                        return isUnexhaustedTopic(label);
                    }))
                {
                    A11y::say(delta > 0 ? "No more topics." : "No previous topics.");
                }
                return true;
            }
            return false;
        });

        // When the persuasion modal closes, reclaim screen-reader input and
        // re-announce the topic the player came from (its disposition/state may
        // have changed as a result of the attempt).
        mPersuasionDialog.mOnClosed = [this] { mA11y.activate(); };
    }

    void DialogueWindow::onTradeComplete()
    {
        MyGUI::UString message = MyGUI::LanguageManager::getInstance().replaceTags("#{sBarterDialog5}");
        addResponse({}, message);
    }

    bool DialogueWindow::exit()
    {
        if ((MWBase::Environment::get().getDialogueManager()->isInChoice()))
        {
            return false;
        }
        else
        {
            resetReference();
            MWBase::Environment::get().getDialogueManager()->goodbyeSelected();
            mTopicsList->scrollToTop();
            return true;
        }
    }

    void DialogueWindow::onWindowResize(MyGUI::Window* sender)
    {
        // if the window has only been moved, not resized, we don't need to update
        if (mCurrentWindowSize == sender->getSize())
            return;

        redrawTopicsList();
        updateHistory();
        mCurrentWindowSize = sender->getSize();
    }

    void DialogueWindow::onMouseWheel(MyGUI::Widget* /*sender*/, int rel)
    {
        if (!mScrollBar->getVisible() || mScrollBar->getScrollRange() == 0)
            return;
        const int step = static_cast<int>(0.3f * rel);
        const int newPos = static_cast<int>(mScrollBar->getScrollPosition()) - step;
        const int maxPos = static_cast<int>(mScrollBar->getScrollRange()) - 1;
        mScrollBar->setScrollPosition(static_cast<size_t>(std::clamp(newPos, 0, maxPos)));
        onScrollbarMoved(mScrollBar, mScrollBar->getScrollPosition());
    }

    void DialogueWindow::onByeClicked(MyGUI::Widget* /*sender*/)
    {
        if (exit())
        {
            MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Dialogue);
        }
    }

    void DialogueWindow::onSelectListItem(const std::string& topic, int /*id*/)
    {
        MWBase::DialogueManager* dialogueManager = MWBase::Environment::get().getDialogueManager();

        if (mGoodbye || dialogueManager->isInChoice())
            return;

        const MWWorld::Store<ESM::GameSetting>& gmst
            = MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>();

        const std::string& sPersuasion = gmst.find("sPersuasion")->mValue.getString();
        const std::string& sCompanionShare = gmst.find("sCompanionShare")->mValue.getString();
        const std::string& sBarter = gmst.find("sBarter")->mValue.getString();
        const std::string& sSpells = gmst.find("sSpells")->mValue.getString();
        const std::string& sTravel = gmst.find("sTravel")->mValue.getString();
        const std::string& sSpellMakingMenuTitle = gmst.find("sSpellMakingMenuTitle")->mValue.getString();
        const std::string& sEnchanting = gmst.find("sEnchanting")->mValue.getString();
        const std::string& sServiceTrainingTitle = gmst.find("sServiceTrainingTitle")->mValue.getString();
        const std::string& sRepair = gmst.find("sRepair")->mValue.getString();

        if (topic != sPersuasion && topic != sCompanionShare && topic != sBarter && topic != sSpells && topic != sTravel
            && topic != sSpellMakingMenuTitle && topic != sEnchanting && topic != sServiceTrainingTitle
            && topic != sRepair)
        {
            onTopicActivated(topic);
            if (mGoodbyeButton->getEnabled())
                MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mGoodbyeButton);
        }
        else if (topic == sPersuasion)
            mPersuasionDialog.setVisible(true);
        else if (topic == sCompanionShare)
            MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Companion, mPtr);
        else if (!dialogueManager->checkServiceRefused(mCallback.get()))
        {
            if (topic == sBarter
                && !dialogueManager->checkServiceRefused(mCallback.get(), MWBase::DialogueManager::Barter))
                MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Barter, mPtr);
            else if (topic == sSpells
                && !dialogueManager->checkServiceRefused(mCallback.get(), MWBase::DialogueManager::Spells))
                MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_SpellBuying, mPtr);
            else if (topic == sTravel
                && !dialogueManager->checkServiceRefused(mCallback.get(), MWBase::DialogueManager::Travel))
                MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Travel, mPtr);
            else if (topic == sSpellMakingMenuTitle
                && !dialogueManager->checkServiceRefused(mCallback.get(), MWBase::DialogueManager::Spellmaking))
                MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_SpellCreation, mPtr);
            else if (topic == sEnchanting
                && !dialogueManager->checkServiceRefused(mCallback.get(), MWBase::DialogueManager::Enchanting))
                MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Enchanting, mPtr);
            else if (topic == sServiceTrainingTitle
                && !dialogueManager->checkServiceRefused(mCallback.get(), MWBase::DialogueManager::Training))
                MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Training, mPtr);
            else if (topic == sRepair
                && !dialogueManager->checkServiceRefused(mCallback.get(), MWBase::DialogueManager::Repair))
                MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_MerchantRepair, mPtr);
        }
        else
            updateTopics();
    }

    void DialogueWindow::setPtr(const MWWorld::Ptr& actor)
    {
        if (actor.isEmpty() || !actor.getClass().isActor())
        {
            Log(Debug::Warning) << "Warning: can not talk with non-actor object.";
            return;
        }

        bool sameActor = (mPtr == actor);
        if (!sameActor)
        {
            // The history is not reset here
            mKeywords.clear();
            mTopicsList->clear();
            for (auto& link : mLinks)
                mDeleteLater.push_back(
                    std::move(link)); // Links are not deleted right away to prevent issues with event handlers
            mLinks.clear();
        }

        mPtr = actor;
        mGoodbye = false;
        mTopicsList->setEnabled(true);

        if (!MWBase::Environment::get().getDialogueManager()->startDialogue(actor, mGreetingCallback.get()))
        {
            // No greetings found. The dialogue window should not be shown.
            // If this is a companion, we must show the companion window directly (used by BM_bear_be_unique).
            MWBase::Environment::get().getWindowManager()->removeGuiMode(MWGui::GM_Dialogue);
            mPtr = MWWorld::Ptr();
            if (isCompanion(actor))
                MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Companion, actor);
            return;
        }

        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mGoodbyeButton);

        setTitle(mPtr.getClass().getName(mPtr));

        updateTopics();
        updateTopicsPane(); // force update for new services

        if (Settings::gui().mControllerMenus && !sameActor)
        {
            setControllerFocus(mControllerFocus, false);
            // Reset focus to very top. Maybe change this to mTopicsList->getItemCount() - mKeywords.size()?
            mControllerFocus = 0;
            setControllerFocus(mControllerFocus, true);
        }

        updateDisposition();
        restock();

        // updateTopics()/updateHistory() above rebuilt the a11y option list via
        // buildAccessibility() now that mPtr is set. onOpen() (which ran just
        // before setPtr on a fresh conversation) already called activate(), but
        // at that point mPtr was empty so the list was empty; re-activate now to
        // select and announce the first real topic. A fresh open announces
        // immediately, so cancel any deferred activation onOpen queued.
        mA11yPendingActivate = false;
        mA11y.activate();
    }

    void DialogueWindow::restock()
    {
        MWMechanics::CreatureStats& sellerStats = mPtr.getClass().getCreatureStats(mPtr);
        float delay = MWBase::Environment::get()
                          .getESMStore()
                          ->get<ESM::GameSetting>()
                          .find("fBarterGoldResetDelay")
                          ->mValue.getFloat();

        // Gold is restocked every 24h
        if (MWBase::Environment::get().getWorld()->getTimeStamp() >= sellerStats.getLastRestockTime() + delay)
        {
            sellerStats.setGoldPool(mPtr.getClass().getBaseGold(mPtr));

            sellerStats.setLastRestockTime(MWBase::Environment::get().getWorld()->getTimeStamp());
        }
    }

    void DialogueWindow::deleteLater()
    {
        mDeleteLater.clear();
    }

    void DialogueWindow::onOpen()
    {
        // Fires both on a fresh conversation (before setPtr, when mPtr is still
        // empty -- buildAccessibility no-ops, and setPtr rebuilds+activates
        // shortly after) and when returning from a pushed sub-mode such as
        // barter/training (setPtr is NOT called again, so this is the only hook
        // to reclaim input). Rebuild to reflect current topics, then defer the
        // re-activation to the next frame (see mA11yPendingActivate): returning
        // from a sub-mode can reveal us and tear us down in the same frame, and
        // an immediate activate would announce a topic during teardown.
        buildAccessibility();
        mA11yPendingActivate = true;
    }

    void DialogueWindow::onClose()
    {
        // Cancel any deferred re-activation (e.g. a sub-mode revealed us and is
        // now closing the whole conversation in the same frame).
        mA11yPendingActivate = false;

        // Always yield screen-reader input when hidden -- whether the whole
        // conversation is ending or a sub-mode (barter/training) is being
        // pushed on top. deactivate() clears the option list; onOpen rebuilds.
        mA11y.deactivate();

        if (MWBase::Environment::get().getWindowManager()->containsMode(GM_Dialogue))
            return;
        // Reset history
        mHistoryContents.clear();
    }

    bool DialogueWindow::setKeywords(const std::list<std::string>& keyWords)
    {
        if (mKeywords == keyWords && isCompanion() == mIsCompanion)
            return false;
        mIsCompanion = isCompanion();
        mKeywords = keyWords;
        updateTopicsPane();
        return true;
    }

    void DialogueWindow::redrawTopicsList()
    {
        mTopicsList->adjustSize();

        // The topics list has been regenerated so topic formatting needs to be updated
        updateTopicFormat();
    }

    void DialogueWindow::updateTopicsPane()
    {
        std::string focusedTopic;
        if (Settings::gui().mControllerMenus && mControllerFocus < mTopicsList->getItemCount())
            focusedTopic = mTopicsList->getItemNameAt(mControllerFocus);

        mTopicsList->clear();
        for (auto& linkPair : mTopicLinks)
            mDeleteLater.push_back(std::move(linkPair.second));
        mTopicLinks.clear();
        mKeywordSearch.clear();

        int services = mPtr.getClass().getServices(mPtr);

        bool travel = (mPtr.getType() == ESM::NPC::sRecordId && !mPtr.get<ESM::NPC>()->mBase->getTransport().empty())
            || (mPtr.getType() == ESM::Creature::sRecordId
                && !mPtr.get<ESM::Creature>()->mBase->getTransport().empty());

        const MWWorld::Store<ESM::GameSetting>& gmst
            = MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>();

        if (mPtr.getType() == ESM::NPC::sRecordId)
            mTopicsList->addItem(gmst.find("sPersuasion")->mValue.getString());

        if (services & ESM::NPC::AllItems)
            mTopicsList->addItem(gmst.find("sBarter")->mValue.getString());

        if (services & ESM::NPC::Spells)
            mTopicsList->addItem(gmst.find("sSpells")->mValue.getString());

        if (travel)
            mTopicsList->addItem(gmst.find("sTravel")->mValue.getString());

        if (services & ESM::NPC::Spellmaking)
            mTopicsList->addItem(gmst.find("sSpellmakingMenuTitle")->mValue.getString());

        if (services & ESM::NPC::Enchanting)
            mTopicsList->addItem(gmst.find("sEnchanting")->mValue.getString());

        if (services & ESM::NPC::Training)
            mTopicsList->addItem(gmst.find("sServiceTrainingTitle")->mValue.getString());

        if (services & ESM::NPC::Repair)
            mTopicsList->addItem(gmst.find("sRepair")->mValue.getString());

        if (isCompanion())
            mTopicsList->addItem(gmst.find("sCompanionShare")->mValue.getString());

        if (mTopicsList->getItemCount() > 0)
            mTopicsList->addSeparator();

        MWBase::WindowManager& windowManager = *MWBase::Environment::get().getWindowManager();
        const Translation::Storage& translationStorage = windowManager.getTranslationDataStorage();

        for (const auto& keyword : mKeywords)
        {
            std::string topicId = Misc::StringUtils::lowerCase(keyword);
            mTopicsList->addItem(keyword, sVerticalPadding);

            auto t = std::make_unique<Topic>(keyword);
            mKeywordSearch.seed(translationStorage.topicKeyword(keyword), topicId);
            t->eventTopicActivated += MyGUI::newDelegate(this, &DialogueWindow::onTopicActivated);
            mTopicLinks[topicId] = std::move(t);

            if (keyword == focusedTopic)
                mControllerFocus = mTopicsList->getItemCount() - 1;
        }

        redrawTopicsList();
        updateHistory();

        if (Settings::gui().mControllerMenus)
            setControllerFocus(mControllerFocus, true);
    }

    void DialogueWindow::updateHistory(bool scrollbar)
    {
        if (!scrollbar && mScrollBar->getVisible())
        {
            mHistory->setSize(mHistory->getSize() + MyGUI::IntSize(mScrollBar->getWidth(), 0));
            mScrollBar->setVisible(false);
        }
        if (scrollbar && !mScrollBar->getVisible())
        {
            mHistory->setSize(mHistory->getSize() - MyGUI::IntSize(mScrollBar->getWidth(), 0));
            mScrollBar->setVisible(true);
        }

        std::shared_ptr<BookTypesetter> typesetter
            = BookTypesetter::create(mHistory->getWidth(), std::numeric_limits<int>::max());

        for (const auto& text : mHistoryContents)
            text->write(typesetter, mKeywordSearch, mTopicLinks);

        BookTypesetter::Style* body = typesetter->createStyle({}, MyGUI::Colour::White, false);

        typesetter->sectionBreak(9);
        // choices
        const TextColours& textColours = MWBase::Environment::get().getWindowManager()->getTextColours();
        mChoices = MWBase::Environment::get().getDialogueManager()->getChoices();
        mChoiceStyles.clear();
        mControllerChoice = -1; // -1 so you must make a choice (and can't accidentally pick the first answer)
        for (std::pair<std::string, int>& choice : mChoices)
        {
            auto link = std::make_unique<Choice>(choice.second);
            link->eventChoiceActivated += MyGUI::newDelegate(this, &DialogueWindow::onChoiceActivated);
            auto interactiveId = TypesetBook::InteractiveId(link.get());
            mLinks.push_back(std::move(link));

            typesetter->lineBreak();
            BookTypesetter::Style* questionStyle = typesetter->createHotStyle(
                body, textColours.answer, textColours.answerOver, textColours.answerPressed, interactiveId);
            typesetter->write(questionStyle, choice.first);
            mChoiceStyles.push_back(questionStyle);
        }

        mGoodbye = MWBase::Environment::get().getDialogueManager()->isGoodbye();
        if (mGoodbye)
        {
            auto link = std::make_unique<Goodbye>();
            link->eventActivated += MyGUI::newDelegate(this, &DialogueWindow::onGoodbyeActivated);
            auto interactiveId = TypesetBook::InteractiveId(link.get());
            mLinks.push_back(std::move(link));
            const std::string& goodbye = MWBase::Environment::get()
                                             .getESMStore()
                                             ->get<ESM::GameSetting>()
                                             .find("sGoodbye")
                                             ->mValue.getString();
            BookTypesetter::Style* questionStyle = typesetter->createHotStyle(
                body, textColours.answer, textColours.answerOver, textColours.answerPressed, interactiveId);
            typesetter->lineBreak();
            typesetter->write(questionStyle, goodbye);
        }

        std::shared_ptr<TypesetBook> book = typesetter->complete();
        mHistory->showPage(book, 0);
        size_t viewHeight = mHistory->getParent()->getHeight();
        if (!scrollbar && book->getSize().second > viewHeight)
            updateHistory(true);
        else if (scrollbar)
        {
            mHistory->setSize(MyGUI::IntSize(mHistory->getWidth(), book->getSize().second));
            // Scroll range should be >= 2 to enable scrolling and prevent a crash
            size_t range = std::max(book->getSize().second - viewHeight, size_t(2));
            mScrollBar->setScrollRange(range);
            mScrollBar->setScrollPosition(range - 1);
            mScrollBar->setTrackSize(
                static_cast<int>(viewHeight / static_cast<float>(book->getSize().second) * mScrollBar->getLineSize()));
            onScrollbarMoved(mScrollBar, range - 1);
        }
        else
        {
            // no scrollbar
            onScrollbarMoved(mScrollBar, 0);
        }

        bool goodbyeEnabled = !MWBase::Environment::get().getDialogueManager()->isInChoice() || mGoodbye;
        bool goodbyeWasEnabled = mGoodbyeButton->getEnabled();
        mGoodbyeButton->setEnabled(goodbyeEnabled);
        if (goodbyeEnabled && !goodbyeWasEnabled)
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mGoodbyeButton);

        bool topicsEnabled = !MWBase::Environment::get().getDialogueManager()->isInChoice() && !mGoodbye;
        mTopicsList->setEnabled(topicsEnabled);

        // Keep the screen-reader option list in sync with the rebuilt window
        // state (topics<->choices, goodbye availability). This is the single
        // refresh point for the dialogue UI, so it's the right place to do it.
        buildAccessibility();
    }

    void DialogueWindow::notifyLinkClicked(TypesetBook::InteractiveId link)
    {
        reinterpret_cast<Link*>(link)->activated();
    }

    void DialogueWindow::onTopicActivated(const std::string& topicId)
    {
        if (mGoodbye)
            return;

        MWBase::Environment::get().getDialogueManager()->keywordSelected(topicId, mCallback.get());
        updateTopics();
    }

    void DialogueWindow::onChoiceActivated(int id)
    {
        if (mGoodbye)
        {
            onGoodbyeActivated();
            return;
        }
        MWBase::Environment::get().getDialogueManager()->questionAnswered(id, mCallback.get());
        updateTopics();
    }

    void DialogueWindow::onGoodbyeActivated()
    {
        MWBase::Environment::get().getDialogueManager()->goodbyeSelected();
        MWBase::Environment::get().getWindowManager()->removeGuiMode(MWGui::GM_Dialogue);
        resetReference();
    }

    void DialogueWindow::onScrollbarMoved(MyGUI::ScrollBar* sender, size_t pos)
    {
        mHistory->setPosition(0, static_cast<int>(pos) * -1);
    }

    void DialogueWindow::addResponse(std::string_view title, std::string_view text, bool needMargin)
    {
        mHistoryContents.push_back(std::make_unique<Response>(text, title, needMargin));
        updateHistory();

        // Speak the NPC's words (the contextual prose the player can't navigate
        // to) and mark them rereadable so R repeats the latest line -- the
        // signature reread convention. The line is SPOKEN without the speaker's
        // name (prefixing it on every line is repetitive), but R rereads it WITH
        // the speaker prefixed (e.g. "Ganciele Douar: I'm an officer...") so the
        // player can recall who said it. The title is the topic heading (empty
        // for greetings); include it so the player knows which topic the
        // response belongs to. interrupt=true so a fresh response cuts off any
        // lingering focus chatter.
        std::string spoken;
        if (!title.empty())
            spoken = std::string(title) + ". ";
        spoken += std::string(text);

        std::string rereadable;
        if (!mPtr.isEmpty())
        {
            std::string_view speaker = mPtr.getClass().getName(mPtr);
            if (!speaker.empty())
                rereadable = std::string(speaker) + ": ";
        }
        rereadable += spoken;

        if (!spoken.empty())
            A11y::sayRereadable(spoken, rereadable, /*interrupt=*/true);
    }

    void DialogueWindow::addMessageBox(std::string_view text)
    {
        mHistoryContents.push_back(std::make_unique<Message>(text));
        updateHistory();

        // These are the transient status notifications the engine shows during
        // dialogue -- "X removed from your inventory", "Your journal has been
        // updated", "87 Gold has been added", etc. They typically arrive in the
        // same frame as (and just after) the NPC's spoken line via addResponse.
        //
        // QUEUE them (interrupt=false) so they read AFTER the dialogue line
        // instead of clobbering it: with interrupt=true each notification cut
        // off the line and the preceding notifications, so only the last one
        // ("87 Gold...") was ever heard. A fresh topic still interrupts the
        // whole lot, because addResponse uses interrupt=true.
        //
        // Use plain say(), NOT sayRereadable(): these are transient status
        // lines, not contextual prose, so they must not overwrite the dialogue
        // line that R (reread) is meant to repeat.
        if (!text.empty())
            A11y::say(std::string(text), /*interrupt=*/false);
    }

    void DialogueWindow::updateDisposition()
    {
        bool dispositionVisible = false;
        if (!mPtr.isEmpty() && mPtr.getClass().isNpc())
        {
            // If actor was a witness to a crime which was payed off,
            // restore original disposition immediately.
            MWMechanics::NpcStats& npcStats = mPtr.getClass().getNpcStats(mPtr);
            if (npcStats.getCrimeId() != -1 && npcStats.getCrimeDispositionModifier() != 0)
            {
                if (npcStats.getCrimeId() <= MWBase::Environment::get().getWorld()->getPlayer().getCrimeId())
                    npcStats.setCrimeDispositionModifier(0);
            }

            dispositionVisible = true;
            mDispositionBar->setProgressRange(100);
            mDispositionBar->setProgressPosition(
                MWBase::Environment::get().getMechanicsManager()->getDerivedDisposition(mPtr));
            mDispositionText->setCaption(
                MyGUI::utility::toString(MWBase::Environment::get().getMechanicsManager()->getDerivedDisposition(mPtr))
                + std::string("/100"));
        }

        if (mDispositionBar->getVisible() != dispositionVisible)
        {
            mDispositionBar->setVisible(dispositionVisible);
            const int offset = (mDispositionBar->getHeight() + 5) * (dispositionVisible ? 1 : -1);
            mTopicsList->setCoord(mTopicsList->getCoord() + MyGUI::IntCoord(0, offset, 0, -offset));
            redrawTopicsList();
        }
    }

    void DialogueWindow::buildAccessibility()
    {
        // Preserve the selected option across the rebuild by remembering its
        // label (the widget-less options have no widget to refocus by). We
        // restore it silently so a routine rebuild (e.g. an NPC response being
        // appended) doesn't talk over the response that was just spoken.
        const std::string previousLabel = mA11y.currentLabel();

        mA11y.clear();

        MWBase::DialogueManager* dialogueManager = MWBase::Environment::get().getDialogueManager();
        const bool inChoice = dialogueManager->isInChoice();

        if (inChoice)
        {
            // Choice prompt: the player must pick one of the inline answers.
            // The topics list is disabled in this state, so expose only the
            // choices (plus Goodbye if offered).
            for (const std::pair<std::string, int>& choice : mChoices)
            {
                const int id = choice.second;
                mA11y.add({ .widget = nullptr,
                    .label = choice.first,
                    .activate = [this, id] { onChoiceActivated(id); } });
            }
        }
        else
        {
            // Normal state: services + dialogue topics. Walk the native topics
            // list so services, the separator, and learned keywords stay in the
            // same order the sighted player sees. Each entry activates through
            // the same onSelectListItem() path the mouse uses.
            for (size_t i = 0; i < mTopicsList->getItemCount(); ++i)
            {
                const std::string& name = mTopicsList->getItemNameAt(i);
                if (name.empty())
                    continue; // separator between services and topics

                // For learned dialogue keywords, append the native new/exhausted
                // state (the UI shows this via topic text colour). Services
                // aren't keywords, so getTopicFlag returns nothing useful for
                // them -- only annotate genuine keywords.
                std::function<std::string()> value;
                const bool isKeyword
                    = std::find(mKeywords.begin(), mKeywords.end(), name) != mKeywords.end();
                if (isKeyword)
                {
                    value = [name] {
                        int flag = MWBase::Environment::get().getDialogueManager()->getTopicFlag(
                            ESM::RefId::stringRefId(name));
                        if (flag & MWBase::DialogueManager::TopicType::Specific)
                            return std::string("new");
                        if (flag & MWBase::DialogueManager::TopicType::Exhausted)
                            return std::string("exhausted");
                        return std::string();
                    };
                }

                mA11y.add({ .widget = nullptr,
                    .label = name,
                    .value = std::move(value),
                    .activate = [this, name, i] { onSelectListItem(name, static_cast<int>(i)); } });
            }
        }

        // Goodbye is available outside of a choice, or when the dialogue
        // manager explicitly offers it as the way out of a choice.
        if (mGoodbyeButton->getEnabled())
        {
            const std::string& goodbye = MWBase::Environment::get()
                                             .getESMStore()
                                             ->get<ESM::GameSetting>()
                                             .find("sGoodbye")
                                             ->mValue.getString();
            mA11y.add({ .widget = nullptr,
                .label = goodbye,
                .activate = [this] { onGoodbyeActivated(); } });
        }

        if (!mA11y.isActive())
        {
            mA11yWasInChoice = inChoice;
            return;
        }

        // Announce policy: when we've just entered a choice prompt, announce the
        // first choice so the player knows they must answer (the NPC's question
        // was spoken by addResponse just before). Otherwise restore the prior
        // selection silently -- the meaningful audio (the NPC response) has
        // already been spoken, and we don't want to chatter on every rebuild.
        const bool enteredChoice = inChoice && !mA11yWasInChoice;
        if (enteredChoice)
            mA11y.focusFirst(/*announce=*/true);
        else if (!previousLabel.empty())
        {
            if (!mA11y.selectByLabel(previousLabel, /*announce=*/false))
                mA11y.focusFirst(/*announce=*/false);
        }
        else
            mA11y.focusFirst(/*announce=*/false);

        mA11yWasInChoice = inChoice;
    }

    void DialogueWindow::announceDisposition()
    {
        if (mPtr.isEmpty() || !mPtr.getClass().isNpc())
        {
            A11y::say("No disposition.");
            return;
        }
        int disposition = MWBase::Environment::get().getMechanicsManager()->getDerivedDisposition(mPtr);
        A11y::say("Disposition: " + std::to_string(disposition) + " of 100.", /*interrupt=*/true);
    }

    bool DialogueWindow::isUnexhaustedTopic(std::string_view label) const
    {
        // Only genuine learned keywords are topics; services and Goodbye are
        // not in mKeywords (same gate buildAccessibility uses to annotate
        // new/exhausted state).
        const std::string name(label);
        if (std::find(mKeywords.begin(), mKeywords.end(), name) == mKeywords.end())
            return false;

        const int flag = MWBase::Environment::get().getDialogueManager()->getTopicFlag(
            ESM::RefId::stringRefId(name));
        return (flag & MWBase::DialogueManager::TopicType::Exhausted) == 0;
    }

    void DialogueWindow::onReferenceUnavailable()
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Dialogue);
    }

    void DialogueWindow::onFrame(float dt)
    {
        checkReferenceAvailable();
        if (mPtr.isEmpty())
            return;

        // Honor a deferred re-activation queued by onOpen (returning from a
        // sub-mode). Doing it here -- one frame later -- means a same-frame
        // teardown (onClose) has already cancelled it, so we don't announce a
        // topic while the window is closing.
        if (mA11yPendingActivate)
        {
            mA11yPendingActivate = false;
            mA11y.activate();
        }

        updateDisposition();
        deleteLater();

        if (mChoices != MWBase::Environment::get().getDialogueManager()->getChoices()
            || mGoodbye != MWBase::Environment::get().getDialogueManager()->isGoodbye())
            updateHistory();

        mA11y.onFrame(dt);
    }

    void DialogueWindow::updateTopicFormat()
    {
        if (!Settings::gui().mColorTopicEnable)
            return;

        for (const std::string& keyword : mKeywords)
        {
            int flag = MWBase::Environment::get().getDialogueManager()->getTopicFlag(ESM::RefId::stringRefId(keyword));
            MyGUI::Button* button = mTopicsList->getItemWidget(keyword);
            const auto oldCaption = button->getCaption();
            const MyGUI::IntSize oldSize = button->getSize();

            bool changed = false;
            if (flag & MWBase::DialogueManager::TopicType::Specific)
            {
                button->changeWidgetSkin("MW_ListLine_Specific");
                changed = true;
            }
            else if (flag & MWBase::DialogueManager::TopicType::Exhausted)
            {
                button->changeWidgetSkin("MW_ListLine_Exhausted");
                changed = true;
            }

            if (changed)
            {
                button->setCaption(oldCaption);
                button->setTextAlign(MyGUI::Align::Left);
                MyGUI::ISubWidgetText* text = button->getSubWidgetText();
                if (text != nullptr)
                    text->setWordWrap(true);
                button->setSize(oldSize);
            }
        }
    }

    void DialogueWindow::updateTopics()
    {
        // Topic formatting needs to be updated regardless of whether the topic list has changed
        if (!setKeywords(MWBase::Environment::get().getDialogueManager()->getAvailableTopics()))
            updateTopicFormat();
    }

    bool DialogueWindow::isCompanion()
    {
        return isCompanion(mPtr);
    }

    bool DialogueWindow::isCompanion(const MWWorld::Ptr& actor)
    {
        if (actor.isEmpty())
            return false;

        return !actor.getClass().getScript(actor).empty()
            && actor.getRefData().getLocals().getIntVar(actor.getClass().getScript(actor), "companion");
    }

    void DialogueWindow::setControllerFocus(size_t index, bool focused)
    {
        // List is mTopicsList + "Goodbye" button below the list.
        if (index > mTopicsList->getItemCount())
            return;

        if (index == mTopicsList->getItemCount())
        {
            mGoodbyeButton->setStateSelected(focused);
        }
        else
        {
            const std::string& keyword = mTopicsList->getItemNameAt(mControllerFocus);
            if (keyword.empty())
                return;

            MyGUI::Button* button = mTopicsList->getItemWidget(keyword);
            button->setStateSelected(focused);
        }

        if (focused)
        {
            // Scroll the side bar to keep the active item in view
            int offset = 0;
            for (int i = 6; i < static_cast<int>(index); i++)
            {
                const std::string& keyword = mTopicsList->getItemNameAt(i);
                if (keyword.empty())
                    offset += 18 + sVerticalPadding * 2;
                else
                    offset += mTopicsList->getItemWidget(keyword)->getHeight() + sVerticalPadding * 2;
            }
            mTopicsList->setViewOffset(-offset);
        }
    }

    bool DialogueWindow::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_A)
        {
            if (mChoices.size() > 0)
            {
                if (mChoices.size() == 1)
                    onChoiceActivated(mChoices[0].second);
                else if (mControllerChoice >= 0 && mControllerChoice < static_cast<int>(mChoices.size()))
                    onChoiceActivated(mChoices[mControllerChoice].second);
            }
            else if (mControllerFocus == mTopicsList->getItemCount())
                onGoodbyeActivated();
            else
                onSelectListItem(mTopicsList->getItemNameAt(mControllerFocus), static_cast<int>(mControllerFocus));
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_B && mChoices.empty())
        {
            onGoodbyeActivated();
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_UP)
        {
            if (mChoices.size() > 0)
            {
                // In-dialogue choice (red text)
                mControllerChoice = std::clamp(mControllerChoice - 1, 0, static_cast<int>(mChoices.size()) - 1);
                mHistory->setFocusItem(mChoiceStyles.at(mControllerChoice));
            }
            else
            {
                // Number of items is mTopicsList.length+1 because of "Goodbye" button.
                setControllerFocus(mControllerFocus, false);
                if (mControllerFocus <= 0)
                    mControllerFocus = mTopicsList->getItemCount(); // "Goodbye" button
                else if (mTopicsList->getItemNameAt(mControllerFocus - 1).empty())
                    mControllerFocus -= 2; // Skip separator
                else
                    mControllerFocus--;
                setControllerFocus(mControllerFocus, true);
            }
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
        {
            if (mChoices.size() > 0)
            {
                // In-dialogue choice (red text)
                mControllerChoice = std::clamp(mControllerChoice + 1, 0, static_cast<int>(mChoices.size()) - 1);
                mHistory->setFocusItem(mChoiceStyles.at(mControllerChoice));
            }
            else
            {
                // Number of items is mTopicsList.length+1 because of "Goodbye" button.
                setControllerFocus(mControllerFocus, false);
                if (mControllerFocus >= mTopicsList->getItemCount())
                    mControllerFocus = 0;
                else if (mControllerFocus == mTopicsList->getItemCount() - 1)
                    mControllerFocus = mTopicsList->getItemCount(); // "Goodbye" button
                else if (mTopicsList->getItemNameAt(mControllerFocus + 1).empty())
                    mControllerFocus += 2; // Skip separator
                else
                    mControllerFocus++;
                setControllerFocus(mControllerFocus, true);
            }
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER && mChoices.size() == 0)
        {
            setControllerFocus(mControllerFocus, false);
            mControllerFocus = mControllerFocus > 5 ? mControllerFocus - 5 : 0;
            setControllerFocus(mControllerFocus, true);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER && mChoices.size() == 0)
        {
            setControllerFocus(mControllerFocus, false);
            mControllerFocus = std::min(mControllerFocus + 5, mTopicsList->getItemCount());
            setControllerFocus(mControllerFocus, true);
        }

        return true;
    }
}
